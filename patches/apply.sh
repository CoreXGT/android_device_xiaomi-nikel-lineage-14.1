#!/bin/bash
# Apply nikel out-of-tree fixes (see README.md).
# Usage (sourceable):  cd device/xiaomi/nikel/patches && . apply.sh && cd -
# Idempotent: patches already applied / webview already replaced are skipped.

# $0 is NOT the script when sourced — use BASH_SOURCE
THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# patches/ = <build-root>/device/xiaomi/nikel/patches → strip 4 components
ROM_ROOT="${THIS_DIR%/*/*/*/*}"

echo "== nikel patches: build root = $ROM_ROOT"

APPLIED_ANY=0
apply_patch() { # <patch-file> <target-dir-relative>
    local patch="$1" dir="$ROM_ROOT/$2"
    if [ ! -d "$dir" ]; then
        echo "!! $2 not found — is the build root correct?"; return 1
    fi
    if git -C "$dir" apply --reverse --check --quiet "$patch" 2>/dev/null; then
        echo "== $2: patch already applied, skip"
        return 0
    fi
    if git -C "$dir" apply --check --quiet "$patch" 2>/dev/null; then
        git -C "$dir" am --quiet "$patch" && echo "== $2: patch applied" && APPLIED_ANY=1
    else
        echo "!! $2: patch does not apply (tree dirty or changed) — apply manually"
        return 1
    fi
}

apply_patch "$THIS_DIR/cmsdk/0001-cmsdk-NetworkTraffic-proc-net-dev-fallback.patch" vendor/cmsdk
apply_patch "$THIS_DIR/audiofx/0001-AudioFX-onStartCommand-guard.patch" packages/apps/AudioFX

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
else
    echo "== nothing to do."
fi
