# Device Tree — Xiaomi Redmi Note 4 (MediaTek) "nikel" — LineageOS 13.0 / 14.1

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

## Platform support

This tree builds against **two** platform sources. `apply.sh` detects which
one is in the build root and applies the matching patch set, and the device
tree picks the matching copies of the few files that differ.

| Platform source | Android | `PLATFORM_VERSION` | patch set |
| :-------------- | :------ | :----------------- | :-------- |
| `cm-13.0`       | 6.0.1   | `6.0.1`            | `patches/cm13/*.patch` |
| `cm-14.1`       | 7.1.2   | `7.1.2`            | `patches/*.patch` |

Nothing is published for nikel on either branch, so the platform source is
a plain LineageOS checkout — the device tree, the vendor blobs and the
patch set are what make it work.

Files that differ between the two platforms:

* `ril/` vs `ril-cm13/` — the `MediaTekRIL` subclass. cm-13.0's `RIL` base
  class has no slot argument on `processUnsolicited()`/`processSolicited()`
  and passes `radioTechnology`/`profile`/`authType` to `setupDataCall()` as
  Strings. `BoardConfig.mk` selects the right copy via `BOARD_RIL_CLASS`.
* `patches/cm13/` — see [patches/cm13/README.md](patches/cm13/README.md)
  for the per-patch rationale and for what is deliberately not ported.

## Build Instructions

```bash
# Prepare your OS
sudo apt update
sudo apt install -y bc bison build-essential ccache curl flex g++-multilib gcc-multilib git \
  git-core gnupg gperf imagemagick lib32ncurses5-dev lib32readline-dev lib32z1-dev \
  liblz4-tool libncurses5 libncurses5-dev libsdl1.2-dev libssl-dev libwxgtk3.0-dev \
  libxml2 libxml2-utils lzop pngcrush rsync schedtool squashfs-tools xsltproc \
  zip zlib1g-dev openjdk-8-jdk python git-lfs pigz

# Get the LineageOS source (13.0 = Android 6.0.1, or 14.1 = Android 7.1.2)
repo init -u git://github.com/LineageOS/android.git -b cm-13.0   # Android 6
# repo init -u git://github.com/LineageOS/android.git -b cm-14.1 # Android 7
repo sync

# Clone device tree and vendor blobs
cd rom_source
git clone https://github.com/CoreXGT/android_device_xiaomi-nikel-lineage-14.1.git -b master device/xiaomi/nikel
git clone https://github.com/CoreXGT/android_vendor_xiaomi_nikel.git -b cm-14.1 vendor/xiaomi/nikel

# Apply out-of-tree patches (netd, frameworks/opt/net/wifi, etc.)
# apply.sh picks the patch set that matches the platform source it finds.
cd device/xiaomi/nikel/patches && . apply.sh && cd -

# Prepare for build
source build/envsetup.sh
export LC_ALL=C
export TMPDIR=<tmp dir>
export CCACHE_DIR=<cache dir>
# Jack server heap — REQUIRED on 8 GB machines (framework dex OOMs otherwise).
# The build launches/restarts the Jack server from this env var
# (see prebuilts/sdk/tools/jack_server_setup.mk).
export JACK_SERVER_VM_ARGUMENTS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx5g"
export ANDROID_JACK_VM_ARGS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx5g"

# -----------------------------------------------------------------------------
#                                       BUILD
#
#   breakfast nikel user     -> lunch lineage_nikel-user       <- build this
#   breakfast nikel          -> lunch lineage_nikel-userdebug  <- debug only
#   brunch nikel user        == breakfast nikel user + mka bacon
#   brunch nikel             == breakfast nikel userdebug + mka bacon — a
#                               bare brunch does NOT keep the variant that
#                               breakfast selected; it always re-lunches
#                               userdebug (vendor/cm/build/envsetup.sh:59-61)
#                             (mka bacon == mka otapackage)
#
# `breakfast` with no second argument defaults to **userdebug**, which sets
# ro.debuggable=1 and makes adbd run as root (uid 0). That is what breaks
# scrcpy's copy/paste — a root caller is rejected by ClipboardService with
#   SecurityException: Calling uid 0 does not own package com.android.shell
# A property of the caller, not a scrcpy bug — see BUGFIXES.md #29. Pass
# `user` explicitly for anything you intend to flash and keep.

breakfast nikel user

# -----------------------------------
# Build
brunch nikel user

# or

# Build otapackage only with 4 jobs
make -j4 otapackage
# -----------------------------------
# -----------------------------------------------------------------------------
```
```bash
# For manage jack-server

# Server list
./prebuilts/sdk/tools/jack-admin list-server

# Kill the server
./prebuilts/sdk/tools/jack-admin kill-server
pkill -f jack-server

# Start the server
./prebuilts/sdk/tools/jack-admin start-server

# Show the server memory
ps aux | grep jack-server | grep -o "\-Xmx[0-9a-z]*"
```


