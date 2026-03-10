#!/usr/bin/env python3
"""
BCM2835 Unicam CSI-2 register analyser
Offsets from drivers/media/platform/bcm2835/vc4-regs-unicam.h
Run as root while streaming: sudo python3 read_unicam.py
"""
import mmap, struct, sys, os

BASE  = 0xfe801000   # RPi4 unicam CSI0 (CSI1 = 0xfe800000)
MSIZE = 0x500

# ---- register offsets (vc4-regs-unicam.h) ----------------------------------
R = {
    "CTRL":  0x000,  "STA":   0x004,  "ANA":   0x008,  "PRI":   0x00c,
    "CLK":   0x010,  "CLT":   0x014,  "DAT0":  0x018,  "DAT1":  0x01c,
    "DAT2":  0x020,  "DAT3":  0x024,  "DLT":   0x028,  "CMP0":  0x02c,
    "CMP1":  0x030,  "CAP0":  0x034,  "CAP1":  0x038,
    "ICTL":  0x100,  "ISTA":  0x104,  "IDI0":  0x108,  "IPIPE": 0x10c,
    "IBSA0": 0x110,  "IBEA0": 0x114,  "IBLS":  0x118,  "IBWP":  0x11c,
    "IHWIN": 0x120,  "IHSTA": 0x124,  "IVWIN": 0x128,  "IVSTA": 0x12c,
    "ICC":   0x130,  "ICS":   0x134,  "IDC":   0x138,  "IDPO":  0x13c,
    "IDCA":  0x140,  "IDCD":  0x144,  "IDS":   0x148,
    "DCS":   0x200,  "DBSA0": 0x204,  "DBEA0": 0x208,  "DBWP":  0x20c,
    "DBCTL": 0x300,  "IBSA1": 0x304,  "IBEA1": 0x308,  "IDI1":  0x30c,
    "DBSA1": 0x310,  "DBEA1": 0x314,
    "MISC":  0x400,
}

# CSI-2 Data Types
DT_NAMES = {
    0x00:"Frame Start",   0x01:"Frame End",
    0x02:"Line Start",    0x03:"Line End",
    0x10:"Generic Short", 0x18:"Null",       0x19:"Blanking",
    0x28:"RAW6",  0x29:"RAW7",  0x2a:"RAW8",  0x2b:"RAW10",
    0x2c:"RAW12", 0x2d:"RAW14", 0x2e:"RAW16", 0x2f:"RAW20",
    0x1e:"YUV422-8bit",   0x24:"RGB888",
}

# Bytes per pixel (numerator, denominator) — packed formats are fractional
# stride = width * num / den
DT_BPP = {
    0x28: (3, 4),   # RAW6   — 6 bits/px → 3 bytes per 4 px
    0x29: (7, 8),   # RAW7   — 7 bits/px → 7 bytes per 8 px
    0x2a: (1, 1),   # RAW8
    0x2b: (5, 4),   # RAW10  — 10 bits/px → 5 bytes per 4 px
    0x2c: (3, 2),   # RAW12  — 12 bits/px → 3 bytes per 2 px
    0x2d: (7, 4),   # RAW14  — 14 bits/px → 7 bytes per 4 px
    0x2e: (2, 1),   # RAW16
    0x2f: (5, 2),   # RAW20  — 20 bits/px → 5 bytes per 2 px
    0x1e: (2, 1),   # YUV422-8bit
    0x24: (3, 1),   # RGB888
}

PUM = ["NONE","6-bit","7-bit","8-bit","10-bit","12-bit","14-bit","16-bit"]
PPM = ["NONE","8-bit","10-bit","12-bit","14-bit","16-bit","?","?"]

# ---------------------------------------------------------------------------

def rd_all():
    try:
        fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    except PermissionError:
        sys.exit("Run as root: sudo python3 read_unicam.py")
    mm = mmap.mmap(fd, MSIZE, mmap.MAP_SHARED, mmap.PROT_READ, offset=BASE)
    v = {}
    for name, off in R.items():
        mm.seek(off)
        v[name] = struct.unpack("<I", mm.read(4))[0]
    mm.close(); os.close(fd)
    return v

def field(val, mask):
    lo = (mask & -mask).bit_length() - 1
    return (val & mask) >> lo

def bit(v, n): return (v >> n) & 1

def sep(title=""):
    if title:
        print(f"\n{'─'*20} {title} {'─'*(37-len(title))}")
    else:
        print("─" * 60)

# ---------------------------------------------------------------------------
v = rd_all()
sep()
print(f"  BCM2835 Unicam  base=0x{BASE:08x}  (RPi4 CSI0)")
sep()

