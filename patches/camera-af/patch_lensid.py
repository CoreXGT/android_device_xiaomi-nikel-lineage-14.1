#!/usr/bin/env python3
"""nikel: make the rear camera's AF (autofocus) chain start.

Root cause (see BUGFIXES.md #24)
---------------------------------
`mediaserver` is 32-bit, so it loads **/system/lib/libcam.hal3a.v3.so** — *not*
the lib64 copy.  All earlier AF work patched lib64 and therefore never executed.

In that 32-bit library, `MCUDrv::lensSearch(uint dev, uint sensorId)` matches
`sensorId` against a lens table that libcameracustom fills in at runtime:

    [0] SensorId=0xffff  LensId=0xffff      <- placeholder
    [1] SensorId=0x0135  LensId=0x9714
    [2] SensorId=0x3103  LensId=0x9714      <- 0x3103 == S5K3L8 chip ID (this phone)
    [3] SensorId=0x0258  LensId=0x0005

The sensor HAL reports `MainSensorIdx = 0x5e20`, which is in **no** entry, so
`MCUDrv::m_u4CurrLensIdx_main` stays 0.  `MCUDrv::getCurrLensID()` then returns
`table[0].LensId == 0xffff`, and `AfMgr::CCTMCUNameinit()` computes

    AfMgr+0x58b8 (AF_FLAG) = (getCurrLensID(dev) != 0xffff)   ->  0

which disables AF for the whole session.  Forcing the match to entry 2 makes
`LensId = 0x9714` and `AF_FLAG = 1`.

The patch
---------
One Thumb-2 instruction, four bytes, same size as what it replaces:

    file 0x0b1542  vaddr 0xb9542
      before: 10 2b  cmp r3, #0x10      \
      before: f5 d1  bne 0xb9532        /  loop that computes the default index
      after:  43 f2 03 15  movw r5, #0x3103

`r5` is `CurrSensorId`.  The replaced loop only sets the *default* index, which
on this device is already 0 (only table[0] has `LensId == 0xffff`), so removing
it changes nothing; `r4` is reloaded with 0xffff right afterwards, so no register
is left stale.  The change is therefore behaviour-preserving except for the
intended sensor-id match.

Reverting is exact: write 10 2b f5 d1 back at 0x0b1542.

Usage
-----
    python3 patch_lensid.py <libcam.hal3a.v3.so>            # in place
    python3 patch_lensid.py <in.so> <out.so>                # copy + patch
    python3 patch_lensid.py --check <libcam.hal3a.v3.so>     # verify only
"""

import hashlib
import shutil
import sys

# vaddr 0xb9542 -> file offset 0xb1542 (LOAD segment: vaddr = file + 0x8000)
OFF = 0x0B1542
STOCK = bytes.fromhex("102bf5d1")   # cmp r3,#0x10 ; bne
PATCH = bytes.fromhex("43f20315")   # movw r5, #0x3103
STOCK_MD5 = "9af2c96b45bc4f6f9741334d7c26eaa5"
PATCH_MD5 = "47b9883f8f7a41674ed11bc8a96ad533"


def state(blob: bytes) -> str:
    cur = blob[OFF:OFF + 4]
    if cur == STOCK:
        return "stock"
    if cur == PATCH:
        return "patched"
    return "unknown(%s)" % cur.hex()


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    check = "--check" in argv
    if not args or len(args) > 2:
        print(__doc__)
        return 2
    src = args[0]
    dst = args[1] if len(args) == 2 else src

    blob = bytearray(open(src, "rb").read())
    if len(blob) < OFF + 4:
        print("!! %s is too small to be libcam.hal3a.v3.so" % src)
        return 1
    st = state(blob)
    print("%s: %s (md5 %s)" % (src, st, hashlib.md5(blob).hexdigest()))

    if st == "patched":
        print("== already patched, nothing to do")
        if dst != src:
            shutil.copyfile(src, dst)
        return 0
    if st != "stock":
        print("!! unexpected bytes at 0x%x: %s" % (OFF, bytes(blob[OFF:OFF + 4]).hex()))
        print("   refusing to touch a binary we do not recognise")
        return 1

    if check:
        print("!! stock binary — AF is disabled (see BUGFIXES.md #24)")
        return 1

    blob[OFF:OFF + 4] = PATCH
    out = bytes(blob)
    if dst == src:
        open(src, "wb").write(out)
    else:
        open(dst, "wb").write(out)
    md5 = hashlib.md5(out).hexdigest()
    print("== patched -> %s  md5 %s" % (dst, md5))
    if md5 != PATCH_MD5:
        print("!! md5 %s != expected %s" % (md5, PATCH_MD5))
        print("   (expected only for the vendor blob shipped in this tree)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
