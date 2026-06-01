#!/usr/bin/env python3
"""
RP1 CFE (Camera Front End) CSI-2 register analyser — Raspberry Pi 5
Based on drivers/media/platform/raspberrypi/rp1_cfe/{csi2,dphy,cfe}.c

Hardware blocks (4 separate IOMEM resources from the rp1/csi@128000 DT node):
  CSI2 DMA  0x1f00128000  (size 0x200)  — csi2.c register set
  DPHY      0x1f0012c000  (size 0x100)  — DW dphy Host registers
  MIPICFG   0x1f00138000  (size 0x100)  — RP1 MIPI top-level config
  FE        0x1f0013c000  (size 0x200)  — PiSP Front-End header regs

Run as root while streaming: sudo python3 read_rp1_cfe.py
"""
import mmap, struct, sys, os, time

# ── Physical base addresses (ARM address space, RP1 via PCIe) ──────────────
BASE_CSI2    = 0x1f00128000
BASE_DPHY    = 0x1f0012c000
BASE_MIPICFG = 0x1f00138000
BASE_FE      = 0x1f0013c000

# ── CSI2 DMA register offsets (csi2.c) ─────────────────────────────────────
CSI2_STATUS              = 0x000
CSI2_QOS                 = 0x004
CSI2_DISCARDS_OVERFLOW   = 0x008
CSI2_DISCARDS_INACTIVE   = 0x00c
CSI2_DISCARDS_UNMATCHED  = 0x010
CSI2_DISCARDS_LEN_LIMIT  = 0x014
CSI2_LLEV_PANICS         = 0x018
CSI2_ULEV_PANICS         = 0x01c
CSI2_IRQ_MASK            = 0x020
CSI2_CTRL                = 0x024

def CSI2_CH_CTRL(x):      return x * 0x40 + 0x28
def CSI2_CH_ADDR0(x):     return x * 0x40 + 0x2c
def CSI2_CH_ADDR1(x):     return x * 0x40 + 0x3c
def CSI2_CH_STRIDE(x):    return x * 0x40 + 0x30
def CSI2_CH_LENGTH(x):    return x * 0x40 + 0x34
def CSI2_CH_DEBUG(x):     return x * 0x40 + 0x38
def CSI2_CH_FRAME_SIZE(x):return x * 0x40 + 0x40
def CSI2_CH_COMP_CTRL(x): return x * 0x40 + 0x44
def CSI2_CH_FE_FRAME_ID(x):return x * 0x40 + 0x48

# CSI2_STATUS bits
IRQ_FS        = lambda x: 1 << x
IRQ_FE        = lambda x: 1 << (4 + x)
IRQ_FE_ACK    = lambda x: 1 << (8 + x)
IRQ_LE        = lambda x: 1 << (12 + x)
IRQ_LE_ACK    = lambda x: 1 << (16 + x)
IRQ_OVERFLOW       = 1 << 20
IRQ_DISCARD_OVFLOW = 1 << 21
IRQ_DISCARD_LLEN   = 1 << 22
IRQ_DISCARD_UNMAT  = 1 << 23
IRQ_DISCARD_INACT  = 1 << 24

# CSI2_CH_CTRL bits
DMA_EN      = 1 << 0
FORCE       = 1 << 3
AUTO_ARM    = 1 << 4
IRQ_EN_FS   = 1 << 13
IRQ_EN_FE   = 1 << 14
IRQ_EN_FE_ACK = 1 << 15
IRQ_EN_LE   = 1 << 16
IRQ_EN_LE_ACK = 1 << 17
FLUSH_FE    = 1 << 28
PACK_LINE   = 1 << 29
PACK_BYTES  = 1 << 30
CH_MODE_MASK = 0x6          # bits 2:1
VC_MASK      = 0x60         # bits 6:5
DT_MASK      = 0x1f80       # bits 12:7
LC_MASK      = 0x0ffc0000   # bits 27:18

CH_MODES = {0: "NORMAL", 1: "REMAP", 2: "COMPRESSED", 3: "FE_STREAMING"}