ORDER = [
    "CTRL","STA","ANA","PRI","CLK","CLT","DAT0","DAT1","DAT2","DAT3","DLT",
    "CMP0","CMP1","CAP0","CAP1",
    "ICTL","ISTA","IDI0","IPIPE","IBSA0","IBEA0","IBLS","IBWP",
    "IHWIN","IHSTA","IVWIN","IVSTA","ICC","ICS","IDC","IDPO","IDCA","IDS",
    "DCS","DBSA0","DBEA0","DBWP","DBCTL","IBSA1","IBEA1","IDI1","DBSA1","DBEA1",
    "MISC",
]
for name in ORDER:
    print(f"  [{R[name]:03x}] {name:<6s} = 0x{v[name]:08x}")

# ===========================================================================
sep("CTRL  (Control)")
ctrl = v["CTRL"]
cpm = bit(ctrl, 3)
print(f"  CPE (capture enable)    : {bit(ctrl,0)}")
print(f"  MEM (write to memory)   : {bit(ctrl,1)}")
print(f"  CPR (periph reset)      : {bit(ctrl,2)}")
print(f"  CPM (0=CSI2, 1=CCP2)    : {cpm}  {'CSI2' if cpm==0 else 'CCP2'}")
print(f"  SOE (stop on error)     : {bit(ctrl,4)}")
print(f"  PFT (pkt framer timeout): {field(ctrl, 0xf00)}")
print(f"  OET (output eng timeout): {field(ctrl, 0x1ff000)}")

# ===========================================================================
sep("STA   (Status / sticky errors)")
sta = v["STA"]
# Sticky flags: cleared by the ISR at each frame end. Reading during streaming
# shows errors accumulated since the last ISR clear (≤ 1 frame window).
STA_F = [
    (0,  "SYN",     "sync detected"),
    (1,  "CS",      "CSI bus active"),
    (2,  "SBE",     "SYNC BYTE ERROR — LP→HS preamble corrupt"),
    (3,  "PBE",     "PACKET BYTE ERROR — ECC/parity failure in header"),
    (4,  "HOE",     "HEADER OVERFLOW ERROR"),
    (5,  "PLE",     "PAYLOAD LENGTH ERROR — received bytes ≠ Word Count in header"),
    (6,  "SSC",     "start-of-stream captured"),
    (7,  "CRCE",    "CRC ERROR — payload data integrity failure"),
    (8,  "OES",     "overflow error sticky"),
    (9,  "IFO",     "IMAGE FIFO OVERFLOW"),
    (10, "OFO",     "OUTPUT FIFO OVERFLOW"),
    (11, "BFO",     "BUFFER FIFO OVERFLOW"),
    (12, "DL",      "DATA LOSS"),
    (13, "PS",      "packet start"),
    (14, "IS",      "image start"),
    (15, "PI0",     "packet ID 0 match"),
    (16, "PI1",     "packet ID 1 match"),
    (17, "FSI_S",   "frame start irq (sticky)"),
    (18, "FEI_S",   "frame end irq (sticky)"),
    (19, "LCI_S",   "line capture irq (sticky)"),
    (20, "BUF0_RDY","buffer 0 ready"),
    (21, "BUF0_NO", "buffer 0 not ready"),
    (22, "BUF1_RDY","buffer 1 ready"),
    (23, "BUF1_NO", "buffer 1 not ready"),
    (24, "DI",      "data irq"),
]
ERRORS = {2, 3, 4, 5, 7, 9, 10, 11, 12}
any_set = False
for bn, short, desc in STA_F:
    if bit(sta, bn):
        any_set = True
        tag = " *** ERROR ***" if bn in ERRORS else ""
        print(f"  [{'!' if bn in ERRORS else ' '}] bit{bn:2d}  {short:<9s}  {desc}{tag}")
if not any_set:
    print("  (no status bits set)")

# ===========================================================================
sep("ANA   (PHY analog)")
ana = v["ANA"]
print(f"  APD (analog power down) : {bit(ana,0)}")
print(f"  BPD (bandgap power dn)  : {bit(ana,1)}")
print(f"  AR  (analog reset)      : {bit(ana,2)}  (1=held in reset)")
print(f"  DDL (disable data lanes): {bit(ana,3)}")
print(f"  CTATADJ                 : {field(ana, 0xf0)}")
print(f"  PTATADJ                 : {field(ana, 0xf00)}")

# ===========================================================================
sep("CLK   (Clock lane)")
clk = v["CLK"]
clt = v["CLT"]
print(f"  CLE  (enable)           : {bit(clk,0)}")
print(f"  CLPD (power down)       : {bit(clk,1)}")
print(f"  CLLPE(LP rcv enable)    : {bit(clk,2)}")
print(f"  CLHSE(HS rcv enable)    : {bit(clk,3)}")
print(f"  CLTRE(HS term enable)   : {bit(clk,4)}")
print(f"  CLAC (settle count)     : {field(clk, 0x1e0)}")
print(f"  CLSTE(clock stopped)    : {bit(clk,29)}")
print(f"  CLT1 (tclk_term_en)     : {clt & 0xff}  (driver default: 2)")
print(f"  CLT2 (tclk_settle)      : {(clt>>8) & 0xff}  (driver default: 6)")

