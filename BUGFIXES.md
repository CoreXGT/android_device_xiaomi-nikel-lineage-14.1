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

| # | Bug | Status | Section |
| :--- | :--- | :--- | :--- |
| 0 | Rear camera HAL failed to load | ✅ FIXED | #0 |
| 1 | SD card never mounted | ✅ FIXED | #1 |
| 2 | Mobile data (LTE) dead | ✅ FIXED | #2 |
| 3 | Sensors registered but no data | ✅ FIXED | #3 |
| 4 | Hotspot 2.4 GHz dies | ✅ FIXED | #4 |
| 5 | Hotspot 5 GHz rejected by framework | ✅ FIXED | #5 |
| 6 | Boot image repacking | ✅ document/tooling | #6 |
| 7 | Misc build/boot fixes | ✅ FIXED | #7 |
| 10 | Video recording fails | ✅ FIXED | #10 |
| 12 | **Front camera never enumerated** | ✅ **FIXED** | #12 |
| 14 | **Off-charge bootloop (MI logo repeats when charging while off)** | ✅ **FIXED (2026-09-19)** | #14 |
| 8 | Voice calls crash C2K modem (MD3) | ❌ NOT FIXED (community-wide) | #8 |
| 8f | — #8 single-variable bisect: 6 axes cleared | ✅ documented | #8f |
| 19 | 4 build-integrity defects found while chasing #8 (`md_log_config` missing, `audio_param/b6a` not copied, MIUI cannot boot on CM14.1 `/data`, MTK MAL blobs absent) | ❌ NOT FIXED | #19 |
| 13 | Rear camera green cast at night / low light | ✅ FIXED (2026-09-14) | #13 |
| 15 | AudioFx has stopped (frequent, especially while ringing) | ✅ FIXED (2026-09-21) | #15 |
| 16 | SMS (Messaging) app crashes when opening a message | ✅ FIXED (2026-09-21) | #16 |
| 17 | AOSP Browser crashes on open (Firefox works) | ✅ FIXED (2026-09-21) | #17 |
| 10b | Fingerprint scanner | ❌ NOT FIXED | #10b |
| 11 | Hotspot 5 GHz DFS channels | ⚠️ minor open | #11 |

Note: older front-camera sections #9 / #9a–#9e / #9b record the
investigation history; several of their interim conclusions were later
proven WRONG and are superseded by #12. Read them only for "what was
ruled out", not for the current status.

**Read §8 before touching the call crash.** It carries an explicit
"do not re-test these" list, and §8f extends it with a controlled
single-variable bisect that cleared six more axes (kernel, IMS/VolTE props,
35 telephony props, audio-param config, the `inotify` loop, SIM2 state).
Section #10b-e is retained but its original "fixed" claim is **refuted** — see
the status correction at the top of that section.

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

## NOT FIXED