# CSI2 data types
DT_NAMES = {
    0x00: "Frame Start",  0x01: "Frame End",
    0x02: "Line Start",   0x03: "Line End",
    0x28: "RAW6",  0x29: "RAW7",  0x2a: "RAW8",  0x2b: "RAW10",
    0x2c: "RAW12", 0x2d: "RAW14", 0x2e: "RAW16", 0x2f: "RAW20",
    0x1e: "YUV422-8",    0x24: "RGB888",
}

# ── DPHY register offsets (dphy.c) ─────────────────────────────────────────
DPHY_VERSION    = 0x000
DPHY_N_LANES    = 0x004
DPHY_RESETN     = 0x008
DPHY_SHUTDOWNZ  = 0x040
DPHY_RSTZ       = 0x044
DPHY_PHY_RX     = 0x048
DPHY_STOPSTATE  = 0x04c
DPHY_TST_CTRL0  = 0x050
DPHY_TST_CTRL1  = 0x054

# ── MIPICFG register offsets (cfe.c) ───────────────────────────────────────
MIPICFG_CFG  = 0x004
MIPICFG_INTR = 0x028
MIPICFG_INTE = 0x02c
MIPICFG_INTF = 0x030
MIPICFG_INTS = 0x034

MIPICFG_CFG_SEL_CSI  = 1 << 0
MIPICFG_INT_CSI_DMA  = 1 << 0
MIPICFG_INT_CSI_HOST = 1 << 2
MIPICFG_INT_PISP_FE  = 1 << 4

# ── FE register offsets (pisp_fe.h) ────────────────────────────────────────
FE_VERSION      = 0x000
FE_CONTROL      = 0x004
FE_STATUS       = 0x008
FE_FRAME_STATUS = 0x00c


# ── Helpers ─────────────────────────────────────────────────────────────────
def open_mem():
    try:
        return os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    except PermissionError:
        sys.exit("Run as root: sudo python3 read_rp1_cfe.py")

def map_block(fd, base, size=0x200):
    return mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)

def rd(mm, off):
    mm.seek(off)
    return struct.unpack("<I", mm.read(4))[0]

def bit(v, n):
    return (v >> n) & 1

def field(v, mask):
    lo = (mask & -mask).bit_length() - 1
    return (v & mask) >> lo

def sep(title=""):
    if title:
        print(f"\n{'─'*20} {title} {'─'*(37-len(title))}")
    else:
        print("─" * 60)

def flag(v, bit_n, label, ok_when=None):
    b = (v >> bit_n) & 1
    if ok_when is not None:
        mark = "✓" if b == ok_when else "!"
    else:
        mark = " "
    return f"  [{mark}] {label:<28s}: {b}"


# ── Frame-size measurement mode ─────────────────────────────────────────────
def measure_frame_size():
    """Poll CH_DEBUG(0) frame counter + CH_ADDR0 to detect frame boundaries.

    CH_DEBUG(0)[31:16] = frame count (incremented on each FE_ACK / frame end).
    When the count increments, a new frame arrived.  We record the wall-clock
    time between two increments to get the frame period and compute fps.
    CH_STRIDE and CH_LENGTH give stride and total buffer size per frame.
    """
    fd = open_mem()
    mm = map_block(fd, BASE_CSI2, 0x200)

    stride  = rd(mm, CSI2_CH_STRIDE(0)) << 4   # hardware stores stride>>4
    length  = rd(mm, CSI2_CH_LENGTH(0)) << 4    # hardware stores length>>4

    print(f"  CH0 stride = {stride} B/line,  length = {length} B/frame")
    if stride:
        print(f"  → {length // stride} lines/frame,  {length} B/frame")
    print()
    print("Polling CH_DEBUG frame counter at 1 ms  (Ctrl-C to stop)\n")

    prev_cnt = rd(mm, CSI2_CH_DEBUG(0)) >> 16
    frame_periods = []
    t_last = time.monotonic()
    n = 0

    try:
        while True:
            time.sleep(0.001)
            debug   = rd(mm, CSI2_CH_DEBUG(0))
            cnt     = debug >> 16
            # line    = debug & 0xffff

            if cnt != prev_cnt:
                now = time.monotonic()
                dt  = now - t_last
                t_last = now
                n += 1
                frame_periods.append(dt)
                fps = 1.0 / dt if dt else 0
                print(f"  frame {n:4d}: Δt={dt*1000:.1f} ms  ({fps:.2f} fps)  "
                      f"count={cnt}")
                prev_cnt = cnt

    except KeyboardInterrupt:
        pass

    mm.close()
    os.close(fd)

    if len(frame_periods) >= 2:
        avg_dt = sum(frame_periods) / len(frame_periods)
        print(f"\n  ── summary ({len(frame_periods)} frames) ──")
        print(f"  avg fps  = {1/avg_dt:.2f}")
        print(f"  min dt   = {min(frame_periods)*1000:.1f} ms")
        print(f"  max dt   = {max(frame_periods)*1000:.1f} ms")
        if length:
            print(f"  bytes/frame (configured) = {length}")
    else:
        print("  Not enough frames detected.  Is streaming active?")
        print("  Check: is DMA_EN set in CH_CTRL(0)?")
    sys.exit(0)


