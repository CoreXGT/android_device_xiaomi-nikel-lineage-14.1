#!/bin/bash
# Apply nikel out-of-tree fixes (see README.md).
# Usage (sourceable):  cd device/xiaomi/nikel/patches && . apply.sh && cd -
# Idempotent: patches already applied / webview already replaced are skipped.
#
# TWO PATCH SETS LIVE HERE:
#
#   patches/*.patch      -> cm-14.1 (Android 7.1) sources
#   patches/cm13/*.patch -> cm-13.0 (Android 6.0.1) sources
#
# apply.sh detects the platform source in the build root and applies the
# matching set. A patch whose target does not exist in that platform is
# reported as SKIPPED (not applicable), never as a failure.
#
# Covers ALL out-of-tree changes (everything outside vendor blobs + this
# device tree):
#   cm-14.1:
#     frameworks_av.patch           -> frameworks/av          (MTK audio porting)
#     frameworks_native.patch       -> frameworks/native      (MTK libui/libgui bits)
#     frameworks_opt_net_wifi.patch -> frameworks/opt/net/wifi (5GHz soft AP)
#     hardware_libhardware.patch    -> hardware/libhardware   (MTK gps.h)
#     system_core.patch             -> system/core            (MTK libxlog, ifc_utils, adbd)
#     system_netd.patch             -> system/netd            (softap ap0, ipv6 rpfilter)
#     system_sepolicy.patch         -> system/sepolicy        (socket ioctl reverts)
#     system_sepolicy_zram.patch    -> system/sepolicy        (init may swapon zram0)
#     frameworks_base_windowmanager.patch -> frameworks/base   (WMG self-assignment removal)
#     system_core/0001-init-always-boot-permissive.patch -> system/core (user-build)
#     cmsdk/... , audiofx/...       -> vendor/cmsdk, packages/apps/AudioFX
#     webview.7z                   -> external/chromium-webview prebuilt
#
#   cm-13.0 (see patches/cm13/README.md for the per-patch rationale):
#     cm13/0001-init-always-boot-permissive.patch      -> system/core
#     cm13/0002-netd-softap-ap0.patch                  -> system/netd
#     cm13/0003-base-overlay-shim-nightdisplay.patch    -> frameworks/base
#     cm13/0004-settings-overlay-shim-gestures.patch    -> packages/apps/Settings
#     cm13/0005-base-windowmanager-self-assignment.patch-> frameworks/base
#     cm13/0006-core-adbd-enabled-at-boot.patch        -> system/core
#     cm13/0007-sepolicy-zram-swap.patch               -> external/sepolicy
#     audiofx/0001-...                                -> packages/apps/AudioFX (both sets)

# $0 is NOT the script when sourced — use BASH_SOURCE
THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# patches/ = <build-root>/device/xiaomi/nikel/patches → strip 4 components
ROM_ROOT="${THIS_DIR%/*/*/*/*}"

echo "== nikel patches: build root = $ROM_ROOT"

# ---------------------------------------------------------------------
# Platform detection: which patch set fits the source in the build root?
PLATFORM_VERSION="$(sed -n 's/^[[:space:]]*PLATFORM_VERSION :=[[:space:]]*//p' \
                        "$ROM_ROOT/build/core/version_defaults.mk" 2>/dev/null | head -1)"
case "$PLATFORM_VERSION" in
    6.0.*|6.*)  PATCHSET="cm13" ;;
    7.*|8.*)     PATCHSET="cm14" ;;
    *)           PATCHSET="cm14" ;;
esac
echo "== platform: PLATFORM_VERSION=${PLATFORM_VERSION:-?} → patch set: $PATCHSET"

APPLIED_ANY=0
SKIPPED_ANY=0

apply_patch() { # <patch-file-relative-to-patches/> <target-dir-relative>
    local patch="$THIS_DIR/$1" dir="$ROM_ROOT/$2"
    if [ ! -d "$dir" ]; then
        echo "!! $2 not found — is the build root correct?"; return 1
    fi
    if git -C "$dir" apply --reverse --check --quiet "$patch" 2>/dev/null; then
        echo "== $2: patch already applied, skip"
        return 0
    fi
    if git -C "$dir" apply --check --quiet "$patch" 2>/dev/null; then
        if git -C "$dir" am --quiet --3way "$patch" 2>/dev/null; then
            echo "== $2: patch applied"
            APPLIED_ANY=1
        else
            git -C "$dir" am --abort 2>/dev/null
            echo "!! $2: patch applies but git am failed — apply manually"
            return 1
        fi
    else
        echo "!! $2: patch does not apply (tree dirty or changed) — apply manually"
        return 1
    fi
}

skip_patch() { # <what> <why>
    echo "-- skip: $1"
    echo "          $2"
    SKIPPED_ANY=1
}

