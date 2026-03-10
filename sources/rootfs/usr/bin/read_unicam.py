#!/usr/bin/env python3
"""
BCM2835 Unicam CSI-2 register analyser — correct offsets from vc4-regs-unicam.h
Run as root while streaming: sudo python3 unicam_analyze.py
"""
import mmap, struct, sys, os

BASE   = 0xfe801000   # RPi4 unicam0 (CSI0 = fe801000, CSI1 = fe800000)
MSIZE  = 0x500

# ---- register offsets (from vc4-regs-unicam.h) ----------------------------
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

DT = {
    0x00:"Frame Start", 0x01:"Frame End", 0x02:"Line Start", 0x03:"Line End",
    0x10:"Generic Short", 0x18:"Null", 0x19:"Blanking",
    0x28:"RAW6",  0x29:"RAW7",  0x2a:"RAW8",  0x2b:"RAW10",
    0x2c:"RAW12", 0x2d:"RAW14", 0x2e:"RAW16", 0x2f:"RAW20",
    0x1e:"YUV422-8bit", 0x24:"RGB888",
}
PUM = ["NONE","6-bit","7-bit","8-bit","10-bit","12-bit","14-bit","16-bit"]
PPM = ["NONE","8-bit","10-bit","12-bit","14-bit","16-bit","?","?"]

# ---------------------------------------------------------------------------

def rd_all():
    try:
        fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    except PermissionError:
        sys.exit("Run as root: sudo python3 unicam_analyze.py")
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

def sep(title=""):
    if title:
        print(f"\n{'─'*20} {title} {'─'*(37-len(title))}")
    else:
        print("─" * 60)

def bit(v, n): return (v >> n) & 1

# ---------------------------------------------------------------------------
v = rd_all()
sep()
print(f"  BCM2835 Unicam  base=0x{BASE:08x}  (RPi4 CSI0)")
sep()

# ---- Raw dump ----
ORDER = [
    "CTRL","STA","ANA","PRI","CLK","CLT","DAT0","DAT1","DAT2","DAT3","DLT",
    "CMP0","CMP1","CAP0","CAP1",
    "ICTL","ISTA","IDI0","IPIPE","IBSA0","IBEA0","IBLS","IBWP",
    "IHWIN","IHSTA","IVWIN","IVSTA","ICC","ICS","IDC","IDPO","IDCA","IDS",
    "DCS","DBSA0","DBEA0","DBWP","DBCTL","IBSA1","IBEA1","IDI1","DBSA1","DBEA1",
    "MISC",
]
for name in ORDER:
    off = R[name]
    print(f"  [{off:03x}] {name:<6s} = 0x{v[name]:08x}")

# ===========================================================================
sep("CTRL  (Control)")
ctrl = v["CTRL"]
cpe = bit(ctrl,0); mem_= bit(ctrl,1); cpr = bit(ctrl,2)
cpm = bit(ctrl,3); soe = bit(ctrl,4)
pft = field(ctrl, 0xf00);   oet = field(ctrl, 0x1ff000)
print(f"  CPE (capture enable)    : {cpe}")
print(f"  MEM (write to memory)   : {mem_}")
print(f"  CPR (periph reset)      : {cpr}")
print(f"  CPM (0=CSI2, 1=CCP2)    : {cpm}  {'CSI2' if cpm==0 else 'CCP2'}")
print(f"  SOE (stop on error)     : {soe}")
print(f"  PFT (pkt framer timeout): {pft}")
print(f"  OET (output eng timeout): {oet}")

