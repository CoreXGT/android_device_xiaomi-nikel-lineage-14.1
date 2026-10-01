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
# Prepare your OS
sudo apt update
sudo apt install -y bc bison build-essential ccache curl flex g++-multilib gcc-multilib git \
  git-core gnupg gperf imagemagick lib32ncurses5-dev lib32readline-dev lib32z1-dev \
  liblz4-tool libncurses5 libncurses5-dev libsdl1.2-dev libssl-dev libwxgtk3.0-dev \
  libxml2 libxml2-utils lzop pngcrush rsync schedtool squashfs-tools xsltproc \
  zip zlib1g-dev openjdk-8-jdk python git-lfs pigz

# Get the LineageOS 14.1 source
repo init -u git://github.com/LineageOS/android.git -b cm-14.1
repo sync

# Device tree and vendor blobs
cd rom_source
git clone https://github.com/CoreXGT/android_device_xiaomi-nikel-lineage-14.1.git -b master device/xiaomi/nikel
git clone https://github.com/CoreXGT/android_vendor_xiaomi_nikel.git -b master vendor/xiaomi/nikel

# Apply out-of-tree patches (netd, frameworks/opt/net/wifi, etc.)
cd device/xiaomi/nikel/patches && . apply.sh && cd -

source build/envsetup.sh
# Jack server heap — REQUIRED on 8 GB machines (framework dex OOMs otherwise).
# The build launches/restarts the Jack server from this env var
# (see prebuilts/sdk/tools/jack_server_setup.mk).
export ANDROID_JACK_VM_ARGS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx6144m"
export LC_ALL=C
export TMPDIR=<tmp dir>
export CCACHE_DIR=<cache dir>

# --------------------------------------
# Build
breakfast nikel

# or

# Build otapackage only with 4 jobs
export MAKEFLAGS="-j4"
make otapackage -j4
# --------------------------------------
```

> The Jack server's `~/.jack-server/config.properties` tweaks (max-service,
> ports) are host-specific and NOT required — `ANDROID_JACK_VM_ARGS` above
> already controls the heap and takes priority on every build. Skip them
> unless you hit Jack OOM or port clashes on your machine.

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
| Sensors (accel/gyro/mag/rotation) | ✅ Fixed | needs MIUI bd54 kernel (shipped in `prebuilt/kernel`), BUGFIXES.md #3 |
| Hotspot 2.4 GHz | ✅ Fixed | netd patches, BUGFIXES.md #4 |
| Hotspot 5 GHz | ✅ Fixed | framework + netd patches, BUGFIXES.md #5 |
| ADB on boot | ✅ Fixed | BUGFIXES.md #7 |
| Camera (rear) | ✅ Fixed | photo + video + autofocus, BUGFIXES.md #0, #10 and #23 — 3A tuning caveat below |
| Camera (front) | ✅ Fixed | photo, BUGFIXES.md #12 (bd54 kernel + kdSensorList swap, shipped in `prebuilt/kernel`) |
| Off-charge (charge while powered off) | ✅ Fixed | boots to Android when charger is plugged while off (no KPOC animation), BUGFIXES.md #14 — `off-mode-charge=0` in `para` partition |
| Voice calls | ❌ Broken | MD3 speech crash, known issue, BUGFIXES.md #8 |
| Fingerprint scanner | ✅ Fixed | Goodix + Kinibi TEE port, BUGFIXES.md #10b/#10b-c |
| FM radio | Not verified | |

## Kernel note

The `prebuilt/kernel` file in this tree is a **MIUI bd54 kernel
(3.18.22)** with a `kdSensorList` binary patch (front/rear driver swap).
It is required: the SamarV bd04 kernel delivers no sensor data and the
MIUI bd13 kernel NACKs the front camera. Building the ROM always packs
this file into boot.img — do NOT replace it with a kernel built from
LOS source (front camera breaks again). Details: BUGFIXES.md #12.

## Known Issues (details in BUGFIXES.md)

1. **Voice calls crash the C2K modem (MD3)** — not fixable from /system; every
   N-gen (7.x) build has this. Use VoIP apps for calls.

2. **Rear-camera 3A tuning belongs to a different phone.** The
   `libcameracustom.so` in this tree carries IMX258 / S5K3P3SX / S5K5E2YA tuning
   and has **zero** S5K3L8 data — the phone's actual sensor. So:

   * Autofocus only engages because BUGFIXES.md #23 forces the lens table to
     match; without it the HAL looks for a lens that the tuning data says does
     not exist, and AF never starts.
   * The white-balance cast in #13 is the same cause — the AWB gains in play are
     IMX258's — and has **no fix inside this tree**.
   * Not fixable by swapping blobs: Xiaomi never shipped this phone past Android
     6.0, and 6.0's camera stack is ABI-incompatible with this 7.1 framework
     (`android::VectorImpl` vs `android::Vector`, BUGFIXES.md #27). Any real fix
     needs Xiaomi's own blob for a matching generation.

> Fingerprint requires the mobicore TEE daemon (shipped + started on
> post-fs-data). If the fingerprint menu errors, check `getprop sys.boot_completed`
> and `logcat | grep gf_` first.

## Credits

* bju2000 — original device tree
* SamarV-121 — device tree, vendor blobs, kernel prebuilt
* AdrianoMartins — libgralloc_extra blobs
* divis1969 — netd hotspot patch
* xen0n, DiedMaster, end222 — patches
* end222 (omega device tree) — GraphicBuffer ABI shim
* CoreXGT — fixes and maintenance
