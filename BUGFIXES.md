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
| 14 | **Off-charge bootloop (logo MI berulang)** | ✅ **FIXED (2026-09-19)** | #14 |
| 8 | Voice calls crash C2K modem (MD3) | ❌ NOT FIXED (community-wide) | #8 |
| 15 | Rear camera green cast at night | ✅ FIXED (2026-09-14) | #13 |
| 16 | AudioFx has stopped | ❌ NOT FIXED | — |
| 17 | SMS (Messaging) app crashes on open | ❌ NOT FIXED | — |
| 18 | AOSP Browser crashes on open | ❌ NOT FIXED | — |
| 10b | Fingerprint scanner | ❌ NOT FIXED | #10b |
| 11 | Hotspot 5 GHz DFS channels | ⚠️ minor open | #11 |

Note: older front-camera sections #9 / #9a–#9e / #9b record the
investigation history; several of their interim conclusions were later
proven WRONG and are superseded by #12. Read them only for "what was
ruled out", not for the current status.

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

---

## NOT FIXED

Note: sections #9 and #9b below are the HISTORICAL investigation trail
of the front camera, which is now FIXED (see #12). They stay here only
so nobody re-discovers the same dead ends.

### 14. Off-charge bootloop (logo MI berulang saat dicolok charger saat HP mati) — FIXED (2026-09-19)

- **Symptom**: HP matikan lalu dicolok charger (dinding maupun PC) → tidak
  ada indikator charging, HP loop logo MI berulang-ulang, tidak pernah boot
  ke home screen.
- **Diagnosis** (tooling: `lsusb` polling USB VID/PID):
  - Loop terukur ~18 detik/cycle: preloader `0e8d:2000` (~3 s) → LK + logo
    `0e8d:2008` (~15 s) → WDT reset. **Kernel tidak pernah jalan** (adbd
    tidak pernah muncul; `last_kmsg` hanya header ram_console karena tiap
    reset preloader membersihkannya, `printk.disable_uart=1` = tanpa UART).
  - Semua komponen KPOC (kernel power off charging) lengkap di ROM:
    `init.mt6797.rc` `on charger` → mount system + `start fuelgauged` +
    `start kpoc_charger` (+ adb), binary `/system/bin/kpoc_charger`
    (26 KB, dari MIUI blob) + `/sbin/healthd` + semua lib (`libshowlogo`,
    `libgui`, `libui`, `libhardware_legacy`, `libsuspend`) ada.
  - Kernel & LK = prebuilt MIUI bd54 (kernel diff vs MIUI stock boot.img
    hanya build-stamp, tanggal, sensor config — KPOC logic identik).
    LK string KPOC lengkap (`mt65xx_bat_init`, `check_bat_protect_status`,
    `< Kernel Power Off Charging Detection Ok>`).
  - **Gate-nya = LK env `off-mode-charge`**: node `/proc/lk_env` di kernel
    (driver MTK `sysenv`, backing store = partisi `para` = `mmcblk0p2`).
    Default `off-mode-charge=1` → LK merutekan boot charger masuk jalur
    KPOC, yang crash di build ini (crash di fase KPOC antara LK jump dan
    USB gadget init — tidak terverifikasi lebih detail karena log ikut
    ter-reset; tidak diselidiki lebih lanjut karena bypass menyelesaikan
    kasus penggunaan).
  - Meng-test boot.img MIUI stock tidak membantu memvalidasi (stuck logo
    juga) karena userdata sudah milik LOS (e4crypt) — boot ROM lain pasti
    gagal mount data.
- **Fix** (root adb):
  ```
  echo "off-mode-charge=0" > /proc/lk_env
  ```
- **Efek**: saat HP mati + charger → HP **boot normal ke Android** (charger
  tetap bekerja, indikator baterai via UI Android; animasi KPOC awal tidak
  ada — trade-off yang diterima).
- **Verifikasi**: `poweroff` → colok USB PC → boot ke home screen OK;
  `/proc/lk_env` = `off-mode-charge=0`; partisi `para` berisi
  `ENV_v1 off-mode-charge=0` → **persisten** (survive reboot & power cycle).
  Setting tersimpan di partisi, bukan per-flash — tidak perlu repatch setiap
  ROM. (Tetap tersimpan walau ganti boot.img karena partisi para tidak di-
  sentuh oleh flash normal.)

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