# ===========================================================================
sep("STA   (Status / sticky errors)")
sta = v["STA"]
STA_F = [
    (0, "SYN",   "sync"),
    (1, "CS",    "CSI active"),
    (2, "SBE",   "SYNC BYTE ERROR ← preamble corrupt"),
    (3, "PBE",   "PACKET BYTE ERROR ← ECC/parity"),
    (4, "HOE",   "HEADER OVERFLOW ERROR"),
    (5, "PLE",   "PAYLOAD LENGTH ERROR"),
    (6, "SSC",   "start-of-stream captured"),
    (7, "CRCE",  "CRC ERROR"),
    (8, "OES",   "overflow error sticky"),
    (9, "IFO",   "IMAGE FIFO OVERFLOW"),
    (10,"OFO",   "OUTPUT FIFO OVERFLOW"),
    (11,"BFO",   "BUFFER FIFO OVERFLOW"),
    (12,"DL",    "DATA LOSS"),
    (13,"PS",    "packet start"),
    (14,"IS",    "image start"),
    (15,"PI0",   "packet ID 0 match"),
    (16,"PI1",   "packet ID 1 match"),
    (17,"FSI_S", "frame start irq"),
    (18,"FEI_S", "frame end irq"),
    (19,"LCI_S", "line cap irq"),
    (20,"BUF0_RDY","buf 0 ready"),
    (21,"BUF0_NO", "buf 0 not ready"),
    (22,"BUF1_RDY","buf 1 ready"),
    (23,"BUF1_NO", "buf 1 not ready"),
    (24,"DI",    "data irq"),
]
ERRORS = {2,3,4,5,7,9,10,11,12}
for bn, short, desc in STA_F:
    if bit(sta, bn):
        tag = " *** ERROR ***" if bn in ERRORS else ""
        print(f"  [{'!' if bn in ERRORS else ' '}] bit{bn:2d}  {short:<8s}  {desc}{tag}")
if not (sta & 0x1fff):
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
t1 = clt & 0xff; t2 = (clt>>8) & 0xff
print(f"  CLT1 (tclk_term_en)     : {t1}  (expected ~2)")
print(f"  CLT2 (tclk_settle)      : {t2}  (expected ~6)")

# ===========================================================================
sep("DAT0 / DAT1  (Data lanes)")
dlt = v["DLT"]
for name in ["DAT0","DAT1"]:
    d = v[name]
    active = "active" if bit(d,0) else "DISABLED"
    print(f"  {name}: DLE={bit(d,0)} DLPD={bit(d,1)} DLLPE={bit(d,2)} "
          f"DLHSE={bit(d,3)} DLTRE={bit(d,4)} DLSM={bit(d,5)}  "
          f"DLSTE(stop)={bit(d,29)} DLFO(fifo_ovf)={bit(d,28)}  [{active}]")
t1 = dlt & 0xff; t2 = (dlt>>8) & 0xff; t3 = (dlt>>16) & 0xff
print(f"  DLT1 (td_term_en)       : {t1}  (expected ~2)")
print(f"  DLT2 (ths_settle)       : {t2}  (expected ~6)")
print(f"  DLT3 (trx_enable)       : {t3}  (expected 0)")

# ===========================================================================
sep("IDI0  (programmed CSI2 data type + VC)")
idi = v["IDI0"]
for i in range(4):
    raw = (idi >> (8*i)) & 0xff
    vc_i  = (raw >> 6) & 0x3
    dt_i  = raw & 0x3f
    label = DT.get(dt_i, f"unknown 0x{dt_i:02x}")
    print(f"  ID{i}: 0x{raw:02x}  VC={vc_i}  DT=0x{dt_i:02x} ({label})")

# ===========================================================================
sep("IPIPE (unpack / pack config)")
ipipe = v["IPIPE"]
pum_v = field(ipipe, 0x7)
ppm_v = field(ipipe, 0x380)
dem_v = field(ipipe, 0xc00)
print(f"  PUM (unpack mode)       : {pum_v} = {PUM[pum_v]}")
print(f"  PPM (pack mode)         : {ppm_v} = {PPM[ppm_v]}")
print(f"  DDM (demosaic)          : {dem_v}")
print(f"  DDM[6:3]                : {field(ipipe, 0x78)}")

# ===========================================================================
sep("Captured image geometry")
ihwin = v["IHWIN"]; ihsta = v["IHSTA"]
ivwin = v["IVWIN"]; ivsta = v["IVSTA"]
ibls  = v["IBLS"]
ibsa  = v["IBSA0"]; ibea  = v["IBEA0"]; ibwp = v["IBWP"]

h_start = ihwin & 0xffff; h_end = (ihwin>>16) & 0xffff
v_start = ivwin & 0xffff; v_end = (ivwin>>16) & 0xffff
h_last  = ihsta & 0xffff
v_last  = ivsta & 0xffff

