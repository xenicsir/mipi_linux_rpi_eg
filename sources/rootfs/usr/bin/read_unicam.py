#!/usr/bin/env python3
"""
Read BCM2711 unicam CSI-2 receiver registers (RPi4, base 0xfe801000).
Run as root while streaming.
"""
import mmap, struct, sys

BASE = 0xfe801000
SIZE = 0x200

DT_NAMES = {
    0x00: "FS (Frame Start)",   0x01: "FE (Frame End)",
    0x02: "LS (Line Start)",    0x03: "LE (Line End)",
    0x28: "RAW6",  0x29: "RAW7",  0x2a: "RAW8",  0x2b: "RAW10",
    0x2c: "RAW12", 0x2d: "RAW14", 0x2e: "RAW16", 0x2f: "RAW20",
    0x1e: "YUV422-8", 0x24: "RGB888",
}

# UNICAM_STA (0x004) bits
STA_BITS = {
    0: "FS", 1: "FE", 2: "LS", 3: "LE",
    4: "BUF", 5: "DI", 6: "UBE", 7: "ARE",
}

try:
    with open("/dev/mem", "rb") as f:
        mm = mmap.mmap(f.fileno(), SIZE, mmap.MAP_SHARED, mmap.PROT_READ,
                       offset=BASE)
except PermissionError:
    sys.exit("Run as root: sudo python3 read_unicam.py")

def rd(off):
    mm.seek(off)
    return struct.unpack("<I", mm.read(4))[0]

print(f"BCM2711 unicam registers @ 0x{BASE:08x}")
print("=" * 60)

# --- PHY / lane registers (0x000-0x02c) ---
print("\n[PHY / Lane]")
for name, off, desc in [
    ("UNICAM_CTRL",  0x000, "Control"),
    ("UNICAM_STA",   0x004, "Status"),
    ("UNICAM_ANA",   0x008, "PHY analog"),
    ("UNICAM_PRI",   0x00c, "Priority"),
    ("UNICAM_CLK",   0x010, "Clock lane"),
    ("UNICAM_DAT0",  0x014, "Data lane 0"),
    ("UNICAM_DAT1",  0x018, "Data lane 1"),
    ("UNICAM_DAT2",  0x01c, "Data lane 2"),
    ("UNICAM_CMP0",  0x020, "Compare 0"),
    ("UNICAM_CMP1",  0x024, "Compare 1"),
    ("UNICAM_CAP0",  0x028, "Capture 0"),
    ("UNICAM_CAP1",  0x02c, "Capture 1"),
]:
    v = rd(off)
    print(f"  {name:16s} [+0x{off:03x}] = 0x{v:08x}  ({desc})")

sta = rd(0x004)
flags = [s for b, s in STA_BITS.items() if sta & (1 << b)]
print(f"  STA decoded: {', '.join(flags) if flags else 'none'}")

# --- Window / timing registers (0x034-0x04c) ---
print("\n[Window / timing]")
for name, off, desc in [
    ("UNICAM_IDI0",  0x034, "Image Data ID 0 (programmed DT/VC)"),
    ("UNICAM_IHWIN", 0x03c, "Image horiz window"),
    ("UNICAM_IVWIN", 0x044, "Image vert window"),
]:
    v = rd(off)
    print(f"  {name:16s} [+0x{off:03x}] = 0x{v:08x}  ({desc})")

# --- IMAGE section (0x0f0-0x12c) ---
print("\n[IMAGE section — active]")
for name, off, desc in [
    ("IMG_0x0f0",    0x0f0, "?"),
    ("IMG_0x0f4",    0x0f4, "?"),
    ("IMG_0x0f8",    0x0f8, "?"),
    ("IMG_0x0fc",    0x0fc, "?"),
    ("IMG_CTRL",     0x100, "Image control"),
    ("IMG_DT_VC",    0x108, "CSI2 DataType + VC (received)"),
    ("IMG_0x10c",    0x10c, "?"),
    ("IMG_IBSA0",    0x110, "DMA buf start addr 0"),
    ("IMG_IBEA0",    0x114, "DMA buf end addr 0"),
    ("IMG_0x118",    0x118, "Bytes per line (stride)"),
    ("IMG_IBSA1",    0x11c, "DMA buf start addr 1"),
    ("IMG_0x124",    0x124, "?"),
    ("IMG_HEIGHT",   0x12c, "Frame height (lines)"),
]:
    v = rd(off)
    print(f"  {name:16s} [+0x{off:03x}] = 0x{v:08x}  ({desc})")

# --- Decoded summary ---
print("\n[Decoded summary]")
dt_vc = rd(0x108)
dt   = dt_vc & 0x3f
vc   = (dt_vc >> 6) & 0x3
bpl  = rd(0x118)
h    = rd(0x12c)
print(f"  CSI-2 DataType : 0x{dt:02x} = {DT_NAMES.get(dt, 'unknown')}")
print(f"  Virtual Channel: {vc}")
print(f"  Bytes / line   : {bpl}  (= {bpl} B/line)")
print(f"  Frame height   : {h} lines")
