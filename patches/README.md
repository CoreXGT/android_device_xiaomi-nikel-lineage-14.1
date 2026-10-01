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

## Camera AF (bug #24)

Two things, neither of them an AOSP source patch:

1. `rootdir/init.mt6797.rc` (already in this tree) — grants `system:camera` on
   `/dev/MAINAF`, `/dev/SUBAF`, `/dev/MAIN2AF`, `/dev/GAF001AF`,
   `/dev/GAF002AF`, `/dev/GAF008AF`. Without it `mediaserver` cannot open the
   VCM motor nodes and autofocus never starts.

2. `patches/camera-af/patch_lensid.py` — patches the **32-bit**
   `vendor/xiaomi/nikel/system/lib/libcam.hal3a.v3.so` so `MCUDrv::lensSearch`
   matches the sensor id and `AfMgr`'s AF-enable flag is set. The preblob in
   `vendor/xiaomi/nikel` is already patched; re-apply or verify with:

   ```
   python3 patches/camera-af/patch_lensid.py --check \
       ../vendor/xiaomi/nikel/system/lib/libcam.hal3a.v3.so
   ```

   Read `patches/camera-af/README.md` before touching any MTK camera library on
   this device: `mediaserver` is 32-bit and loads `system/lib/`, not `lib64/`.

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
| `camera-af/patch_lensid.py` + `rootdir/init.mt6797.rc` | vendor blobs (binary) | #24 rear camera AF never starts |