# ── Main register dump ───────────────────────────────────────────────────────
if "--frame-size" in sys.argv:
    measure_frame_size()

fd = open_mem()
mm_csi2    = map_block(fd, BASE_CSI2,    0x200)
mm_dphy    = map_block(fd, BASE_DPHY,    0x100)
mm_mipicfg = map_block(fd, BASE_MIPICFG, 0x100)
mm_fe      = map_block(fd, BASE_FE,      0x100)

def rc(off): return rd(mm_csi2,    off)
def rd_dphy(off): return rd(mm_dphy,    off)
def rm(off): return rd(mm_mipicfg, off)
def rf(off): return rd(mm_fe,      off)

sep()
print("  RP1 CFE  —  Raspberry Pi 5  (rp1/csi@128000)")
sep()
print(f"  CSI2    base = 0x{BASE_CSI2:011x}")
print(f"  DPHY    base = 0x{BASE_DPHY:011x}")
print(f"  MIPICFG base = 0x{BASE_MIPICFG:011x}")
print(f"  FE      base = 0x{BASE_FE:011x}")

# ── Raw dump ─────────────────────────────────────────────────────────────────
sep("CSI2 raw registers")
CSI2_REGS = [
    ("STATUS",             CSI2_STATUS),
    ("QOS",                CSI2_QOS),
    ("DISCARDS_OVERFLOW",  CSI2_DISCARDS_OVERFLOW),
    ("DISCARDS_INACTIVE",  CSI2_DISCARDS_INACTIVE),
    ("DISCARDS_UNMATCHED", CSI2_DISCARDS_UNMATCHED),
    ("DISCARDS_LEN_LIMIT", CSI2_DISCARDS_LEN_LIMIT),
    ("LLEV_PANICS",        CSI2_LLEV_PANICS),
    ("ULEV_PANICS",        CSI2_ULEV_PANICS),
    ("IRQ_MASK",           CSI2_IRQ_MASK),
    ("CTRL",               CSI2_CTRL),
]
for name, off in CSI2_REGS:
    print(f"  [0x{off:03x}] {name:<24s} = 0x{rc(off):08x}")
for ch in range(4):
    for fname, fn in [("CH_CTRL",      CSI2_CH_CTRL),
                      ("CH_ADDR0",     CSI2_CH_ADDR0),
                      ("CH_ADDR1",     CSI2_CH_ADDR1),
                      ("CH_STRIDE",    CSI2_CH_STRIDE),
                      ("CH_LENGTH",    CSI2_CH_LENGTH),
                      ("CH_DEBUG",     CSI2_CH_DEBUG),
                      ("CH_FRAME_SIZE",CSI2_CH_FRAME_SIZE),
                      ("CH_COMP_CTRL", CSI2_CH_COMP_CTRL),
                      ("CH_FE_FRAME_ID",CSI2_CH_FE_FRAME_ID)]:
        off = fn(ch)
        print(f"  [0x{off:03x}] {fname}({ch}){'':14s} = 0x{rc(off):08x}")