### Build notes

- **RAM**: 8 GB is enough with `-j4`; `mka`/`brunch` force `-j$(nproc)` which
  OOMs on 8 GB machines — prefer `make otapackage -j4`. Note: `export
  MAKEFLAGS="-j4"` does **not** cap `mka`/`brunch` (they pass an explicit
  `-j$(nproc)` on the command line, which overrides MAKEFLAGS). Extra `-j`
  arguments appended to `mka` do work — `mka bacon -j4` builds with 4 jobs.
- **Jack**: cm-13.0 and cm-14.1 both compile **every** java module with Jack
  (`build/core/combo/javac.mk` sets `ANDROID_COMPILE_WITH_JACK := true`), so a
  Jack server has to be reachable. `prebuilts/sdk/tools/jack` starts one
  itself when it cannot connect, so the build works without any manual
  step — but the server it launches uses the heap from
  `ANDROID_JACK_VM_ARGS`/`JACK_SERVER_VM_ARGUMENTS`.
  A **manual** `jack-admin start-server` fails with
  `JACK_JAR: unbound variable` unless `JACK_JAR` is exported, because
  `jack-admin` runs with `set -o nounset` and only the build system sets
  that variable:
  ```bash
  export JACK_JAR=$PWD/out/host/linux-x86/framework/jack.jar
  export JACK_VM_COMMAND="java -jar $PWD/out/host/linux-x86/framework/jack-launcher.jar"
  ./prebuilts/sdk/tools/jack-admin start-server
  ```
  Do NOT set `ANDROID_COMPILE_WITH_JACK=false` — it breaks the build.
- **curl**: `prebuilts/sdk/tools/jack` calls `curl --no-proxy`, which needs
  curl ≥ 7.86 (Ubuntu 20.04+). On older curl the Jack compile fails with
  `ERROR: Communication error with Jack server (2)`; the workaround is to
  use `--noproxy` instead.
- **TMPDIR**: some build steps need >2 GB in /tmp — set `export TMPDIR=<big dir>`.
- **ccache**: enabled by default in cm-14.1; point it somewhere persistent with
  `export CCACHE_DIR=<path>`.
