# cm-13.0 (Android 6.0.1) patch set

The nikel device tree, the vendor blobs and the original patch set are all
**cm-14.1 (Android 7.1)**. This directory holds the ports of that patch set
to **cm-13.0 (Android 6.0.1)**, which is what the ROM is currently built
against (`build/core/version_defaults.mk` → `PLATFORM_VERSION := 6.0.1`).

`../apply.sh` detects the platform source in the build root and applies
exactly one set:

| platform        | patch set applied |
| :-------------- | :---------------- |
| 6.0.x (cm-13.0) | `cm13/*.patch`    |
| 7.x / 8.x       | the top-level `*.patch` files |

## What is ported

| cm13 patch | target | what it does |
| :--------- | :----- | :----------- |
| `0001-init-always-boot-permissive.patch` | `system/core` | `selinux_is_enforcing()` always returns false, so every variant boots with `androidboot.selinux=permissive` (a user build otherwise wedges in early-init `mount_all` and never leaves the MI logo). Also grows `ENV[]` from 32 to 64 entries — the MTK `ril-daemon-mtk` needs more sockets than fit. |
| `0002-netd-softap-ap0.patch` | `system/netd` | The MTK supplicant drives the AP interface as `ap0`, so the hostapd control socket is `/data/misc/wifi/hostapd/ap0` and the hostapd `interface=` argument is `ap0`. The cm-14.1 patch's second hunk (make the ip6tables `rpfilter` rule non-fatal) is **not** ported: cm-13.0's `NatController` never installs an `rpfilter` rule, so there is nothing to make non-fatal. |
| `0003-base-overlay-shim-nightdisplay.patch` | `frameworks/base` | Declares `config_nightDisplayAvailable`. The nikel overlay (written for cm-14.1) overrides it, and aapt refuses an overlay that names a resource the base package does not have — that fails the `framework-res` build. Night display does not exist on Android 6.0.1, so nothing reads it. |
| `0004-settings-overlay-shim-gestures.patch` | `packages/apps/Settings` | Same for `config_gesture_settings_enabled` (the "Gestures" settings screen is a cm-14.1 feature). |
| `0005-base-windowmanager-self-assignment.patch` | `frameworks/base` | `WindowManagerGlobal.getWindowManagerService()` assigns `sWindowManagerService` to itself from its own getter — drop the redundant self-assignment. |
| `0006-core-adbd-enabled-at-boot.patch` | `system/core` | Keeps the `disabled` line of the `adbd` service commented out so adbd comes up on boot instead of waiting for a USB property trigger (BUGFIXES.md #7). On cm-14.1 this service lives in `rootdir/init.usb.rc`; on cm-13.0 it lives in `rootdir/init.rc` with an identical stanza. |
| `0007-sepolicy-zram-swap.patch` | `external/sepolicy` | `init.zram.rc` (device tree) runs `mkswap`+`swapon` on `/dev/block/zram0`, which carries the `ram_device` label, so init needs `rw_file_perms` on it. cm-13.0 keeps the policy in `external/sepolicy` (not `system/sepolicy`) and has no `ram_device` rule yet. |
| `0008-vendor-icu-use-platform-icu.patch` | `vendor/xiaomi/nikel` | Keeps the **MIUI** `libicuuc.so` from overwriting the platform's own ICU 55.1. The platform also builds `libicui18n` from that ICU, and the two builds are not ABI compatible — the platform's `libicui18n` references the `icu_55` C++ vtables, which the MIUI ICU build does not have (`cannot locate symbol "_ZTVN6icu_5512CacheKeyBaseE"`). Since `libicui18n` sits in the `LD_PRELOAD` chain (`libmtk_symbols.so`), the failure killed every process init started. The blacklist is **conditional** on `PLATFORM_VERSION`: on cm-14.1 the vendor ICU has to stay (that platform ships ICU 58 and the MTK blobs need the ICU-55 flavour, which the `icu55.c` shim provides). |
| `0009-core-ifc-mtk-extras.patch` | `system/core` | Adds the two MTK-specific `libnetutils` entry points the MTK RIL daemon calls: `ifc_ccmni_md_cfg()` (CCMNI/multi-SIM modem remapping) and `ifc_set_txq_state()`. Without them `mtk-ril.so` cannot be loaded at all (`E/RILD: dlopen failed: cannot locate symbol "ifc_ccmni_md_cfg"`). cm-13.0's `libnetutils` already provides the generic ones (`ifc_disable`, `ifc_remove_default_route`, `ifc_reset_connections`). Ported from the cm-14.1 `system_core.patch`. |

The AudioFX onStartCommand guard (`../audiofx/0001-...`) applies unchanged to
both platforms and is applied by both sets.

## What is intentionally NOT ported, and why

| cm-14.1 patch | why not |
| :------------- | :------ |
| `vendor/cmsdk` NetworkTraffic (`/proc/net/dev` fallback, BUGFIXES #18) | cm-13.0's `cmsdk` has no `statusbar/NetworkTraffic.java` — the status-bar traffic widget does not exist there, so there is nothing to patch. |
| `frameworks_opt_net_wifi` (5 GHz soft AP) | cm-13.0's wifi stack has neither `SoftApManager.java` nor `ApConfigUtil.java`; the soft-AP implementation is completely different, and the cm-14.1 problem (a mandatory HAL country code for 5 GHz) does not exist in that form. Verify 5 GHz AP on device instead. |
| `hardware_libhardware` (MTK `gps.h`) | cm-13.0 still ships the **legacy** GPS HAL API (`GpsSvStatus` / `GPS_MAX_SVS`), which is what the `gps.mt6797.so` vendor blob was built against. The cm-14.1 patch only *adds* the MTK legacy structs to a header that already had the new Gnss API — on cm-13.0 there is nothing to add. |
| `system_sepolicy` (socket-ioctl reverts) | The 11 stacked commits revert an Android-7 sepolicy tightening. cm-13.0's policy is still in the pre-tightening state (numeric ioctl ranges in `ioctl_macros`), and this ROM boots `androidboot.selinux=permissive` anyway. |
| `frameworks_av` (MTK audio porting) | **Needs a manual port** — the patch touches cm-14.1 `audioflinger/Threads.cpp` and `media/libmedia/Android.mk`. See "Still to port" below. |
| `frameworks_native` (MTK libui/libgui port) | **Needs a manual port** — 16 files, of which 7 are new `mediatek/` sources that can be copied verbatim; the rest are cm-14.1 `ui`/`gui` internals. See "Still to port" below. |
| `system_core` (MTK porting: libxlog, `ifc_utils`, `fs_config`, `init.usb.rc`) | **Needs a manual port.** The `libxlog/` shim is redundant — the device tree already ships `libmtk_symbols` (`mtk_xlog.cpp`) which provides `__xlog_buf_printf`. The `fs_config.c` hunk disables a `surfaceflinger` `CAP_SYS_NICE` entry that cm-13.0's `fs_config.c` does not have. The `init.usb.rc` hunk is covered by `0006-core-adbd-enabled-at-boot.patch`. The `ifc_utils`/`ifc.h` MTK extras are ported in `0009-core-ifc-mtk-extras.patch` — the rest of the hunk is not needed. |

## Still to port

1. **`frameworks/native` MTK libui/libgui port** — the MTK sensor HAL needs the
   legacy-SV extensions in `SensorManager.h`/`.cpp`, the MTK gralloc needs the
   `GraphicBufferMapper` lock symbol, and SurfaceFlinger needs the
   `DisplayUtils`/`Fence` dump hooks. The seven `mediatek/*` sources are new
   files and can be copied verbatim; only the seven modified files need their
   cm-13.0 context.
2. **`frameworks/av` MTK audio porting** — `media/libmedia/mtkaudio_stubs.cpp`
   is a new file (copy verbatim); `services/audioflinger/Threads.cpp` and
   `media/libmedia/Android.mk` need their cm-13.0 context.
3. **`system/core` `ifc_utils`/`ifc.h` MTK extras** — **done**, see
   `0009-core-ifc-mtk-extras.patch`. Only the two functions the MTK RIL
   actually calls were ported; the remaining ~260 lines of the cm-14.1 hunk
   are not needed on cm-13.0.

Until those are ported the ROM boots and the non-MTK-framework features work,
but MTK-specific paths (sensor HAL, MTK gralloc, MTK audio offload) may not.