print(f"  IHWIN  programmed       : [{h_start}..{h_end}]  (0=disabled=capture all)")
print(f"  IVWIN  programmed       : [{v_start}..{v_end}]")
print(f"  IHSTA  last H count     : {h_last} bytes received in last line")
print(f"  IVSTA  last V count     : {v_last} lines received in last frame")
print(f"  IBLS   line stride      : {ibls} bytes/line")
if ibls > 0:
    pixels_per_line = ibls // 2    # Y16 = 2 bytes/pixel
    print(f"           → {pixels_per_line} px/line @ 2 B/px (Y16)")
    if h_last > 0:
        print(f"  IHSTA vs IBLS           : {h_last} bytes received, stride={ibls} bytes "
              f"→ {'OK' if h_last == ibls else f'MISMATCH (delta={h_last-ibls})'}")
print(f"  IBSA0  buf start        : 0x{ibsa:08x}")
print(f"  IBEA0  buf end          : 0x{ibea:08x}  (size={ibea-ibsa} bytes)")
print(f"  IBWP   write pointer    : 0x{ibwp:08x}")
if ibsa and ibls:
    offset = ibwp - ibsa
    lines_done = offset // ibls
    print(f"           → {offset} bytes from start = {lines_done} complete lines")

# ===========================================================================
sep("CMP0/CMP1  (packet compare / frame-end detect)")
for name in ["CMP0","CMP1"]:
    c = v[name]
    pce = bit(c,31); gi = bit(c,9); cph = bit(c,8)
    vc  = field(c, 0xc0); dt = c & 0x3f
    print(f"  {name}: PCE={pce} GI={gi} CPH={cph} VC={vc} DT=0x{dt:02x} ({DT.get(dt,'?')})")

# ===========================================================================
sep("MISC  (FL0/FL1 flip)")
misc = v["MISC"]
print(f"  FL0 (flip line order ?)  : {bit(misc,6)}")
print(f"  FL1 (flip byte order ?)  : {bit(misc,9)}")

# ===========================================================================
sep("ERROR SUMMARY")
errs = []
if bit(sta, 2):  errs.append("SBE  — sync byte error: preamble/LP→HS timing issue")
if bit(sta, 3):  errs.append("PBE  — packet byte error: ECC/data corruption")
if bit(sta, 4):  errs.append("HOE  — header overflow")
if bit(sta, 5):  errs.append("PLE  — payload length error: line size mismatch")
if bit(sta, 7):  errs.append("CRCE — CRC error: data integrity failure")
if bit(sta, 9):  errs.append("IFO  — image FIFO overflow")
if bit(sta,10):  errs.append("OFO  — output FIFO overflow")
if bit(sta,11):  errs.append("BFO  — buffer FIFO overflow")
if bit(sta,12):  errs.append("DL   — data loss")
if bit(v["DAT0"],28): errs.append("DAT0.DLFO — lane 0 FIFO overflow")
if bit(v["DAT1"],28): errs.append("DAT1.DLFO — lane 1 FIFO overflow")
if errs:
    for e in errs:
        print(f"  [ERR] {e}")
else:
    print("  No error bits set")

sep("PIXEL-SHIFT DIAGNOSIS")
if ibls > 0 and h_last > 0 and h_last != ibls:
    delta = h_last - ibls
    print(f"  *** STRIDE MISMATCH: IHSTA={h_last} B, IBLS={ibls} B, delta={delta:+d} B")
    print(f"      Each frame the write pointer drifts by {delta} B → pixel shift!")
elif ibls > 0 and pum_v != 7:
    print(f"  WARNING: PUM={pum_v} ({PUM[pum_v]}) but Y16 data needs PUM=7 (16-bit)")
    print(f"           Wrong unpack mode → byte-level misalignment possible")
elif ppm_v != 5:
    print(f"  NOTE: PPM={ppm_v} ({PPM[ppm_v]}), expected 5 (16-bit pack) for Y16")
else:
    print(f"  Stride matches, unpack/pack correct → no register-level cause found")
    print(f"  Consider: DMA alignment, wrapped write pointer, or camera-side issue")

sep()