- **Host toolchain**: cm-13.0's build helpers are **Python 2** — the logtags
  generator (`build/tools/java-event-log-tags.py`) and
  `check_radio_versions.py` are Python 2 scripts. With `python` pointing at
  Python 3 the build dies with
  `TypeError: cannot use a string pattern on a bytes-like object` while
  generating `*LogTags.java`. Use `export PATH=<python2>/bin:$PATH` (Ubuntu
  18.04's `python` is still 2.7). `flex-2.5.39` also breaks on glibc ≥ 2.27.
- **Maven**: cm-13.0's `vendor/cm/ambientsdk` module is fetched at build time
  with `mvn` (`com.cyngn.ambient:ambientsdk:1.6.0-147069776-39fe2d07` from
  `https://repo1.maven.org/maven2`). Without `mvn` the build fails with
  `Error 127` on that step. Either install Maven, or pre-place the AAR:
  ```bash
  curl -sSLo out/target/common/obj/JAVA_LIBRARIES/ambientsdk_intermediates/com.cyngn.ambient.ambientsdk-1.6.0-147069776-39fe2d07.aar \
    https://repo1.maven.org/maven2/com/cyngn/ambient/ambientsdk/1.6.0-147069776-39fe2d07/ambientsdk-1.6.0-147069776-39fe2d07.aar
  ```
  

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
| Fingerprint scanner | ✅ Fixed | Goodix + Kinibi TEE port, BUGFIXES.md #10b/#10b-c; template storage moved out of `/data/app` (patched `mcDriverDaemon`) so enrolled fingers survive reboots, #10b-f |
| FM radio | Not verified | |
| IR remote | 🔧 Fixed in tree | driver was **rejecting every buffer** (`write()` returned 0) and the HAL read that as success; now sends durations per `u32` — BUGFIXES.md §33 |

## Kernel note

The `prebuilt/kernel` file in this tree is a **MIUI bd54 kernel
(3.18.22)** with a `kdSensorList` binary patch (front/rear driver swap).
It is required: the SamarV bd04 kernel delivers no sensor data and the
MIUI bd13 kernel NACKs the front camera. Building the ROM always packs
this file into boot.img — do NOT replace it with a kernel built from
LOS source (front camera breaks again). Details: BUGFIXES.md #12.

Note that this is the **stock kernel of the phone** (the phone ships
MIUI on Android 6.0.1). It has nothing to do with the Android version of
the ROM built from this tree: the same prebuilt kernel is used by the
cm-13.0 (Android 6) and the cm-14.1 (Android 7) builds. A prebuilt
kernel is only missing something if `kernel/<vendor>/<device>` source
exists — then the build compiles the kernel instead. Deleting
`prebuilt/kernel` without providing a replacement does not build:
`device.mk` still copies it into `boot.img` and make aborts with
`No rule to make target 'device/xiaomi/nikel/prebuilt/kernel'`.

## Audio HAL note

`prebuilt/audio.primary.mt6797.so` is the **32-bit** MTK audio HAL. It is
the MIUI stock blob (`md5 e0ca7239…`, same file the vendor tree ships)
with **13 bytes patched**:

| offset | stock | patched | effect |
| :----- | :---- | :------ | :----- |
| `0x0d94e8` | `e92d4070` | `e12fff1e` | `push {r4,lr}` → `bx lr` — the "BGSound On/Off/Config" path returns immediately |
| `0x0d9a84` | `26` | `00` | SpeechPilot `2F26` → `2F00` |
| `0x0db338` | `e92d40f0` | `e12fff1e` | `bx lr` (BGSound) |
| `0x0dba9c` | `e92d40f0` | `e12fff1e` | `bx lr` (BGSound) |

It is installed through `PRODUCT_COPY_FILES` after the vendor inherit, so it
overrides `system/lib/hw/audio.primary.mt6797.so` only; `lib64` stays
stock. The same blob is used by the cm-13.0 and the cm-14.1 build — it is
an Android 6 era (MIUI) binary, not an Android 7 one.

Note that this patch does **not** fix the MD3 call crash (BUGFIXES.md #8);
that one lives in the modem firmware.

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
> and `logcat | grep gf_` first. Enrolled fingers are stored by the TEE in
> `/data/misc/mcregistr` (the shipped `mcDriverDaemon` is a same-length
> binary patch pointing the TA storage there — PackageManager deletes every
> non-APK directory under `/data/app`, see BUGFIXES.md #10b-f).

## Credits

* bju2000 — original device tree
* SamarV-121 — device tree, vendor blobs, kernel prebuilt
* AdrianoMartins — libgralloc_extra blobs
* divis1969 — netd hotspot patch
* xen0n, DiedMaster, end222 — patches
* end222 (omega device tree) — GraphicBuffer ABI shim
* CoreXGT — fixes and maintenance
