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
- **Conclusion**: kernel/modem-era speech subsystem incompatibility (M-gen
  firmware + N-gen AP stack). Only M-gen (Android 6) stacks work.
- **Status**: not fixable from /system without kernel source + modem research
  (e.g., EE dumps via Comsecuris `mtk-baseband-sanctuary`).
- **Workaround**: VoIP (WhatsApp/Telegram). Alternatively use an Android 6.0
  ROM for calls.

### 9. Camera front (5 MP) — sensor not enumerated

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
  1. Confirm the front camera ever worked on this unit (stock MIUI / arΩma)
     to rule out hardware damage.
  2. The `I/O error` for every SUB driver suggests the sensor never answers —
     check the sub-camera power rails (VCAM) and the MCLK for sensor dev 2
     (`/proc/driver/camsensor2`, kernel `camera_hw` driver state during probe).
  3. If a front-camera-capable kernel is found (a kernel whose
     `/proc/driver/camera_info` shows a `CAM[2]` entry), test whether its
     sensor driver set still matches the HAL: the HAL's own driver table
     (printed as `SENSOR_DRVNAME_S5K5E2YA_MIPI_RAW`) must line up with the
     kernel driver indices — an index mismatch makes the MAIN search latch
     onto the wrong chip (we saw `0x5e20` on the main slot).
  4. A kernel source rebuild (sensor driver enable + hwmsen fix) would solve
     both this and keep the bd13 sensor fixes.

### 10. Video recording — encoder never instantiated (front camera issue too)

- **Symptom** (after fix #0): photo capture works, video recording fails
  immediately ("failed to record video"). All resolutions fail.
- **Root cause chain** (confirmed step by step):
  1. `MediaRecorder` picks the FIRST `video/avc` encoder from the
     MediaCodecList. The MTK HW encoder (`OMX.MTK.VIDEO.ENCODER.AVC`,
     registered in `configs/media_codecs.xml`) is **missing from the runtime
     list**, so MediaCodec falls back to the software encoder
     (`OMX.google.h264.encoder`).
  2. The SW encoder dies in EXECUTING state (`OMX_ErrorUndefined
     0x80001001`) because the MTK camera HAL feeds it vendor gralloc buffers
     it cannot map.
  3. `media_codecs.xml` has a stray `.` after `/>` in the H263 entry which
     aborted the XML parse before the MTK encoder entries — fixed in
     `configs/media_codecs.xml`, **but that alone is not enough** (see below).
  4. The runtime codec list comes from the `media.codec` HAL service, whose
     `libMtkOmxCore.so` returns `InvalidComponentName (0x80001002)` for every
     MTK component. The 64-bit ROM runs 32-bit media processes, and the
     **64-bit MTK component libs are absent** (`libMtkOmxVenc.so`,
     `libMtkOmxVdecEx.so`, `libvcodec_oal.so` exist only in /system/lib);
     MIUI M-gen also lacks 64-bit versions (its mediaserver is 32-bit and
     hosts OMX in-process).
  5. Swapping `libcameracustom.so` with the MIUI build crashes mediaserver
     (`cust_getFlashMaxIDutyiiiPiS_` missing) — do not repeat.
  6. Disabling the `media.codec` HAL service (`stop mediacodec`) breaks
     decoding for all apps — reverted.
- **Leads for the next attempt**:
  1. Make the `media.codec` HAL actually enumerate the MTK core — compare
     `ltrace`-style: in mediaserver the same 32-bit libs instantiate fine
     (`makeComponentInstance(OMX.MTK.VIDEO.ENCODER.AVC)` succeeds at startup),
     while in the HAL `libMtkOmxCore.so` returns InvalidComponentName. The
     difference must be found (process name? property? the core's init reads
     something per-process?).
  2. Alternative: run a 32-bit `mediacodec` HAL with the MTK component libs
     present and correctly registered (they are, in /system/lib).
  3. Alternative: patch the framework to host OMX components inside
     mediaserver again (M-gen behaviour) instead of the media.codec HAL.
  4. Verify with the dex codec-list tool:
     `CLASSPATH=/data/local/tmp/test2.dex app_process /system/bin Test2`
     (see below) — success = `OMX.MTK.VIDEO.ENCODER.AVC` appears as ENC.
- **Tool**: a codec-list checker built from `Test2.java` (Java 7 + dx) run
  via `app_process`; source is 12 lines — rebuild as needed.

### 10b. Fingerprint scanner

- Driver node exists (`fpc_irq` input, `/dev/fpsensor`) but HAL/service
  integration has not been built or tested on this tree.

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