sep("DPHY raw registers")
for name, off in [("VERSION",    DPHY_VERSION),
                  ("N_LANES",    DPHY_N_LANES),
                  ("RESETN",     DPHY_RESETN),
                  ("PHY_SHUTDOWNZ", DPHY_SHUTDOWNZ),
                  ("PHY_RSTZ",   DPHY_RSTZ),
                  ("PHY_RX",     DPHY_PHY_RX),
                  ("PHY_STOPSTATE", DPHY_STOPSTATE),
                  ("TST_CTRL0",  DPHY_TST_CTRL0),
                  ("TST_CTRL1",  DPHY_TST_CTRL1)]:
    print(f"  [0x{off:03x}] {name:<24s} = 0x{rd_dphy(off):08x}")

sep("MIPICFG raw registers")
for name, off in [("CFG",  MIPICFG_CFG),
                  ("INTR", MIPICFG_INTR),
                  ("INTE", MIPICFG_INTE),
                  ("INTF", MIPICFG_INTF),
                  ("INTS", MIPICFG_INTS)]:
    print(f"  [0x{off:03x}] {name:<24s} = 0x{rm(off):08x}")

sep("FE raw registers")
for name, off in [("VERSION",      FE_VERSION),
                  ("CONTROL",      FE_CONTROL),
                  ("STATUS",       FE_STATUS),
                  ("FRAME_STATUS", FE_FRAME_STATUS)]:
    print(f"  [0x{off:03x}] {name:<24s} = 0x{rf(off):08x}")

# ═══════════════════════════════════════════════════════════════════════════
sep("CSI2 STATUS (sticky — cleared by ISR each frame)")
sta = rc(CSI2_STATUS)
sta_bits = []
for ch in range(4):
    if sta & IRQ_FS(ch):   sta_bits.append(f"FS[{ch}]")
    if sta & IRQ_FE(ch):   sta_bits.append(f"FE[{ch}]")
    if sta & IRQ_FE_ACK(ch): sta_bits.append(f"FE_ACK[{ch}]")
    if sta & IRQ_LE(ch):   sta_bits.append(f"LE[{ch}]")
    if sta & IRQ_LE_ACK(ch): sta_bits.append(f"LE_ACK[{ch}]")
if sta & IRQ_OVERFLOW:       sta_bits.append("OVERFLOW *** ERROR ***")
if sta & IRQ_DISCARD_OVFLOW: sta_bits.append("DISCARD_OVERFLOW *** ERROR ***")
if sta & IRQ_DISCARD_LLEN:   sta_bits.append("DISCARD_LEN_LIMIT *** ERROR ***")
if sta & IRQ_DISCARD_UNMAT:  sta_bits.append("DISCARD_UNMATCHED *** ERROR ***")
if sta & IRQ_DISCARD_INACT:  sta_bits.append("DISCARD_INACTIVE")
if sta_bits:
    for b in sta_bits:
        print(f"  [!] {b}")
else:
    print("  (no bits set — normal between frames)")

# ── Discard counters ─────────────────────────────────────────────────────────
sep("CSI2 discard counters")
for label, off in [("OVERFLOW",   CSI2_DISCARDS_OVERFLOW),
                   ("INACTIVE",   CSI2_DISCARDS_INACTIVE),
                   ("UNMATCHED",  CSI2_DISCARDS_UNMATCHED),
                   ("LEN_LIMIT",  CSI2_DISCARDS_LEN_LIMIT)]:
    v   = rc(off)
    cnt = v & 0xffffff
    dt  = (v >> 24) & 0x3f
    vc  = (v >> 30) & 0x3
    if cnt:
        dt_name = DT_NAMES.get(dt, f"0x{dt:02x}")
        print(f"  [!] {label:<12s}: count={cnt}  last DT={dt_name}  VC={vc}  *** ERROR ***")
    else:
        print(f"  [ok] {label:<12s}: 0")