# ===========================================================================
sep("DAT0 / DAT1  (Data lanes)")
dlt = v["DLT"]
for name in ["DAT0", "DAT1"]:
    d = v[name]
    state = "active" if bit(d, 0) else "DISABLED"
    print(f"  {name}: DLE={bit(d,0)} DLPD={bit(d,1)} DLLPE={bit(d,2)} "
          f"DLHSE={bit(d,3)} DLTRE={bit(d,4)} DLSM={bit(d,5)}  "
          f"DLSTE(stopped)={bit(d,29)} DLFO(fifo_ovf)={bit(d,28)}  [{state}]")
print(f"  DLT1 (td_term_en)       : {dlt & 0xff}  (driver default: 2)")
print(f"  DLT2 (ths_settle)       : {(dlt>>8) & 0xff}  (driver default: 6)")
print(f"  DLT3 (trx_enable)       : {(dlt>>16) & 0xff}  (driver default: 0)")

# ===========================================================================
sep("IDI0  (programmed CSI-2 DT + VC per slot)")
idi = v["IDI0"]
active_dt = None
for i in range(4):
    raw  = (idi >> (8 * i)) & 0xff
    vc_i = (raw >> 6) & 0x3
    dt_i = raw & 0x3f
    label = DT_NAMES.get(dt_i, f"unknown 0x{dt_i:02x}")
    print(f"  ID{i}: 0x{raw:02x}  VC={vc_i}  DT=0x{dt_i:02x} ({label})")
    if i == 0 and dt_i != 0:
        active_dt = dt_i

# ===========================================================================
sep("IPIPE (unpack / pack config)")
ipipe = v["IPIPE"]
pum_v = field(ipipe, 0x7)
ppm_v = field(ipipe, 0x380)
print(f"  PUM (unpack mode)       : {pum_v} = {PUM[pum_v]}")
print(f"  PPM (pack mode)         : {ppm_v} = {PPM[ppm_v]}")
print(f"  DDM (demosaic mode)     : {field(ipipe, 0xc00)}")
# Note: for RAW16/RGB888 (already byte-aligned), PUM=NONE is valid.
# Unicam uses PUM/PPM mainly for sub-byte-packed formats (RAW10/12/14).

# ===========================================================================
sep("Image geometry")
ihwin = v["IHWIN"]; ihsta = v["IHSTA"]
ivwin = v["IVWIN"]; ivsta = v["IVSTA"]
ibls  = v["IBLS"]
ibsa  = v["IBSA0"]; ibea  = v["IBEA0"]; ibwp = v["IBWP"]

h_start = ihwin & 0xffff;  h_end  = (ihwin >> 16) & 0xffff
v_start = ivwin & 0xffff;  v_end  = (ivwin >> 16) & 0xffff
h_last  = ihsta & 0xffff
v_last  = ivsta & 0xffff

print(f"  IHWIN  (H window)       : [{h_start}..{h_end}]  (0,0 = no windowing, capture all)")
print(f"  IVWIN  (V window)       : [{v_start}..{v_end}]")

# IHSTA = byte count of the last CSI-2 long packet processed by the hardware.
# This reflects the Word Count field of that packet, NOT the full line stride.
# For cameras that send one complete line per CSI-2 packet:  IHSTA ≈ IBLS.
# For cameras that split a line into multiple CSI-2 packets: IHSTA < IBLS  (normal).
print(f"  IHSTA  (last pkt WC)    : {h_last} bytes")
print(f"  IVSTA  (V counter)      : {v_last}")
print(f"  IBLS   (DMA line stride): {ibls} bytes/line", end="")
if ibls > 0 and active_dt is not None and active_dt in DT_BPP:
    num, den = DT_BPP[active_dt]
    if ibls * den % num == 0:
        px = ibls * den // num
        dt_label = DT_NAMES.get(active_dt, f"0x{active_dt:02x}")
        print(f"  → {px} px/line  ({num}/{den} B/px, {dt_label})", end="")
print()

# Buffer / write pointer
buf_size = ibea - ibsa if ibea > ibsa else 0
print(f"  IBSA0  (buf start)      : 0x{ibsa:08x}")
print(f"  IBEA0  (buf end)        : 0x{ibea:08x}  (size = {buf_size} bytes", end="")
if buf_size > 0 and ibls > 0:
    print(f" = {buf_size // ibls} lines)", end="")
