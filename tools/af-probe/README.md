# AF / VCM probe tools

Small static ARM programs and a trace helper used to diagnose **AF not focusing**
on nikel (see `BUGFIXES.md` §32).

They exist because the fault is in the kernel's AF driver and cannot be seen from
Android logs: the driver swallows its own i2c errors and reports success to
userspace, so every HAL-side log line looks healthy.

## What they established

* `/dev/MAINAF` is single-open — the camera app must be **closed** first.
* `SETDRVNAME` takes a 20-byte blob; the name is NUL-terminated at byte 9 and
  bytes 10–19 are ignored. The VCM i2c address is **not** in it.
* `SETDRVNAME("DW9714AF")` returns `1`; every other spelling returns `EPERM`.
  Binding the session is what makes `GET` return real data.
* `GET` struct layout: `[0]=curpos [4]=macro [8]=inf`. `curpos` never updates —
  not even after a successful `MOVETO`.
* `SETPARA` has no case in the driver: `cmdId 0..7` all return `EPERM`.
* `MOVETO` validates its target against the `[macro, inf]` window and returns
  `EINVAL` outside it.
* **The real finding:** the driver addresses the VCM at i2c `0x0e`, but nikel's
  DW9714AF VCM is at `0x72` (DT `camera_main_af@72`, driver `MAINAF` bound to
  `2-0072`). Every transfer NAKs with `EREMOTEIO` (`-121`) and the lens never
  moves.

## Building

Needs bionic headers; the NDK arm gcc 4.8 in `prebuilts` lacks them, so use 4.9:

```sh
ROOT=<path-to>/rom_source
$ROOT/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androideabi-gcc \
    --sysroot=$ROOT/prebuilts/ndk/current/platforms/android-23/arch-arm \
    -static -O2 -o probe12 probe12.c
adb push probe12 /data/local/tmp/ && adb shell chmod 755 /data/local/tmp/probe12
```

Run as root (`adb root` works on this userdebug build) and with the camera app
**closed through the UI** — `am force-stop` does not drop the session.

## Files

| File | Purpose |
| :--- | :--- |
| `probe12.c` | Maps the driver's whole ioctl surface: SETPARA sweep, `GET` layout, SETINFPOS/SETMACROPOS, MOVETO range validation. |
| `probe13.c` | Binds like the HAL and issues three long `MOVETO` holds. |
| `probe14.c` | Issues `SETDRVNAME` with a chosen lens name then `MOVETO` — used to show `DW9714AF` and `DW9718AF` both land on `0x0e`. |
| `trace_bus2.sh` | Runs `probe13` under an ftrace filter on i2c adapter 2, showing the actual addresses used. |

## Observing the failure without looking at the lens

The lens is too small to watch and the camera cannot be open while the device is
probed, so use the kernel's own trace instead:

```sh
adb shell mount -t debugfs none /sys/kernel/debug
adb shell /data/local/tmp/trace_bus2.sh
```

The output shows writes like `i2c_write: i2c-2 #0 a=00e … [03-00-fa]` followed by
`i2c_result: i2c-2 n=0 ret=-121`. That pair is the whole bug in two lines.

To watch the same traffic during a normal camera open instead (which also shows
the AF thread moving the lens), start the trace first, then:

```sh
adb shell am start -a android.media.action.IMAGE_CAPTURE
```

## Suppressing the log noise that hides AF logs

The MTK AE/AWB algorithms emit ~1500 lines/minute from uid `media`, which trips
logd's `chatty` suppression and silently deletes that uid's other output —
including AF lines. Silence the flood at the source before capturing:

```sh
for t in AeAlgo awb_algo LuxLevels aaa_hal_sttCtrl aaa_state_mgr aao_buf_mgr \
         afo_buf_mgr path_cam pd_mgr Malog SeninfDrvImp; do
    adb shell setprop log.tag.$t W
done
```

`logcat -G 32M` enlarges the buffer but does **not** prevent this.
