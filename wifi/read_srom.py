#!/usr/bin/env python3
"""Read the external SPROM of a BCM43602 word by word through ChipCommon.

READ ONLY: the only command ever issued is SROM_CONTROL = START | OP_READ (opcode 0),
exactly like Broadcom's srom_cc_cmd(SRC_OP_READ). WREN/WRITE/WRDIS opcodes are never
used (asserted below). Run as root with no driver bound. The host BAR0 window in PCI
config space is restored on exit.
"""
import ctypes, mmap, os, struct, sys, time

DEV, OUT = sys.argv[1], sys.argv[2]  # PCI device, output file (or directory)
WORDS = int(sys.argv[3], 0) if len(sys.argv) > 3 else 1024
SYS = f"/sys/bus/pci/devices/{DEV}"
CC_BASE, BAR0_WINDOW = 0x18000000, 0x80
SROMCTL, SROMADDR, SROMDATA = 0x190, 0x194, 0x198
START, OPCODE_MASK, OP_READ = 0x80000000, 0x60000000, 0x00000000
READ_CMD = START | OP_READ
assert READ_CMD & OPCODE_MASK == OP_READ  # never WRITE/WREN/WRDIS

if os.path.exists(f"{SYS}/driver"):
    sys.exit("a driver is still bound; unbind it first")
cfg = os.open(f"{SYS}/config", os.O_RDWR)
cfg_r32 = lambda off: struct.unpack("<I", os.pread(cfg, 4, off))[0]
cfg_w32 = lambda off, v: os.pwrite(cfg, struct.pack("<I", v), off)
if cfg_r32(0) != 0x43ba14e4:
    sys.exit("not a BCM43602 (14e4:43ba)")

fd = os.open(f"{SYS}/resource0", os.O_RDWR | os.O_SYNC)
mm = mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
ptr = ctypes.addressof((ctypes.c_char * 0x1000).from_buffer(mm))
def r32(off): return ctypes.c_uint32.from_address(ptr + off).value
def w32(off, v):
    assert off in (SROMADDR, SROMCTL)
    if off == SROMCTL:
        assert v == READ_CMD
    ctypes.c_uint32.from_address(ptr + off).value = v

saved = cfg_r32(BAR0_WINDOW)
words = []
try:
    cfg_w32(BAR0_WINDOW, CC_BASE)
    assert cfg_r32(BAR0_WINDOW) == CC_BASE
    assert r32(0) & 0xffff == 0xaa52, "chipcommon id mismatch"
    ctl0 = r32(SROMCTL)
    print(f"sromcontrol before 0x{ctl0:08x}")
    if not ctl0 & 1:
        sys.exit("SROM not present")
    for w in range(WORDS):
        w32(SROMADDR, w * 2)
        w32(SROMCTL, READ_CMD)
        for _ in range(1000):
            ctl = r32(SROMCTL)
            if not ctl & START:
                break
            time.sleep(0.00001)
        else:
            sys.exit(f"SROM busy timeout at word {w}")
        words.append(r32(SROMDATA) & 0xffff)
    print(f"sromcontrol after  0x{r32(SROMCTL):08x}")
finally:
    cfg_w32(BAR0_WINDOW, saved)
    print(f"BAR0 window restored to 0x{cfg_r32(BAR0_WINDOW):08x}")

data = struct.pack(f"<{len(words)}H", *words)
name = OUT
if os.path.isdir(OUT):
    name = f"{OUT}/bcm43602-srom-{time.strftime('%Y%m%d-%H%M%S')}.bin"
old = os.umask(0o077)  # the image holds the MAC address and calibration data
open(name, "wb").write(data)
os.umask(old)
print(f"saved {name}: {len(words)} words, 0xffff {words.count(0xffff)}, 0x0000 {words.count(0)}")