Note: sections #9 and #9b below are the HISTORICAL investigation trail
of the front camera, which is now FIXED (see #12). They stay here only
so nobody re-discovers the same dead ends.

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
- **2026-09-15 malam — BREAKTHROUGH: EX record MD3 ter-decode penuh** (artefak: `ccci_dump3.txt`, `dmesg_call_full3.txt`, `md3_exrec.bin` di `nikelbuild/`; `md3_exrec.bin` = parse blok "Dump MD EX log" `Base: ffffffc0b8299beb`):
  - Data diambil dari `/proc/ccci_dump` (buffer CCCI NORMAL yang tidak tercetak console karena `ccci_debug_enable` default 4; set `echo 6 > /sys/kernel/ccci/debug` untuk semua print / 5 untuk detail-EE saja tanpa noise).
  - Record: `ex_type=15 LTE_EXP`, **file = `mon/monfatalerror.c`**, **ExStr = `Ex Enter` + `Exception Nested Happened! \r\n`**, `Hisr65`, PC/LR MD3 = `0x00106A85`/`0x00106A84`, param `Ex D 0x204 / 0xD1`.
  - String `mon/monfatalerror.c` + `Exception Nested Happened!` hanya ada di **MD3 firmware `modem_3_3g_n.img` @file 0x3119e0** (bukan MD1) → EE ini milik monitor MD3 sendiri.
  - **Nested** = EE MD3 masuk untuk ke-2x; record pertama (penyebab asli speech) tertimpa. PC/LR 0x106A84 = loop mailbox-read monitor MD3 (`bl 0x1063ac` read 12-byte, msg `[0]='Y' [9]=8 [4]=1`).
  - **Mapping MD3: runtime addr = file offset − 0x200** (dibuktikan dump `Base: ffffff80045fc000` isi "MMM\0..." = file 0x200). Semua disasm di atas runtime; literal pools: `monfatalerror` ref @runtime 0x1066be/0x106cae (pool 0x1066d4/0x106cc0), `Ex Enter` @0x1066ea (pool 0x106a1c), `Nested` @0x106726 (pool 0x106a30).
  - Fungsi teridentifikasi (runtime): mailbox read = `0x1063ac` (inner `0x989f4`), monitor main loop = `0x106870`–`0x10699a` (msg `[0]='Y'`, len filter `[9]`∈{8,11}, `[4]`/`[8]`==1), EX record builder = `0x1067c0`–`0x106810` (`strb type 5/15` ke `[r4+0x10]`, copy ke `[r4+0xfc..0x138]`), log helper = `0x106248` (args r0=level, r2=str, r3=len).
  - **Kesimpulan baru**: bukan MD1 broadcast type-19 yang langsung mematikan — MD3 monitor sendiri masuk EE **nested** saat speech path on; exception ASLI (instance pertama) tidak ter-record. Lapisan speech handler MD3 masih harus dipetakan.
- **2026-09-15 lanjut — tooling + patch diagnostik MD3** (backup firmware: `/tmp/md3off/modem_3_3g_n.img.orig` md5 `bfe0d82a183724a1387cec901e7aecc8`):
  - Disassembler pool-aware dibuat (`/tmp/md3off/md3dis.py`): thumb16 `ldr rX,[pc,#imm]` literal scan + anotasi string rodata. EE entry MD3 = runtime `0x1066e8` (file `0x1068e8`): print `Ex Enter` → cek EE counter `[0x69dffc]` (`0xff`=fresh, `+1==1`=fresh, lain=nested → print `Exception Nested Happened!`, copy task context `[0x5903c0]` ke record, **`b self` hang**).
  - `ee=a3f` vs `a3d`: bit1 `MD_EE_DUMP_ON_GOING` — lama hang (dump on-going), baru setelah patch tidak hang.
  - **Patch diagnostik `nested2fresh`**: file `0x106924` `1ad0`→`1ae0` (`beq fresh`→`b fresh`, nested path selalu jalan fresh, tidak hang). md5 `49c57753808dfeb7edb48a0e44658832`. Flash via TWRP (`/system/etc/firmware/modem_3_3g_n.img`).
  - **Hasil tes MO**: `ee=a3d` (tidak hang), tapi record TETAP `LTE_EXP` + file `mon/monfatalerror.c` + code1/2 `"mon/monf"` → file/code ini **hardcoded identitas handler EE MD3**, BUKAN info fault asli. Instance SWINT pertama tidak pernah membawa file/line ke AP; context fault asli hanya ada di EE dump internal MD3 (butuh mdlogger/DHL yang tidak ada di LOS).
  - Paket CCIF pertama sebelum EE: `Q0 Rx msg 0 24 80000006 0` (36 byte, MD3→AP "Ex Enter") 110 ms setelah speech alloc; `Q0 Rx 80000006` dari MD1 kemungkinan pesan speech type-19 pemicu.
  - **2026-09-15 malam 2 — reverse lanjutan MD3 speech path** (checkpoint, belum ketemu handler tepat):
    - `Q0 Rx 80000006` = paket CCIF MD3→AP berisi notif EE ("Ex Enter", 36 byte) — EFEK crash, bukan pemicu. Pesan speech MD1→MD3 via SMEM `0x8e200000` antar-modem, tak terlihat di AP.
    - Konstanta `0x80000006` literal MD3 @file `0x2391ad/0x309a21` (bukan pool code).
    - Peta speech MD3: task `SpeechReadMsg`/`SpeechWriteMsg`, queue `S2_SPEECH`, `SPC2K_UL_GetSpeechFrame`, `SvcSendSpeechConnMsg`, `mdSpeechLoopBackModeMsgProc`; module `mdipc/` (cc_irq_msg_v2/v2, cc_sys_comm_v2, cc_irq_spinlock) + `hwd/hwd_speech/` (hwdsph/hwdvm/hwdaudioservice). Code mdipc runtime ~`0x100200`–`0x101000` (init `0x100448`: alloc 4 group `bl 0xff198`, `bl 0x1008bc/0x100f74/0x1013b4`, register msg `0x10deb8`).
    - Tooling: `/tmp/md3off/md3dis.py` (pool-aware thumb16 disasm + string anotasi; thumb32 `ldr.w` scanner kosong — pool MD3 dicampur data, butuh Ghidra/IDA untuk lanjut).
    - Faktor mempermudah patch: MD3 C2K nikel = data-only (speech GSM selalu MD1) → NO-OP handler speech MD3 praktis aman, tapi fungsi handler belum teridentifikasi.
  - **2026-09-16 — patch `eeoff` (MD3 EE entry → `bx lr`)**: runtime `0x1066e8` (file `0x1068e8`) `b5f0...`→`4770 bf00`, md5 `9207f7cc9124fae10f4985a40ef51f8c`. **GAGAL**: MD3 masih kirim EX (`ee=a3d` @158s, voice_trigger→110ms) — paket EX dikirim state machine CCIF MD3 **sebelum** call EE entry (EE entry hanya build record). Salinan kedua pool `0x106cc0` = fungsi log biasa, bukan EE entry. Firmware **revert** ke original. Dengan begitu semua patch AP-side & MD3-side berbasis paket/EE gagal; tersisa: patch MD1 speech broadcast type-19 (reverse modem_1_ulwctg_n.img 15.8 MB) atau kernel reset-policy — keduanya besar.
  - **Firmware di-revert** ke original `bfe0d82a...` (baseline bersih). `nested2fresh` tidak diadopsi.
  - **2026-09-17 — tes MOLY Vernee W1539 (madOS Apollo Lite) di MD1 nikel — GAGAL + NVRAM kena**:
    - MD firmware modem nikel sebenarnya di **partisi**: `md1img`=`mmcblk0p12` (24MB, W1603.P90), `md1dsp`=p13 (4MB), `md1arm7`=p14, `md3img`=p15 (5MB) — `/system/etc/firmware/*` hanya fallback (patch firmware via /system kemarin TIDAK pernah berdampak; perubahan ee a3f→a3d = timing race, bukan efek patch).
    - Backup partisi sebelum tes: `/sdcard/md1img_backup.img` (24MB, md5 `bfd11123`), `/sdcard/md1dsp_backup.img` (4MB, `69acba4b`); NVRAM backup `/sdcard/nvram_md_backup.tgz` (93KB).
    - Flash Vernee `modem_1` W1539.V27 (madOS zip, 15.3MB) + `dsp_1` ke partisi → boot loop (`md1 bootup/reset_start`, `NOT_READY`), MOLY Vernee inkompatibel nikel (X20 vs X20M calib/config).
    - Restore p12+p13 dari backup → partisi asli kembali (`bfd11123`/`69acba4b`), md1/md3 ready.
    - **SISA DAMPAK**: NVRAM `/data/nvram/md/NVRAM` tersentuh firmware Vernee (`SWCHANGE` di-update Vernee, `NVD_DATA` timestamps 2026-09-14/16). `SWCHANGE*` dihapus; `NVD_DATA` di-wipe biar regenerate → SIM flapping `READY↔NOT_READY` + `wait to reset` loop di kedua SIM, kambuh-kambuhan. Belum pulih 100% — perlu cold power off (baterai dicabut 10s) + tunggu NVRAM regenerate. Tidak boleh flash firmware modem asing lagi; MD3 partisi p15 saat ini = copy `md3rom.img.orig` (`bfe0d82a`) yang dulu normal (sinyal OK, call tetap crash).
  - **2026-09-17 malam — PULIH TOTAL via fastboot stock MIUI V10.2.1.0** (`/run/media/corex/System/ROM-nikel/nikel_global_images_V10.2.1.0.MBFMIXM_20190123.0000.00_6.0_global/images/`):
    - Penyebab flapping terverifikasi: partisi `md3img` (p15) berisi copy `/system` (beda versi, head `b4 45 3e 00` size `0x3e45b4` vs file `0x3e1a04`) + NVRAM sisa Vernee → MD1 hang (`AT+CGREG?` no response), WDT reset loop (`wait to reset`).
    - Fix: **fastboot flash modem partisi langsung** — `fastboot flash md1img images/md1rom.img && fastboot flash md1dsp images/md1dsp.img && fastboot flash md1arm7 images/md1arm7.img && fastboot flash md3img images/md3rom.img` (stock `flash_all.sh` MIUI memang flash modem via fastboot — tidak perlu SP Flash Tool/scatter untuk partisi modem). Bootloader unlocked → OKAY semua.
    - Stock `md3rom.img` head `b4 45 3e 00` = **identik header partisi asli** (md5 `cadc0922`, size `0x3e5610`) — sumber md3rom asli ketemu di MIUI fastboot ROM.
    - **Hasil: sinyal pulih penuh** (`READY,READY`, md1+md3 ready, TELKOMSEL+3, baseband W1603.P90) — TANPA format NVRAM, IMEI utuh, file `NVD_IMEI/MP0B_001` tidak tersentuh.
    - Pelajaran: recovery zip MIUI ≠ full stock; yang memperbaiki modem = **fastboot ROM** (berisi `md1rom/md1dsp/md1arm7/md3rom/preloader` + scatter). NVRAM campur firmware asing pulih dengan re-flash modem stock tanpa wipe.
- **Jalur fix yang terbuka (belum dikerjakan)**: patch firmware MD3 `modem_3_3g_n.img` (file di `/system/etc/firmware`, flashable via TWRP):
  1. Reverse handler pesan speech MD3 (penerima SMEM MD1↔MD3 type-19 / pesan mailbox `[9]=8/[9]=11`) → NO-OP atau swallow.
  2. Atau patch nested-EE guard supaya instance pertama tidak tertimpa (dapat file/line assert asli).
  - Tooling berikutnya: disassembler pool-aware per fungsi (entrypoints 0x1063ac/0x106248/0x1067c0), scan `ldr rX,[pc,#imm]` literal 16-bit (script sudah jadi, hasil 4 ref di atas).
- **Conclusion**: kernel/modem-era speech subsystem incompatibility (M-gen
  firmware + N-gen AP stack). Only M-gen (Android 6) stacks work.
- **Status**: not fixable from /system without kernel source + modem research
  (e.g., EE dumps via Comsecuris `mtk-baseband-sanctuary`).
- **Workaround**: VoIP (WhatsApp/Telegram). Alternatively use an Android 6.0
  ROM for calls.

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

## 8f. Controlled single-variable bisect, 2026-09-30 — 6 axes cleared

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

## 19. Build-integrity defects found while chasing #8 (2026-09-30)

None of these are the call crash — all four are real defects in the LOS build
and none of them is caused by the crash. They were found because #8 forced a
full differential against a working MIUI `/system`. **Not fixed**; listed so the
next person does not rediscover them.

### 19.1 RIL SELinux denials — MISDIAGNOSED, correcting the record

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

### 19.2 `md_log_config` is missing

```
E ccci_mdinit(0): Open md_log_config file failed, errno=2!   (ENOENT)
```

LOS-only (0 occurrences with the MIUI `/system`). Not currently known to break
anything, but it means the CCCI modem-logger has no config and is running
unconfigured.

### 19.3 `etc/audio_param/b6a/` is missing from the build; parser lib is stale — PARTIALLY FIXED

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

### 19.4 MIUI cannot boot on CM14.1 `/data` — `system_server` crash loop

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

### 19.5 MTK MAL / AudioLink blob set is absent (not yet diagnosed)

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

### 19.6 Reverse engineering MD1: investigated, NOT FEASIBLE as-is (2026-09-30)

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

## 20. Rear camera: why AF is dead and the low-light cast survives — full diagnosis (2026-10-01)

Data-driven follow-up to #13. Everything below was measured on the running
ROM (`build $ date 2026-10-01`, LOS 14.1 + the #12/#13 fixes, MIUI V10.2.1.0
flash layout).

### 20.1 Kernel differences are ruled out — do not patch the kernel driver

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

### 20.2 AF never runs at all

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

### 20.3 Both symptoms trace to the same cause: a borrowed 3A profile

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

### 20.4 The samarv reference camera stack does NOT work here — tested, reverted

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

### 20.5 The per-unit OTP path is still reachable (untested)

- `/dev/CAM_CAL_DRV` exists on LOS (char 239:0, `system:camera`).
- MIUI's `libcameracustom` exports `CAM_CALInit`, `CAM_CALDeviceName` and
  `S5K3L8_CAM_CALGetCalData(unsigned char*)`, so the whole CAM_CAL client is
  inside that library and can be dlopen'd rather than reimplemented.
- Ioctl constants recovered from `S5K3L8_CAM_CALGetCalData` disassembly:
  `0xc0146905` = `_IOWR('i', 5, 326)` and `0x020b00ff` = `_IOW(0, 0xff, 2816)`.
- Nothing in the HAL3A debug property list exposes an AWB/AF gain override,
  so a live gain experiment is not available; `debug.awb_mgr.lock` is the
  closest (lock only).

### 20.6 AF profile matrix — DONE (2026-10-01): AF is NOT profile-driven

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

### 20.7 Where AF actually has to come from

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

## 21. Injecting a library into the camera stack without reflashing (2026-10-01)

Built while attempting the OTP route (20.5). The technique is reusable for any
experiment that needs code running inside `mediaserver`, and it produced one
result that changes an earlier conclusion.

### 21.1 The mechanism

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

### 21.2 MIUI's libcameracustom DOES load into LOS's HAL3A

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

### 21.3 S5K3L8_CAM_CALGetCalData is not a getter — it needs a request buffer

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

### 21.4 CAM_CAL works on LOS - but the values are NOT a stable factory table

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

### 21.5 The request fields are NOT the missing piece (negative result)

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

### 21.6 The zero gains come from NVRAM, not from libcameracustom

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

### 21.7 Two more probe pitfalls

- `adb push` over a **bind-mounted file** does not update the mount: the mount
  holds the old inode. Always `umount -l` + `mount --bind` again after pushing
  a new build, otherwise you silently keep testing the previous one.
- Verify the build actually produced a new binary before blaming the device
  (the chained `clang && ld && adb push` swallowed one failure and the "new"
  run was the previous probe).

---

## 22. Autofocus is dead: two gates found and patched, one still open (2026-10-01)

Baseline, measured on a freshly wiped /data (so none of this is stale state):
over a full preview + shutter cycle the AF log contains **304 lines, 300 of
which are init**, and the only "runtime" lines are four calls to
`setAFMode`. Zero `doAFProcBuf`, zero focus steps, zero motor positions,
even with `debug.af_motor.position=1` and a forced
`debug.af_fullscan.step`. No VCM/AF activity in `dmesg` at all.

### 22.1 Gate 1 — the algorithm is handed mode 0 (patched)

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

### 22.2 Gate 2 — AfMgr drops the mode when a flag is clear (patched)

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

### 22.3 Result: both gates open, AF still does nothing

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

### 22.4 Patches are harmless but currently ineffective

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

### 22.5 What a real fix needs

Either (a) keep reversing the HAL until every gate is open, and then supply
a lens calibration, or (b) obtain an AF OTP/calibration source. Since
(1) the kernel has no AF OTP, (2) this unit has no AF data in NVRAM, and
(3) MIUI's working AF comes from its own HAL1 path, option (b) has no
source on this device — the honest conclusion is that dead AF is a platform
limitation here, not a tuning mistake.
