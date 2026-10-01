# Camera AF (autofocus) — nikel

Two independent defects had to be fixed to get the rear camera's AF chain to
start. Both are in this directory / the vendor preblobs; nothing here is a
source patch to AOSP.

## 1. `/dev/MAINAF`, `/dev/SUBAF` were 0600 root:root — `init.mt6797.rc`

`mediaserver` runs as uid 1006 (`camera`). The VCM/AF motor nodes are created by
devtmpfs as `crw------- root root`, and this ROM's `init.mt6797.rc` camera block
listed every camera node *except* the AF ones, so nothing ever widened them.
`MCUDrv` could not `open()` them, so `m_fdMCU` stayed invalid and the whole lens
initialisation silently no-opped.

Fixed by adding the six node names the HAL3A knows about to the existing
`chmod 0660` / `chown system camera` block in `rootdir/init.mt6797.rc`:

    /dev/MAINAF  /dev/SUBAF  /dev/MAIN2AF
    /dev/GAF001AF  /dev/GAF002AF  /dev/GAF008AF

Verified on device: every other camera node is `crw-rw---- system camera` while
these two were the only `crw-------` ones.

## 2. Lens-table lookup never matched — `patch_lensid.py`

`MCUDrv::lensSearch()` compares the sensor id reported by the sensor HAL
(`0x5e20`) against a lens table that libcameracustom fills in at runtime. That
value is in no table entry, so `m_u4CurrLensIdx_main` stayed 0,
`MCUDrv::getCurrLensID()` returned `0xffff`, and

    AfMgr+0x58b8  (AF_FLAG) = (getCurrLensID(dev) != 0xffff)   ->  0

disabled AF for the whole session. `patch_lensid.py` forces the comparison to
match table entry 2 (`SensorId 0x3103` = the S5K3L8 chip ID, `LensId 0x9714`).
One 4-byte Thumb-2 instruction; see the script's docstring for the exact bytes
and the reason the replacement is behaviour-preserving.

    python3 patch_lensid.py --check /system/lib/libcam.hal3a.v3.so

## Which library matters

`mediaserver` is **32-bit**. It maps `/system/lib/libcam.hal3a.v3.so`, not the
`lib64` copy:

    $ adb shell 'grep libcam.hal3a /proc/$(pidof mediaserver)/maps'
    /system/lib/libcam.hal3a.v3.so

Both copies are kept in the vendor blobs, and both export the same `MCUDrv`
symbols, so a patch aimed at the wrong one looks perfectly reasonable and
silently does nothing. Check this before touching any MTK camera library.

## What this does and does not fix

Fixed and verified on device:

* `LensMCU[idx]2 [CurrSensorId]0x3103,[CurrLensIdx]0x0002` — the lens lookup
  now matches and `AF_FLAG` becomes 1.
* The AF command pipeline reaches the algorithm for the first time
  (`Cmd_triggerAF`, `Cmd_lockAF`, `Cmd_unlockAF`, `Cmd_cancelAF`).
* The 3A state machine enters `StateAF`
  (`aaa_state_mgr: StateCameraPreview --> StateAF`). Before the patch that
  transition never appeared in any log.

Still not working:

* No `AFthread` exists in `mediaserver` (38 tasks, none named `AFthread`), so
  the per-buffer `doAF()`/`processAF()` never runs and the motor is never
  commanded. `ThreadRawImp::enableAFThread()` obtains its thread from
  `NS3A::IEventIrq::createInstance()` and never logs, so that call either is not
  reached or returns NULL. This is the next thing to chase.
