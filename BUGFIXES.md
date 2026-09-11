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

### 9b. Camera front (5 MP) — VCAM_D regulator wiring

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