print(")")
print(f"  IBWP   (write pointer)  : 0x{ibwp:08x}", end="")
if ibsa and ibls and buf_size:
    # Handle wrap-around: IBWP may be < IBSA0 if it wrapped past IBEA0
    if ibwp >= ibsa:
        offset = ibwp - ibsa
    else:
        offset = buf_size - (ibsa - ibwp)   # wrapped
        print(f"  [wrapped]", end="")
    lines_done = offset // ibls
    print(f"  → offset={offset} B, ~{lines_done} lines from start", end="")
print()

# ===========================================================================
sep("CMP0/CMP1  (packet compare / frame-end detect)")
for name in ["CMP0", "CMP1"]:
    c = v[name]
    pce = bit(c, 31); gi = bit(c, 9); cph = bit(c, 8)
    vc  = field(c, 0xc0); dt = c & 0x3f
    print(f"  {name}: PCE={pce} GI={gi} CPH={cph} VC={vc} "
          f"DT=0x{dt:02x} ({DT_NAMES.get(dt, '?')})")

# ===========================================================================
sep("MISC")
misc = v["MISC"]
print(f"  FL0 (bit6): {bit(misc,6)}   FL1 (bit9): {bit(misc,9)}")

# ===========================================================================
sep("ERROR SUMMARY")
errs = []
if bit(sta,  2): errs.append("SBE  — sync byte error: LP→HS preamble issue (D-PHY level)")
if bit(sta,  3): errs.append("PBE  — packet byte error: header ECC/parity failure")
if bit(sta,  4): errs.append("HOE  — header overflow")
if bit(sta,  5): errs.append("PLE  — payload length error: received bytes ≠ Word Count")
if bit(sta,  7): errs.append("CRCE — CRC error: payload data corruption")
if bit(sta,  9): errs.append("IFO  — image FIFO overflow")
if bit(sta, 10): errs.append("OFO  — output FIFO overflow")
if bit(sta, 11): errs.append("BFO  — buffer FIFO overflow")
if bit(sta, 12): errs.append("DL   — data loss")
if bit(v["DAT0"], 28): errs.append("DAT0.DLFO — lane 0 FIFO overflow")
if bit(v["DAT1"], 28): errs.append("DAT1.DLFO — lane 1 FIFO overflow")
if errs:
    for e in errs:
        print(f"  [ERR] {e}")
else:
    print("  No error bits set")

# ===========================================================================
sep("DIAGNOSIS")
ple  = bit(sta, 5)
crce = bit(sta, 7)
fifo = bit(sta, 9) or bit(sta, 10) or bit(sta, 11)
dl   = bit(sta, 12)

if not any([ple, crce, fifo, dl,
            bit(sta,2), bit(sta,3), bit(sta,4),
            bit(v["DAT0"],28), bit(v["DAT1"],28)]):
    print("  OK — no error flags set.")
    print()
    # Informational: CSI-2 packet vs line relationship
    if ibls > 0 and h_last > 0:
        if h_last == ibls:
            print(f"  IHSTA = IBLS = {ibls} B → camera sends one CSI-2 packet per line")
        elif h_last < ibls:
            if ibls % h_last == 0:
                print(f"  IHSTA ({h_last} B) < IBLS ({ibls} B), ratio = {ibls // h_last}"
                      f" → camera sends {ibls // h_last} packets per line  (normal)")
            else:
                print(f"  IHSTA ({h_last} B) < IBLS ({ibls} B)"
                      f" → camera sends multiple packets per line  (normal)")
        else:
            print(f"  IHSTA ({h_last} B) > IBLS ({ibls} B)"
                  f" → unexpected: last packet larger than DMA stride")
else:
    # Pixel-shift mechanism: PLE means the receiver lost line-boundary sync.
    # Bytes that belong to line N+1 are written into the tail of line N's
    # DMA slot, causing a cumulative shift in the output image.
    if ple:
        print("  [!] PLE is set → line framing error.")
        print("      The CSI-2 Word Count in a packet header did not match the")
        print("      bytes actually received. This desynchronises the DMA line")
        print("      pointer, causing pixel shifts in the captured image.")
        if h_last > 0 and ibls > 0:
            delta = h_last - ibls
            if delta != 0:
                print(f"      IHSTA={h_last} B vs IBLS={ibls} B → last packet delta = {delta:+d} B")
    if crce:
        print("  [!] CRCE is set → CRC-16 mismatch on at least one line packet.")
        print("      Check: CRC polynomial (CCITT 0x1021, init 0xFFFF),")
        print("             byte range (payload only, not header),")
        print("             byte order (little-endian, LSB first).")
    if ple and crce:
        print("  [!] PLE + CRCE together: header bit errors (wrong ECC) may corrupt")
        print("      the Word Count field, causing both errors simultaneously.")
    if fifo:
        print("  [!] FIFO overflow → data rate exceeds DMA throughput.")
    if dl:
        print("  [!] DL (data loss) → packets were dropped.")

sep()
