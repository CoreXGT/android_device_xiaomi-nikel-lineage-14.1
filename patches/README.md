# Out-of-tree patches for nikel

These patches belong to cm-14.1 projects that upstream no longer
maintains, so they are kept here instead of being pushed upstream.
Apply them after `repo sync`, before `breakfast`/`make`.

## Apply

```
cd <build-root>

# cmsdk — NetworkTraffic: /proc/net/dev fallback (status bar speed, bug #18)
git -C vendor/cmsdk am <path-to-this-tree>/device/xiaomi/nikel/patches/cmsdk/0001-cmsdk-NetworkTraffic-proc-net-dev-fallback.patch

# AudioFX — onStartCommand null guard (bug #15)
git -C packages/apps/AudioFX am <path-to-this-tree>/device/xiaomi/nikel/patches/audiofx/0001-AudioFX-onStartCommand-guard.patch
```

Then rebuild the affected modules (see BUGFIXES.md #15 and #18):

```
source build/envsetup.sh
breakfast nikel
make SystemUI AudioFX -j6
```

Note: the NetworkTraffic fix only takes effect through SystemUI
(cmsdk is statically linked into SystemUI) — flashing just
`org.cyanogenmod.platform.jar` is not enough.

## Also required (not a patch — binary replacement)

`external/chromium-webview/prebuilt/{arm,arm64}/webview.apk` must be
the cm-14.1-compatible `com.android.webview 60.0.3112.78`
(BUGFIXES.md #16/#17). The upstream prebuilt repo only contains
modern SDK-29 builds which the package manager rejects on 7.1.
The correct APK is shipped here compressed:

```
7z x patches/webview.7z -o<tmp>          # md5 2021fea0adff94bffc9f9990b56077c6
cp <tmp>/webview.apk external/chromium-webview/prebuilt/arm/webview.apk
cp <tmp>/webview.apk external/chromium-webview/prebuilt/arm64/webview.apk
```

(it contains both ABIs — the same file goes to arm/ and arm64/)

## Index

| patch | project | fixes |
|---|---|---|
| `cmsdk/0001-cmsdk-NetworkTraffic-proc-net-dev-fallback.patch` | vendor/cmsdk | #18 status bar speed always 0 |
| `audiofx/0001-AudioFX-onStartCommand-guard.patch` | packages/apps/AudioFX | #15 AudioFx has stopped |
