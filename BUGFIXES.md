# Nikel (Redmi Note 4 MTK / MT6797) — Bug Tracker & Fix Documentation

Target ROM: LineageOS 14.1 (cm-14.1), device tree `device/xiaomi/nikel`,
vendor blobs `vendor/xiaomi/nikel`.
Purpose: anyone picking up this ROM should be able to verify fixes that are
already in and attack the remaining bugs without re-discovering anything.

Bug diagnostics use these code paths repeatedly:
- `logcat -b radio` (RIL/modem), plain `logcat` (HALs, framework)
- `dmesg` (kernel: CCCI, WiFi mode switches, sensor probes)
- `/proc/bus/input/devices`, `/sys/bus/i2c/devices/*/{name,driver}` (hardware inventory)
- `dumpsys sensorservice`, `dumpsys mount` (sensor and storage state)

## Status overview

| # | Bug | Status | Where |
| :--- | :--- | :--- | :--- |
| 0 | Rear camera HAL failed to load | ✅ FIXED | §0 |
| 1 | SD card never mounted | ✅ FIXED | §1 |
| 2 | Mobile data (LTE) dead | ✅ FIXED | §2 |
| 3 | Sensors registered but no data | ✅ FIXED | §3 |
| 4 | Hotspot 2.4 GHz dies | ✅ FIXED | §4 |
| 5 | Hotspot 5 GHz rejected by framework | ✅ FIXED | §5 |
| 6 | Boot image repacking | ✅ documented | §6 |
| 7 | Misc build/boot fixes | ✅ FIXED | §7 |
| 8 | Voice calls crash C2K modem (MD3) | ❌ NOT FIXED (community-wide) | §8, §8f, §10b-e, §19.6 |
| 10 | Video recording fails | ✅ FIXED | §10 |
| 10b | Fingerprint scanner | ✅ FIXED | §10b, §10b-d |
| 10b-c | Goodix enroll error 1058 | ⚠️ open — TEE gatekeeper missing | §10b-c |
| 11 | Hotspot 5 GHz DFS channels | ⚠️ minor open | §11 |
| 12 | **Front camera never enumerated** | ✅ **FIXED** | §12 (supersedes §9, §9b) |
| 13 | **Rear camera green cast at night** | ✅ **FIXED** (3A profile remap, measured) | §13 |
| 14 | **Off-charge bootloop** | ✅ **FIXED (2026-09-19)** | §14 |
| 15 | AudioFx has stopped | ✅ FIXED (2026-09-21) | §15 |
| 16 | SMS (Messaging) app crashes | ✅ FIXED (2026-09-21) | §16 |
| 17 | AOSP Browser (Jelly) crashes on open | ✅ FIXED (2026-09-21) | §17 |
| 18 | Status bar network speed stuck at 0 | ✅ FIXED (2026-09-22) | §18 |
| 19 | Build-integrity defects found while chasing #8 | ❌ NOT FIXED | §19 |
| 20 | Rear-camera AF dead + low-light cast — full diagnosis | 🔍 diagnosis | §20, §22 |
| 21 | Injecting a library into the camera stack without reflashing | ✅ technique | §21 |
| 22 | AF dead: two gates patched, one still open | 🔍 negative result | §22 |
| 23 | **Rear-camera AF never starts** | ⚠️ **PARTIAL** — AF engages, does not converge | §23, §24, §25, §27.8 |
| 24 | The AF motor chain, mapped end to end | ✅ documented | §24 |
| 25 | VCM open proven; gdb Thumb dead end | ✅ documented | §25 |
| 26 | Camera tuning blob is for the wrong sensor | 🔍 root cause | §26, §27 |
| 27 | Load blocker is one symbol; blob is IMX258's | 🔍 root cause, **§27.8's dead-end evidence voided — see §30** | §27, §30 |
| 28 | **IR remote: HAL never enabled** | ✅ FIXED (not verified on hardware) | §28 |
| 29 | **ADB ran as root → scrcpy clipboard dead** | ✅ FIXED (needs a `user` build) | §29 |
| 30 | **Audit of §0/§13/§23/§27 against the tree** | 🔍 2 corrections, 1 voided dead-end | §30 |
| 31 | **Live re-check of the AF chain** | 🔍 §23 fixes confirmed; `EPERM` on the first VCM ioctl | §31 |

**Two things are commonly misread here:**

* **§13 is fixed** (green cast) — the 3A profile remap, measured at night
  G\*2/(R+B) = 1.68, the best of the full-resolution profiles tried. An earlier
  draft of §27.8 wrongly claimed it was unfixable in this tree; that has been
  retracted in place.

* **§23 is partial, not fixed.** The gating bugs are gone and AF now engages,
  runs a full search cycle and times out (§23.7), but the lens never reaches
  focus. The search parameters come from the IMX258-sourced profile, so range
  and thresholds do not match this lens (§27.8).

**Read §8 before touching the call crash.** It carries an explicit "do not
re-test these" list, §8f extends it with a six-axis single-variable bisect, and
§10b-e records a theory that was tested and **refuted**.

**§9 / §9b are superseded by §12** — kept only for "what was ruled out".

---

## FIXED

### 0. Camera (rear, 13 MP) — HAL failed to load

- **Symptom**: every camera app fails; `CameraService: camera hardware module
  doesn't exist`; the real error only appears when mediaserver restarts:
  `dlopen failed: cannot locate symbol "__pthread_gettid" referenced by
  /system/lib/libmtkjpeg.so` → `Could not load camera HAL module: -22`.
- **Root cause**: `libmtkjpeg.so` is an M-era blob referencing
  `__pthread_gettid`, a private bionic symbol **removed in Android N**.
  The 32-bit camera HAL chain cannot dlopen, so the whole camera subsystem
  never registers.
- **Fix**: binary-patch the blob's dynamic string table —
  `__pthread_gettid` → `gettid` (present in N bionic, same version node
  `LIBC`; call sites always pass the current pthread, so semantics match).
  The patched blob is committed in `vendor/xiaomi/nikel/system/lib/libmtkjpeg.so`
  (vendor commit `f91cbe3`). The 64-bit copy does not reference the symbol.
- **Verify**:
  ```
  kill $(pidof mediaserver); sleep 5
  logcat | grep -E 'CameraService|dlopen'
  dumpsys media.camera | grep -E 'Number of camera|module API'
  ```

### 1. SD card never mounted

- **Symptom**: kernel detects the card (`mmc1:aaaa` in `/sys/bus/mmc/devices`,
  `mmcblk1` present), but vold ignores it; Settings shows no SD card.
- **Root cause**: fstab entry used `/devices/mtk-msdc.0/11240000.MSDC1*`
  (uppercase) while the kernel DEVPATH is `/devices/mtk-msdc.0/11240000.msdc1/...`
  (lowercase). The vold glob match is case-sensitive, so the sdcard1 volume
  was never managed.
- **Diagnose**: `readlink -f /sys/bus/mmc/devices/mmc1:*` and compare the path
  against the `voldmanaged=sdcard1` line in `rootdir/fstab.mt6797`.
- **Fix**: `rootdir/fstab.mt6797` — `11240000.MSDC1*` → `11240000.msdc1*`
  (commit `4b549f8`).
- **Lesson**: always verify the real sysfs path; do not copy fstab entries
  between kernels — casing and bus addresses differ.

### 2. Mobile data (LTE) dead with generic RIL

- **Symptom**: SETUP_DATA_CALL always fails; no IP on `ccmni1`.
- **Root cause** (two independent problems):
  1. Generic AOSP `RILJ` sends a 7-parameter SETUP_DATA_CALL. MediaTek's RIL
     expects 8 parameters (with `interfaceId`). Fixed by adding a
     `MediaTekRIL` class (`ril/telephony/java/com/android/internal/telephony/`)
     overriding setupDataCall, wired with `BOARD_RIL_CLASS` in BoardConfig.mk
     and `ro.telephony.ril_class=MediaTekRIL` (commit `d6fec51`).
  2. `ro.mtk_ims_support=1` (and the other c2k/IMS props in system.prop)
     route the RIL through `getImsParam`, which fails on this blob and
     aborts SETUP_DATA_CALL. Removed: `ro.mtk_ims_support`,
     `ro.mtk_volte_support`, `ro.mtk_c2k_support`, `ro.mtk.eccci.c2k`,
     `ro.mtk_md_world_mode_support`, `ro.mtk_world_phone_policy`
     (commits `231b47e`, `0fdf418`).
  3. Also required: `ro.telephony.ril.config=fakeiccid`.
- **Lesson**: adding MTK feature props back (`mtk_ps2_rat`, IMS, C2K...) to
  this tree re-breaks data. Do not re-add them.

### 3. Sensors registered but no data

- **Symptom**: `dumpsys sensorservice` lists 13 sensors (BMI160 accel/gyro,
  YAS537 mag, LTR579 light/prox + fusion), apps see the list, but no values
  ever arrive. `bsthal` logs `attribute ... does not exist:Permission denied`.
- **Root cause** (two layers, BOTH required):
  1. **Kernel**: the SamarV bd04 prebuilt kernel (`d12156cc`) never delivers
     sensor data to userspace even when the HAL enables the driver
     (`enable=1` reaches the HAL, zero events on `/dev/input/event4/6/8`).
     The MIUI bd13 kernel (`9c8a0026`, 3.18.22+ 2019-03-21, from the hellas
     arOmega ROM) delivers data. Now shipped as `prebuilt/kernel`.
  2. **Permissions**: `bsthal` (uid system) must WRITE the driver sysfs
     nodes under `/sys/bus/platform/drivers/{gsensor,gyroscope,msensor}/`
     and the `/dev/input/event*` nodes. MTK AOSP 6.0 shipped these rules in
     `init.project.rc`; LOS does not. Fixed by
     `rootdir/init.sensor-fix.rc` (installed to `/system/etc/init/`, parsed
     automatically by init), which chowns/chmods every node to
     `system:system 0660` and creates `/data/misc/sensor` for calibration
     profiles (commit `8426022`).
- **Diagnose**:
  ```
  logcat | grep -E 'bsthal|Sensors:'      # EACCES on sysfs = rule missing
  getevent /dev/input/event6              # shake with auto-rotate on
  ls /sys/bus/platform/drivers/gsensor/   # nodes exist? owner root:root = broken
  ```
- **Not relevant**: `android.hardware.sensors@1.0-service` (HIDL, Android 8+),
  `COMPAT_SENSORS_M`, `sensor_list.txt` with QMC6983D — those belong to other
  devices (nikel uses BMI160/YAS537, not QST parts).

### 4. Hotspot 2.4 GHz / 5 GHz dies ~1 s after AP-ENABLED

- **Symptom**: hostapd starts fine (`AP-ENABLED` in logcat) for both bands,
  then the interface is torn down by the kernel (`NL80211_CMD_STOP_AP`) and
  the framework unloads the whole wifi driver
  (`wlan.driver.status=unloaded` → init writes `0` to `/dev/wmtWifi`).
- **Root cause chain** (all pieces required to understand):
  1. Framework tethering runs `nat enable ap0 <upstream>` (upstream is
     `ccmni0` when mobile data is the default network).
  2. netd `NatController` installs an IPv6 anti-spoof rule:
     `ip6tables -t raw -A natctrl_raw_PREROUTING -m rpfilter --invert ...`
     The **MTK 3.18 kernel has no `xt_rpfilter` module** → the command fails.
  3. `NatController::setForwardRules` treated that as fatal → whole
     `nat enable` fails with `Nat operation failed (No such device)`.
  4. `TetherInterfaceSM` throws, framework tears down the AP,
     `SoftApStateMachine`/`WifiStateMachine` transition to InitialState and
     call `unloadDriver()` — killing the driver and the running AP.
- **Fixes**:
  - `system/netd` `NatController.cpp`: the rpfilter rule is now non-fatal —
    logged and skipped (patch `system_netd.patch`, commit `ad39e14`).
  - netd `SoftapController.cpp`: the hostapd ctrl unix socket path must be
    `/data/misc/wifi/hostapd/ap0` (patch `system_netd.patch`, commit `c1e5b75`;
    completes the rejected hunk of the original divis1969 patch).
- **5 GHz specific** (channel selection + country code, see next entry).
- **Diagnose**: `logcat | grep -E 'NatController|TetherInterfaceSM|SoftapController'`
  and `dmesg | grep MTK-WIFI` (the killer writes `WIFI_write 0` from init).

### 5. Hotspot 5 GHz rejected by framework (no wificond)

- **Symptom**: `SoftApManager: Failed to set country code, required for
  setting up soft ap in 5GHz`; 2.4 GHz toggle works, 5 GHz does nothing.
- **Root cause**: cm-14.1 has **no wificond** (and no source for it), so
  `WifiNative.setCountryCodeHal()` and `getChannelsForBand()` always fail.
  Three gates block 5 GHz:
  1. `ApConfigUtil.updateApChannelConfig` forces the band back to 2 GHz when
     the HAL provides no 5 GHz channel list.
  2. 5 GHz + no country code → ERROR_GENERIC.
  3. `SoftApManager` aborts when `setCountryCodeHal` fails.
- **Fix** (patch `frameworks_opt_net_wifi.patch`, commit `852164fe` in
  `frameworks/opt/net/wifi`):
  - `ApConfigUtil`: `DEFAULT_AP_CHANNEL_5G = 36` (non-DFS); both fallback
    branches keep the user's 5 GHz band instead of forcing 2 GHz.
  - `SoftApManager`: HAL country-code failure no longer aborts 5 GHz start
    (country code is still set through supplicant).
- **Residual risk**: the MTK AP firmware might still refuse 5 GHz
  (`fwReload` is 2.4-only on some stacks) — verified working on nikel.

### 6. Boot image repacking

- **Repacking boot.img manually (extract cpio + re-mkbootimg) is dangerous**:
  wrong `tags_addr`/cmdline in the rebuilt header = black screen bootloop.
  The build system's `make bootimage` packs the MTK header correctly
  (tags `0x44000000`, cmdline `bootopt=... selinux=permissive`).
  Always change ramdisk files (fstab, rc) in `rootdir/` and rebuild instead.
- **Recovery** when a bad boot image is flashed: MTK BROM mode always works —
  power off (hold power ~15 s), hold Vol- and connect USB (preloader window
  is ~1 s per bootloop cycle), use mtkclient:
  ```
  sudo cp mtkclient/Setup/Linux/*.rules /etc/udev/rules.d/
  python3 mtk.py w boot <good-boot.img>
  ```

### 7. Misc build/boot fixes

- `apply.sh` CRLF stripping (`9965f92`), `.ht120.mtc` rename (`94af771`),
  GraphicBuffer ABI shim from the omega tree (`e160b8b`), vendor blob
  symlinks (vendor repo commit `b66472e`), `nikel-vendor.mk` bridge
  (`2060134`), libgralloc_extra blobs (`9692dda`), ADB enabled on boot
  (`1597ece`).

### 10. Video recording — FIXED (2026-09-10)

- **Symptom**: photo capture works, video recording fails immediately. All
  resolutions fail.
- **Root cause chain** (fully traced):
  1. The SW h264 encoder dies with vendor gralloc buffers (0x80001001).
  2. The HW encoder (`OMX.MTK.VIDEO.ENCODER.AVC`) is never registered:
     `Mtk_OMX_Init` fails with **`ParseMtkCoreConfig failed. Can't open
     /vendor/etc/mtk_omx_core.cfg`** — the MTK OMX core reads its component
     table from this config file, missing from the ROM, so every MTK OMX
     component returned `InvalidComponentName`.
  3. `media_codecs.xml` also had a stray `.` after `/>` aborting the parse
     before the MTK encoder entries.
- **Fix** (vendor commit `dc13e71`): replace the M-gen OMX stack with the
  **N-gen set from Vernee Apollo Lite madOS 7.1.2** (same Helio X20 SoC) —
  `libMtkOmxCore/Venc/VdecEx`, `libvcodec_{utility,drv,oal}`,
  `libstagefrighthw` (32+64 bit) + **`system/vendor/etc/mtk_omx_core.cfg`**.
  Verified: `OMX.MTK.VIDEO.ENCODER.AVC` enumerated (11 encoders vs 8) and
  video recording works end-to-end.
- **Known minor issues after the fix**: occasional stutter on some videos,
  and `audiofx has stopped` when playing — not yet investigated.

### 12. Front camera — FIXED (2026-09-14, kernel migration bd13 → bd54 + kdSensorList swap)

