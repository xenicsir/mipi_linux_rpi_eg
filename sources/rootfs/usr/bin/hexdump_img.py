#!/usr/bin/env python3
"""
hexdump_img.py <file> <bytes_per_word> <endian> [-]

  bytes_per_word : 1, 2, 3 or 4
  endian         : le (little-endian) or be (big-endian)
  -              : print to stdout instead of a file

Output file: <file>_<bytes_per_word>B_<endian>_hex.txt

Example:
  python3 hexdump_img.py frame.y14 2 le
  python3 hexdump_img.py frame.y16 2 be -
  python3 hexdump_img.py frame.rgb 3 be
"""
import sys, struct, os

if len(sys.argv) not in (4, 5):
    print(__doc__)
    sys.exit(1)

filename       = sys.argv[1]
bytes_per_word = int(sys.argv[2])
endian         = sys.argv[3].lower()
to_stdout      = len(sys.argv) == 5 and sys.argv[4] == '-'

if bytes_per_word not in (1, 2, 3, 4):
    sys.exit("bytes_per_word must be 1, 2, 3 or 4")
if endian not in ("le", "be"):
    sys.exit("endian must be 'le' or 'be'")

hex_w = bytes_per_word * 2

data  = open(filename, 'rb').read()
total = len(data)
n     = total // bytes_per_word

if bytes_per_word == 3:
    words = []
    for i in range(n):
        b = data[i*3 : i*3+3]
        if endian == 'be':
            words.append(b[0] << 16 | b[1] << 8 | b[2])
        else:
            words.append(b[2] << 16 | b[1] << 8 | b[0])
else:
    fmt_char = {1: 'B', 2: 'H', 4: 'I'}[bytes_per_word]
    fmt_end  = '<' if endian == 'le' else '>'
    words = list(struct.unpack(fmt_end + fmt_char * n, data[:n * bytes_per_word]))

COLS_WORDS = 1024
ROWS = n // COLS_WORDS if n % COLS_WORDS == 0 else 1
if ROWS == 1:
    COLS_WORDS = n

PER_LINE = 16
dst = f'{filename}_{bytes_per_word}B_{endian.upper()}_hex.txt'
out = sys.stdout if to_stdout else open(dst, 'w')

def write(s):
    try:
        out.write(s)
    except BrokenPipeError:
        sys.exit(0)

write(f'{os.path.basename(filename)}  {bytes_per_word}B/word  {endian.upper()}  '
      f'{COLS_WORDS}x{ROWS} words  ({total} bytes)\n')
def col_hdr(i, w):
    h = f'+{i}'
    return h.rjust(w) if len(h) <= w else str(i).rjust(w)
write('[row,col]  ' + '  '.join(col_hdr(i, hex_w) for i in range(PER_LINE)) + '\n')
write('-' * (11 + PER_LINE * (hex_w + 2)) + '\n')
for row in range(ROWS):
    for col in range(0, COLS_WORDS, PER_LINE):
        vals = words[row * COLS_WORDS + col : row * COLS_WORDS + col + PER_LINE]
        write(f'[{row:03d},{col:04d}]  ' +
              '  '.join(f'{v:0{hex_w}X}' for v in vals) + '\n')
    write('\n')

if not to_stdout:
    out.close()
    print(f'Written {ROWS} rows x {COLS_WORDS} words -> {dst}')
