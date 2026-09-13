# Device Tree — Xiaomi Redmi Note 4 (MediaTek) "nikel" — LineageOS 14.1

Fork of bju2000's device tree, maintained by CoreXGT.
**Bug tracker and detailed fix documentation: see [BUGFIXES.md](BUGFIXES.md).**

## Spec Sheet

| Feature                 | Specification                     |
| :---------------------- | :-------------------------------- |
| Codename                | nikel                             |
| Chipset                 | Mediatek MT6797 (Helio X20/X25)   |
| CPU                     | Deca-core 2.1 GHz (A72 x2 + A53)  |
| GPU                     | Mali-T880 MP4                     |
| Memory                  | 2/3/4 GB                          |
| Shipped Android Version | 6.0.1 (MIUI M-gen)                |
| Storage                 | 32/64 GB eMMC                     |
| MicroSD                 | Up to 256 GB                      |
| Battery                 | 4100 mAh (non-removable)          |
| Dimensions              | 151 x 76 x 8.5 mm                 |
| Display                 | 5.5" 1920x1080 IPS (~401 PPI)     |
| Rear Camera             | 13 MP, LED flash                  |
| Front Camera            | 5 MP                              |
| Sensors                 | BMI160 (accel+gyro), YAS537 (mag), LTR579 (light/prox), Bosch |
| WiFi                    | MT6632 802.11 a/b/g/n/ac, 2.4+5GHz |
| Bluetooth               | 4.2                               |
| Release Date            | January 2017                      |

## Build Instructions

```bash
# Get the LineageOS 14.1 source
repo init -u git://github.com/LineageOS/android.git -b cm-14.1
repo sync

# Device tree and vendor blobs
git clone https://github.com/CoreXGT/android_device_xiaomi-nikel-lineage-14.1.git -b master device/xiaomi/nikel
git clone https://github.com/CoreXGT/android_vendor_xiaomi_nikel.git -b master vendor/xiaomi/nikel

# Apply out-of-tree patches (netd, frameworks/opt/net/wifi, etc.)
cd device/xiaomi/nikel/patches && . apply.sh && cd -

# Build
source build/envsetup.sh
breakfast nikel
make otapackage -j4

# Jack server fix
# add this to ~/.jack-server/config.properties
jack.server.max-jars-size=104857600
jack.server.max-service=2
jack.server.service.port=8076
jack.server.max-service.by-mem=1\=2147483648\:2\=3221225472\:3\=4294967296
jack.server.admin.port=8077
jack.server.config.version=2
jack.server.time-out=7200

jack.server.vm-args=-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx2048m
```

### Build notes

- **RAM**: 8 GB is enough with `-j4`; `mka`/`brunch` force `-j$(nproc)` which
  OOMs on 8 GB machines — prefer `make otapackage -j4`.
- **Jack**: on newer JDKs the Jack server needs TLSv1/1.1 re-enabled in
  `java.security` and a manual start:
  `jack-admin start-server -Djack.home=$HOME/.jack-server -Xmx6g -cp ...`
  (do NOT set `ANDROID_COMPILE_WITH_JACK=false` — it breaks the build).
- **TMPDIR**: some build steps need >2 GB in /tmp — set `export TMPDIR=<big dir>`.
- **ccache**: enabled by default in cm-14.1; point it somewhere persistent with
  `export CCACHE_DIR=<path>`.
- **Host toolchain**: `check_radio_versions.py` is Python 2 and flex-2.5.39
  breaks on glibc ≥ 2.27 — use
  `export PATH=<python2>/bin:$PATH LC_ALL=C`. **note**: My latest build using Ubuntu 18 LTS, so I haven’t had this problem

## Feature Status

| Feature | Status | Notes |
| :--- | :--- | :--- |
| Boot / WiFi / Bluetooth | ✅ Working | |
| Mobile data (LTE) | ✅ Fixed | MediaTekRIL class, see BUGFIXES.md #2 |
| SD card | ✅ Fixed | fstab case mismatch, BUGFIXES.md #1 |
| Sensors (accel/gyro/mag/rotation) | ✅ Fixed | needs MIUI bd13 kernel + rc, BUGFIXES.md #3 |
| Hotspot 2.4 GHz | ✅ Fixed | netd patches, BUGFIXES.md #4 |
| Hotspot 5 GHz | ✅ Fixed | framework + netd patches, BUGFIXES.md #5 |
| ADB on boot | ✅ Fixed | BUGFIXES.md #7 |
| Camera (rear) | ✅ Fixed | libmtkjpeg bionic symbol patch, BUGFIXES.md #0 |
| Camera (front) | ❌ Broken | sensor not enumerated, BUGFIXES.md #9 |
| Voice calls | ❌ Broken | MD3 speech crash, known issue, BUGFIXES.md #9 |
| Fingerprint scanner | ❌ Not tested/known issue | |
| FM radio | Not verified | |

## Known Issues (details in BUGFIXES.md)

1. **Voice calls crash the C2K modem (MD3)** — not fixable from /system; every
   N-gen (7.x) build has this. Use VoIP apps for calls.
2. **Front camera not enumerated** — rear camera fixed (see #0); the front sensor probe fails kernel-side, see #9.
3. **Fingerprint scanner** — untested on this tree.

## Credits

* bju2000 — original device tree
* SamarV-121 — device tree, vendor blobs, kernel prebuilt
* AdrianoMartins — libgralloc_extra blobs
* divis1969 — netd hotspot patch
* xen0n, DiedMaster, end222 — patches
* end222 (omega device tree) — GraphicBuffer ABI shim
* CoreXGT — fixes and maintenance