if [ "$PATCHSET" = "cm13" ]; then
    # ---------------- cm-13.0 (Android 6.0.1) -----------------------------
    apply_patch "cm13/0001-init-always-boot-permissive.patch"       system/core
    apply_patch "cm13/0006-core-adbd-enabled-at-boot.patch"         system/core
    apply_patch "cm13/0002-netd-softap-ap0.patch"                   system/netd
    apply_patch "cm13/0003-base-overlay-shim-nightdisplay.patch"    frameworks/base
    apply_patch "cm13/0005-base-windowmanager-self-assignment.patch" frameworks/base
    apply_patch "cm13/0004-settings-overlay-shim-gestures.patch"    packages/apps/Settings
    apply_patch "cm13/0007-sepolicy-zram-swap.patch"                external/sepolicy
    apply_patch "cm13/0008-vendor-icu-use-platform-icu.patch"       vendor/xiaomi/nikel
    apply_patch "cm13/0009-core-ifc-mtk-extras.patch"               system/core
    apply_patch "audiofx/0001-AudioFX-onStartCommand-guard.patch"    packages/apps/AudioFX

    skip_patch "vendor/cmsdk NetworkTraffic patch (#18)" \
        "cm-13.0's cmsdk has no statusbar/NetworkTraffic.java — the feature does not exist there"
    skip_patch "frameworks_opt_net_wifi 5GHz-soft-AP patch" \
        "cm-13.0's wifi stack has neither SoftApManager.java nor ApConfigUtil.java — different soft-AP implementation"
    skip_patch "hardware_libhardware MTK gps.h patch" \
        "cm-13.0 still ships the legacy GPS HAL API (GpsSvStatus/GPS_MAX_SVS) that the MTK gps.mt6797.so blob was built against"
    skip_patch "system_sepolicy socket-ioctl reverts" \
        "cm-13.0's policy is already pre-tightening (numeric ioctl ranges) and this ROM boots androidboot.selinux=permissive"
    skip_patch "frameworks_av MTK audio porting" \
        "written against cm-14.1 audioflinger/libmedia — needs a manual port (see patches/cm13/README.md)"
    skip_patch "frameworks_native MTK libui/libgui port" \
        "written against cm-14.1 ui/gui internals — needs a manual port (see patches/cm13/README.md)"
    skip_patch "system_core MTK porting (libxlog, remaining ifc_utils extras)" \
        "the parts the MTK RIL actually needs are ported in cm13/0009; the rest needs a manual port (see patches/cm13/README.md)"
else
    # ---------------- cm-14.1 (Android 7.1) -------------------------------
    apply_patch "cmsdk/0001-cmsdk-NetworkTraffic-proc-net-dev-fallback.patch" vendor/cmsdk
    apply_patch "audiofx/0001-AudioFX-onStartCommand-guard.patch" packages/apps/AudioFX
    apply_patch "system_core/0001-init-always-boot-permissive.patch" system/core
    apply_patch "frameworks_av.patch" frameworks/av
    apply_patch "frameworks_native.patch" frameworks/native
    apply_patch "frameworks_opt_net_wifi.patch" frameworks/opt/net/wifi
    apply_patch "hardware_libhardware.patch" hardware/libhardware
    apply_patch "system_core.patch" system/core
    apply_patch "system_netd.patch" system/netd
    apply_patch "system_sepolicy.patch" system/sepolicy
    apply_patch "system_sepolicy_zram.patch" system/sepolicy
    apply_patch "frameworks_base_windowmanager.patch" frameworks/base
fi

# webview prebuilt (binary, shipped compressed)
WEBVIEW_OK_MD5="2021fea0adff94bffc9f9990b56077c6"
WV_ARM="$ROM_ROOT/external/chromium-webview/prebuilt/arm/webview.apk"
WV_ARM64="$ROM_ROOT/external/chromium-webview/prebuilt/arm64/webview.apk"
CUR_MD5="$(md5sum "$WV_ARM" 2>/dev/null | cut -d' ' -f1)"
if [ "$CUR_MD5" = "$WEBVIEW_OK_MD5" ]; then
    echo "== webview prebuilt: already correct, skip"
elif [ -f "$THIS_DIR/webview.7z" ]; then
    TMPW="$(mktemp -d)"
    if 7z x -y -o"$TMPW" "$THIS_DIR/webview.7z" >/dev/null 2>&1 \
        && [ "$(md5sum "$TMPW/webview.apk" | cut -d' ' -f1)" = "$WEBVIEW_OK_MD5" ]; then
        cp "$TMPW/webview.apk" "$WV_ARM"
        cp "$TMPW/webview.apk" "$WV_ARM64"
        echo "== webview prebuilt: replaced (arm + arm64)"
        APPLIED_ANY=1
    else
        echo "!! webview.7z extract/md5 mismatch — replace prebuilt manually"
    fi
    rm -rf "$TMPW"
else
    echo "!! patches/webview.7z missing — see README.md"
fi

if [ "$APPLIED_ANY" = 1 ]; then
    echo "== done. Rebuild affected modules:"
    echo "   cd $ROM_ROOT && source build/envsetup.sh && breakfast nikel && make SystemUI AudioFX -j6"
elif [ "$SKIPPED_ANY" = 1 ]; then
    echo "== done (nothing new to apply)."
else
    echo "== nothing to do."
fi