# ── Per-channel decode ────────────────────────────────────────────────────────
for ch in range(4):
    ctrl   = rc(CSI2_CH_CTRL(ch))
    debug  = rc(CSI2_CH_DEBUG(ch))
    fsize  = rc(CSI2_CH_FRAME_SIZE(ch))
    stride = rc(CSI2_CH_STRIDE(ch)) << 4
    length = rc(CSI2_CH_LENGTH(ch)) << 4
    addr0  = rc(CSI2_CH_ADDR0(ch))
    addr1  = rc(CSI2_CH_ADDR1(ch))

    dma_en  = bool(ctrl & DMA_EN)
    mode    = CH_MODES.get(field(ctrl, CH_MODE_MASK), "?")
    vc      = field(ctrl, VC_MASK)
    dt      = field(ctrl, DT_MASK)
    lc      = field(ctrl, LC_MASK)
    frames  = debug >> 16
    lines   = debug & 0xffff
    w = fsize & 0xffff
    h = (fsize >> 16) & 0xffff

    # skip inactive channels (not configured)
    if not dma_en and not stride and not length:
        continue

    sep(f"Channel {ch}  ({'ACTIVE' if dma_en else 'STOPPED'})")
    print(f"  DMA_EN      : {'1  ← capturing' if dma_en else '0  ← NOT capturing'}")
    print(f"  Mode        : {mode}")
    print(f"  VC / DT     : VC={vc}  DT=0x{dt:02x} ({DT_NAMES.get(dt, 'unknown')})")
    print(f"  Frame size  : {w} × {h} px")
    print(f"  Stride      : {stride} bytes/line")
    print(f"  Buffer size : {length} bytes", end="")
    if stride:
        print(f"  = {length // stride} lines", end="")
    print()
    print(f"  DMA addr    : 0x{((addr1 << 32) | addr0) << 4:012x}")
    print(f"  Frames rcvd : {frames}")
    print(f"  Current line: {lines}" + (f" / {lc}" if lc else ""))
    irqs = []
    if ctrl & IRQ_EN_FS:    irqs.append("FS")
    if ctrl & IRQ_EN_FE:    irqs.append("FE")
    if ctrl & IRQ_EN_FE_ACK: irqs.append("FE_ACK")
    if ctrl & IRQ_EN_LE:    irqs.append("LE")
    if ctrl & IRQ_EN_LE_ACK: irqs.append("LE_ACK")
    print(f"  IRQs enabled: {', '.join(irqs) if irqs else 'none'}")
    print(f"  PACK_LINE   : {int(bool(ctrl & PACK_LINE))}  "
          f"PACK_BYTES: {int(bool(ctrl & PACK_BYTES))}  "
          f"AUTO_ARM: {int(bool(ctrl & AUTO_ARM))}  "
          f"FORCE: {int(bool(ctrl & FORCE))}")

# ── DPHY ─────────────────────────────────────────────────────────────────────
sep("DPHY")
ver  = rd_dphy(DPHY_VERSION)
vmaj = (ver >> 24) - ord('0')
vmin = ((ver >> 16) - ord('0')) * 10 + ((ver >> 8) - ord('0'))
print(f"  VERSION     : 0x{ver:08x}  → DW dphy v{vmaj}.{vmin:02d}")
nlanes = rd_dphy(DPHY_N_LANES)
print(f"  N_LANES     : {nlanes}  → {nlanes + 1} active data lane(s)")
print(f"  RESETN      : {rd_dphy(DPHY_RESETN) & 1}  (1 = running)")
print(f"  PHY_SHUTDOWNZ: {rd_dphy(DPHY_SHUTDOWNZ) & 1}  (1 = powered on)")
print(f"  PHY_RSTZ    : {rd_dphy(DPHY_RSTZ) & 1}  (1 = out of reset)")

phy_rx   = rd_dphy(DPHY_PHY_RX)
stopstate = rd_dphy(DPHY_STOPSTATE)
print(f"  PHY_RX      : 0x{phy_rx:08x}  "
      f"rxclkactivehs={bit(phy_rx,0)}  rxulpsclknot={bit(phy_rx,1)}")
stop0 = bit(stopstate, 0)
stop1 = bit(stopstate, 1)
stopc = bit(stopstate, 16)
print(f"  PHY_STOPSTATE: 0x{stopstate:08x}")
print(f"    lane0 stop={stop0}  lane1 stop={stop1}  clock stop={stopc}")
if stop0 and stop1:
    print(f"    → both data lanes in LP-11 (stop state) : no HS burst in progress")
elif not stop0 and not stop1:
    print(f"    → data lanes active (HS or LP transition)")