Root cause chain (supersedes #9/#9a–#9e and #9b's conclusions):

1. The booted kernel in the working LOS boot (`boot_bd13_backup.img`)
   is a MIUI **bd13** build. The MIUI-M stock ROM (V10.2.2.0 MBFCNXM)
   actually ships a **bd54** build. Both are 3.18.22+ with identical
   `kdSensorList` layout, identical function addresses, and identical
   DTBs — but bd13's front-camera path (power-on + probe) NACKs the
   S5K5E8YX B6-qteck module on i2c-3 @0x18 even with VCAM_D 1.22 V
   correctly applied, while the bd54 kernel probes and initializes it
   (OTP `awb_flag = 0x01`, same as MIUI-M).
2. On both kernels `kdSensorList[3]` = `s5k3l8mipirawqteck` (rear) but
   the LOS HAL (`libcameracustom.so`) sends `drvIdx=3` for the SUB slot,
   where MIUI's own kernel expectation is the front driver at list
   index 6 (`s5k5e8yxb6mipirawqteck`, id 0x5e85). The fix is a
   full-entry swap of `kdSensorList` entries 3 and 6 (48-byte entries:
   `{id u32, name[32], pad u32, fn u64}` at raw offset `0x115a390`,
   stride `0x30`) so HAL drvIdx 3 maps to the qteck-B6 driver.
3. No i2c slave patch is needed: the bd54 qteck-B6 driver's native
   `i2c_addr_table` = `{0x30, 0x20, 0xff}` (write id 0x30 = 7-bit 0x18,
   the module's real address). The earlier `18 2d` patch on bd13 hit
   the wrong driver's table and is NOT required.

Tooling facts learned along the way (apply to any future kernel test):

- `fastboot boot` does NOT deliver a bd13/bd54 kernel image to this
  device (LK's RAM-boot gunzip rejects the 19,726,336-byte Image;
  TWRP's smaller image RAM-boots fine). All "fastboot boot" tests of
  patched bd13 kernels never ran — the device silently booted the
  flashed image. Kernel tests must be `fastboot flash boot` + reboot
  (restore with `boot_bd13_backup.img` on bootloop).
- The 16 MB boot partition layout is:
  `[hdr 2048][gz(Image 19726336) 8328830][dtb 131649][pad][ramdisk gz 1631820][864 KB blob + MTK cert1/cert2 structures]`.
  A repacked image that truncates the tail bootloops; shifting the tail
  (zopfli-compressed kernel, ~317 KB shorter) boots fine — certs are
  not position- or content-verified in practice.
- `/proc/version` reads a banner copy at `0xdd20d0` (bd13 has two
  copies); patching only `0xb2a0c0` makes `/proc/version` look
  unpatched while patches are live. Use `camera_info`/driver behavior,
  not the banner, to verify delivery.

Result: `boot_bd54_swap.img` (bd54 kernel + kdSensorList 3↔6 swap +
LOS ramdisk, flashed to the boot partition):

```
CAM[1]:s5k3l8mipirawnew; CAM[2]:s5k5e8yxb6mipirawqteck;
dumpsys media.camera: Number of camera devices: 2
camera2: cam 0 facing BACK, cam 1 facing FRONT
front capture via camera2 (Test6): JPEG 415056 bytes — real image
rear capture: JPEG 350747 bytes — real image
```

**Build integration:** the patched kernel is shipped as
`device/xiaomi/nikel/prebuilt/kernel`
(`[gz(bd54 Image + kdSensorList swap)][dtb]`, byte-identical to the
flashed working boot), so every `make otapackage` build produces a
boot.img with the fix included — no manual flashing step needed by
other builders. Artifacts kept in `tmp/mados/`: `boot_bd54_swap.img`
(working boot, also `boot_WORKING_bd54_frontcam.img`),
`kernel_bd54_swap.raw` / `kernel_bd54_swap.gz`,
`boot_bd13_backup.img` (restore point).

---

### 13. Rear camera green cast — FIXED (2026-09-14, 3A tuning profile remap)

**Symptom:** rear photos are strongly tinted green, most visible in low
light (front camera normal). Device Info HW also showed both cameras
with identical default info (array 3072x1728, focal 3.5).

**Root cause:** the HAL3 static-metadata constructors live in the M-gen
blob `libcam.metadataprovider.so` (32-bit; mediaserver is 32-bit) and are
compiled per sensor as
`constructCustStaticMetadata_DEVICE_{SCALER,FEATURE,REQUEST}_SENSOR_DRVNAME_<X>`
for 9 MTK reference sensors (imx214, imx230, imx258, imx377, ov23850,
s5k2x8, s5k3m2, s5k3p3sx, s5k5e2ya). None of nikel's sensors
(ov13853 / s5k3l8new / s5k5e8yxb6qteck) is in that list. The lookup keys
off the drvname string inside `libcameracustom.so` (`libcam.halsensor`
dlsym's the constructor by name), so the HAL falls back to built-in
defaults — and the rear sensor's 3A runs with mismatched tables → green
AWB cast. `libcam.metadata.so` is an empty shim in this ROM; the LENS
constructors are absent for ALL sensors, hence focal stays 3.5
(unfixable without MTK's metadata build system).

**Fix:** remap the drvname strings in `libcameracustom.so`
(both 32-bit and 64-bit, same-length string replacement):

- rear: `SENSOR_DRVNAME_S5K5E2YA_MIPI_RAW` → `SENSOR_DRVNAME_IMX258_MIPI_RAW`
  (13MP class — neutral AWB, full 4160x3120 works)
- front: `SENSOR_DRVNAME_S5K5E8YX_MIPI_RAW` → `SENSOR_DRVNAME_S5K5E2YA_MIPI_RAW`
  (correct 5MP-class metadata, neutral)

Profile test matrix (same scene, camera2 capture): stock
G/(R+B)=1.74 (green), IMX258 1.20 (best), IMX214 1.77 (green),
S5K3P3SX also worse than IMX258. IMX258 wins for the rear; S5K5E2YA for
the front. **Note:** all earlier "IMX258/IMX214 capture is black / AE
broken" observations were FALSE — the phone was lying face-down on a
dark desk. Re-test under light before trusting a black JPEG.

**Verification (live, bind-mount + `killall mediaserver`):**

```
rear 2560x1920: RGB (80,126,65) stock -> (114,124,93) imx258, neutral
rear 4160x3120: full 13MP capture works (slightly green in dark corners)
front 1280x960: RGB (115,115,109), neutral
```

Shipped in `vendor/xiaomi/nikel/system/lib{,64}/libcameracustom.so`
(commit `b2663f3`).

**2026-09-19 follow-up — night/low-light cast (ACCEPTED as limitation):**

Rear photos are still noticeably green at night / low indoor light
(measured `G*2/(R+B)` on a white wall: rear 1.68 vs front 1.02 under the
same lamp). Findings, so nobody repeats the work:

- **Why the front is neutral**: `libcameracustom.so` exports
  `getAWBParam2_s5k5e8yx()` which reads the front sensor's per-unit AWB
  OTP (dmesg: `s5k5e8 otp group1 awb_flag = 0x01`, unit gains ≈ golden
  gains). The rear has no calibration path at all: there is no
  `getAWBParam2_s5k3l8*`, the bd54 kernel s5k3l8 driver has no OTP read
  (only `module_id_ofilm=7`), and all camera NVRAM LIDs
  (`CAMERA_Para`, `CAMERA_3A`, `CAMERA_SENSOR`, `CAMERA_SHADING*`,
  `CAMERA_PLINE*`) are at version `000` in the FILE_VER table = never
  written — no per-unit calibration exists anywhere on this unit.
- **Raw nvram partition (p18)** still contains the factory camera-LID
  name table, but the `/data/nvram/APCFG` mirror never had camera LID
  files; the daemon table (`AllMap`) lists 936 LIDs, none of them
  camera. Flashing stock MIUI adds nothing — the blobs are identical.
- **Night profile matrix** (same wall, no flash, all 9 constructors
  available in `libcam.metadataprovider.so` were tested by bind-mounting
  a patched `libcameracustom.so` with a same-length drvname remap):

  | rear profile | night G*2/(R+B) | max resolution |
  |---|---|---|
  | **IMX258 (shipped)** | **1.68** | 13 MP 4160x3120 |
  | S5K5E2YA | 1.54 | 3072x1728 only (5 MP metadata caps scaler) |
  | S5K3P3SX | 1.78 | 13 MP |
  | IMX230 | 1.78 | |
  | IMX214 / S5K2X8 | 1.80 | |
  | S5K3M2 | 1.92 | |
  | IMX377 | 2.18 | |
  | OV23850 | camera fails to open | |

  Test artifacts: `nikelbuild/cam_green_night/` (test libs, sample
  photos, nvram p18 dump, FILE_VER/AllMap, s5k3l8 driver sources from
  begonia/mt6761/Hikari trees).

- **Decision**: keep IMX258 (best night of all full-res profiles, best
  daylight). Further options if someone picks this up: binary-tweak the
  IMX258 AWB/CCM tables for high-gain conditions, or implement an
  S5K3L8 OTP/EEPROM AWB reader (kernel driver + HAL `getAWBParam2`
  shim). The module EEPROM is on I2C 0xa0; the PDAF block is at
  0x0763/1404 B per the public W1540 driver; the AWB block layout was
  not found in public sources.

---

### 14. Off-charge bootloop (MI logo repeats when charger is plugged while powered off) — FIXED (2026-09-19)

- **Symptom**: power off the phone, then plug in a charger (wall or PC USB)
  → no charging indicator, the MI boot logo loops forever, never boots to
  the home screen.
- **Diagnosis** (USB VID/PID polling with `lsusb`):
  - Measured reset cycle ≈ 18 s: preloader `0e8d:2000` (~3 s) → LK + logo
    `0e8d:2008` (~15 s) → WDT reset. **The kernel never starts** (adbd never
    appears; `last_kmsg` contains only the ram_console header because the
    preloader clears it on every reset; `printk.disable_uart=1` = no UART).
  - All KPOC (kernel power off charging) pieces are present in this ROM:
    `init.mt6797.rc` `on charger` → mount system + `start fuelgauged` +
    `start kpoc_charger` (+ adb), binaries `/system/bin/kpoc_charger`
    (26 KB, MIUI blob) + `/sbin/healthd` + all needed libs (`libshowlogo`,
    `libgui`, `libui`, `libhardware_legacy`, `libsuspend`) exist.
  - Kernel & LK = prebuilt MIUI bd54 (kernel diff vs MIUI stock boot.img is
    only build-stamp, date, sensor config — KPOC logic identical). LK has
    the full KPOC path (`mt65xx_bat_init`, `check_bat_protect_status`,
    `< Kernel Power Off Charging Detection Ok>`).
  - **The gate is the LK env var `off-mode-charge`**: exposed by the
    `/proc/lk_env` node in the kernel (MTK `sysenv` driver, backing store =
    the `para` partition = `mmcblk0p2`). Default `off-mode-charge=1` routes
    charger boots into the KPOC path, which crash-loops in this build
    (crash happens between the LK jump and USB gadget init — not verified
    in detail because the logs get reset each cycle; not investigated
    further since bypassing it solves the use case).
  - Testing the stock MIUI boot.img does not validate anything (it also
    loops) because userdata already belongs to LOS (e4crypt) — a boot of a
    different ROM always fails to mount the data partition.
- **Fix** (root adb):
  ```
  echo "off-mode-charge=0" > /proc/lk_env
  ```
- **Effect**: charger plugged while off → the phone **boots normally into
  Android** (charging still works, battery indicator via the Android UI;
  no KPOC animation at boot — accepted trade-off).
- **Verification**: `poweroff` → plug PC USB → boots to home screen OK;
  `/proc/lk_env` = `off-mode-charge=0`; the `para` partition contains
  `ENV_v1 off-mode-charge=0` → **persistent** (survives reboot and power
  cycle). The setting lives in a partition, not in a flash — it does not
  need to be re-applied per ROM and is untouched by normal flashing.
  **Note for a fresh flash**: a brand-new install still has
  `off-mode-charge=1`, so the echo above must be run once (a build-time
  permanent fix could write this via an init service).

### 15. AudioFx has stopped (crashes when sounds play) — FIXED (2026-09-21)

- **Symptom**: "AudioFx has stopped" appears frequently, especially when a
  ringtone / notification / any audio session starts.
- **Root cause**: `AudioFxService.onCreate()` calls
  `DevicePreferenceManager.initDefaults()`, which fails on this device with
  `AudioEffect: set/get parameter error` (the MTK audio effects HAL does not
  implement the standard `Equalizer` effect commands). `onCreate()` then
  `stopSelf()`s **without ever creating `mSessionManager`**, but audio
  session broadcasts keep arriving and `onStartCommand()` dereferenced the
  null manager → `NullPointerException` on every sound.
- **Fix** (`packages/apps/AudioFX`, commit `5f3adde`): guard
  `onStartCommand()` — if `mSessionManager == null` return `START_STICKY`
  instead of crashing. Side effect (accepted): AudioFX effects cannot work
  on this device until the MTK effects HAL implements the standard
  Equalizer; the app just no longer crashes.
- **Rebuild**: `source build/envsetup.sh && breakfast nikel && make AudioFX`
  on the build VM (jack needed `-Xmx6144m`; default 8 GB VM OOM'd at the
  default settings — also set `jack.server.max-service=4` in
  `~/.jack-server/config.properties`). New APK flashed to
  `/system/priv-app/AudioFX/AudioFX.apk` via TWRP zip
  (`nikelbuild/audiofx_nikel_fix.zip`).

### 16. Messaging (SMS) crashes when opening a message — FIXED (2026-09-21)
### 17. Jelly (AOSP browser) crashes on open — FIXED (2026-09-21)

- **Symptom**: opening a message in Messaging → "messaging has stopped"
  (list view was fine); opening Jelly browser → inflate exception. Both apps
  crash with:
  `android.webkit.WebViewFactory$MissingWebViewPackageException: Failed to
  load WebView provider: No WebView installed`.
- **Root cause**: `/system/app/webview/webview.apk` shipped in the build was
  a **modern SDK-29 (Android 10) WebView** (the tree's
  `external/chromium-webview/prebuilt/*` mirrors contain Chromium
  100–119 imports, and `arm/` was even a 133-byte placeholder). PMS rejects
  it at boot: `Requires newer sdk version #29 (current version is #25)` →
  no WebView provider → any app touching WebView crashes (Messaging links
  via `Linkify` → `WebView.findAddress`, Jelly inflates `WebViewExt`).
- **Fix**: replace the webview prebuilt with the cm-14.1-compatible
  `com.android.webview 60.0.3112.78` (platform 7.1.1, both arm+arm64 libs
  inside — taken from the known-good SamarV ROM zip; LineageOS no longer
  publishes the cm-14.1 webview prebuilt). Applied to the build tree
  (`external/chromium-webview/prebuilt/{arm,arm64}/webview.apk`) and flashed
  to the device via TWRP zip (`nikelbuild/webview_nikel_fix.zip`; note:
  this device's TWRP update-binary does not know `set_perm`, so the
  updater-script only mounts/extracts/unmounts). Verified:
  `pm path com.android.webview` = 60.0.3112.78, Messaging and Jelly open
  without crashes.

### 18. Status bar network speed indicator always shows 0 — FIXED (2026-09-22)

- **Symptom**: the status bar traffic indicator (CM `NetworkTraffic`) shows
  0 kbps for both directions; Data usage is also empty.
- **Root cause**: the indicator reads
  `TrafficStats.getTotalTx/RxBytes()`, whose native implementation parses
  `/proc/net/xt_qtaguid/iface_stat_fmt`. On the MIUI bd54 kernel the
  qtaguid per-uid/iface counters never accumulate (all zeros even with
  active traffic, verified: `iface_stat_all` counts 360+ MB while
  `iface_stat`/`iface_stat_fmt` stay 0), so everything reading qtaguid
  shows 0.
- **Fix** (`vendor/cmsdk`, NetworkTraffic.java): when both TrafficStats
  deltas are zero, fall back to summing the per-interface counters from
  `/proc/net/dev` (skipping `lo`).
- **Deployment gotcha**: replacing `org.cyanogenmod.platform.jar` is NOT
  enough — SystemUI statically links the cmsdk
  (`Lorg/cyanogenmod/internal/statusbar/NetworkTraffic;` lives inside
  SystemUI.apk's classes.dex). Rebuild SystemUI itself
  (`make SystemUI`, picks up the patched cmsdk) and flash the new
  SystemUI.apk via TWRP.

### 10b. Fingerprint scanner (Goodix + Kinibi TEE) — FIXED (2026-09-14)

- **Status: WORKING.** Enrollment, unlock and screen-off wake-up verified on
  the integrated build. The full stack ships in the ROM: Kinibi TEE runtime
  (`mcDriverDaemon` + `ld.mc`, started on `post-fs-data` before keystore),
  MIUI `fingerprintd` (hardcodes HAL id `gf_fingerprint`),
  `gf_fingerprint.default.so` + `goodixfingerprintd` + `libgf_*` +
  `libgoodixfingerprintd_binder`, the 66-file MIUI mcRegistry (incl. the
  Goodix trustlet and the AOSP gatekeeper trustlet), and the TEE gatekeeper
  HAL (`gatekeeper.mt6797.so` = `libMcGatekeeper.so`, 64+32 — required, see
  #10b-c). The kernel bd54 prebuilt's Goodix driver loads `gf_ta.axf` into
  the TEE at probe.

- **Sensor identity — FINAL (2026-09-14, corrected):** the device sensor is a
  **GOODIX on SPI0** (`soc/spi@1100a000/goodix-fp@1`), NOT the FPC1145. Proof:
  with the TEE daemon up, the kernel-driver-loaded goodix TA (`gf_ta.axf`)
  answers `GF_CMD_INIT` with **err = 0** inside the TEE, while the FPC TA
  returns -3/FPC_ERROR_COMM (it probes SPI1, where nothing is attached).
  MIUI's `/system/bin/fingerprintd` hardcodes HAL id **"gf_fingerprint"**;
  the `fingerprint.mt6797.so` FPC stack in MIUI's system.img is for another
  device revision.
- **Real stack (from MIUI system.img):**
  `fingerprint.mt6797.so` (FPC TEE HAL, `fpc_tee_*`, 32+64 bit) +
  `lib_fpc_tac_shared.so` (hardcodes `/system/app/mcRegistry/0401…0.tlbin`)
  + trustlet `0401…0.tlbin` (the FPC TA, MCLF/Thumb, 698 KB) + SPI device
  root `030b/030c` (Drspi, load-on-demand from the registry) +
  `mcDriverDaemon` (t-base V006, Aug 2018 build; TEE = V009 from the `tee1`
  partition, untouched) + MIUI `fingerprintd` (aarch64, Android 23; the
  daemon interface is unchanged in N so LOS's framework works with it).
- **Dead ends (proven):** the "fpsensor" HAL/TA stack (`0522…tlbin`,
  Leadcore) belongs to another device variant — its `-12` failure was a
  red herring. Kernel kthread `ex_open` wants trustlet `070505…` which does
  not exist in the MIUI registry either (same single failure), and MIUI
  mounts no `/efs` auth token (MC_AUTH_TOKEN_PATH points nowhere on MIUI
  too) — both irrelevant. SPI1 needs no 070505 session; the TA drives SPI
  itself via the Drspi device root.
- **Live status before integration:** TEE runtime verified working from
  userspace (device open, 0401 trustlet load, session, notify, TCI round
  trip, `mcGetSessionErrorCode`=0 — the TA is alive and answering). The
  TA's `INIT` command returns message error **-3 = FPC_ERROR_COMM** (via
  the TAC's own error table), i.e. its in-TEE sensor SPI transfer fails;
  root cause not visible from the normal world. `clk_enable` sysfs write
  is real (`mt_spi_enable_clk`). All live tests ran against a
  bind-mount-hacked system; the integrated ROM boot is the real test.
- **Integration (this commit):**
  `vendor/xiaomi/nikel/system/`: FPC HAL 32+64 as
  `lib{,64}/hw/fingerprint.mt6797.so`, `lib_fpc_tac_shared.so` 32+64,
  `bin/mcDriverDaemon`, `bin/ld.mc`, `bin/fingerprintd`,
  `lib{,64}/libMcClient.so` + `libMcRegistry.so`, and the full 66-file
  Kinibi registry at `app/mcRegistry/`. The obsolete fpsensor HAL and the
  device-tree wrapper shim (`device/fingerprint/`) are removed — MIUI's
  `fingerprint.mt6797.so` is a real `fingerprint` HAL and is found by
  `hw_get_module` directly.
  `init.nikel-fp.rc`: `mobicore` daemon at **class core** (MIUI-exact 7
  drbins, user system) started on `on fs`; `fingerprintd` class
  late_start; FPC sysfs nodes (`soc:fpc_interrupt@0/{clk_enable,hw_reset,
  chip_id,irq,do_wakeup}`) chowned to system; `/data/fpc` + `/data/fpsensor`
  created; `MC_AUTH_TOKEN_PATH=/data` (MIUI uses a non-existent /efs, same
  result: daemon runs with device endorsements disabled).
- **Next:** validate on the integrated build; if the TA still returns
  -3, the suspects are EMI-MPU region setup inside Drspi and the TEE's
  view of the kernel-published SPI device.
- **Boot-loop lesson (2026-09-14, fixed):** adding `libMcClient.so` to
  /system made MTK's `keystore.mt6797.so` (Keymaster TEE HAL) load for
  the first time; without the mobicore daemon it retries forever, keystore
  never registers, system_server NPE-loops on `LockdownVpnTracker`. Fix:
  the daemon must be up before keystore (class main) — init.nikel-fp.rc is
  installed as `system/etc/init/nikel-fp.rc` (auto-imported) and starts it
  on `post-fs-data`. Verified live: manual daemon start → keystore wraps
  "Keymaster TEE HAL" → boot completes. On that same boot the FPC TA's
  INIT still returns -3/FPC_ERROR_COMM, so the sensor-SPI failure is real
  and not an artifact of the hacked test environment.

### 10b-d. Fingerprint verified end-to-end (2026-09-14)

- After vendor `0e5ea60` (TEE gatekeeper HAL) and a fresh flash: PIN setup
  works (TEE gatekeeper), enrollment completes, unlock works. The MIUI-era
  templates that triggered the limit error earlier are no longer an issue —
  1058 was always the gatekeeper/HMAC failure (see #10b-c), not a template
  count problem (the real limit error is 1005).
- Debugging aids if it regresses: `logcat | grep -aE '\[gf_'` (HAL/TA logs),
  `logcat | grep -a 'software GateKeeper'` (must NOT appear — if it does,
  the TEE gatekeeper HAL is missing or fails to load), `service check
  android.hardware.fingerprint.IGoodixFingerprintDaemon`, `service check
  android.security.keystore`, `pidof mcDriverDaemon goodixfingerprintd
  fingerprintd`.

### 23. Autofocus: the previous stage patched the wrong library (2026-10-01)

§20 and §22 concluded that dead AF was "a platform limitation on this ROM".
That conclusion was wrong, and so was every patch in §22. Two independent
defects were sitting in plain sight.

#### 23.0 `mediaserver` is 32-bit — it never loads `lib64`

```
$ adb shell 'grep libcam.hal3a /proc/$(pidof mediaserver)/maps'
/system/lib/libcam.hal3a.v3.so
```

Both `system/lib/` and `system/lib64/` ship a `libcam.hal3a.v3.so`, and both
export the same `MCUDrv` / `AfMgr` symbol names, so a patch aimed at the wrong
copy is indistinguishable from a correct one until you check `/proc/*/maps`.
§22 patched the lib64 copy in its entirety (11 gates, two libraries) and none of
it ever executed. All of it is kept in `cam_af/` as research artifacts only;
the lib64 prebuilt is back to stock (`md5 1e0823e3`).

Correct targets for this device:

| library | path | notes |
|---|---|---|
| HAL3A | `/system/lib/libcam.hal3a.v3.so` | 32-bit ARM/Thumb, 912 KB, delta vaddr-file = `0x8000` |
| 3A algorithm | `/system/lib/lib3a.so` | 32-bit ARM |
| custom | `/system/lib/libcameracustom.so` | lens + CAM_CAL tables |

#### 23.1 The VCM / AF motor nodes were 0600 root:root

```
crw------- 1 root root 229, 0 /dev/MAINAF
crw------- 1 root root 227, 0 /dev/SUBAF
crw-rw---- 1 system camera 242, 0 /dev/camera-isp
crw-rw---- 1 system camera 238, 0 /dev/kd_camera_flashlight
```

`mediaserver` runs as uid 1006 (`camera`). devtmpfs creates every node
`0600 root:root`, and this ROM widens them from the `chmod`/`chown` block in
`rootdir/init.mt6797.rc`. That block listed every camera node **except** the AF
ones — it has `/dev/DW9714AF` (another MTK project's VCM) but not this phone's
`/dev/MAINAF`. So `MCUDrv`'s `open()` failed, `m_fdMCU` stayed invalid, and the
lens initialisation silently no-opped. The HAL's own error strings
(`Err: [mcuIOC_S_SETDRVNAME] please check kernel driver`) never appeared
because nothing got far enough to call them.

**Fix:** add the six node names the HAL3A knows about to that block —
`MAINAF`, `SUBAF`, `MAIN2AF`, `GAF001AF`, `GAF002AF`, `GAF008AF` — as
`chmod 0660` + `chown system camera`. Verified live: `chmod 666` on the two
nodes was enough to make the difference, and it survives as an init rule so it
no longer depends on an `adb shell` after every reboot.

#### 23.2 The real gate: the lens-table lookup never matches

`AfMgr::CCTMCUNameinit(int)` (0x81cf0, 300 bytes) in the **32-bit** HAL3A:

```
0x081d30 blx NSCam::IHalSensorList::get()
0x081d44 tbb [pc, ip]              ; sensorDev - 1 -> 4 cases
0x081d60 blx r3                    ; vtable slot 9 (u32 mode, u32* out)
0x081d9e blx MCUDrv::lensSearch     ; (dev, CurrSensorId)
0x081da6 blx MCUDrv::getCurrLensID ; -> table[CurrLensIdx].LensId
0x081daa movw r1, #0xffff
0x081db2 subs r3, r0, r1
0x081dba movne r3, #1              ; <-- the gate
0x081dc0 str  r3, [r4, #0x58b8]    ; AfMgr + 0x58b8 = AF_FLAG
```

`MCUDrv::lensSearch(uint dev, uint sensorId)` (0xb949c, 796 bytes) compares
`sensorId` against a 16-entry table that `LensCustomGetInitFunc()` fills in from
`libcameracustom` at runtime:

```
LensMCU[LensInitTable-0][SensorId]0xffff,[LensId]0xffff
LensMCU[LensInitTable-1][SensorId]0x0135,[LensId]0x9714
LensMCU[LensInitTable-2][SensorId]0x3103,[LensId]0x9714
LensMCU[LensInitTable-3][SensorId]0x0258,[LensId]0x0005
```

The sensor HAL reports `MainSensorIdx = 0x5e20`, which is in **no** entry:

```
CAM_CUS_MSDK GetCameraCalData(MainSensorIdx=5e20) Enter
CAM_CUS_MSDK SensorId == pstSensorInitFunc[2].SensorId=5e20   <-- libcameracustom's own table matches
CAM_CUS_MSDK SensorId != pstSensorInitFunc[1].SensorId=3103
```

So the two vendor blobs disagree: `libcameracustom` knows `0x5e20`, the
`MCUDrv` lens table does not. With no match, `m_u4CurrLensIdx_main` keeps its
default (the last entry whose `LensId` is `0xffff`, i.e. 0),
`getCurrLensID()` returns `0xffff`, and `AF_FLAG` is 0 for the whole session.

#### 23.3 The patch

One 4-byte Thumb-2 instruction, replacing the loop that computes the *default*
index (which on this device is already 0, so removing it is behaviour-neutral;
`r4` is reloaded with `0xffff` immediately afterwards):

| file offset | vaddr | before | after |
|---|---|---|---|
| `0x0b1542` | `0xb9542` | `10 2b` `f5 d1` — `cmp r3,#0x10` ; `bne 0xb9532` | `43 f2 03 15` — `movw r5, #0x3103` |

`0x3103` is the S5K3L8 chip ID, so table entry 2 is the correct match for this
phone; entries 1 and 2 share `LensId 0x9714` anyway, so the resulting AF
behaviour is identical either way. Reverting is writing `10 2b f5 d1` back.

| file | md5 | contents |
|---|---|---|
| `cam_af/libcam.hal3a.v3.32.stock` | `9af2c96b` | stock 32-bit HAL3A |
| `cam_af/libcam.hal3a.v3.32.patched` | `47b9883f` | + lens-id match |

The vendor preblob `vendor/xiaomi/nikel/system/lib/libcam.hal3a.v3.so` is
patched, and `patches/camera-af/patch_lensid.py` reproduces it from stock and
verifies it (`--check`).

#### 23.4 What the patch actually bought — measured, not assumed

| observation | before | after |
|---|---|---|
| `MCUDrv` lens lookup | `CurrLensIdx 0` | `LensMCU[idx]2 [CurrSensorId]0x3103,[CurrLensIdx]0x0002` |
| AF commands reaching the algorithm | only `setAFMode` | `Cmd_triggerAF`, `Cmd_lockAF`, `Cmd_unlockAF`, `Cmd_cancelAF` |
| 3A state machine | never enters AF | `aaa_state_mgr: StateCameraPreview --> StateAF` (6 transitions over 3 focus taps) |
| photo capture | works | works (no regression) |

The `StateAF` transition count is the honest headline: **0 occurrences before the
patch, 6 after**, across otherwise identical log captures.

#### 23.5 What is still broken

`doAF()` still never runs, so the motor is still never commanded.

```
$ adb shell 'ls /proc/$(pidof mediaserver)/task/*/comm'   # 38 tasks
3ATHREAD  AESenThd  F858THREAD  CamClient@Previ  ...  — no AFthread
```

`AfMgr` has `StateCAF` / `StateTAF` states and
`ThreadRawImp::enableAFThread(AfStateMgr*)` (0x656d4, 148 bytes) exists, but
it obtains its thread from `NS3A::IEventIrq::createInstance()` rather than
`pthread_create`, and it never logs — so either it is not reached or it returns
NULL. The 3A state machine reaches `StateAF` and leaves again without processing
a single buffer. There is no camera IRQ in `/proc/interrupts` either; the HAL's
`HwEventIrq` is built around the `AFIrq` / `HwIRQ3A` names.

That is the next investigation, and unlike §22 it can be done against the
library that actually runs.

> **Superseded (2026-10-01).** `doAF()` does now run — §23.7 measured the full
> search cycle, and §24/§25 proved the motor is opened and commanded. What is
> still missing is **convergence**: the lens never reaches focus, because the
> search range, step count and thresholds come from the IMX258-sourced tuning
> profile (§26.3, §27.8). The blocker therefore moved from "the algorithm is
> never invoked" to "the algorithm is invoked with parameters that do not
> describe this lens".

#### 23.6 Corrections to earlier sections

* §20's "AF is a platform limitation on this ROM" — **retracted.** At least two
  concrete bugs were involved (node permissions, lens-table mismatch), and both
  are fixed. The no-AF-OTP / empty-NVRAM findings from §20 still stand, but as
  a statement about calibration quality, not about AF being unable to run.
* §22's eleven gates and their addresses are lib64 addresses. They describe a
  binary that is never loaded. The 32-bit equivalents are different code.
* §22's "there are no direct `bl` call sites, a vtable-slot walk is required" is
  true but was used to justify stopping; with the 32-bit library the same scan
  does resolve `MCUDrv::lensSearch` -> `AfMgr::CCTMCUNameinit` via the ARM PLT.

#### 23.7 Follow-up: AF now runs a full search cycle and times out

After the §23.3 patch and the §23.1 node permissions, the AF chain engages
completely. `MtkCam/StreamingProcessor` reports a full state machine cycle per
focus tap (three taps, one log):

```
[0:isAfCallback] AFstate(1 -> 3), msg(0), msgExt(0), AfCb(0)
[0:isAfCallback] AFstate(3 -> 5), msg(4), msgExt(0), AfCb(1)      <- searching
[0:isAfCallback] AFstate(5 -> 3), msg(0), msgExt(0), AfCb(0)      <- ~3.1 s later
[0:isAfCallback] AFstate(3 -> 6), msg(2048), msgExt(0), AfCb(1)
[0:isAfCallback] AFstate(6 -> 3), msg(0), msgExt(0), AfCb(0)      <- ~4.6 s later
```

So AF is no longer dead: it triggers, searches for about three seconds, and
gives up. That is the signature of an AF that runs but cannot converge, not one
that never starts — a completely different failure from §20/§22.

Also confirmed present and healthy at the framework level:

```
afeng-max-focus-step: 1023        <- the AF engine knows the motor step range
focus-mode: auto
focus-distances: 0.95,1.9,Infinity
```

#### 23.8 What the remaining blocker is, precisely

Three independent checks all say the VCM motor is never commanded:

| check | result |
|---|---|
| `dmesg \| grep -iE 'mainaf\|vcm\|lens\|motor\|gaf'` | empty — the VCM driver never logs anything |
| HAL error strings (`invalid m_fdMCU`, `mcuIOC_*`, `please check kernel driver`) | never printed, so the ioctl path is not reached at all |
| `debug.af_motor.position=1` (read by `lib3a.so`, `AfAlgo::isAFMotorStop`) | no motor-position log ever appears |

The reason it is hard to see from the code is that every entry point into the
lens driver is a virtual call through a vtable that is zero-filled in the file
and only populated by the loader:

* `MCUDrv::lensSearch` / `getCurrLensID` are reachable only because
  `AfMgr::CCTMCUNameinit` calls them through the ARM PLT (§23.2).
* `GAFLensDrv::init`, `LensDrv::init`, `LensSensorDrv::init`,
  `GAFLensDrv::moveMCU`, `GAFLensDrv::setMCUInfPos` and `LensCustomInit` have
  **zero** direct `bl` call sites in `.text` — all eight PLT stubs exist and all
  eight are called virtually.
* `_ZTVN6NS3Av35AfMgrE` and `_ZTV8AfMgrDev<...>` are zero in the file, and this
  build's `.rel.dyn` (`ANDROID_REL`, 0x3b98 bytes at file 0x43ac4) does not
  contain usable addends for that range, so the slot order cannot be recovered
  statically. It *can* be read from the running process
  (`/proc/$(pidof mediaserver)/mem` at `load_bias + 0xde890`,
  `load_bias = 0xec918000` on this boot) — that is the next concrete step.

#### 23.9 Diagnostic worth keeping

Patching the `cbz` at `0x65724` (`ThreadRawImp::enableAFThread`, guard before the
failure log) to a `nop` makes the log unconditional and is a one-instruction way
to tell "not called" from "called and failed":

```
E Hal3ARawImp/thread: [enableAFThread()] Err: 591:, [enableAFThread] result(0)
```

With the lens-id patch alone the AF thread **is** created (`AFthread`,
`AFOBufThread_1`, `AAOBufThread_1` all appear in `/proc/$(pidof mediaserver)/task`),
so the nop is diagnostic only and is *not* part of the shipped patch. An earlier
sampling that showed no `AFthread` was a timing artifact — the thread is created
lazily when AF is engaged and torn down when it is not.

#### 23.10 State of the working tree

* `init.mt6797.rc` and the vendor preblob both carry the fixes, but the device is
  still running the *old* `system.img`, so the node permissions on the phone
  right now come from a manual `chmod 666 /dev/MAINAF /dev/SUBAF`. After a reboot
  without a rebuild the AF chain will be back to "starts, finds no lens, AF off".
* Correct order for verifying on hardware: build and flash, then check
  `ls -la /dev/MAINAF` shows `crw-rw---- system camera` before blaming anything
  else.

### 28. IR remote: the HAL was already written, never enabled (2026-10-01)

#### 28.1 Symptom

An installed IR remote app (`com.duokan.phone.remotecontroller`) opens fine but has
no "add remote" button, where the same app on other phones shows one.

#### 28.2 The hardware and driver are all present

```
/dev/irtx                crw-rw---- system system 245,0
kernel symbols           irtx_probe, irtx_isr, switch_irtx_gpio, compare_irtx_code
driver source            drivers/misc/mediatek/irtx/mt6797/mt_irtx.c
DT (SoC, mt6797.dtsi)    irtx@1101d000, compatible = "mediatek,irtx",
                         pwm_ch = <3>, clock-frequency = <26000000>
kernel config            CONFIG_MTK_IRTX_PWM_SUPPORT=y
ueventd.mt6797.rc:80     /dev/irtx  0660  system  system
```

`/dev/irtx` is owned by `system`, which is exactly what `system_server` runs as,
so permissions are already correct.

#### 28.3 The stack is JNI-era, not HIDL

`frameworks/base/services/core/java/com/android/server/ConsumerIrService.java`
declares `native long halOpen()`, `native int halTransmit(long, int, int[])` and
`native int[] halGetCarrierFrequencies(long)`. The JNI shim
`frameworks/base/services/core/jni/com_android_server_ConsumerIrService.cpp` calls
`hw_get_module(CONSUMERIR_HARDWARE_MODULE_ID, …)`. So no `hardware/interfaces/ir`
is needed — which is fortunate, because that directory does not exist in this
tree at all.

`hasIrEmitter()` returns `mNativeHal != 0`, so with the module missing every app
hides its IR UI. That is the actual reason the button is absent — nothing is
wrong with the app.

#### 28.4 The MTK HAL already existed, in the device tree

`device/xiaomi/nikel/consumerir/consumerir.c` (12 210 bytes, committed as
`1e29b00`) is a complete implementation. Its waveform conversion, which had to be
recovered from the driver, is:

```c
buffer_len = ceil(total_time / (float)32);      /* one bit per microsecond */
for (i = 0; i < pattern_len; i++)
    for (j = 0; j < pattern[i]; j++) {         /* pattern[] arrives in uS */
        if (current_level) *(wave_buffer + int_ptr) |=  (1 << bit_ptr);
        else               *(wave_buffer + int_ptr) &= ~(1 << bit_ptr);
        bit_ptr++; if (bit_ptr == 32) { bit_ptr = 0; int_ptr++; }
    }
current_level = !current_level;
ioctl(fd, IRTX_IOC_SET_CARRIER_FREQ, &carrier_freq);
write(fd, (char *)wave_buffer, buffer_len * 4);
```

This matches the driver exactly and explains its otherwise odd constant:
`PWM_MODE_MEMORY_REGS.HDURATION = 25` at a 26 MHz clock is 0.96 µs, i.e. one
microsecond per bit. Its `IRTX_IOC_SET_CARRIER_FREQ` is `_IOW('R', 0, unsigned
int)` = `0x40045200`, byte-identical to `IRTX_IOC_SET_CARRIER_FREQ` in the
kernel's `mt_irtx.h`.

#### 28.5 Three gates were shut

```
device/xiaomi/nikel/board.mk:29            MTK_IRTX_SUPPORT := true
device/xiaomi/nikel/consumerir/Android.mk  ifeq ($(strip $(MTK_IRTX_SUPPORT)),yes)
```

`true` is not `yes`, so the entire module definition was skipped. Then:

* `device/xiaomi/nikel/consumerir/Android.mk` marks the module
  `LOCAL_MODULE_TAGS := optional`, so even when defined it is installed only if
  named in `PRODUCT_PACKAGES`. Nothing did.
* `android.hardware.consumerir` was never installed; the AOSP file already exists
  at `frameworks/native/data/etc/android.hardware.consumerir.xml` and was simply
  not listed.

Result: `/system/lib/hw/` had no `consumerir.*.so` at all.

#### 28.6 Fix — three lines, all three must land together

| file | change |
|---|---|
| `board.mk` | `MTK_IRTX_SUPPORT := true` → `yes` |
| `common.mk` | `PRODUCT_PACKAGES += consumerir.$(TARGET_BOARD_PLATFORM)` |
| `permissions.mk` | install `android.hardware.consumerir.xml` into `system/etc/permissions` |

They are not independent. `ConsumerIrService`'s constructor throws if the feature
is declared but `halOpen()` returns 0, **and** if `halOpen()` returns non-zero
while the feature is absent — either way `system_server` fails to boot:

```java
mNativeHal = halOpen();
if (hasSystemFeature(FEATURE_CONSUMER_IR)) {
    if (mNativeHal == 0) throw new RuntimeException("FEATURE_CONSUMER_IR present, but no IR HAL loaded!");
} else if (mNativeHal != 0) {
    throw new RuntimeException("IR HAL present, but FEATURE_CONSUMER_IR is not set!");
}
```

#### 28.7 Known, deliberate: SELinux

`/dev/irtx` is labelled `device:s0` and this tree has no rule granting the
`system` domain `device:chr_file`. Every transmit will therefore log

```
avc: denied { open } for … path="/dev/irtx" … permissive=1
```

which is allowed, because this ROM runs SELinux permissive — consistently with
`ioctl_defines` and `ioctl_macros` being deleted from `system/sepolicy`
(-2802 lines, see the tree-wide diff). Not worth an sepolicy edit right before a
build for a denial that changes nothing.

#### 28.8 Not verified on hardware

The device-side check could not run: `/dev/irtx` is `0660 system:system`, the adb
shell is `uid=2000(shell)`, there is no `su`, and `adb root` is refused. Running
MIUI's own `/bin/consumerird` from adb therefore failed at `open()` and produced
no driver log — a misleading result, since the real HAL runs as `system`. The
protocol itself is confirmed from source on both sides; what still needs hardware
is whether a transmitter LED is physically present.

### 29. ADB ran as root, which silently killed scrcpy's clipboard (2026-10-01)

#### 29.1 Symptom

scrcpy 3.3.4 mirrors fine, keyboard and mouse work, but `Ctrl+V` does nothing
and the device→PC clipboard sync never fires. Everything else about scrcpy is
fine, so it reads as a scrcpy bug. It is not.

#### 29.2 The actual failure

Running `scrcpy --verbosity=debug` against the device produced, on the
server's control thread:

```
[server] ERROR: Exception on thread Thread[control-recv,5,main]
java.lang.SecurityException: Calling uid 0 does not own package com.android.shell
	at android.os.Parcel.readException(Parcel.java:1692)
	at android.content.IClipboard$Stub$Proxy.getPrimaryClip(IClipboard.java:187)
	at android.content.ClipboardManager.getPrimaryClip(ClipboardManager.java:134)
	at com.genymobile.scrcpy.wrappers.ClipboardManager.getText(ClipboardManager.java:27)
	at com.genymobile.scrcpy.device.Device.getClipboardText(Device.java:106)
	at com.genymobile.scrcpy.device.Device.setClipboardText(Device.java:119)
```

`ClipboardService` only answers a caller that owns `com.android.shell` (uid
2000). On this device adbd was uid 0:

```
$ adb shell id
uid=0(root) gid=0(root) groups=(...,1011(adb),...) context=u:r:su:s0
$ adb shell getprop ro.debuggable
1
```

Two consequences, and the second is the worse one:

1. `Ctrl+V` cannot work — it has to write the device clipboard first.
2. The exception kills the **control-recv thread**, so *every* scrcpy shortcut
   dies, not just the clipboard ones. And because clipboard autosync runs at
   startup, it crashed without the user touching copy/paste at all.

`--no-clipboard-autosync` avoids the startup probe and the exception
disappears entirely — verified. `MOD+Shift+V` ("inject computer clipboard text
as a sequence of key events") still works under it, because that path never
touches the clipboard. `MOD+C` / `MOD+X` also work: they inject the Android
COPY/CUT keycodes and never call the clipboard service.

#### 29.3 Root cause: two build-time overrides, from two commits

```makefile
# device/xiaomi/nikel/system.prop:73-76   — commit 1597ece, "enable ADB by default"
ro.debuggable=1
ro.adb.secure=0
persist.sys.usb.config=adb

# device/xiaomi/nikel/device.mk:80-82     — commit 1e29b00 (initial import), "# Debug"
ADDITIONAL_DEFAULT_PROPERTIES += ro.adb.secure=0
ADDITIONAL_DEFAULT_PROPERTIES += ro.secure=0
ADDITIONAL_DEFAULT_PROPERTIES += ro.debuggable=1
```

`persist.sys.usb.config=adb` is what made ADB come up on every boot;
`init.usb.rc:38` turns it into `start adbd`:

```
on property:sys.usb.config=adb && property:sys.usb.configfs=0
    write /sys/class/android_usb/android0/functions ${sys.usb.config}
    start adbd
```

The `ro.debuggable=1` is what made that adbd run as **root**, which is what
broke the clipboard.

#### 29.4 Why deleting those lines is not enough

`breakfast nikel` with no second argument defaults to **userdebug**:

```sh
# vendor/cm/build/envsetup.sh — sourced from build/envsetup.sh:1723
if [ -z "$variant" ]; then
    variant="userdebug"
fi
lunch lineage_$target-$variant
```

and a userdebug build injects `ro.debuggable=1` from the build system itself.
So the tree-side removal only takes effect on a **user** build. `brunch nikel`
is exactly `breakfast nikel` + `mka bacon`, so the convenient command is also
the one that silently produces a root-adb ROM.

#### 29.5 Fix

| file | change |
| :--- | :--- |
| `system.prop` | `persist.sys.usb.config=adb` → `mtp`; `ro.debuggable` / `ro.adb.secure` overrides removed |
| `device.mk` | the three `ADDITIONAL_DEFAULT_PROPERTIES` debug overrides removed; only `ro.adb.secure=1` kept |
| build | `breakfast nikel user` — **not** `breakfast nikel` / `brunch nikel` |

ADB then no longer starts on its own, USB enumerates as MTP, and ADB can still
be enabled by hand from Developer options with an RSA fingerprint. A `user`
build also means `ro.debuggable=0`, so adbd is not root and the clipboard works.

Deliberately kept: ADB reachable on demand. It is still the only practical way
to recover this device — see §23.10, `/dev/MAINAF` reverts to `crw-------` on
every boot until the `init.mt6797.rc` fix (§23.1) is actually in the flashed
`system.img`, and that needs `adb shell chmod`.

#### 29.6 If a root shell is wanted on purpose

`adb root` is refused on a user build, and there is no `su` in this ROM
(`/system/xbin/su` and `/system/bin/su` both absent). Building `userdebug`
brings the root shell back — and the clipboard exception with it. The two are
not separable: root adbd *is* the thing ClipboardService rejects.

---

## NOT FIXED

### 8. Voice calls crash the C2K modem (MD3) — known, community-wide

- **Symptom**: MO call: `ATD` accepted (OK) then `+ECPI 130` release ~1.4 s
  later, "Mobile network not available", RIL restart. MT call: rings ~2 s
  then missed. Both SIMs.
- **Root cause**: when the speech path is allocated, a speech-param message
  (type 19) is broadcast MD1→MD3 via SMEM; ~0.5 s later MD3 (C2K) throws
  `CCCI_MD_MSG_EXCEPTION` (`ee=a3d/a3f`, `LTE_EXP`), the watchdog resets
  MD1+MD3, radio drops.
- **What has been ruled out** (do not re-test these):
  - AT sequences are identical to working MIUI (M-gen).
  - RIL chain, vendor blobs, `/system/etc` identical (md5) to a known-good
    SamarV ROM which still fails calls.
  - Audio HAL/speech libs: MIUI versions tested (32+64 bit) — still crashes.
  - Modem firmware: original / hellas arΩma / official V10.2.1.0 — all crash.
  - 2G-only mode still crashes → not CSFB related.
  - Single-SIM and dual-SIM both crash.
  - 2026-09-15 deep dive — new dead ends (logs in `nikelbuild/radio_*_test.log`, `miui_radio.log`, `getprop_los_2g.txt`):
    - `EVADSMOD` not the trigger: LOS now `IMS: AT+EVADSMOD=1 Fail !!` + `+CME ERROR: 100` identical to MIUI (`miui_radio.log:39422`), yet MO still `+ECPI: 1,130` → `RADIO_UNAVAILABLE` (`radio_2g_call2.log:237`, `21:22:34` after patch). `setprop persist.mtk.*` does not suppress init.
    - `AT+CMUT=0` → `ERROR`/`GENERIC_FAILURE` on both ROMs — not a differentiator.
    - `ATD` format `ATD=xxx` in MIUI is redaction only (0 raw numbers in 48698 lines); LOS `ATD+628…;` vs `ATD0823…;` both crash, format irrelevant. Blob only has `ATD%s%s;`/`ATDE%s%s;` (`0x897c8`/`0x89790`).
    - `EVOCD`/`+EVOCD:6` + `UNSOL 3052` identical both sides.
    - IMS init suppression via 1-byte blob patch `AT+E`→`AT+X` (10 strings: `ECSRA=1`×2 + `EIMS*`×8, `/tmp/md3off/*_mtk-ril.so.patched` md5 `2d2e…`/`d536…`, verified 0 `ECSRA`/`EIMS` in radio after push via recovery) — still `ECPI 130` MO `21:22:34.972` → `RADIO_UNAVAILABLE 21:22:35.465`. Init not the gate.
    - MD3 boot disable (`init.modem.rc:149,156` `ccci3_fsd`/`ccci3_mdinit` → `disabled`, boot `9785344` B) — MD1 asserts `cc_irq.c:1022` `ee=23f` ASSERT at boot (`dmesg 72.330*`), SMEM mdipc requires MD3. Reverted via `boot_backup_20260915.img` (16 MiB, `mmcblk0p21`).
    - AP→MD3 speech path block `chmod 000 /dev/ccci3_aud` (audioserver single fd `ccci_aud` only) — still `ee=a3f` `ccci3/ken MD exception timer 2` at `730–731s` MO, `2474s` MT + `voice_trigger 1→0→1` before. Crash is modem-internal MD1→MD3 SMEM type 19 broadcast, not AP device.
    - Slot-independent: MO via `SUB1`/`RIL_SOCKET_2` (`DIAL [SUB1]` `20:52:16.930`) same `ECPI 130` `20:52:16.948` → `RADIO_UNAVAILABLE 20:52:17.331` as `SUB0`. MIUI success was `SUB1` but LOS fails both slots.
- **2026-09-30 — single-variable A/B bisect (see §8f). ADD to the do-not-re-test list:**
  - **Kernel** — LOS kernel + MIUI ramdisk + MIUI `/system` = **no crash**. The
    kernel is not the cause. (`tmp_miuidiff/x/testC_boot_LOSkernel_MIUIrd.img`)
  - **IMS / VoLTE props** — MIUI with `ro.mtk_ims_support=1` deleted = **no
    crash**. That prop is provably the switch that enables the C2K/ECCVI speech
    negotiation (1 line in MIUI, 105 with the prop removed, same as LOS) — and
    the negotiation runs to completion (`Ack done`) without crashing. So
    `CheckSpeechParamAckAllArrival() Fail` is normal on *both* ROMs.
  - **35 telephony props** (incl. `ril.first.md=1`, `ro.mtk_mobile_management=1`,
    `ro.mtk_antibricking_level=2`, `ro.mtk_perf_simple_start_win=1`) on the LOS
    system = **still crashes**. See §10b-e — that entry's "fix" is refuted.
  - **Audio param config** — `libaudio_param_parser.so` swapped for MIUI's
    (32+64 bit) **plus** the missing `etc/audio_param/b6a/` tree (61 XMLs)
    = **still crashes**. (`DT_NEEDED` identical, only new symbol `atoi`.)
  - **SIM2 state** — SIM2 forced to register properly (`mServiceState=0`,
    `EmergOnly=false`, PLMN 51021) = **still crashes**. The long-standing
    "broken SIM2" confound is now closed.
  - **Symptoms, not causes** (do not use as evidence): the `AudioParamParser`
    `inotify_add_watch failed` loop (see §19.3) tracks the crash perfectly at
    n=5 and is **not** causal — patching the watched path away (`0` errors) still
    crashes; `MobiCore mcd` hotplug storms run at the same 19-20/s in *every*
    configuration including the working one; `AudioALSAStreamOut: open()` and
    `EnableSideToneFilter` appear in both crashing and non-crashing captures.
  - **Cannot be done at all**: swapping the native RIL (`mtk-ril.so`,
    `librilmtk.so`, `libril.so`) — LOS ships AOSP 7.1 `libril.so`, MIUI ships
    6.0; the ABI is incompatible and a previous swap attempt overwrote the
    shared 64-bit `libutils`/`libbinder`/`libbase` and nearly bricked the device.
    The speech/C2K libraries are in fact byte-size identical across both ROMs
    (`libc2kril.so` 94684, `libc2kutils.so` 26356,
    `libviatelecom-withuim-ril.so` 269228) — only the generic, version-locked
    Android stack differs.
- **2026-09-15 late night — BREAKTHROUGH: MD3 EX record fully decoded** (artifacts: `ccci_dump3.txt`, `dmesg_call_full3.txt`, `md3_exrec.bin` in `nikelbuild/`; `md3_exrec.bin` = parsed out of the "Dump MD EX log" block, `Base: ffffffc0b8299beb`):
  - Data read from `/proc/ccci_dump` (the CCCI NORMAL buffer is not printed to the console because `ccci_debug_enable` defaults to 4; set `echo 6 > /sys/kernel/ccci/debug` for all prints, or 5 for EE details only, without the noise).
  - Record: `ex_type=15 LTE_EXP`, **file = `mon/monfatalerror.c`**, **ExStr = `Ex Enter` + `Exception Nested Happened! \r\n`**, `Hisr65`, PC/LR MD3 = `0x00106A85`/`0x00106A84`, param `Ex D 0x204 / 0xD1`.
  - The strings `mon/monfatalerror.c` and `Exception Nested Happened!` exist only in the **MD3 firmware `modem_3_3g_n.img` @file 0x3119e0** (not in MD1) → this EE belongs to MD3's own monitor.
  - **Nested** = the MD3 EE has now been entered for the 2nd time; the first record (the real speech cause) was overwritten. PC/LR 0x106A84 = MD3's mailbox-read monitor loop (`bl 0x1063ac` reads 12 bytes, msg `[0]='Y' [9]=8 [4]=1`).
  - **MD3 mapping: runtime addr = file offset − 0x200** (proved by the dump `Base: ffffff80045fc000` whose content "MMM\0..." sits at file 0x200). Everything below is disassembled at runtime addresses; literal pools: `monfatalerror` ref @runtime 0x1066be/0x106cae (pool 0x1066d4/0x106cc0), `Ex Enter` @0x1066ea (pool 0x106a1c), `Nested` @0x106726 (pool 0x106a30).
  - Identified functions (runtime): mailbox read = `0x1063ac` (inner `0x989f4`), monitor main loop = `0x106870`–`0x10699a` (msg `[0]='Y'`, length filter `[9]`∈{8,11}, `[4]`/`[8]`==1), EX record builder = `0x1067c0`–`0x106810` (`strb type 5/15` into `[r4+0x10]`, copy to `[r4+0xfc..0x138]`), log helper = `0x106248` (args r0=level, r2=str, r3=len).
  - **New conclusion**: the MD1 broadcast type-19 is not what kills the AP directly — MD3's own monitor enters an EE **nested** when the speech path is on; the ORIGINAL exception (the first instance) is never recorded. MD3's speech handler layer still has to be mapped.
- **2026-09-15, continued — MD3 tooling + diagnostic patch** (firmware backup: `/tmp/md3off/modem_3_3g_n.img.orig` md5 `bfe0d82a183724a1387cec901e7aecc8`):
  - Wrote a pool-aware disassembler (`/tmp/md3off/md3dis.py`): thumb16 `ldr rX,[pc,#imm]` literal scan + rodata string annotation. MD3 EE entry = runtime `0x1066e8` (file `0x1068e8`): print `Ex Enter` → check the EE counter `[0x69dffc]` (`0xff`=fresh, `+1==1`=fresh, anything else=nested → print `Exception Nested Happened!`, copy the task context `[0x5903c0]` into the record, then `b self` and hang).
  - `ee=a3f` vs `a3d`: bit1 = `MD_EE_DUMP_ON_GOING` — it used to hang (dump on-going) and only stopped hanging after the patch.
  - **Diagnostic patch `nested2fresh`**: file `0x106924` `1ad0`→`1ae0` (`beq fresh`→`b fresh`, so the nested path always takes the fresh branch and does not hang). md5 `49c57753808dfeb7edb48a0e44658832`. Flash via TWRP (`/system/etc/firmware/modem_3_3g_n.img`).
  - **MO test result**: `ee=a3d` (no hang), but the record STILL reads `LTE_EXP` + file `mon/monfatalerror.c` + code1/2 `"mon/monf"` → that file/code is the **hardcoded identity of MD3's EE handler**, NOT the real fault information. The first SWINT instance never carries file/line to the AP; the true fault context lives only in MD3's internal EE dump (needs mdlogger/DHL, which LOS does not have).
  - First CCIF packet before the EE: `Q0 Rx msg 0 24 80000006 0` (36 bytes, MD3→AP "Ex Enter") 110 ms after speech alloc; `Q0 Rx 80000006` from MD1 is most likely the triggering speech type-19 message.
  - **2026-09-15, second night — continued reverse of the MD3 speech path** (checkpoint; the exact handler still not found):
    - `Q0 Rx 80000006` = a CCIF packet MD3→AP carrying the EE notification ("Ex Enter", 36 bytes) — an EFFECT of the crash, not the trigger. The speech message MD1→MD3 travels over SMEM `0x8e200000` between modems and is invisible on the AP.
    - The `0x80000006` constant is an MD3 literal @file `0x2391ad`/`0x309a21` (not pool code).
    - MD3 speech map: tasks `SpeechReadMsg`/`SpeechWriteMsg`, queue `S2_SPEECH`, `SPC2K_UL_GetSpeechFrame`, `SvcSendSpeechConnMsg`, `mdSpeechLoopBackModeMsgProc`; modules `mdipc/` (cc_irq_msg_v2/v2, cc_sys_comm_v2, cc_irq_spinlock) + `hwd/hwd_speech/` (hwdsph/hwdvm/hwdaudioservice). mdipc code at runtime ~`0x100200`–`0x101000` (init `0x100448`: allocate 4 groups via `bl 0xff198`, `bl 0x1008bc/0x100f74/0x1013b4`, register msg `0x10deb8`).
    - Tooling: `/tmp/md3off/md3dis.py` (pool-aware thumb16 disasm + string annotation; the thumb32 `ldr.w` scanner returns nothing — MD3's pools are interleaved with data, so Ghidra/IDA is needed to go further).
    - Factor that makes patching easier: nikel's MD3 C2K is data-only (GSM speech always goes through MD1) → a NO-OP MD3 speech handler is practically safe, but the handler function has not been identified.
  - **2026-09-16 — patch `eeoff` (MD3 EE entry → `bx lr`)**: runtime `0x1066e8` (file `0x1068e8`) `b5f0...`→`4770 bf00`, md5 `9207f7cc9124fae10f4985a40ef51f8c`. **FAILED**: MD3 still sends the EX (`ee=a3d` @158 s, voice_trigger→110 ms) — the EX packet is emitted by MD3's CCIF state machine **before** the EE entry call (the EE entry only builds the record). The second copy of pool `0x106cc0` is an ordinary logging function, not the EE entry. Firmware **reverted** to the original. This kills every AP-side and MD3-side patch based on the packet/EE; what is left: patch the MD1 speech broadcast type-19 (reverse `modem_1_ulwctg_n.img`, 15.8 MB) or change the kernel reset policy — both are large.
  - **Firmware reverted** to the original `bfe0d82a...` (clean baseline). `nested2fresh` was not adopted.
  - **2026-09-17 — test Vernee MOLY W1539 (madOS Apollo Lite) on nikel's MD1 — FAILED + NVRAM hit**:
    - nikel's modem firmware actually lives in the **partitions**: `md1img`=`mmcblk0p12` (24 MB, W1603.P90), `md1dsp`=p13 (4 MB), `md1arm7`=p14, `md3img`=p15 (5 MB) — `/system/etc/firmware/*` is only a fallback (patching the firmware through /system had NO effect; the `ee a3f`→`a3d` change was a timing race, not an effect of the patch).
    - Partition backups before the test: `/sdcard/md1img_backup.img` (24 MB, md5 `bfd11123`), `/sdcard/md1dsp_backup.img` (4 MB, `69acba4b`); NVRAM backup `/sdcard/nvram_md_backup.tgz` (93 KB).
    - Flashed Vernee `modem_1` W1539.V27 (madOS zip, 15.3 MB) + `dsp_1` into the partitions → boot loop (`md1 bootup/reset_start`, `NOT_READY`); Vernee MOLY is incompatible with nikel (X20 vs X20M calibration/config).
    - Restored p12+p13 from the backups → the original partitions came back (`bfd11123`/`69acba4b`), md1/md3 ready.
    - **REMAINING DAMAGE**: the Vernee firmware touched `/data/nvram/md/NVRAM` (`SWCHANGE` updated to Vernee, `NVD_DATA` timestamps 2026-09-14/16). `SWCHANGE*` was deleted and `NVD_DATA` wiped so it would regenerate → both SIMs flapped `READY`↔`NOT_READY` with a `wait to reset` loop, intermittently. Not 100% recovered — it needed a cold power-off (battery out for 10 s) plus time for the NVRAM to regenerate. No foreign modem firmware may be flashed again; partition p15 currently holds a copy of `md3rom.img.orig` (`bfe0d82a`) that used to be normal (signal OK, calls still crash).
  - **2026-09-17, night — FULL RECOVERY via stock MIUI V10.2.1.0 fastboot** (`/run/media/corex/System/ROM-nikel/nikel_global_images_V10.2.1.0.MBFMIXM_20190123.0000.00_6.0_global/images/`):
    - Flapping cause verified: partition `md3img` (p15) held a copy of `/system` (a different version, head `b4 45 3e 00`, size `0x3e45b4` vs the file's `0x3e1a04`) plus Vernee's leftover NVRAM → MD1 hung (`AT+CGREG?` no response) with a WDT reset loop (`wait to reset`).
    - Fix: **flash the modem partitions directly over fastboot** — `fastboot flash md1img images/md1rom.img && fastboot flash md1dsp images/md1dsp.img && fastboot flash md1arm7 images/md1arm7.img && fastboot flash md3img images/md3rom.img` (MIUI's stock `flash_all.sh` does flash the modem over fastboot — no SP Flash Tool or scatter file needed for the modem partitions). Bootloader unlocked → all good.
    - The stock `md3rom.img` head `b4 45 3e 00` = **identical to the original partition's header** (md5 `cadc0922`, size `0x3e5610`) — the genuine `md3rom` was found in the MIUI fastboot ROM.
    - **Result: signal fully recovered** (`READY,READY`, md1+md3 ready, TELKOMSEL+3, baseband W1603.P90) — with NO NVRAM format, IMEI intact, `NVD_IMEI/MP0B_001` untouched.
    - Lesson: a MIUI recovery zip ≠ full stock; what actually repairs the modem is the **fastboot ROM** (it carries `md1rom/md1dsp/md1arm7/md3rom/preloader` + the scatter file). NVRAM polluted by foreign firmware recovers by re-flashing the stock modem with no wipe.
- **Open path (not attempted)**: patch the MD3 firmware `modem_3_3g_n.img` (the copy in `/system/etc/firmware`, flashable via TWRP):
  1. Reverse the MD3 speech message handler (the SMEM MD1↔MD3 type-19 receiver / the mailbox messages `[9]=8`/`[9]=11`) → NO-OP or swallow them.
  2. Or patch the nested-EE guard so the first instance is not overwritten (which yields the real assert file/line).
  - Next tooling step: a per-function pool-aware disassembler (entrypoints `0x1063ac`/`0x106248`/`0x1067c0`) scanning 16-bit `ldr rX,[pc,#imm]` literals (the script is already written; it produced the 4 references above).
- **Conclusion**: kernel/modem-era speech subsystem incompatibility (M-gen
  firmware + N-gen AP stack). Only M-gen (Android 6) stacks work.
- **Status**: not fixable from /system without kernel source + modem research
  (e.g., EE dumps via Comsecuris `mtk-baseband-sanctuary`).
- **Workaround**: VoIP (WhatsApp/Telegram). Alternatively use an Android 6.0
  ROM for calls.

### 10b-c. Goodix enroll error 1058 — TEE gatekeeper missing

- **Symptom (2026-09-14, after the Goodix switch):** the fingerprint menu
  works, the sensor produces IRQs and the framework gets acquisitions, but
  enrollment instantly fails. The TA disassembly shows 1058 is NOT the
  template limit (that is 1005, `gf_algo_fingers_limit_check`): 1056/1057/
  1058 are three token checks in `gf_ta_invoke_cmd_entry_point` — 1058 is
  the **HMAC verification of the 69-byte enroll/auth token** (37-byte HAT
  + 32-byte HMAC) computed by `gf_generate_hmac` → `get_hmac_key`, which
  derives the key from a 12-byte seed inside the TA via a tlApi call.
- **Root cause:** LineageOS ships no `gatekeeper.*.so` at all; in
  `system/core/gatekeeperd/gatekeeperd.cpp`, `hw_get_module_by_class()`
  fails and gatekeeperd logs "falling back to software GateKeeper"
  (`SoftGateKeeperDevice`). The soft-signed token cannot satisfy the TA's
  HMAC check → every enroll returns 1058.
- **Fix:** MIUI's TEE gatekeeper HAL exists in `system{,-img}/lib{,64}/hw/
  libMcGatekeeper.so` with `gatekeeper.mt6797.so` / `gatekeeper.nikel.so`
  symlinks pointing at it (plus the AOSP `3d08821c…` gatekeeper trustlet,
  already present in the shipped registry). Shipped both arches as
  `system/lib{,64}/hw/gatekeeper.mt6797.so` (vendor commit 0e5ea60).
  Requires `libgatekeeper.so` (built by AOSP `system/gatekeeper`), already
  in the ROM.
- **Caveat:** credentials enrolled while the soft gatekeeper was active
  (any PIN/pattern set before this fix) cannot be verified by the TEE
  gatekeeper. Remove the screen lock before flashing, or wipe
  `/data/misc/keystore` + `/data/system/locksettings*` in recovery.

### 11. Hotspot 5 GHz DFS channels

- Only non-DFS channels are guaranteed; if the MTK AP firmware rejects
  5 GHz at `fwReload`, the fallback is to ship 2.4 GHz only and remove the
  5 GHz band from the framework gate (see #5 patch).

---

### 19. Build-integrity defects found while chasing #8 (2026-09-30)

None of these are the call crash — all four are real defects in the LOS build
and none of them is caused by the crash. They were found because #8 forced a
full differential against a working MIUI `/system`. **Not fixed**; listed so the
next person does not rediscover them.

#### 19.1 RIL SELinux denials — MISDIAGNOSED, correcting the record

An earlier note in this section claimed `mtkrild` runs in the SELinux `toolbox`
domain. **That was wrong** and is corrected here.

Runtime check on the device:

```
mtkrild      label=u:r:mtkrild:s0        ls -Z  u:object_r:mtkrild_exec:s0
gsm0710muxd  label=u:r:gsm0710muxd:s0    ls -Z  u:object_r:gsm0710muxd_exec:s0
mnld         label=u:r:mnld:s0
```

The domain and the executable label are both correct. The `toolbox` string that
prompted the original claim was a `scontext` on a *single* `avc: denied` line,
not the process's domain. Lesson: confirm a SELinux claim with
`/proc/<pid>/attr/current` and `ls -Z <binary>`, never from one audit line.

What survives, and correlates perfectly with the crash:

| capture | `avc: denied` total | for `mtkrild` | crash |
| :--- | ---: | ---: | :--- |
| MIUI `/system` (+ LOS kernel) | 176 | **0** | no |
| LOS `/system` | 630 | **146** | yes |
| LOS `/system` (test G) | 453 | **88** | yes |

The reproducible part is the *denial count*, not a mislabelled domain. The
denials are an **effect**, not a cause: they appear only once the LOS system
drives the RIL into a path that requests access it does not have. Since the LOS
boot runs `androidboot.selinux=permissive` they are all allowed and block
nothing. Do not chase this as a crash cause.

Note: `device/xiaomi/nikel` still ships **no `sepolicy/` directory and no
`BOARD_SEPOLICY*` line**, so its policy is inherited wholesale from CM14.1's
AOSP tree. That happens to be adequate here (all three MTK daemons get correct
domains via AOSP's `rild`/`radio` rules plus MTK's own prebuilt policy), but it
is fragile and worth a native `sepolicy/` for the MTK daemons if the policy is
ever tightened. `SamarV-121/android_device_xiaomi_nikel` (branch `test1/cm-14.1`)
does carry a 59-file `sepolicy/` with `ril-daemon-mtk.te`,
`ccci_fsd.te`, `ccci_mdinit.te`, `md_ctrl.te`, `gsm0710muxd.te`,
`muxreport.te`, `mnld.te`, `nvram_daemon.te`, `thermal_manager.te` and the
matching `file_contexts`. That tree is the same upstream this device tree was
forked from (initial commit `db572dc`, Samar Vispute, 2017-05-14), and its
`sepolicy/` was never present in this lineage's history. Useful as a reference
if a native policy is ever needed — **but note its build also fails calls**, so
it is not a working-call reference for #8.

#### 19.2 `md_log_config` is missing

```
E ccci_mdinit(0): Open md_log_config file failed, errno=2!   (ENOENT)
```

LOS-only (0 occurrences with the MIUI `/system`). Not currently known to break
anything, but it means the CCCI modem-logger has no config and is running
unconfigured.

#### 19.3 `etc/audio_param/b6a/` is missing from the build; parser lib is stale — PARTIALLY FIXED

**Now fixed and verified on device (2026-09-30).** Committed in the vendor
repo as `d16b395`; a flashable image is at
`nikelbuild/tmp_miuidiff/x/b6a_system.img`
(md5 `a113886f681a0880cc157db6903de51f`, pristine LOS + these two changes).

- `proprietary-blobs.txt` lists `etc/audio_param/*.xml` as a **flat** list, so
  the board-specific `b6a/` subtree present in MIUI (61 XMLs, 380 KiB) was
  never copied. Fixed by dropping the 61 files into
  `vendor/xiaomi/nikel/system/etc/audio_param/b6a/` — the vendor tree already
  does `find-copy-subdir-files(*, vendor/xiaomi/nikel/system/, system/)`, so
  nothing else needed changing. All 61 verified to parse as valid XML.
- `libaudio_param_parser.so` in the LOS build is an **older blob**: it has 0
  occurrences of `b6a` and 0 of `/proc/cmdline`, so it cannot select the
  per-board parameter directory. The MIUI blob has 2 and 4 respectively.
  Replaced both the 32-bit and 64-bit copies. `DT_NEEDED` is the same set of
  10 libraries in the same order and the only new undefined symbol is `atoi`
  from libc, so the swap is link-safe. Note `lib/` and `lib64/` in the vendor
  tree are symlinks to `../system/`, so replacing the files under `system/`
  covers both. `audioserver` is 32-bit, so `system/lib` is the copy that
  actually loads (verified at runtime: md5 `a698d5aaac12af3f…`).
- Verified on device: 61 files present, correct md5, `md1`/`md3` ready. The
  audio HAL now uses Xiaomi's own `b6a` tuning.

**The 1 Hz `inotify` log flood is NOT fixed. Root cause corrected.**

An earlier version of this section blamed the loop on inotify being
unsupported on FUSE. **That was wrong.** `/proc/uptime`-verified on device:

```
$ adb shell strace -f -p <audioserver> -e trace=inotify_add_watch
1354  inotify_add_watch(7, "/sdcard/.audio_param/", IN_CLOSE_WRITE)
      = -1 EACCES (Permission denied)
```

It is a plain `EACCES` — an ordinary permission problem, not a FUSE
limitation. `/sdcard` is a FUSE mount mounted with `default_permissions`, so
the FUSE daemon simply applies the directory mode:

```
/sdcard/.audio_param   root:sdcard_rw   mode 0771
audioserver groups:    1006 1013 1026 1031 2950 3001 3002 3003 3007
                       (sdcard_rw = 3009 is NOT among them)
```

Consequences, each verified:
- `mkdir /sdcard/.audio_param_b6a` does **not** help — the directory was
  already present, and the failure is access, not absence. Creating it also
  does not stop the errors.
- `chmod 755` / `chown` are **no-ops**: `/storage/emulated` is a FUSE mount
  with `user_id=1023` that rejects metadata operations, and `/system` is
  read-only.
- Patching the watched path is **not viable either**: the field is 21 bytes
  and `/system/etc/audio_param` is 23 characters. There is no existing
  directory of 21 or fewer characters that would resolve correctly, so any
  such patch would point the parser at the wrong location.

The real fix is to add `audioserver` to the `sdcard_rw` group, i.e. a SELinux
change. That is blocked on the same grounds as §19.1: `secilc` is not
available on the build host and `rom_source/system/sepolicy` is a much newer
AOSP tree than CM14.1, so the policy has to be recompiled from matching
sources first.

Still a **symptom, not the crash cause** — two independent confirmations:
patching the watched path away drove the count to 0 with the modem still
throwing `ee=a3f` (test E), and shipping the full `b6a` + MIUI parser setup
still crashed (test D, and again on the device).

#### 19.4 MIUI cannot boot on CM14.1 `/data` — `system_server` crash loop

```
Caused by: java.lang.NumberFormatException: Invalid long:
           "0 0 0 0 1790711087323 0 1790711087822 0"
  at com.android.server.pm.PackageManagerService$PackageUsage.readLP(:1130)
  at PackageManagerService.<init>  →  SystemServer.startBootstrapServices
```

`/data/system/packages.xml` written by CM14.1 (Android 7.1) is not parseable
by MIUI (Android 6.0). `system_server` throws in its constructor, init restarts
it, it throws again — observed **40 restarts in 20 minutes** — so
`BOOT_COMPLETED` is never sent and the phone sits in the boot animation
forever with the SoC at full load.

- **Fix: always Format Data when switching ROMs.** After a format MIUI booted
  normally in 7 minutes.
- Symptom to recognise: boot animation loops indefinitely, `adb` is up, adb
  `logcat` is *busy* (that is the crash loop, not progress), and
  `getprop sys.boot_completed` stays empty while `system_server`'s pid changes.
- Related: #14 (off-charge bootloop) is a *different* failure with a similar
  look — check `dmesg`/`bootanimation` progress before assuming either.

#### 19.5 MTK MAL / AudioLink blob set is absent (not yet diagnosed)

MIUI ships 20 telephony/audio files that the LOS build does not:
`libfvaudio_call.so` (voice-call audio) plus `libmal.so` and
`libmal_{rds,datamngr,epdga,imsmngr,mdmngr,nwmngr,rilproxy,simmngr}.so`
(32 and 64 bit each). The client half, `libmdfx.so`, is **md5-identical** in
both ROMs (`22402641209261b875020d7ca0b9a8c9`) and contains the MFI client
(`MFI-Conn`, `mfia_task_bootstrap`, `mal-mfi`). On LOS the client is reached and
then fails repeatedly (`MFI-Conn: socket_local_client() error 2!!`),
correlating perfectly with the crash at n=6 (0/0/0 on MIUI-system captures,
20/20/119 on LOS-system captures).

**But `mal-mfi` does not exist anywhere** — not in `/system/bin`, not in
`/vendor/bin` (this device has no populated `/vendor` partition at all), and
`/vendor` inside the system image holds only 5 (LOS) / 7 (MIUI) entries with no
MFI. So MIUI never enters that code path and the correlation is a consequence
of *how* the LOS system drives the RIL, not a blob that can simply be added.
Do not spend time adding the 20 libs expecting the error to go away.

---

#### 19.6 Reverse engineering MD1: investigated, NOT FEASIBLE as-is (2026-09-30)

Follow-up to #8, since `chmod 000 /dev/ccci3_aud` proved the crash is
modem-internal and MD1 is the last untested layer.

**Setup.** Ghidra 12.1.3 headless, `md1rom.img` (15,992,880 byte, md5
`ff4cb9b57670d69731b72c9898b793a8`, base `0x00f3f7e0`, magic `0x58881688`,
`LCSH6797_6C_LW_M_MDBIN_PCB01_MT6797_S00.MOLY_LR11_W1603_MD_MP_V13_18_P90`).
Imported as `ARM:LE:32:v7`; full autoanalysis in 329 s; project 283 MB.

**Finding — MD1 has no symbol table.** An early count suggested ~13k
`_NAME` strings, which looked like a full symbol table. It is not:

```
"AUBUAF"            = ARM instructions that happen to decode as ASCII
"index < MPU_REGION_NUM" = an assert/trace string
(len16 + name) candidates  = 0
4-byte address before name = 0 of 2000
sorted?              = False   (a real symbol table is always sorted)
Ghidra symbol table   = 0
```

They are runtime trace/assert strings, as on MD3. The string count was a
false lead — do not repeat it.

**Finding — the runtime shortcut that made MD3 tractable does not exist for
MD1.** MD3 was mapped because the exception record carried `PC/LR
0x00106A85/0x00106A84` from `/proc/ccci_dump`. That buffer does not contain
an MD1 exception record at all:

```
ccci_dump*.txt (4 files, all pre-existing):
  23 unique hex addresses, all in 0x0040xxxxx-0x00455xxxxx
  MD1 base is 0x00f3f7e0  ->  zero matching addresses
  contents are "Dump MD layout struct" (pointers/sizes), not program counters
  "EE0: 00000000 00000000 ..." 3x, all zero
  only MD3 EE is recorded ("ee=a3d"/"ee=a3f")
```

So MD1 is 3x larger than MD3, has no symbols, and yields no PC/LR hint.
Mapping the SMEM type-19 receiver means searching ~3.4M ARM instructions by
pattern with no label to confirm a hit — and the disassembly alone cannot
tell a correct identification from a plausible one.

**Verdict: not feasible without vendor symbols or an MD1-side EE trace.**
Recorded so nobody spends a day rediscovering it. The only route that would
change this is a leaked MTK symbol map for `W1603.P90`, or MD logger/DHL
output from a stock MIUI stack (both MTK-proprietary).

Note: MD1 is TrustZone secure world, so even a known handler would be reached
through an indirect dispatch table, not the direct xrefs that sufficed on MD3.

---

---

## Investigation log — superseded findings and negative results

Kept so nobody re-treads them. Several conclusions here were later proven wrong
and are corrected in place — read the correction notes, not just the claim.

### 9. Camera front (5 MP) — historical trail (SUPERSEDED, fix in #12)

**Status: FIXED — final root cause and fix in section #12.** The trail
below is kept so nobody re-discovers it. Interim conclusions that were
later proven WRONG are marked with **[CORRECTION]**:

- **[CORRECTION]** "chip 0x5e20 / S5K5E2YA is the front sensor" — wrong;
  the front chip is **S5K5E8YX B6-qteck (id 0x5e85)** (see #9d trail and
  #12).
- **[CORRECTION]** "the swap approach is abandoned as unreliable" — the
  kdSensorList e3↔e6 swap is exactly part of the final fix; the earlier
  failures were because the test method (`fastboot boot`) never actually
  delivered the patched kernel, and because the bd13 kernel's
  front-camera power path is broken regardless of the swap.
- **[CORRECTION]** "HAL must send slave id 0x18" — wrong; the bd54
  kernel driver's own `i2c_addr_table` already contains 0x30
  (7-bit 0x18) and works with the stock LOS HAL.

Original trail (2026-09-10/11):

- **Symptom**: after fix #0 the rear camera (camera 0, BACK) works, but
  `Number of camera devices: 1` — the front sensor is never enumerated, so
  no app offers the front camera. Same on the bd04 and bd13 kernels.
- **Diagnosis trail** (do not repeat):
  - Kernel registers only `CAM[1]: s5k3l8mipirawnew` in `/proc/driver/camera_info`
    at boot — the sub-slot probe fails at boot on both kernels.
  - Runtime enumeration (`ImgSensorDrv impSearchSensor`, restart mediaserver
    to capture it): MAIN slot drivers 0/1 → `Err-ctrlCode (I/O error)`,
    driver 2 → chip responds with ID `0x5e20`; **SUB slot drivers 20000+ →
    all `Err-ctrlCode (I/O error)`**, `getSocketPosition:[2][-1]`.
  - Both kernels compile the same sensor driver set: `s5k3l8` variants (rear)
    + `s5k5e8yx` b6/sunny/qteck variants (front, 5 MP) + `s5k4h8`, `imx258`,
    `ov13853`, `s5k2x8` (other variants).
  - Replacing `libcameracustom.so` with the MIUI 9 (arΩma) build (20.9 MB vs
    our 10.6 MB) crashes mediaserver: `libcam.hal3a.v3.so` needs
    `cust_getFlashMaxIDutyiiiPiS_` which the MIUI lib does not export —
    M/N stack mixing (see lesson 1). Reverted.
- **Leads for the next attempt**:
  1. **The core loads in the HAL but its component registry stays empty.**
     Both processes are 32-bit and load `libMtkOmxCore.so`; the mediaserver
     additionally has `libMtkOmxVenc.so` / `libMtkOmxVdecEx.so` /
     `libvcodec_utility.so` / `libvcodecdrv.so` / `libvcodec_oal.so` mapped —
     the HAL does NOT (verified via /proc/<pid>/maps). The MTK core registers
     its components by dlopening those libs at core-init; that dlopen fails
     or is skipped inside the HAL. Find the init path difference
     (`MtkOmxCoreInit` reads something per-process or dlopens the component
     libs which fail on missing deps in the HAL's linker namespace).
  2. Compare environments: `cat /proc/<pid>/maps` for mediaserver vs
     media.codec, diff the loaded lib sets, and check what the core opens at
     init (`/dev/Vcodec`, NVRAM files) — run the HAL as root (temporarily
     `user root` in /system/etc/init/mediacodec.rc) to rule out uid issues
     (already tested: root alone does not fix it).
  3. Alternative: make the framework host OMX in mediaserver again
     (M-gen behaviour) — `OMXClient::connect()` has the path
     (`media.stagefright.codecremote=0` moves component INSTANCES to
     mediaserver and works — verified live — but the CODEC LIST used for
     selection still comes from the media.codec HAL, so the HW encoder is
     still never selected).
  4. A kernel source rebuild (sensor driver enable + hwmsen fix) would solve
     the front camera and possibly the media stack cleanly.
- **Tool**: a codec-list checker built from `Test3.java` (Java 7 + dx) run
  via `app_process`; source is 12 lines — rebuild as needed.

### 9b. Camera front (5 MP) — VCAM_D regulator wiring (historical trail, SUPERSEDED by #12)

- **Symptom**: front camera never enumerates. Kernel probe of every SUB
  driver fails; the front sensor never gets digital power.
- **Diagnosis trail** (do not repeat):
  - `_hwPowerOn powertype:9 powerId:1200000` fails → `Fail to enable digital
    power` → the sensor stays unpowered → `i2c-3 addr 0x2d ACK error` → no
    sensor ID → 1 camera device only.
  - DT (`soc/kd_camera_hw1@1a040000`) wires `vcamd_sub-supply` (and
    `vcamd_main2-supply`) to phandle 0x2f = **ldo_vgp3** — 1,200,000 µV is
    NOT a valid step in the kernel's VGP3 voltage table (steps 1.0/1.05/
    1.1/1.22/1.3/1.5/1.8V — decoded from the kernel binary at 0xbda2f0).
  - DTB **patched live** (boot.img offset 8330878, FDT): both
    `vcamd_sub-supply`/`vcamd_main2-supply` → phandle 0x80 = `ldo_vcamd`
    (range 900000–1210000). The wrong-rail problem is gone but the failure
    remains — the kernel's `mt6351_VCAMD_voltages` table
    (900000/950000/1000000/1050000/1200000/1500000/1800000) DOES contain
    1200000, yet `regulator_set_voltage` still fails inside `_hwPowerOn` —
    the remaining blocker is inside the kernel's mtk_regulator/mt6351
    driver or the camera_hw power switch; not resolvable without kernel
    source.
  - Patched boot kept flashed (vcamd wiring to ldo_vcamd is more correct
    than vgp3); boot backup: `/data/local/tmp/boot_bd13_backup.img`.
- **Conclusion**: needs kernel source (the camera_hw driver power path +
  the mt6351 voltage tables). MIUI M-gen works because its HAL drives the
  power differently (likely via the cam_ldo pinctrl GPIO path).
- **UPDATE — power path SOLVED via kernel binary patch (2026-09-10 late)**:
  The N-gen Apollo Lite kernel source
  (`github.com/MediatekAndroidDevelopers/android_kernel_vernee_apollo_lite`,
  branch n-7.1.2) contains the official workaround in
  `drivers/misc/mediatek/imgsensor/src/mt6797/camera_hw/kd_camera_hw.c`:
  the SUB DVDD converts Vol_1200 → Vol_1220 before regulator_set_voltage
  ("vcamd: unsupportable voltage range"). Our bd13 kernel binary predates
  this fix. Binary-patched the kernel image (boot kernel section,
  decompressed): the sub-sensor power tables at 10 offsets (0x115a808...,
  pattern `[4, 2800000, 0, 5, 1200000, 0, 7, ...]`) changed 1200000 →
  1220000. Result: **`powerId:1220000`, "Fail to enable digital power" = 0**
  — the front sensor now receives power. Remaining blocker: the front chip
  (ID 0x5e20) does not ACK on i2c-3 @ 0x36 (it previously ACKed during the
  MAIN-slot search — I2C mux / socket wiring mystery, next kernel-source
  task: kd_MultiSensorOpen bus switching). Patched boot (KNOWN-GOOD, now
  flashed): `/data/local/tmp/boot_frontcam.img` = binary-patched kernel
  (1220000 power tables) + DTB with vcamd_sub/main2 → ldo_vcamd. Boot
  backup: `/data/local/tmp/boot_bd13_backup.img`. Kernel source obtained:
  `kernel_apollo_n/` (n-7.1.2 branch, Helio X20).
- **Why the sub search still fails**: the SUB sensor search probes the
  **i2c-3 adapter** (kd_sensorlist.c: gI2CBusNum=BUS_NUM2 →
  g_pstI2Cclient2 = the "kd_camera_hw_bus2" client bound to the
  i2c@11014000 node), but the front chip (0x5e20) demonstrably responds on
  **i2c-2** (the MAIN adapter — it was FOUND there during the main-slot
  search reading 0x5e20). The rear s5k3l8 also sits at 0x36 on i2c-2, so
  the two sensors share address 0x36 on one bus — a hardware I2C mux is
  involved (an MT6306-style switch driven by kd_MultiSensorOpen's
  gI2CBusNum logic, whose original M-gen implementation differed).
- **ROOT CAUSE FOUND (session 3)**: the bd13 kernel (3.18.22) has **NO
  S5K5E2YA driver at all**. Its kdSensorList includes ov13853, s5k3l8
  (ofilm/sunny/qteck variants), s5k2x8, s5k4h8, imx377, imx258 and
  s5k5e8yx(b6) (sensor_id 0x5e80) — but the nikel front chip is S5K5E2YA
  (sensor_id 0x5e20). No matching driver ⇒ "No imgsensor alive" forever.
  Meanwhile /system/lib64/libcameracustom.so DOES contain the string
  "s5k5e2yamipiraw", so the userspace HAL side knows the sensor.
- **I2C bus fix applied and PROVEN**: DTB surgery — moved
  /soc/i2c@11014000/camera_sub@2d to /soc/i2c@11013000/camera_sub@36
  (reg 0x36) so g_pstI2Cclient2 (the SUB path, BUS_NUM2) binds to i2c-2.
  IMPORTANT: reg must NOT duplicate an existing client on the same bus —
  reg 0x36 duplicates camera_main@36 and i2c_check_addr_busy rejects it,
  leaving g_pstI2Cclient2 NULL → kernel panic → bootloop. Fix: set reg
  to a FREE address (0x10); the sensor driver overrides the slave ID
  dynamically anyway (probe logs show addr 0x36 from the driver's own
  SET_SLAVE_I2C_ID). Current flashed boot:
  `/data/local/tmp/boot_subi2c2.img` = kernel-patched + camera_sub moved
  to i2c-2 (reg 0x10) + vcamd 1220000 fixes. All sub probes now hit
  i2c-2; SUB VCAM_D powers up at 1220000 with zero regulator failures;
  I2C still NACKs because the sensor's driver is missing.
- **FDT surgery tooling proven**: Python parse/serialize of the flashed
  DTB (offset 2048+8332913 in the boot image, FDT magic d00dfeed,
  version 17). Re-serialize = semantically identical (verified by full
  property walk). `tmp/mados/dtb_repack.bin`, `dtb_moved2.bin`,
  `boot_repack.img` (control), `boot_subi2c2.img`.
- **Remaining fix path**: rebuild kernel from kernel_apollo_n
  (3.18.64, same LTS family as bd13 3.18.22, same MT6797 SoC) which
  contains S5K5E2YA driver, then splice: [new kernel gz][bd13 DTB
  patched][LineageOS ramdisk]. Risks: defconfig differences (fuel gauge
  CW2015, audio codec, touch) and CCCI/RIL compat between 3.18.22 and
  3.18.64. Fallback = boot_frontcam.img / boot_bd13_backup.img remain
  on /data/local/tmp.
- **Trial: MIUI-M HAL (2026-09-11) — DEAD END.** Replaced
  /system/lib{,64}/libcameracustom.so with the official MIUI 6 copies
  (from miui_HMNote4_V10.2.2.0.MBFCNXM). Facts learned:
  * MIUI M's own libcameracustom references SENSOR_DRVNAME_S5K5E8YX_B6
    (front = S5K5E8YX B6, sensor id 0x5e84 per bd13 kdSensorList) — NOT
    S5K5E2YA. The earlier "0x5e20/s5k5e2ya" reading was wrong.
  * The M lib fails on Android N with -22 because it needs the M-era
    MTK symbol __xlog_buf_printf (removed in N liblog). Built a
    nostdlib shim libxlg.so (DT_NEEDED liblog.so) exporting
    __xlog_buf_printf -> __android_log_vprint (same arg order) and
    byte-patched DT_NEEDED "liblog.so"->"libxlg.so" (same length) —
    after this dlopen of the M lib SUCCEEDS (verified via
    app_process + System.load).
  * Still -22: LOS camera.mt6797.so is N-era mtkcam
    (NSCam::CamDeviceManagerImp, libcameraservice.so family) whose
    C++ internals don't match the M-era libcameracustom. Making it work
    requires swapping the whole mtkcam family (dozens of libs) — high
    risk, not pursued.
  * The official MIUI-M kernel (from the same ROM) was also checked:
    SAME sensor list as bd13 (no s5k5e2ya; s5k5e8yxb6 present), and its
    DTB is byte-identical to bd13's (md5 97ba844f...). Conclusion: the
    nikel front camera on MIUI-M is driven by the s5k5e8yxb6 driver,
    which bd13 ALREADY has. So front camera fail is NOT a missing
    driver — the chip simply never ACKs on i2c-3/0x36 even with correct
    SUB power (1220000 OK) and also on i2c-2/0x36. Suspect: the front
    socket's reset/PDN GPIO wiring (pinctrl in the camera_hw2 platform
    node) is not being driven the way MIUI's HAL expects, or the camera
    connector seat is marginal. Next honest step = inspect
    /soc/kd_camera_hw2@1a040000 pinctrl + GPIO state during a probe.
  * /system/etc/camera/ does NOT exist in LineageOS (MIUI has it with
    per-sensor XML) — harmless for detection but relevant if HAL work
    is ever resumed.
  * libcameracustom LOS was restored from backup (md5 verified);
    libxlg shim removed.
- **DECISIVE SESSION 4 (2026-09-11): front camera fully decoded.**
  * Proven by flashing the OFFICIAL MIUI-M full ROM: front AND rear
    camera both work (photo+video). Hardware is healthy.
  * Captured MIUI kernel dmesg while opening the front camera:
    driver = **s5k5e8yxb6mipirawqteck** (drvIdx 6), power
    VCAMA 2.8V + VCAM_D **1220000** + VCAM_IO 1.8V, I2C read of the
    sensor ID happens at slave **0x18** on **i2c-3**, and the OTP read
    returns real data (awb_flag=0x01). The slave id 0x18 is supplied
    by the MIUI HAL through SENSOR_FEATURE_SET_SLAVE_I2C_ID; the LOS
    N-era HAL never sends it, so the driver falls back to its
    per-socket default (SUB = 0x2d) and the chip stays silent.
  * Kernel list-swap experiments (bd13 kernel, moving b6/qteck into
    drvIdx 3) made the correct driver get probed, but the slave id is
    still 0x2d and several boot attempts also broke the REAR camera
    (module-EEPROM probes at 0x10/0x18 NACK) for reasons not fully
    understood — the swap approach is abandoned as unreliable.
  * Current proven-good state: boot_bd13_backup.img + LOS
    (rear camera OK, all other fixes intact). Front camera fix
    requires either (a) LOS HAL to send slave 0x18 for the sub slot
    (patch libcameracustom / port MIUI sensor config), or (b) a kernel
    patch forcing the qteck driver's SUB i2c_write_id to 0x18
    (constant not yet located in the stripped binary).
  * Boot used during experiments that reproduces the MIUI sensor path
    exactly: tmp/mados/boot_qteck.img (kernel swap3 + DTB asli).
- **[CORRECTION 2026-09-14 — final: FIXED, see #12]** The whole VCAM_D /
  I2C-mux / slave-id trail above ends here. Final truth: bd13's front
  power+probe path itself is broken (NACK even with correct power); the
  bd54 MIUI kernel drives the same module fine; the only kernel change
  needed besides switching to bd54 is the kdSensorList e3↔e6 swap
  (drvIdx3→front driver). No DTB surgery, no slave-id HAL patch, no i2c
  table patch is needed.

### 10b-e. Call crash (MD3 modem exception) — radio-prop theory, TESTED AND REFUTED (2026-09-30)

> **Status correction (2026-09-30).** This section was originally written as
> "fixed by restoring missing MIUI radio props" and claimed *zero* `ee=a3`
> afterwards. **That claim is false and has been re-tested.** The prop set below
> plus 35 further telephony props was applied to a pristine LOS system and every
> outgoing call still produced `ee=a3f` / `Unbalanced enable for IRQ 319` /
> modem reset (test F, `§8f`). Most of the props listed as "the fix" were
> **already present** in the LOS `build.prop` before this experiment — see the
> md5/diff record in `CALL_CRASH.md`. Kept here as a record of what was tried
> and why it does not work; do not cite it as a fix.

The original symptom description was accurate: every outgoing call crashed the
modem within ~2 s. MD3 (C2K) raised `exception type(15): Fatal error
(LTE_EXP)` with fatal code `mon/monf...` (`mon/monfatalerror.c`, speech RX HISR
path), the kernel logged `[ccci3/ken]MD exception timer 2! ee=a3f`, and the
radio stack reset. Long investigation (see `CALL_CRASH.md` in the build
workspace) established:

- The native radio stack is MIUI-identical (kernel, MD1/MD3 images, mtkrild,
  mtk-ril.so, muxd, audio HAL), and a known-good SamarV ROM with md5-identical
  `mtk-ril.so` / `librilmtk` / `libmal` / `libmdfx` / `libaed` **also** fails
  calls — so neither firmware nor the mux daemon explains it.
- The theory tested in this section was that the M-gen C2K/world-phone radio
  configuration was missing from the LOS `build.prop`, leaving the C2K speech
  bridge between MD1 and MD3 half-configured. **Disproved** — see above.
- The decisive experiment remains the one in §8: blocking the AP's access to
  the speech path (`chmod 000 /dev/ccci3_aud`) does **not** stop the crash, so
  the fatal event is the modem-internal MD1→MD3 SMEM type-19 broadcast, not
  anything the AP does to a device.

Prop set that was applied and did **not** fix the crash:

```
mtk.eccci.c2k=enabled
ro.mtk_md_sbp_custom_value=0
persist.radio.apm_sim_not_pwdn=1
persist.radio.default.sim=0
persist.radio.mobile_data=0,0
persist.radio.gemini_support=1
persist.radio.flashless.fsm=0
persist.radio.flashless.fsm_cst=0
persist.radio.flashless.fsm_rw=0
persist.radio.mtk_dsbp_support=1
persist.radio.mtk_ps2_rat=W/G
persist.gemini.sim_num=2
persist.mtk_dynamic_ims_switch=1
ro.gemini.smart_sim_switch=false
ro.mediatek.gemini_support=true
ril.read.imsi=1
ril.specific.sm_cause=0
ril.radiooff.poweroffMD=0
ril.flightmode.poweroffMD=1
ro.mtk_external_sim_support=1
ro.mtk_external_sim_only_slots=0
ro.sim_me_lock_mode=0
ro.sim_refresh_reset_by_modem=1
ro.mtk_eap_sim_aka=1
ro.mtk_sim_hot_swap_common_slot=1
ro.mtk_modem_monitor_support=1
ro.ril.enable.amr.wideband=1
```

DO NOT add these (verified failures):

- `ro.mtk_srlte_support=1` — **MD1 boot hangs at stage S2** (`md_boot_stats`
  = "TC S2"; muxd freezes, `ril.muxreport` never set, rild never starts).
- `ro.mtk_world_phone_policy=0` — **ril-daemon-mtk fails to start** even
  from build.prop at boot (not just a runtime-setprop artifact as once
  suspected).
- ims/volte props (`ro.mtk_ims_support`, `ro.mtk_volte_support`,
  `persist.mtk.volte.enable`, `persist.mtk.ims.video.enable`,
  `persist.dbg.volte_avail_ovr`) — **safe to re-add, but they do not fix the
  call crash** (test A, 2026-09-30: MIUI with `ro.mtk_ims_support=1` deleted
  still called fine, so the prop is not the cause; and test F, LOS with all of
  them added, still crashes). The 2017 mtk-ril `getImsParam` failure
  documented in `build.prop` (§2) is the only reason to leave them out.

Reference MIUI source: MIUI Hellas 9.3.21 v10-6.0 (HMNote4) full ROM; its
`system/build.prop` is the authoritative source for these values.

Status recorded on 2026-09-27 and **superseded**: "outgoing calls now connect
with no modem reset". That observation did not reproduce — 7 further
single-variable tests since then all crash (§8f). The extras that were
installed from the MIUI image for C2K parity are still in place and remain
unproven individually necessary: `/system/etc/mddb/*` (C2K modem database,
absent from the LOS build), `mcd_default.conf`, `mdb_pub.key`, `mdbversion`;
`/system/bin/MtkCodecService` (run manually; add an init service on the next
boot.img rebuild). `viarild`/`libviatelecom-withuim-ril.so` were copied over
but stock MIUI does not ship viarild either (disabled); harmless if mdinit
fails to start it.

MD3 partition (mmcblk0p15) was re-flashed from the true stock
`/system/etc/firmware/modem_3_3g_n.img` (bfe0d82a) — the previous partition
content (cadc0922) was an experimental-era image, not stock. Note that the
crash reproduces with **both** images and with the official V10.2.1.0
firmware, so the modem image is not the differentiator either.

---

### 8f. Controlled single-variable bisect, 2026-09-30 — 6 axes cleared

Method: each test changed exactly one thing relative to a known-good or
known-bad baseline and re-measured `ee=a3f`, `MD exception`,
`Unbalanced enable for IRQ 319`, `DEVAPC` and whether the call reached
`ACTIVE`. Images are raw `ext4` system images edited with `debugfs` (no
mount, no root on the host needed); boot images repacked with a ~40-line
Python Android-boot packer.

| # | Change | Crash? | Conclusion |
| :--- | :--- | :--- | :--- |
| ref | stock MIUI | no | baseline |
| A | MIUI minus `ro.mtk_ims_support=1` | no | IMS prop not the cause |
| C | MIUI + **LOS kernel**, MIUI ramdisk | no | **kernel not the cause** |
| F | pristine **LOS + 35 telephony props** | **yes** | props not the cause |
| D | LOS + MIUI `libaudio_param_parser.so` + `b6a` XMLs | **yes** | audio param not the cause |
| E | LOS + watched path patched off (loop 37→0) | **yes** | inotify loop is a symptom |
| G | LOS with **SIM2 properly registered** | **yes** | SIM2 state not the cause |

**Crash timing (new, and it matters):** the exception fires **1.7–8 s before
the user presses dial**, while the AP is idle (`+ECSQ` signal polls, WiFi
`SIGNAL_POLL`, ALS only). The call then fails with
`DisconnectCause (ERROR) Cellular network not available` because the modem is
already dead. **The call is the victim, not the trigger.** Anyone re-testing
this should therefore not assume "nothing happened until ATD".

Instrumentation limits found the hard way:
- **MIUI cannot supply a comparison trace.** Its blob-based RIL has the
  `AT`/`RILJ` D-level logging compiled out: 5 `RILJ` lines total vs 2.117 on
  LOS, and `setprop log.tag.RILJ/RIL/AT/ATCI/RILMUXD D` installs the
  properties but produces nothing. `dmesg` on MIUI has 30 `ccci` lines (all
  `ccci1/net invalid ccmni rx channel(0x60)`) vs ~2.900 on LOS. So a
  modem-facing message diff MIUI↔LOS is **not possible** without rebuilding
  the vendor RIL blob with logging enabled.
- **`sys.boot_completed` + `system_server` pid stability are the real
  liveness signals.** A capture that looked alive (logcat flowing at +2.7k
  lines/10 s) was in fact a `system_server` crash loop; §19.4.
- A capture reference that lacks a needed log is not a baseline. The old MIUI
  reference capture had no `RILJ` at all and had to be discarded.

---

### 20. Rear camera: why AF is dead and the low-light cast survives — full diagnosis (2026-10-01)

Data-driven follow-up to #13. Everything below was measured on the running
ROM (`build $ date 2026-10-01`, LOS 14.1 + the #12/#13 fixes, MIUI V10.2.1.0
flash layout).

#### 20.1 Kernel differences are ruled out — do not patch the kernel driver

The MIUI and LOS kernels were extracted from their own `boot.img` and
compared byte for byte:

| | gz offset | Image size | Image md5 |
|---|---|---|---|
| MIUI V10.2.1.0 boot.img | `0x800` | 19,726,336 | `de51d56da46e07e14fefcdba30919b0c` |
| LOS `prebuilt/kernel` | `0x0` | 19,726,336 | `0f5f8264dca45e95b37bbe789b8e95f3` |

Same size, **187 differing bytes in 8 regions**:

| offset | what it is |
|---|---|
| `0x0b2a07d`, `0x0b2a0d3`, `0x0b2a13c`, `0x10a683e` | build banners (`bd19` vs `bd54`, dates) |
| `0x0ffb040` | `.note.gnu.build-id` hash |
| `0x1075448` | embedded blob, 105 bytes, not camera related |
| `0x115a420`, `0x115a4b0` | `kdSensorList` entry order — **the intentional #12 swap** |

So the LOS kernel is code-identical to MIUI's; the only real delta is the
fix #12 already ships. Two corrections to earlier notes in this file:

- The `kdSensorList` order change is **not** a bug — #12 swaps entries 3 and
  6 on purpose. (48-byte entries `{id u32, name[32], pad u32, fn u64}`,
  stride `0x30`, base `0x115a390`; LOS order is
  `[s5k3l8new, s5k5e8yxb6qteck, s5k3l8sunny, s5k5e8yxb6, s5k3l8qteck,
  s5k5e8yxb6sunny]`.)
- The bd54 s5k3l8 driver **does** export OTP white-balance code —
  `S5k3L8_MIPI_read_otp_wb`, `S5k3L8_MIPI_write_otp_wb`,
  `S5k3L8_MIPI_algorithm_otp_wb1`,
  `S5k3L8_MIPI_update_wb_register_from_otp` are all present in
  `/proc/kallsyms` on the booted kernel. The earlier "no OTP read" note was
  wrong.

#### 20.2 AF never runs at all

With the HAL3A debug switches on (`debug.af_mgr.enable`,
`debug.pd_vc.enable`, `log.tag.AFv2=VERBOSE`), a full preview + shutter
sequence produced **only init logs** — 228 AF lines, every one of them from
`af_mgr_v3: Start()`, `AfAlgo [initAF]`, `config Zoom`, `AFv2 AF running
version 3` and a single `setAFMode`:

```
AfAlgo: [AfAlgo0][setAFMode][Mode]0        <- AF_OFF, once, never changed
AfAlgo: [AFv2][param] i4ReadOTP 1          <- AF expects OTP lens calibration
AfAlgo: [AFv2][param] i4InfPos 200
aaa_hal_sttCtrl: querySensorStaticInfo 585 PDSupport=0   <- no PDAF (expected)
```

Zero `doAFProcBuf` / focus-step / motor lines across a whole capture, and
pressing the hardware focus key changes nothing. Static metadata for **both**
cameras reports:

```
Camera 0 (rear)  : max-num-focus-areas: 0     max-num-metering-areas: 9
Camera 1 (front) : max-num-focus-areas: 0     max-num-metering-areas: 9
```

So AF is dead for rear *and* front; it is not a rear-sensor problem.

#### 20.3 Both symptoms trace to the same cause: a borrowed 3A profile

The #13 fix remaps the rear drvname to `SENSOR_DRVNAME_IMX258_MIPI_RAW`, so
the s5k3l8 runs on MTK's IMX258 metadata **and** tuning. The HAL says so
out loud at init:

```
AppTsf: [TsfInit][Warning] Not Valid AWB Golden Gain R(0) G(0) B(0), set to 512!
AppTsf: [TsfInit][Warning] Not Valid AWB Unit Gain R(0) G(0) B(0), set to 512!
awb_algo: [PV AWB Gain] LV = 43, Rgain = 748, Ggain = 512, Bgain = 1036
```

Zero golden/unit gains -> neutral placeholder 512 -> the AWB algorithm
free-runs, and #13's night cast (1.68) is what it produces. The same
borrowed-profile gap is why the AF metadata yields 0 focus regions and AF is
never started.

#### 20.4 The samarv reference camera stack does NOT work here — tested, reverted

`lineage-14.1-20170812-UNOFFICIAL-nikel-samarv.zip` ships a completely
different camera stack than this tree:

| file | samarv | this tree (shipped) |
|---|---|---|
| `system/lib/libcameracustom.so` | 20,799,796 (`00a983f4`) | 10,551,236 (`1b89c679`) |
| `system/lib64/libcameracustom.so` | 20,917,768 (`bfb6de33`) | 10,611,000 (`08b80b0f`) |
| `system/lib/libcam.hal3a.v3.so` | 892,160 (`60997c9c`) | md5 `9af2c96b` |
| `system/lib64/libcam.hal3a.v3.so` | 1,555,912 (`7c93fad8`) | md5 `1e0823e3` |

Flashing all four as a matched pair makes the camera module fail to load:

```
CameraService: getCameraVendorTagDescriptor: camera hardware module doesn't exist
CAM_PhotoModule: Failed to open camera:0
FATAL EXCEPTION: main (org.cyanogenmod.snap)
  java.lang.ArrayIndexOutOfBoundsException: length=0; index=0
      at com.android.camera.PhotoModule.initializeFocusManager(PhotoModule.java:2732)
```

`length=0` = the HAL reports zero cameras. Reverted to the tree libs
(`/tmp/opencode/revcam_camlibs.zip`); camera and preview confirmed working
again (`CameraService::connect ... camera ID 0`, `startPreview: SurfaceHolder`).
Do not retry the samarv camera libs — the module needs its whole matching
camera set, not just these two libraries.

#### 20.5 The per-unit OTP path is still reachable (untested)

- `/dev/CAM_CAL_DRV` exists on LOS (char 239:0, `system:camera`).
- MIUI's `libcameracustom` exports `CAM_CALInit`, `CAM_CALDeviceName` and
  `S5K3L8_CAM_CALGetCalData(unsigned char*)`, so the whole CAM_CAL client is
  inside that library and can be dlopen'd rather than reimplemented.
- Ioctl constants recovered from `S5K3L8_CAM_CALGetCalData` disassembly:
  `0xc0146905` = `_IOWR('i', 5, 326)` and `0x020b00ff` = `_IOW(0, 0xff, 2816)`.
- Nothing in the HAL3A debug property list exposes an AWB/AF gain override,
  so a live gain experiment is not available; `debug.awb_mgr.lock` is the
  closest (lock only).

#### 20.6 AF profile matrix — DONE (2026-10-01): AF is NOT profile-driven

#13's matrix only ever measured **colour**, so the 6 same-length-swappable
profiles were re-run reading the AF state instead. Method: the rear drvname
slot is a 30-byte string (`SENSOR_DRVNAME_IMX258_MIPI_RAW` at `0x56430` in
the 32-bit lib, `0x8811a8` in the 64-bit one), patched to
`SENSOR_DRVNAME_<X>_MIPI_RAW`, bind-mounted over both
`libcameracustom.so` copies, `killall mediaserver`, then read
`dumpsys media.camera` + the AF logs.

| profile | mount | `max-num-focus-areas` | `setAFMode` | runtime AF lines | AppTsf warns | max JPEG |
|---|---|---|---|---|---|---|
| IMX258 (shipped) | verified | 0 | 0 | 0 | 2 | 4160x3120 |
| IMX214 | verified | 0 | 0 | 0 | 2 | 4160x3120 |
| IMX230 | verified | 0 | 0 | 0 | 2 | 4160x3120 |
| IMX377 | verified | 0 | 0 | 0 | 2 | 4160x3120 |
| S5K3M2 | verified | 0 | 0 | 0 | 2 | 4160x3120 |
| S5K2X8 | verified | 0 | 0 | 0 | 2 | **5120x3840** |

The S5K2X8 row is the control that proves the remap really reaches the HAL:
its max JPEG changes from 13 MP to 20 MP. So the profile swap works, and AF
metadata does **not** come from it — AF is identical for all six. Choosing a
different constructor cannot fix AF; only colour (#13) was ever profile-
dependent.

Two tooling traps hit while doing this, both worth remembering:

- `mediaserver` is **32-bit** (`/system/bin/mediaserver: ELF 32-bit LSB arm`),
  so only `system/lib/libcameracustom.so` matters for metadata/tuning. A
  matrix run that only swapped the `lib64` copy measures nothing.
- A **stale bind mount survived from an earlier session**:
  `/dev/block/mmcblk0p29 on /system/lib/libcameracustom.so (deleted)`. Any new
  `mount --bind` onto that path then fails with `No such file or directory`,
  silently producing a no-op test matrix. `umount -l` (lazy) clears it —
  plain `umount` returns `Invalid argument`. Always verify the post-mount
  `md5sum` matches the pushed file before trusting a bind-mount experiment.

#### 20.7 Where AF actually has to come from

`max-num-focus-areas: 0` for both cameras and every profile means the AF
config is not per-sensor-profile. The only AF getter `libcameracustom.so`
exports is untemplated:

```
_Z10getAFParamv          <- one AF config for all sensors
_Z11getAWBParamILN11NSIspTuning12ESensorDev_TE1E2E4E8E   <- per-device AWB
_Z10getAEParamILN11NSIspTuning12ESensorDev_TE1E2E4E8E   <- per-device AE
```

so the AF config comes from `getAFParam()` (an `NVRAM_CAMERA_AF_CFG_STRUCT`
filled from compiled-in data, since the NVRAM `CAMERA_3A` LID is empty on
this unit). Making AF work therefore means supplying a populated AF config —
i.e. patching a struct inside `libcameracustom.so`, which needs MTK's
`NVRAM_CAMERA_AF_CFG_STRUCT` layout and the address of `getAFParam`'s data.
That is the remaining route, alongside the OTP route in 20.5.

---

### 21. Injecting a library into the camera stack without reflashing (2026-10-01)

Built while attempting the OTP route (20.5). The technique is reusable for any
experiment that needs code running inside `mediaserver`, and it produced one
result that changes an earlier conclusion.

#### 21.1 The mechanism

No NDK in this tree (`prebuilts/ndk` is source, not a prebuilt; the AOSP
`arm-linux-androideabi-4.9` GCC has an empty sysroot), so a standalone binary
cannot be built. A **shared object** needs no crt objects, and that is enough:

```
CL=prebuilts/clang/linux-x86/host/3.6/bin/clang      # --target=arm-linux-androideabi
LD=prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androideabi-ld
$CL --target=arm-linux-androideabi -fPIC -fno-builtin -c -O2 -o probe.o probe.c
$LD -shared -o libcameracustom.so probe.o -L. -l:l.so   # -> DT_NEEDED /data/local/l.so
adb shell "mount --bind /data/local/tmp/probe.so /system/lib/libcameracustom.so"
adb shell "killall mediaserver"
```

Pieces that had to be right:

- **Absolute-path DT_NEEDED works.** `bionic/linker/linker.cpp:1677`:
  *"If the name contains a slash, we should attempt to open it directly and
  not search the paths."* So `/data/local/l.so` is loadable without touching
  `/system`. The DT_NEEDED string comes from the linked library's SONAME, so
  the pushed copy had its `DT_SONAME` overwritten in place with
  `/data/local/l.so` (18-byte slot, old value `libcameracustom.so`; the file
  had to be named `l.so` for the host link).
- **File offset != vaddr** in that library: `DT_STRTAB` is `0x9a60` as a vaddr
  but the string lives at file offset `0x6a60`, i.e. `file = vaddr - 0x3000`.
- **`mediaserver` is 32-bit** — only `system/lib/libcameracustom.so` is used.
- `-fno-builtin` is required: without it clang turns a hand-written byte-loop
  memset into `__aeabi_memset4`, which lives in libgcc and is not loaded.
- `/data/local/tmp` is `drwxrwx--x shell shell`, so `mediaserver` cannot
  create files there — pre-create the output file and `chmod 666`.
- SELinux does not block any of this: every denial logged
  `permissive=1` on this build.

#### 21.2 MIUI's libcameracustom DOES load into LOS's HAL3A

This revises #9's "too risky to swap" note. With the probe in place of
`libcameracustom.so`, the camera stack got all the way to:

```
dlopen failed: cannot locate symbol "_Z21cust_getFlashMaxIDutyiiiPiS_"
               referenced by "/system/lib/libcam.hal3a.v3.so"
```

— i.e. 15 of the 16 symbols resolve from MIUI's library, and the only gap is
`cust_getFlashMaxIDuty(int,int,int,int*,int*,int*)` (flash-calibration duty).
Exporting a stub for that one symbol made the whole stack load. So the earlier
"missing symbol may crash mediaserver" worry is wrong: the linker only needs
the symbol to exist. Whether MIUI's tuning data is *usable* by HAL3A is still
untested.

#### 21.3 S5K3L8_CAM_CALGetCalData is not a getter — it needs a request buffer

Calling it with a poisoned buffer crashes deterministically:

```
Fatal signal 11 (SIGSEGV) ... fault addr 0x0544cf64
  #00 /data/local/l.so (S5K3L8_CAM_CALGetCalData+627)
```

`+627` is `ldr.w r7, [sb, r4, lsl #2]`, and `r4` is loaded from the
**caller's** buffer at `[r7]` in the prologue (`ldr r4, [r7]` at +0x5a). With
`0xA5A5A5A5` in the buffer, that indexes ~2.7 GB past the 326-byte struct.
Reproduced with the rear sensor powered and the preview running, so it is not
a power/timing issue.

Recovered protocol (from the same disassembly):

- `fd = open(CAM_CALDeviceName())`, and `CAM_CALInit` is a 4-byte no-op stub.
- `ioctl(fd, 0xc0146905, cfg)` where `cfg` is built on the stack as
  `{ u32 A; u32 4; u32 caller_buf[0x20]; u32 caller_buf[0x24]; u32 &cfg;
  char name[92] /* zeroed, then filled from the device name */ }`
  (`A` comes from a PC-relative constant table, not from the caller).
- A second `ioctl(fd, 0x20b00ff /* _IOW(0,0xff,2816) */, ...)` follows.
- `CAM_CALDeviceName` is 20 bytes (returns a constant string) and
  `CAM_CALInit` is 4 bytes — neither does real work.

**What is still missing** is the caller's buffer layout, i.e. what
`buf[0x00]`, `buf[0x20]` and `buf[0x24]` must contain. That is answerable
offline by disassembling `S5K3L8_DoCamCalAWBGain(int,int,int,char*)` — its
fourth argument is the buffer it fills before calling `GetCalData`. No device
work needed for that step.

#### 21.4 CAM_CAL works on LOS - but the values are NOT a stable factory table

Calling MIUI's own client directly, with a **zeroed** request buffer (safe,
because `buf[0]` is an index and 0 is valid) and `id = 8`:

```c
fd = open("/dev/CAM_CAL_DRV", O_RDWR);          /* fd = 7 */
S5K3L8_DoCamCalAWBGain(fd, b, 8, buf);         /* ret = 0 */
```

Results (`b` = 0..12, all identical within a session):

| run | `buf[0x894]` (R) | `buf[0x898]` (G) | `buf[0x89c]` (B) | flag `buf[0x874]` |
|---|---|---|---|---|
| 1 | 6569 | 512 | 105 | 1 |
| 2 | 6755 | 512 | 170 | 1 |

What this does and does not prove:

- **Does**: the CAM_CAL route is alive on this ROM — `open()` succeeds, both
  `ioctl(0xc0146905)` calls return >= 0, and MIUI's parsing code runs to
  completion without the +628 crash (that crash was purely our poisoned input
  buffer). So per-unit data *can* be pulled from the kernel on LOS.
- **Does not**: these are **not** the factory white-balance gains. They are
  stable within one session but differ between sessions (6569/105 vs
  6755/170), so they track live state rather than immutable OTP. `G` is a
  constant 512 — the function hard-codes it (`mov r0, #0x200; str r0,
  [r4, #0x898]`), only R and B are computed from the two words the kernel
  returns through pointers in the ioctl config.
- The two request fields that are still unknown are `buf[0x20]` and
  `buf[0x24]`: `DoCamCalAWBGain` copies them straight into the ioctl config
  (`ldr r2, [r4, #0x20]` / `ldr r3, [r4, #0x24]`) and we send zeros, so the
  kernel is being asked for an unidentified block. In MIUI those two fields are
  filled by the caller from the 3A struct. Until they are known, the returned
  words cannot be interpreted - which is consistent with them looking like
  live noise rather than calibration.

**Next step if this is picked up again:** find the caller that fills
`buf[0x20]` / `buf[0x24]` before invoking `DoCamCalAWBGain` (in MIUI's
`libcamalgo.so` or the HAL1 tuning path) and replay those two values. Also
worth dumping the two kernel-returned words directly rather than the derived
gains, which needs a probe that reads the ioctl config struct itself.

#### 21.5 The request fields are NOT the missing piece (negative result)

Instead of hunting the HAL1 caller (these functions have **zero** direct
callers — they are only reached through a function-pointer table handed out by
`GetSensorInitFuncList`, and MIUI's `system.new.dat` is a sparse image while
its flashable zip is a block OTA, so the HAL1 library is not extractable from
what is on disk), the two unknown request fields were swept directly on the
device: `S5K3L8_DoCamCalAWBGain(fd, 0, 8, buf)` with `buf` zeroed except one
field at a time, 13 values each, all in one session.

| swept field | values | effect on R/G/B |
|---|---|---|
| `buf[0x20]` | 0..12 | none |
| `buf[0x24]` | 0..12 | none |
| `buf[0x04]` | 0..2 | none |
| `b` (arg 2) | 0..12 | none (earlier run) |

Within a session the result is bit-identical on every call
(`R=0x1934 G=0x200 B=0x122`), so the request fields are **not** what selects
the data. Across sessions it changes (6452/290, 6569/105, 6755/170 for
R/B), so whatever the driver returns tracks session/sensor state rather than
immutable per-unit calibration. `G` is always `0x200` because the function
hard-codes it.

Remaining open question, and the cheap test for it: whether the variation is
because a different physical sensor was powered (the function hard-codes
sensor id 8, but the kernel reads whichever sensor is live) or because the
driver returns live sensor registers. One run with the rear camera aimed at a
dark covered lens versus a bright scene would settle it. Until that is done,
these numbers must not be written into any tuning.

Tooling note: the probe's output file must be `touch`ed and `chmod 666`-ed
**before** the run — `mediaserver` cannot create files in
`/data/local/tmp`, and a bare `chmod` on a non-existent path silently fails,
which looks exactly like "the constructor never ran".

#### 21.6 The zero gains come from NVRAM, not from libcameracustom

Using the same injection to call the tuning getters inside mediaserver, the
live tuning data is **not** zero:

```
AWB_PARAM E1/E2/E4/E8  (4 separate structs, identical contents)
  +00: 00 02 00 00 (0x200=512)  ff 1f 00 00 (0x1fff=8191)  00 01 00 00 (256)  13 00 00 00 (19)
  +10: 08 00 00 00 (8) then 0x64=100 repeated, with 0x42=66 / 0x21=33 / 0x01 blocks
AWB_PARAM2_default / _s5k5e8yx / _s5k5e2ya : every field 0x200 = 512 (neutral)
AF_PARAM : populated (0x1,0x1,0x2,0x3,0x3,0x4b0,...)
```

So the AWB tables shipped in `libcameracustom.so` are populated. The
`AppTsf: Not Valid AWB Golden Gain R(0) G(0) B(0)` / `Unit Gain` warning
therefore originates from the **NVRAM `CAMERA_3A` struct** — which
`getTuningFromNvram` reads and which is empty on this unit (see the nvram
section above). The HAL does not backfill those specific fields from
`libcameracustom`, so they stay zero and get replaced by the neutral 512.

**Consequence for any fix:** writing the tuning has to happen in NVRAM, not in
the shared library. There is no writer on the device (`nvram_daemon` only; no
`nvram` CLI), `/nvdata/APCFG/APRDCL/` contains `AUXADC`, `FILE_VER`,
`HWMON_*`, ... but no `CAMERA_*` file at all, and the factory never created one.
Two ways forward, both unproven:

1. Create `/nvdata/APCFG/APRDCL/CAMERA_3A` from TWRP with a hand-built
   `NVRAM_CAMERA_3A_STRUCT` (needs the correct version header and struct
   layout, and correct FILE_VER entry).
2. Drive MTK's FOK interface (`/dev/nvram` via `libnvram`) from an injected
   library — needs the CAMERA_3A FOK key, which is MTK-internal.

Either way there is still the problem from 21.4: **valid gain values for the
s5k3l8 do not exist anywhere on this device or in the ROM**, so there is
nothing correct to write yet. Empirical per-unit gains (photograph the same
dark scene under MIUI and LOS and derive the ratio) remain the only source.

#### 21.7 Two more probe pitfalls

- `adb push` over a **bind-mounted file** does not update the mount: the mount
  holds the old inode. Always `umount -l` + `mount --bind` again after pushing
  a new build, otherwise you silently keep testing the previous one.
- Verify the build actually produced a new binary before blaming the device
  (the chained `clang && ld && adb push` swallowed one failure and the "new"
  run was the previous probe).

---

### 22. Autofocus is dead: two gates found and patched, one still open (2026-10-01)

Baseline, measured on a freshly wiped /data (so none of this is stale state):
over a full preview + shutter cycle the AF log contains **304 lines, 300 of
which are init**, and the only "runtime" lines are four calls to
`setAFMode`. Zero `doAFProcBuf`, zero focus steps, zero motor positions,
even with `debug.af_motor.position=1` and a forced
`debug.af_fullscan.step`. No VCM/AF activity in `dmesg` at all.

#### 22.1 Gate 1 — the algorithm is handed mode 0 (patched)

`NS3A::AfAlgo::setAFMode(LIB3A_AF_MODE_T)` lives in **`lib3a.so`** (32-bit
ARM, delta vaddr−file = `0x5000`):

```
0x75b70 cmp  r3, #2
0x75b78 sub  r2, r7, #1        ; r7 = mode
0x75b7c cmp  r2, #8            ; only modes 1..9 have a case
0x75b80 addls pc, pc, r2, lsl #2
0x75b84 b    0x75bf0           ; mode 0 -> default: AF is never configured
```

Modes 1..9 jump to per-mode setup; **mode 0 falls straight through to the
default return**, which is why the HAL's own `setAFMode` log always showed
`[AfAlgo0][setAFMode][Mode]0`.

Patch: `mov r7, r1` → `mov r7, #4` (continuous-picture) at file offset
`0x70b40` (`e1a07001` → `e3a07004`). After this the log shows
`[AfAlgo0][setAFMode][Mode]4` four times per session.

#### 22.2 Gate 2 — AfMgr drops the mode when a flag is clear (patched)

`NS3Av3::AfMgr::setAFMode(int,int)` in **`libcam.hal3a.v3.so`** (AArch64,
delta `0x1d000`):

```
0xc7b04 add  x2, x0, #5, lsl #12
0xc7b10 ldr  w3, [x2, #0x904]     ; w3 = this->0x5904, an "AF enable" flag
0xc7b14 cbz  w3, #0xc7b60         ; flag == 0 -> return, mode never stored
0xc7b18 ldr  w4, [x2, #0x914]     ; previous mode
0xc7b1c cmp  w4, w1
0xc7b20 b.eq return               ; same mode -> no-op
```

Patch: `cbz w3, #0xc7b60` → `nop` (`0x34000263` → `0xd503201f`) at file
offset `0xaab14`.

#### 22.3 Result: both gates open, AF still does nothing

With both patches live the algo now receives mode 4, yet the runtime AF
log stays empty, a forced fullscan produces nothing, and `dmesg` shows no
VCM traffic. So at least one more gate remains inside the AF state machine
(`AfMgr::Start`, `doAF`, `UpdateState*` are all present and unstripped in the
HAL, so it is findable — it is simply more reversing).

The likely root of the remaining gates is data, not code: the AF params say
`i4ReadOTP 1`, `AfMgr::readOTP()` exists, but **the kernel exports only
white-balance OTP** (`S5k3L8_MIPI_read_otp_wb` and friends — 15 S5K3L8
symbols total, none of them AF). This unit has no per-unit AF calibration in
NVRAM either. So even with the state machine forced open, the AF algorithm
would have no lens calibration to work from.

#### 22.4 Patches are harmless but currently ineffective

Verified after applying both: camera opens, capture works, colours fine
(`L=139.8`, `idx=1.048`). They change 4 bytes each and can be reverted by
flashing `system.img`.

Artifacts in `nikelbuild/cam_af/`:

| file | md5 | note |
|---|---|---|
| `lib3a.so.orig` | `5ab967b7` | stock 32-bit `lib3a.so` |
| `lib3a.so.af_mode4` | `347c8553` | gate 1 patched |
| `libcam.hal3a.v3.so.af_flag_bypass` | `8c1f95af` | gate 2 patched |

They are installed by hand on the device (`adb remount` + `cp` + `chcon
u:object_r:system_file:s0`), **not** in the build. Note that replacing a
system file without `chcon` produces
`PackageManagerService: There must be at least one intent filter verifier`
and a bootloop — the SELinux label must be restored.

#### 22.5 What a real fix needs

Either (a) keep reversing the HAL until every gate is open, and then supply
a lens calibration, or (b) obtain an AF OTP/calibration source. Since
(1) the kernel has no AF OTP, (2) this unit has no AF data in NVRAM, and
(3) MIUI's working AF comes from its own HAL1 path, option (b) has no
source on this device — the honest conclusion is that dead AF is a platform
limitation here, not a tuning mistake.

#### 22.6 Eleven gates found; patching them does not start AF

Continuing past §22.2. The AF-enable flag lives at `AfMgr + 0x5904` (the
`CCTOP` = contrast-AF flag; note 32-bit load/store offsets are scaled by 4, so
`0x904` in the instruction is byte offset `0x5904` from the object). Writers and
readers were located by scanning for that scaled immediate:

| site | what | action |
|---|---|---|
| `0xc7b14` | `AfMgr::setAFMode` early-returns when the flag is clear | `nop` |
| `0xccc80` | `AfMgr::Start()`: a sensor-HAL virtual call (`[x2,#0x18]`) returning < 0 branches to the error path, which does `str wzr, [x21,#0x904]` — **AF disabled for the rest of the session** | `nop` (force the success path) |
| `0xc82d4, 0xc83e4, 0xc8428, 0xc862c, 0xc86d4, 0xcaa78, 0xcada0, 0xcb418` | eight `cbz <flag>` guards, one of them the first instruction after `AfMgr::doAF()`'s prologue | all `nop` |

`AfMgr::CCTOPAFEnable()` / `CCTOPAFDisable()` (20/20-byte functions) are the
intended setters, and `AfMgr::Start()` writes the flag from six places.

**Result after all eleven patches:** the HAL still logs nothing but
`[AfAlgo0][setAFMode][Mode]4`, `doAF()` is never invoked, and no VCM traffic
appears in `dmesg`. So the remaining gate is upstream, in the MtkCam
`StateMgr` dispatch that decides per buffer whether the AF subsystem runs —
a separate and much larger reversing job (`AfStateMgr::transitState` at
`0xa6ff8` turned out to be only a 104-byte table setter, not the decision).

Also relevant: everything in the AF chain is reached through `IAfMgr` /
`IAfAlgo` vtables, so there are **no direct `bl` call sites** for
`doAF`, `Start`, `setAFMode`, `UpdateState*` or `readOTP` — a plain
caller scan finds nothing and a vtable-slot walk is required.

**Verdict after this stage:** the AF chain is gated at least eleven times and
the last gate sits in the state-machine dispatcher. Even past it, the
algorithm has no lens calibration (kernel exports WB OTP only, unit has no AF
data in NVRAM), so the expected end state is "AF runs but cannot focus".
The patches are kept only as research artifacts:

| file | md5 | contents |
|---|---|---|
| `lib3a.so.orig` | `5ab967b7` | stock |
| `lib3a.so.af_mode4` | `347c8553` | gate 1 |
| `libcam.hal3a.v3.so.af_flag_bypass` | `8c1f95af` | gate 2 |
| `libcam.hal3a.v3.so.af_gates_open` | `957e41ff` | gates 3-11 |

### 24. The AF motor chain, mapped end to end (2026-10-01)

Follow-up to §23.7/§23.8: instead of guessing at the missing `doAF` call, the
whole motor path was resolved statically. All addresses are in the **32-bit**
`libcam.hal3a.v3.so` (vaddr = file + `0x8000`).

#### 24.1 Reading the relocated vtables

The vtables are zero-filled in the file and filled by the loader, and this
build's `.rel.dyn` is `ANDROID_REL` with no usable addends for that range, so
they were read out of the live process instead:

```
$ adb shell 'grep libcam.hal3a.v3.so /proc/$(pidof mediaserver)/maps' | head -1
ec920000-ec9f0000 r-xp 00000000 ... /system/lib/libcam.hal3a.v3.so
load_bias = 0xec920000 - 0x8000 = 0xec918000
$ dd if=/proc/$(pidof mediaserver)/mem bs=4096 skip=$(( (0xec918000+0xdb000)/4096 )) count=8 | od -An -tx4 -v
```

The offset convention was pinned by a cross-check rather than assumed:
`StateMgr::postToASenThread()` does `ldr r1,[r3,#4]` and the slot at
`_ZTVThreadRawImp + 0xc` is `postToAESenThread`, so **address point = `_ZTV` + 8**.

```
ThreadRawImp : [+0x04] postToAESenThread  [+0x08] enableAFThread  [+0x0c] disableAFThread
GAFLensDrv   : [+0x0c] init  [+0x10] uninit  [+0x14] moveMCU  [+0x18] getMCUInfo  [+0x1c] setMCUInfPos
AfMgr (IAfMgr sub-object, address point 0xe2d60) : [+0x9c] AfMgr::doAF
```

#### 24.2 The chain

```
AfMgr::Start()                                   0x84a04  (3076 bytes)
  0x84d30  blx MCUDrv::getCurrLensID(dev)
  0x84d44  str.w r0, [r4, #0x58b8]     AF_FLAG = (LensId != 0xffff)
  0x84d52  ldr.w r3, [r6, #0x9c]       r6 = this + 0x59800
  0x84d5a  bne  0x84e0c                 already have a driver -> skip
  0x84d5c  ldr  r0, [r6, #0x78]         LensId
  0x84d5e  blx  MCUDrv::createInstance(LensId)
  0x84d62  str.w r0, [r6, #0x9c]        this + 0x5989c = driver
  0x84d9e  ldr  r2, [r3, #0xc]          driver->init(sensorDev)      [vtable+0x0c]

MCUDrv::createInstance(uint LensId)              0xb9476
  LensId == 0x1000                     -> 0xc1768 -> 0x5ae64  LensSensorDrv::getInstance()
  LensId in {0xff0001,0xff0002,0xff08} -> 0xc1778 -> 0x5ae70  GAFLensDrv::getInstance()
  anything else                        -> 0xc1788 -> 0x5ae7c  LensDrv::getInstance()
  (0xc17xx are 16-byte long-branch veneers: bx pc; ldr ip,[pc]; add pc,ip,pc)

AfMgr::MoveLensTo(int&, unsigned)                0x83e1c
  0x83e24  ldr.w r0, [r3, #0x9c]        the driver pointer
  0x83e28  cbz   r0, 0x83e52            NULL -> silently do nothing
  0x83e4e  ldr   r3, [r5, #0x14]        driver->moveMCU(sensorDev, pos)  [vtable+0x14]
  0x83e50  blx   r3
```

#### 24.3 The VCM is opened successfully — verified, not assumed

`LensDrv`'s strings sit together in `.rodata` and name the node it uses:

```
0xd4038  "LensDrv"
0xd4040  "Err: %5d:, [getMCUInfo] ioctl - mcuIOC_G_MOTORCALPOS, error %s"
0xd407f  "/dev/MAINAF"
0xd408b  "Err: %5d:, main Lens error opening %s"
0xd40b1  "Err: %5d:, [mcuIOC_S_SETDRVNAME] please check kernel driver"
0xd4111  "/dev/MAIN2AF"
```

With `/dev/MAINAF` at mode 666 none of those errors appears. To prove the code
is actually reached (rather than the errors simply never being printed), the
node was temporarily made unreadable:

```
$ adb shell chmod 000 /dev/MAINAF && adb shell killall mediaserver
$ adb logcat -d | grep 'LensDrv'
E LensDrv : Err:   153:, main Lens error opening Permission denied
```

So `LensDrv::init()` runs, `open("/dev/MAINAF")` succeeds, and both ioctls
(`mcuIOC_S_SETDRVNAME`, `mcuIOC_G_MOTORCALPOS`) return without error. This is
also the cleanest possible confirmation that the §23.1 permission fix matters:
without it, `init` fails here and nothing downstream can work.

`GAFLensDrv` is *not* the right driver for this phone — it is the one for the
`/dev/GAF001AF` family, which this handset does not have. `LensDrv` opening
`/dev/MAINAF` is correct here.

#### 24.4 The lens table's LensId column is ignored

`MCUDrv::createInstance` only recognises `0x1000`, `0xff0001`, `0xff0002` and
`0xff08`. The `LensId` values libcameracustom puts in the lens table are
`0x9714` (entries 1 and 2) and `0x0005` (entry 3) — **none of them is
recognised**, so the `else` branch is always taken. That happens to select
`LensDrv`, which is the right driver for this handset, but it means the table's
LensId is decorative here: the HAL is running on the default branch, not on a
deliberate match. Worth remembering before trusting any future tuning change
that keys off LensId.

#### 24.5 What is still missing, stated precisely

The motor is available and initialised, so the failure is that nobody asks it
to move during a search. `AfMgr::doAF()` (0x84464, 1292 bytes) is the per-frame
entry point, reached only as `IAfMgr` vtable slot `+0x9c`, and:

* no `bl` reaches it, and no `bl` reaches the PLT stub for it;
* no `ldr.w Rt,[Rn,#0x9c]` followed by an indirect call exists in `.text` —
  all 36 hits for that immediate are plain field accesses on unrelated objects;
* the only library that imports `IAfMgr` methods at all is `libacdk.so`, and it
  imports `IAfMgr::init`, `CCTOPAFEnable`, `CCTMCUNameinit` and a dozen others
  but **not** `doAF`.

So the caller is inside `libcam.hal3a.v3.so` but uses an indirection this scan
does not model (most likely a function-pointer table rather than a vtable).
That is the single remaining unknown, and it is a lookup problem, not a
diagnosis problem: everything upstream of it is now measured and working.

### 25. Proving the VCM opens, and a gdb dead end worth recording (2026-10-01)

#### 25.1 `LensDrv::init` runs and the open + ioctls succeed — proven

§24.3 argued this from the *absence* of error strings, which is weak. The
strong version: make the open fail on purpose and confirm the error appears.

```
$ adb shell chmod 000 /dev/MAINAF && adb shell killall mediaserver
$ adb logcat -d | grep LensDrv
E LensDrv : Err:   153:, main Lens error opening Permission denied
```

So `LensDrv::init()` is reached from `AfMgr::Start()`'s
`driver->init(sensorDev)` call, `open("/dev/MAINAF")` fails as expected at
mode 000, and at mode 666 it succeeds and neither `mcuIOC_S_SETDRVNAME` nor
`mcuIOC_G_MOTORCALPOS` reports anything. This is also the cleanest possible
demonstration that the §23.1 permission fix is load-bearing rather than
cosmetic: the whole chain downstream of it is fine, and only the node mode
decides whether the motor is reachable at all.

#### 25.2 Correction: the AFO "size = 0" message is not evidence of anything

While looking for why the AF statistic buffer never arrives, the log shows:

```
E afo_buf_mgr: [dequeueHwBuf()] Err: 227:, [dequeueHwBuf] rDQBuf.mvOut.size = 0
E aao_buf_mgr:  [dequeueHwBuf()] Err: 231:, [dequeueHwBuf] rDQBuf.mvOut.size = 0
```

`aao_buf_mgr` is the **AE** path, and AE demonstrably works (1570 `AeAlgo`
lines per capture). So this message is a benign "no new buffer at this
dequeue" notice emitted by both managers, not a missing-buffer report. An
earlier reading of it as the smoking gun was wrong. Likewise
`StatisticPipe: [deque] WARNING: TG12/TG13 port_0:already stopped` appears
exactly twice per session, at stream teardown.

#### 25.3 gdb works on this device, except in the one library that matters

`/system/bin/gdbserver` is present, `ro.debuggable=1`, and the AOSP prebuilt
(`prebuilts/gdb/linux-x86/bin/gdb-orig`, 7.11) drives it fine. Breakpoints in
ARM code resolve with usable backtrames:

```
Thread 1 "mediaserver" hit Breakpoint 3, pthread_mutex_lock
#1  android::IPCThreadState::getAndExecuteCommand()   libbinder.so
#2  android::IPCThreadState::joinThreadPool(bool)      libbinder.so
```

Two gotchas, both worth writing down because they cost hours:

1. **A pty is required.** gdb runs `continue` asynchronously when stdin is not
   a terminal, so every subsequent command fails with *"Cannot execute this
   command while the target is running"*. Running gdb under
   `pty.fork()` makes it synchronous.
2. **Breakpoints in `libcam.hal3a.v3.so` never fire, because gdb decodes that
   library as ARM when it is Thumb-2.** `x/6i` on `AfMgr::Start` renders

   ```
   0xed5c6a04 <_ZN6NS3Av35AfMgr5StartEv>:  stmdb sp!, {r4, r5, ...}
   ```

   but the actual bytes are `2d e9 f0 4f`, which is Thumb-2 `push.w
   {r4-r11, lr}`. gdb therefore plants an ARM breakpoint where Thumb
   executes. `set arm force-mode thumb` and `set arm fallback-mode thumb` are
   both ignored, because the objfile's ELF architecture says ARM and gdb only
   consults `.ARM.attributes` when it has real debug info — this library is
   dynsym-only ("missing debugging information" in `info sharedlibrary`).
   Hardware breakpoints (`hbreak`) fail the same way.

So: the AF call graph cannot be walked with the debugger on this build without
first giving gdb a mode-correct view of the library. Everything that could be
established statically has been (§24), and the one thing that cannot is
whether `AfMgr::doAF` is ever reached.

### 26. The camera tuning blob in this tree is for the wrong sensor (2026-10-01)

This is the most consequential finding of the whole investigation, and it was
found without touching the phone: the MIUI blobs were sitting on disk the whole
time.

#### 26.1 MIUI's blobs are extractable after all

Earlier notes (§20) recorded MIUI's libraries as unextractable. That is only
true of the **OTA packages**. The **fastboot global images** are ordinary
filesystem images:

```
$ file .../nikel_global_images_V10.2.1.0.MBFMIXM_20190123.0000.00_6.0_global/images/system.img
Android sparse image, version: 1.0, Total of 786384 4096-byte output blocks
$ simg2img system.img miui_system.raw && file miui_system.raw
Linux rev 1.0 ext4 filesystem data, volume name "system"
$ debugfs -R "dump /lib/lib3a.so MIUI_lib3a.so" miui_system.raw
```

For contrast, the OTA route is genuinely dead: the 9.3.21 zip's
`system.new.dat` starts `ff 5a ff 50 55 ff 5a 00` — not the sparse magic
`3a ff 26 ed`, i.e. encrypted; and the 10.2.2 zip carries a zero-byte
`system.patch.dat`. The transfer lists are in the "new" format, which stores
no filenames. `pre-device=nikel` on both, so there is no non-nikel MIUI here to
compare against — but none is needed.

#### 26.2 Every camera library differs from MIUI's

| file | MIUI V10.2.1.0 | this tree | size MIUI / ours |
|---|---|---|---|
| `lib3a.so` | `c0c2cb6f` | `5ab967b7` | 720 976 / 782 824 |
| `libcam.hal3a.v3.so` | `22e95a3d` | `1e0823e3` stock | 892 160 / 912 640 |
| `libcameracustom.so` | `7a2db01a` | `1b89c679` | 20 795 700 / 10 551 236 |
| `libcam.halsensor.so` | `0edadfce` | `92cf1101` | 431 724 / 271 980 |

All 32-bit, all with identical `DT_NEEDED` sets, so these are same-platform
builds — not an ABI mismatch, a different source tree.

#### 26.3 The tuning blob has no S5K3L8 support whatsoever

```
$ strings -a MIUI_libcameracustom.so | grep -c S5K3L8      -> 28   (40 case-insensitive, 44 byte-level)
$ strings -a libcameracustom.so     | grep -c S5K3L8      ->  0   ( 0 byte-level too)
```

MIUI's blob exports the sensor's CAM_CAL routines — `S5K3L8_DoCamCalAWBGain`,
`S5K3L8_DoCamCalModuleVersion`, `S5K3L8_DoCamCal2AGain`, `S5K3L8_DoCamCalPartNumber`,
`S5K3L8_DoCamCalSingleLsc`, … (23 exported `S5K3L8_*` symbols in total). This
tree's blob has **not one** byte of it.

That single fact explains the whole §23 tangle. The lens table and the sensor
id that `MCUDrv::lensSearch` compares against come out of this blob, and they
describe some *other* sensor:

```
pstSensorInitFunc[] = { 0x258, 0x3103, 0x5e20 }   <- none of them is the S5K3L8
lens table LensId    = 0x9714 / 0x0005            <- which MCUDrv::createInstance does not know either
MainSensorIdx       = 0x5e20                     <- and libcameracustom matches it against its own table
```

So §23.2's "the two vendor blobs disagree" was the real defect, seen from one
side: this tree pairs an AF framework with a tuning blob for a different
project. The 4-byte patch in §23.3 is a workaround for a symptom of that.

#### 26.4 MIUI's blob is not drop-in compatible with this tree

Pushing it (`adb push`, no flash) breaks the camera completely:

```
E CameraService: getCameraVendorTagDescriptor: camera hardware module doesn't exist
E CAM_PhotoModule: Failed to open camera:0
E AndroidRuntime: java.lang.ArrayIndexOutOfBoundsException: length=0; index=0
```

`/proc/$(pidof mediaserver)/maps` then shows only `libcamera_client.so`,
`libcamera_metadata.so`, `libcameraservice.so` — **no** `libcameracustom`, no
`libcam.halsensor`, no `lib3a`, no `libcamalgo`, no `libcam.hal3a.v3`. The MTK
chain dies before its first library and says nothing: no linker error, no
`dlopen` error, no 3A log at all. Swapping `libcam.halsensor.so` alongside it
changes nothing.

The obvious suspect is one symbol. Across the whole camera stack, `libcameracustom`
is consumed by 13 libraries needing 168 symbols, and exactly one is absent from
MIUI's blob:

```
_Z21cust_getFlashMaxIDutyiiiPiS_     (torch/flash max duty — unrelated to AF/AWB/AE)
```

and both blobs are linked `FLAGS_1: NOW`, so every relocation resolves at load
time rather than lazily. `LensCustomInit`, `LensCustomGetInitFunc`,
`LensCustomSetIndex` and `cust_isNeedAFLamp` are all present in both, and
`libcameracustom`'s own nine undefined imports are identical in both. **This
suspect is not proven** — the failure is silent, and "0 cameras, no message"
could equally be a vendor-tag or metadata-layout mismatch between an Android
8/9 blob and this Android 7.1 framework.

#### 26.5 State after this round

The phone is back on the known-good configuration and verified: LOS blobs
unmodified, the §23.3 lens-id patch in place, `/dev/MAINAF` at mode 666,
`MtkCam` 312 lines, no hardware-module error, `CurrLensIdx 0x0002`, `AFv2`
algorithm running. MIUI's extracted blobs are kept in
`/mnt/System/ROM-nikel/work/miui_camera_blobs/` for reference.

Two consequences worth stating plainly:

* **The green cast (§13) has the same origin, but it is fixed.** Wrong-project AWB
  tuning and a missing `S5K3L8_DoCamCalAWBGain` explain exactly the wrong gains
  that were measured — that is why the cast appeared. §13 fixed it by remapping
  to the best profile the blob offers, not by correcting the blob.
* **The correct fix for AF is the right blob, not a smaller patch.** Making a
  blob that knows S5K3L8 complete 3A init on a 7.1 HAL3A is the next piece of
  work; until then §23.3 stays in as the mitigation, and convergence stays open
  (see the correction in §27.8).

### 27. Following up §26: the load blocker is one symbol, and the tuning blob is IMX258's

#### 27.1 The missing symbol was the whole load failure — proven

§26.4 guessed at `_Z21cust_getFlashMaxIDutyiiiPiS_` under `BIND_NOW`. Tested rather
than guessed: instead of adding a symbol to MIUI's blob, redirect LOS's HAL3A so
it stops importing a name that blob does not have. The import is a
`R_ARM_JUMP_SLOT` entry in `.rel.plt`; only its `r_info` symbol index needs to
change.

```python
# in system/lib/libcam.hal3a.v3.so (.rel.plt)
reloc[2172] off=0x000e3b84  r_info 0x00011316 -> 0x00008f16
#                    _Z21cust_getFlashMaxIDutyiiiPiS_  ->  _Z17cust_isNeedAFLampiii
```

Eight bytes of `libcam.hal3a.v3.so`, nothing else touched. Result, with MIUI's
`libcameracustom.so` in place:

```
before:  maps = libcameracustom? NO  halsensor? NO  lib3a? NO  camalgo? NO  hal3a? NO
after :  maps = libcameracustom yes  halsensor yes  lib3a yes  camalgo yes  hal3a yes
         E CameraService: camera hardware module doesn't exist  ->  gone
```

All 13 consumers' symbol needs are satisfiable once that one import goes away, so
the MTK stack loads whole. **§26.4's suspect is confirmed.** The redirect is a
test crutch, not a fix — it mis-binds a call with a different signature — but it
is what made the rest of this section observable.

#### 27.2 With the blob loaded, the sensor is finally identified correctly — and then 3A stops

```
baseline (this tree): HAL3A asks for ..._SENSOR_DRVNAME_IMX258_MIPI_RAW
MIUI blob:            HAL3A asks for ..._SENSOR_DRVNAME_S5K3L8_MIPI_RAW_NEW
```

The MIUI blob is read correctly and the sensor is finally the right one. The
blob in this tree is IMX258 / S5K3P3SX / S5K5E2YA tuning data — the `0x258` in
`pstSensorInitFunc[]` is literally the IMX258 sensor id, and IMX258 is what the
HAL picks at runtime. Nothing to do with nikel.

It also enumerates properly, which it never does with this tree's blob:

```
[enumDeviceLocked] i4DeviceNum=2
[enumDeviceLocked] [0x00] ... facing:0 orientation=90      <- back
[enumDeviceLocked] [0x01] ... facing:1 orientation=270     <- front
```

Then it dies before AF: `AFv2` never appears (baseline: 48 lines), no preview,
no capture. It stops right after
`CamProfile}[CamDeviceManagerBase::getNumberOfDevices] : (0-th) ===> [start-->now: 250 ms]`
with no error on any level.

#### 27.3 The static-metadata warnings are pre-existing noise, not a regression

MIUI's blob triggers far more of them (208 "not found" of 224 attempts, against
96 of 150 in the baseline), which looked alarming until the counts were split by
outcome:

```
constructCustStaticMetadata_* returning status[0]   baseline: 0     MIUI blob: 0
```

**Every** `impConstructStaticMetadata_by_SymbolName` lookup fails in both
configurations, including the working one. The strings are in none of the four
libraries involved (`libcam.hal3a.v3.so`, `libcameracustom.so`, `libcamalgo.so`,
`lib3a.so` — byte-level search, all zero), so the name is assembled at runtime
and resolved elsewhere. The volume difference just reflects HAL3A asking about
the S5K3L8 rather than the IMX258. Ignore these warnings; they are not the
signal.

#### 27.4 No correct tuning blob exists in any local ROM

| source | `libcameracustom.so` | sensors | S5K3L8 |
|---|---|---|---|
| this tree (samarv) | `1b89c679`, 10 551 236 B | IMX258, S5K3P3SX, S5K5E2YA | 0 |
| `madOS_7.1.2_apollo_lite` | `7928b6b0`, 10 551 236 B | + S5K5E8YX | 0 |
| MIUI V10.2.1.0 nikel | `7a2db01a`, 20 795 700 B | S5K3L8 ×5, OV13853, … | 44 |
| `HP/RedN4` project | `da44d012`, 20 795 700 B | same set | 44 |

- madOS is byte-for-byte the same size as ours with one extra sensor and still no
  S5K3L8 — it is the same upstream MTK tuning blob. Its `lib3a.so` is *identical*
  to ours (`5ab967b7`), confirming our tree ships that base unmodified.
- `HP/RedN4` is this same ROM built elsewhere (its README is "LineageOS 14.1 for
  Xiaomi Redmi Note 4 MTK (nikel)"). Its 20 795 700 B blob is the same size as
  MIUI's and differs in 51% of 4 K blocks — the same 6.0-era build with different
  data, and equally free of `constructCustStaticMetadata` strings. No better.
- MIUI's own nikel image is not what its name says: `ro.build.version.sdk=23`,
  `release=6.0`, fingerprint `6.0/MRA58K/V10.2.1.0.MBFMIXM`. A converted 6.0-era
  ROM. That is why its tuning blob cannot finish 3A init against a 7.1 HAL3A.

#### 27.5 Where this leaves the AF fix

Bridging two MTK camera framework generations — making a 6.0-converted tuning
blob complete 3A init on a 7.1 HAL3A — is a project, not a patch, and there is no
correct-generation blob anywhere on this machine to start from. Getting one means
pulling stock MIUI for this device over the network.

So §23.3 stays as the shipped mitigation, but its standing changed: it is no
longer "a patch for a mysterious mismatch". It is a workaround for a **wrong
vendor blob**, now demonstrated rather than inferred:

* `libcameracustom.so` here is IMX258 tuning data for a phone this tree is not.
* Swap in a blob that knows S5K3L8 and the sensor is identified correctly —
  proven, in §27.2.
* §23.3's four bytes then only have to point the lens table at the entry the
  right blob would have supplied itself.

The same blob explains the §13 green cast's **origin** — the AWB gains in play
are IMX258's, which is why the cast appeared at all. §13 is nonetheless fixed,
by remapping to the best profile available; see the correction in §27.8.

#### 27.6 Device state

Restored and verified: LOS blobs untouched (`libcameracustom.so 1b89c679`,
`libcam.halsensor.so 92cf1101`, `libcam.hal3a.v3.so 47b9883f` = the §23.3
lens-id patch), `/dev/MAINAF` 666, `MtkCam` 403 lines, `AFv2` 48 lines,
`CurrLensIdx 0x0002`, no hardware-module error.

MIUI blobs kept at `/mnt/System/ROM-nikel/work/miui_camera_blobs/`.
`/tmp/opencode/hal_noflash.so` is the §27.1 redirect build (not for shipping).

#### 27.7 Every other local source of a tuning blob is exhausted

Not a guess — each one was opened and checked.

| source | Android | `libcameracustom.so` | S5K3L8 |
|---|---|---|---|
| fastboot images V10.2.1.0 nikel | 6.0/MRA58K | `7a2db01a` 20 795 700 B | 44 hits |
| OTA `miui_HMNote4_V10.2.2.0` | 6.0/MRA58K | same file (identical ext4 UUID `da594c53…`) | same |
| OTA `…9.3.21` hellas | 6.0/MRA58K | `system.new.dat` encrypted, header `ff5aff50 55ff5a00` | unreachable |
| `madOS_7.1.2_apollo_lite` | 7.1 | `7928b6b0` 10 551 236 B, IMX258/S5K3P3SX/S5K5E2YA/S5K5E8YX | 0 |
| `HP/RedN4` project blob | 7.1 build | `da44d012` 20 795 700 B, same 6.0-era lineage | 44, same scheme |
| `cust.img` (MIUI, 518 MB raw) | — | `/cust` is two-letter language dirs (`ab`, `ad`, `ae`, …) plus `/app/customized` | no tuning |

Two more dead ends worth recording so they are not retried:

* The 10.2.2 `system.new.dat` reads as raw ext4 (superblock magic `53ef` at `0x438`,
  same UUID as the fastboot image) but is **not** a contiguous image. Padding it
  out to the superblock's 780 231 blocks gives `EXT2 directory corrupted`. It is
  Xiaomi's own block-stream layout, not simg-diff.
* `cust.img` is regionalisation and preinstalled apps, not per-device tuning.

MIUI 9.3.21 is the same MIUI 10 with cosmetic changes and no camera difference,
so it is not worth a TWRP install to pull one file from it.

Net: **no blob of a matching generation exists on this machine.** Getting a real
one means a stock MIUI for this device on Android 8.1/9, and extracting it
offline costs nothing — the same `simg2img` + `debugfs` path as §26.1, no wipe, no
fastboot, no LOS reinstall. Only then is the §27.1 eight-byte redirect worth
replaying to see whether a matching-generation blob completes 3A init.

#### 27.8 The Android 6.0 camera stack cannot run on this framework — that closes it

The §26.1 suggestion of "pull a stock MIUI for this device" was wrong, and the
device tree says so itself:

```
| Shipped Android Version | 6.0.1 (MIUI M-gen) |
```

Xiaomi never took the Redmi Note 4 (mt6797) past Android 6.0. There is no stock
MIUI on Android 8.1/9 to pull. The only MIUI that exists for nikel is the 6.0
one already extracted and tested.

That leaves using the whole matched 6.0 3A set rather than one blob. It was
never actually tested — §27.1's "Test A" died in the loader on the one-symbol
bug, so it says nothing about the generation question. Checked offline first,
across all five MIUI 3A libraries (`libcam.hal3a.v3`, `libcameracustom`, `lib3a`,
`libcam.halsensor`, `libcamalgo`):

```
MIUI libcam.hal3a.v3 UND total              219
  satisfied by the 5 MIUI libraries          104
  closable by this tree's libraries           87
  unresolvable anywhere                       28
```

and across all five combined: 222 out-of-set imports, of which **122 do not
exist anywhere in this tree**. The 28 that sink `libcam.hal3a.v3` are almost
entirely one thing:

```
_ZN7android10VectorImpl12appendVectorERKS0_
_ZN7android10VectorImpl13editArrayImplEv
_ZN7android10VectorImpl16editItemLocationEj
_ZN5NSCam9IMetadata6IEntry9push_backERKyNS_9Type2TypeIyEE
…
```

That is the AOSP 6.0→7.1 camera-metadata ABI change (`android::VectorImpl`
became `android::Vector`, `IEntry` moved). It lives in
`libcamera_metadata.so`, which on this device is **built from LOS 14.1 source,
not shipped as a blob** — `strings /system/lib/libcamera_metadata.so | grep -c
VectorImpl` → 0. It cannot be swapped. Swapping it would mean shipping an
AOSP 6.0 camera framework into a 7.1 tree, i.e. rebuilding `frameworks/av`,
which is exactly what this ROM is not.

So, stated plainly:

* MIUI for nikel exists only as Android 6.0, and 6.0's camera stack is
  ABI-incompatible with this 7.1 framework in a way no blob swap can bridge.
* `libcameracustom.so` in this tree is IMX258 tuning data and will stay that
  way. Its AWB/AE/AF parameters are wrong for this sensor, permanently.
* Therefore §23.3 is not a stopgap pending a better blob — it is the only
  option available.

**Correction (2026-10-01).** An earlier version of this section also claimed
that §13's green cast "has no fix inside this tree". **That was wrong and is
retracted.** §13 *is* fixed — by the 3A profile remap, and measured: the
shipped IMX258 profile gives night G\*2/(R+B) = 1.68, the best of the
full-resolution profiles tested (S5K5E2YA 1.54 but 5 MP only; S5K3P3SX 1.78 at
13 MP but rejected for daylight). There is no contradiction between §13 and
§26: the blob carrying IMX258 data is precisely *why* a profile remap was
needed in the first place.

What genuinely remains open is **autofocus convergence**. The AF plumbing bugs
are fixed (§23) — AF engages, runs a full search cycle and times out (§23.7) —
but the lens does not reach focus. That is a tuning problem, not a plumbing
one: the search parameters the algorithm uses come from the same
IMX258-sourced profile, so the range, step count and thresholds do not match
this lens. Fixing it needs S5K3L8 AF tuning data, which §27.8 shows does not
exist for this framework generation.

---

### 30. Auditing this document against the tree (2026-10-01)

Every checkable claim in the camera sections was re-verified against the files as
they exist right now, not against this document. Most holds. Four do not, and one
of them invalidates the basis for closing the AF problem.

#### 30.1 What verified

| claim | section | result |
|---|---|---|
| `libcam.hal3a.v3.so` = `47b9883f` | §23.3, §27.6 | ✅ matches |
| `libcameracustom.so` = `1b89c679` | §26, §27.6 | ✅ matches |
| `libcam.halsensor.so` = `92cf1101` | §27.6 | ✅ matches |
| `lib3a.so` = `5ab967b7` | §27.4 | ✅ matches |
| lib64 `libcam.hal3a.v3.so` = `1e0823e3`, back to stock | §23.0 | ✅ matches |
| six AF nodes widened in the init `chmod`/`chown` block | §23.1 | ✅ all six present, `chmod 0660` + `chown system camera` |
| §23.3's 4-byte patch is the shipped blob | §23.3 | ✅ **reproduced byte-identically**, see 30.2 |

The AF patch chain was reproduced end to end rather than trusted:

```
stock  9af2c96b45bc4f6f9741334d7c26eaa5   (out/.../target_files-6223261ea7)
  -> patch_lensid.py
patched 47b9883f8f7a41674ed11bc8a96ad533
  cmp against vendor/xiaomi/nikel/lib/libcam.hal3a.v3.so  ->  identical
```

`patch_lensid.py --check` correctly reports `stock` on the stock blob and
`already patched` on the shipped one.

#### 30.2 Correction 1 — the stock blob's documented location does not exist

§23.3's table lists `cam_af/libcam.hal3a.v3.32.stock` (`9af2c96b`) and
`cam_af/libcam.hal3a.v3.32.patched` (`47b9883f`), and §23.0 says the §22 research
artifacts "are kept in `cam_af/`". **There is no `cam_af/` directory anywhere in
this repository.** The stock blob only exists inside `out/`:

```
out/target/product/nikel/obj/PACKAGING/target_files_intermediates/
    lineage_nikel-target_files-{1c6bd44c6b,6223261ea7}/SYSTEM/lib/libcam.hal3a.v3.so
```

`out/` is disposable — `mka clean` or a fresh checkout destroys the only copy, and
after that `patch_lensid.py` cannot be run at all, since it refuses to touch
anything it does not recognise. The documented input to the documented procedure
is gone. Restoring the stock blob to `patches/camera-af/` is a one-file fix and
closes this.

#### 30.3 Correction 2 — `libcam.metadata.so` is not an "empty shim"

§13 states "`libcam.metadata.so` is an empty shim in this ROM; the LENS
constructors are absent for ALL sensors". That reading comes from the wrong file.
`vendor/xiaomi/nikel/lib/libcam.metadata.so` is a **symlink**, and a reader that
takes `stat` size at face value sees 32–42 bytes:

```
lib/libcam.metadata.so -> ../system/lib/libcam.metadata.so      (symlink, 32 B target string)
lib/libcam.metadata.so    = 32 B   md5 b03892a9
lib/system/lib/libcam.metadata.so = 83 504 B  md5 b03892a9
```

The same applies to `libcamera_metadata.so` (35 B link → 36 448 B real) and
`libcam.metadataprovider.so` (40 B link → 431 720 B real). None of them are shims.
Any conclusion of the form "library X is empty in this tree" reached by measuring
`vendor/xiaomi/nikel/lib/` is unreliable and should be re-derived from
`vendor/xiaomi/nikel/system/lib/`.

#### 30.4 Correction 3 — §27.8's dead end rests on a wrong measurement

§27.8 concludes that the Android 6.0 camera stack cannot be bridged to this 7.1
framework, and cites as the reason that `libcamera_metadata.so` contains zero
`VectorImpl` symbols and "cannot be swapped" because it is built from LOS source.

**`VectorImpl` is not in `libcamera_metadata.so`.** It is in the libraries that
*use* it:

| library | `VectorImpl` exports |
|---|---|
| `libcamera_metadata.so` | 0 |
| `libcameraservice.so` | 22 |
| `libcamera_client.so` | 18 |
| `libgui.so` | 19 |

Those three are already LOS 14.1 builds in this tree and they do export the
symbols. The import side was then re-measured from scratch, unioning every
defined symbol in the tree: the eight camera libraries, `libc++_shared.so`
(armeabi-v7a), and all 407 `.so` in the built `target_files` `SYSTEM/lib`
(158 788 unique symbols).

| MIUI library | undefined imports | unresolvable in this tree |
|---|---|---|
| `miui_lib3a.so` | 61 | 0 |
| `miui_libcam.hal3a.v3.so` | 298 | **2** |
| `miui_libcam.halsensor.so` | 120 | 0 |
| `miui_libcameracustom.so` | 48 | 0 |
| **union of all four** | **357** | **2** |

§27.8 reports 28 unresolvable imports sinking `libcam.hal3a.v3` and 122 across the
set. The measured figure is **2 (0.6%)**, and both are missing *overloads*, not a
changed ABI:

```
_ZN5NSCam9IMetadata6IEntry9push_backERKyNS_9Type2TypeIyEE
    MIUI wants  push_back(uint32_t const&, Type2Type<unsigned int>)
    tree has   push_back(MRational const&, …)   push_back(MSize const&, …)
               push_back(char|short|float|int8|double const&, …)
    -> the plain uint32_t overload is simply absent

_ZN4NS3A7IPdAlgo14createInstanceEi
    MIUI wants  NS3A::IPdAlgo::createInstance(int)
    tree has   NS3A::IPdAlgo::createInstance(int, int)
    -> one fewer parameter
```

Neither name appears as an import in any of the tree's camera libraries, and
neither appears as a `dlsym` string in the tree's `lib3a.so` or
`libcam.hal3a.v3.so`. In MIUI's `libcam.hal3a.v3.so` both are
`R_ARM_JUMP_SLOT` — real calls, not name lookups.

**What this does and does not mean.** It does *not* establish that 3A init will
complete; §27.8's conclusion may still turn out to be right. What it does
establish is that the evidence offered for it is void, so the problem is open again
and cheap to retest. Concretely, the next experiment is narrower than "rebuild
`frameworks/av`":

* §27.2 already showed all five MIUI 3A libraries **load** and the sensor is
  identified as `S5K3L8_MIPI_RAW_NEW` (§27.2), which is only possible under lazy
  binding — an eager `BIND_NOW` would have refused the load on these two names.
* §27.2's symptom is silence: it stops right after
  `getNumberOfDevices() ... [start-->now: 250 ms]` with no error at any level, and
  `AFv2` never appears. A call through an unresolved `JUMP_SLOT` lands on address
  0 and takes `mediaserver` down, which looks exactly like that from the log.
* So: serve those two symbols from a small shim and watch `mediaserver`. The
  `wrap.<soname>` linker property plus `setprop ctl.restart mediaserver` does this
  without reflashing, which is the §21 technique already used here. A shim needs
  the right C++ layout to forward the call, so the first version should return
  `NULL`/log rather than guess — the point is to learn whether that call site is
  reached at all.

#### 30.5 What is still correct

Nothing in §0, §23, §24, §25, §26 or §27.1–§27.3 is contradicted by this audit.
The `§23.3` patch is reproducible, the AF plumbing fixes are real, and §26's
finding that `libcameracustom.so` in this tree is IMX258 tuning data stands —
that is a separate fact from whether a matching-generation blob can be made to
run.

**Correction to §27.8's own wording:** it is titled "that closes it". It does not
close it. The correct statement is "the *ABI-bridge* route is unavailable, for the
wrong reason given; the two-symbol route is untested."

#### 30.6 Trivia worth fixing while in here

* `patch_lensid.py`'s docstring and its `--check` message both point at
  "BUGFIXES.md #24" for the root cause. It is §23.
* §23.3 gives the vendor path as `vendor/xiaomi/nikel/system/lib/…`, which exists
  and is correct, but note that `vendor/xiaomi/nikel/lib/` holds symlinks into it
  (see 30.3) — always resolve through `system/lib/` when measuring.

### 31. Live re-check of the AF chain on a fresh build (2026-10-01)

The device came up on a freshly built `lineage_nikel-userdebug 7.1.2 NJH47F
b225f1b3fd`. §23's fixes were re-verified against it at runtime, not against the
document, and one new blocker appeared that no section records.

#### 31.1 §23.3's patch is confirmed effective at runtime

```
D LensMCU : LensMCUlensSearch() - Entry
D LensMCU : LensMCU[CurrSensorDev]0x0001 [CurrSensorId]0x5e20
D LensMCU : LensMCU[LensInitTable-2][SensorId]0x3103,[LensId]0x9714
D LensMCU : LensMCU[idx]2 [CurrSensorId]0x3103,[CurrLensIdx]0x0002
D LensMCU : LensMCU[CurrLensIdx]2
```

`CurrLensIdx 2` and the `0x3103` match are exactly what §23.4 documented. The
log tag is **`LensMCU`**, not `MCUDrv` — §23.4 does not name it, and grepping for
`MCUDrv` finds nothing. Two consequences worth keeping:

* The entry that carries `LensId 0x9714` is index 2, as §23.3's table predicts.
* `getCurrLensID()` returns `0x9714`, so `AF_FLAG` is 1 and AF is not
  short-circuited at `AfMgr+0x58b8`. §23.2's gate is genuinely open.

The patch bytes were also re-confirmed in the shipped blob: `movw r5, #0x3103`
at vaddr `0xb9542` in `MCUDrv::lensSearch`, and the replaced `cmp r3,#0x10 /
bne` pair is gone.

#### 31.2 §23.1's node permissions are confirmed, and the ioctl is now reached

```
D LensDrv : main lens init() [m_userCnt]0  +
D LensDrv : [main Lens Driver]DW9714AF
D LensDrv : main lens init() [m_userCnt]1 [fdMCU_main]349 -
D LensDrv : setMCUParam() - a_CmdId = 1
D LensDrv : setMCUParam() - a_Param = 0
E LensDrv : Err:   627:, [setMCUMacroPos] ioctl - mcuIOC_T_SETPARA, error Operation not permitted
D LensDrv : main lens uninit() [m_userCnt]1 [fdMCU_main]349 +
```

`/dev/MAINAF` opens (`fd 349`). **This contradicts §23.5**, whose table asserts
that the HAL error strings "`invalid m_fdMCU`, `mcuIOC_*`, `please check kernel
driver`" were "never printed, so the ioctl path is not reached at all". The ioctl
path is now reached, and the very first command is refused.

`mcuIOC_T_SETPARA` is dispatched from `LensDrv::setMCUParam(1, 0)` — a distinct
code path from `mcuIOC_T_SETMACROPOS`, which the binary also carries and which
did not run.

#### 31.3 The refusal is not SELinux

Every `avc:` record in `dmesg` on this build ends `permissive=1`, so the policy
is not enforcing and cannot be the source of the `EPERM`. The refusal therefore
comes from the driver itself. The kernel is a prebuilt `boot.img` — there is no
kernel source tree on this machine, so the condition producing `EPERM` in the
MTK VCM driver cannot be read yet.

Note also that no `mcuIOC_S_SETDRVNAME` line appears before `setMCUParam`,
although the HAL carries `Err: %5d:, [mcuIOC_S_SETDRVNAME] please check kernel
driver`. That string not printing means either the call succeeded or it was never
made; the two are not distinguishable from this log. Whether the driver name is
registered before parameters are set is the first thing to determine next.

#### 31.4 Two of this session's own mistakes, recorded so they are not repeated

Both were false negatives produced by the measurement, not by the device:

1. `am start -n com.android.camera2/...` was issued with stderr sent to
   `/dev/null`. **That package does not exist on this build** — the camera is
   `org.cyanogenmod.snap`. The command failed, the "kamera dibuka" text that
   followed was an unrelated `echo`, and the `MtkCam` frames then read were the
   user's own session.
2. `logcat -c` was run *after* the camera was already open. `LensMCU` and
   `LensDrv` are emitted during AF-manager init, i.e. at camera open, so the
   clear erased exactly the lines being looked for. Every subsequent read said
   "never called".

The correct sequence is: force-stop, `logcat -c`, *then* open the camera, then
read. Both mistakes produced a confident "the code never runs" verdict from a
check that had not actually been performed — the same failure mode as §26.4's
guessed symbol and §27.8's library mix-up.

#### 31.5 A regression that blocks the next step

§24 resolved vtables by reading `/proc/$(pidof mediaserver)/mem`, after getting
the load bias from `/proc/$(pidof mediaserver)/maps`. **Neither is readable on
this build.** `adb shell` runs as uid 2000 (`ro.debuggable=0` despite the
userdebug display id) and `adb root` reports "root access is disabled by system
setting". So:

```
$ adb shell 'grep libcam /proc/372/maps'
grep: /proc/372/maps: Permission denied
```

§29's ADB change is correct as far as it goes, but it removed the capability the
AF investigation depended on. Whatever root access is used going forward has to
be a deliberate choice, not a side effect — a userdebug build with
`ro.debuggable=1`, or a kernel with `CONFIG_ADB_ROOT=y` and the setting enabled
in Developer options.

#### 31.6 State

No source change. The §23 fixes are live and doing their job; the motor's first
ioctl is refused with `EPERM` by the driver, and that refusal is the open item.

## How to apply the out-of-tree fixes

All fixes outside `device/xiaomi/nikel` and `vendor/xiaomi/nikel` are shipped
as patches in `device/xiaomi/nikel/patches/` and applied by `apply.sh`:

| Patch file | Target repo | Content |
| :--- | :--- | :--- |
| `system_netd.patch` | `system/netd` | hostapd ctrl file `wlan0`→`ap0` in `SoftapController.cpp`; rpfilter tether rule non-fatal in `NatController.cpp` |
| `frameworks_opt_net_wifi.patch` | `frameworks/opt/net/wifi` | 5 GHz soft AP: fixed fallback channel 36, no country-code gate |
| `system_core.patch` | `system/core` | existing cm-14.1 device patches |
| `system_sepolicy.patch` | `system/sepolicy` | SELinux rules |
| `frameworks_av.patch` | `frameworks/av` | existing device patches |
| `frameworks_native.patch` | `frameworks/native` | existing device patches |
| `hardware_libhardware.patch` | `hardware/libhardware` | existing device patches |

If you edit `system/netd` or `frameworks/opt/net/wifi` directly instead of
applying the patches, regenerate the patch afterwards:

```bash
cd system/netd
git format-patch <base-commit>..HEAD --stdout > device/xiaomi/nikel/patches/system_netd.patch
```

- After vendor `0e5ea60` (TEE gatekeeper HAL) and a fresh flash: PIN setup
  works (TEE gatekeeper), enrollment completes, unlock works. The MIUI-era
  templates that triggered the limit error earlier are no longer an issue —
  1058 was always the gatekeeper/HMAC failure (see #10b-c), not a template
  count problem (the real limit error is 1005).
- Debugging aids if it regresses: `logcat | grep -aE '\[gf_'` (HAL/TA logs),
  `logcat | grep -a 'software GateKeeper'` (must NOT appear — if it does,
  the TEE gatekeeper HAL is missing or fails to load), `service check
  android.hardware.fingerprint.IGoodixFingerprintDaemon`, `service check
  android.security.keystore`, `pidof mcDriverDaemon goodixfingerprintd
  fingerprintd`.

> **Status correction (2026-09-30).** This section was originally written as
> "fixed by restoring missing MIUI radio props" and claimed *zero* `ee=a3`
> afterwards. **That claim is false and has been re-tested.** The prop set below
> plus 35 further telephony props was applied to a pristine LOS system and every
> outgoing call still produced `ee=a3f` / `Unbalanced enable for IRQ 319` /
> modem reset (test F, `§8f`). Most of the props listed as "the fix" were
> **already present** in the LOS `build.prop` before this experiment — see the
> md5/diff record in `CALL_CRASH.md`. Kept here as a record of what was tried
> and why it does not work; do not cite it as a fix.

The original symptom description was accurate: every outgoing call crashed the
modem within ~2 s. MD3 (C2K) raised `exception type(15): Fatal error
(LTE_EXP)` with fatal code `mon/monf...` (`mon/monfatalerror.c`, speech RX HISR
path), the kernel logged `[ccci3/ken]MD exception timer 2! ee=a3f`, and the
radio stack reset. Long investigation (see `CALL_CRASH.md` in the build
workspace) established:

- The native radio stack is MIUI-identical (kernel, MD1/MD3 images, mtkrild,
  mtk-ril.so, muxd, audio HAL), and a known-good SamarV ROM with md5-identical
  `mtk-ril.so` / `librilmtk` / `libmal` / `libmdfx` / `libaed` **also** fails
  calls — so neither firmware nor the mux daemon explains it.
- The theory tested in this section was that the M-gen C2K/world-phone radio
  configuration was missing from the LOS `build.prop`, leaving the C2K speech
  bridge between MD1 and MD3 half-configured. **Disproved** — see above.
- The decisive experiment remains the one in §8: blocking the AP's access to
  the speech path (`chmod 000 /dev/ccci3_aud`) does **not** stop the crash, so
  the fatal event is the modem-internal MD1→MD3 SMEM type-19 broadcast, not
  anything the AP does to a device.

Prop set that was applied and did **not** fix the crash:

```
mtk.eccci.c2k=enabled
ro.mtk_md_sbp_custom_value=0
persist.radio.apm_sim_not_pwdn=1
persist.radio.default.sim=0
persist.radio.mobile_data=0,0
persist.radio.gemini_support=1
persist.radio.flashless.fsm=0
persist.radio.flashless.fsm_cst=0
persist.radio.flashless.fsm_rw=0
persist.radio.mtk_dsbp_support=1
persist.radio.mtk_ps2_rat=W/G
persist.gemini.sim_num=2
persist.mtk_dynamic_ims_switch=1
ro.gemini.smart_sim_switch=false
ro.mediatek.gemini_support=true
ril.read.imsi=1
ril.specific.sm_cause=0
ril.radiooff.poweroffMD=0
ril.flightmode.poweroffMD=1
ro.mtk_external_sim_support=1
ro.mtk_external_sim_only_slots=0
ro.sim_me_lock_mode=0
ro.sim_refresh_reset_by_modem=1
ro.mtk_eap_sim_aka=1
ro.mtk_sim_hot_swap_common_slot=1
ro.mtk_modem_monitor_support=1
ro.ril.enable.amr.wideband=1
```

DO NOT add these (verified failures):

- `ro.mtk_srlte_support=1` — **MD1 boot hangs at stage S2** (`md_boot_stats`
  = "TC S2"; muxd freezes, `ril.muxreport` never set, rild never starts).
- `ro.mtk_world_phone_policy=0` — **ril-daemon-mtk fails to start** even
  from build.prop at boot (not just a runtime-setprop artifact as once
  suspected).
- ims/volte props (`ro.mtk_ims_support`, `ro.mtk_volte_support`,
  `persist.mtk.volte.enable`, `persist.mtk.ims.video.enable`,
  `persist.dbg.volte_avail_ovr`) — **safe to re-add, but they do not fix the
  call crash** (test A, 2026-09-30: MIUI with `ro.mtk_ims_support=1` deleted
  still called fine, so the prop is not the cause; and test F, LOS with all of
  them added, still crashes). The 2017 mtk-ril `getImsParam` failure
  documented in `build.prop` (§2) is the only reason to leave them out.

Reference MIUI source: MIUI Hellas 9.3.21 v10-6.0 (HMNote4) full ROM; its
`system/build.prop` is the authoritative source for these values.

Status recorded on 2026-09-27 and **superseded**: "outgoing calls now connect
with no modem reset". That observation did not reproduce — 7 further
single-variable tests since then all crash (§8f). The extras that were
installed from the MIUI image for C2K parity are still in place and remain
unproven individually necessary: `/system/etc/mddb/*` (C2K modem database,
absent from the LOS build), `mcd_default.conf`, `mdb_pub.key`, `mdbversion`;
`/system/bin/MtkCodecService` (run manually; add an init service on the next
boot.img rebuild). `viarild`/`libviatelecom-withuim-ril.so` were copied over
but stock MIUI does not ship viarild either (disabled); harmless if mdinit
fails to start it.

MD3 partition (mmcblk0p15) was re-flashed from the true stock
`/system/etc/firmware/modem_3_3g_n.img` (bfe0d82a) — the previous partition
content (cadc0922) was an experimental-era image, not stock. Note that the
crash reproduces with **both** images and with the official V10.2.1.0
firmware, so the modem image is not the differentiator either.

---