# ── MIPICFG ──────────────────────────────────────────────────────────────────
sep("MIPICFG")
cfg  = rm(MIPICFG_CFG)
intr = rm(MIPICFG_INTR)
ints = rm(MIPICFG_INTS)
print(f"  CFG         : 0x{cfg:08x}  "
      f"SEL_CSI={bit(cfg,0)} ({'CSI mode' if bit(cfg,0) else 'not CSI'})")
print(f"  INTR (raw)  : 0x{intr:08x}  "
      f"CSI_DMA={bit(intr,0)}  CSI_HOST={bit(intr,2)}  PISP_FE={bit(intr,4)}")
print(f"  INTS (sticky): 0x{ints:08x}")

# ── FE ───────────────────────────────────────────────────────────────────────
sep("PiSP FE (Front End)")
fe_ver = rf(FE_VERSION)
print(f"  VERSION     : 0x{fe_ver:08x}  → HW v{(fe_ver>>16)&0xff}.{(fe_ver>>8)&0xff}")
fe_ctrl = rf(FE_CONTROL)
fe_sta  = rf(FE_STATUS)
fe_fsta = rf(FE_FRAME_STATUS)
print(f"  CONTROL     : 0x{fe_ctrl:08x}")
print(f"  STATUS      : 0x{fe_sta:08x}")
print(f"  FRAME_STATUS: 0x{fe_fsta:08x}")

# ── Errors & Diagnosis ───────────────────────────────────────────────────────
sep("ERROR SUMMARY")
errors = []
panics_lo = rc(CSI2_LLEV_PANICS)
panics_hi = rc(CSI2_ULEV_PANICS)
if panics_lo: errors.append(f"LLEV_PANICS = {panics_lo}  (low-level DMA back-pressure)")
if panics_hi: errors.append(f"ULEV_PANICS = {panics_hi}  (upper-level DMA back-pressure)")
for label, off in [("DISCARDS_OVERFLOW",  CSI2_DISCARDS_OVERFLOW),
                   ("DISCARDS_INACTIVE",  CSI2_DISCARDS_INACTIVE),
                   ("DISCARDS_UNMATCHED", CSI2_DISCARDS_UNMATCHED),
                   ("DISCARDS_LEN_LIMIT", CSI2_DISCARDS_LEN_LIMIT)]:
    cnt = rc(off) & 0xffffff
    if cnt:
        errors.append(f"{label} = {cnt}")
if errors:
    for e in errors:
        print(f"  [ERR] {e}")
else:
    print("  No error counters set")

sep("DIAGNOSIS")
ch0_ctrl   = rc(CSI2_CH_CTRL(0))
ch0_frames = rc(CSI2_CH_DEBUG(0)) >> 16

if not (ch0_ctrl & DMA_EN):
    print("  [!] CH0 DMA_EN = 0 → channel not active.")
    print("      The CSI-2 DMA is not running.  Possible causes:")
    print("      • No streaming started  (v4l2-ctl --stream-mmap not active)")
    print("      • Driver failed to queue a buffer (check dmesg)")
    print("      • stream-count reached 0 and streaming stopped")
else:
    if ch0_frames == 0:
        print("  [!] DMA active but 0 frames captured.")
        stop = rc(CSI2_DISCARDS_INACTIVE) & 0xffffff
        unmat = rc(CSI2_DISCARDS_UNMATCHED) & 0xffffff
        if stop or unmat:
            print(f"      DISCARDS: inactive={stop} unmatched={unmat}")
            print("      Camera may not be sending data on the expected VC/DT.")
        else:
            print("      No discards.  PHY may not be receiving any MIPI packets.")
            print("      Check: camera powered? I2C init succeeded? MIPI lanes connected?")
    else:
        print(f"  [ok] CH0 is capturing.  Frames received: {ch0_frames}")
        if not errors:
            print("  [ok] No errors.")

stop_d = rd_dphy(DPHY_STOPSTATE)
if stop_d & 0x3:
    if not (ch0_ctrl & DMA_EN):
        print("  [i] PHY data lanes in stop (LP-11): consistent with DMA inactive.")
    else:
        print("  [!] PHY data lanes in stop (LP-11) while DMA is active.")
        print("      Camera is not sending HS data.  Check camera streaming state.")

sep()
