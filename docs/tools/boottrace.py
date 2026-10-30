# boottrace.py <elf>: read the BOOT_TRACE buffers over openocd :4444 (no reset) and print the first seconds as a timeline
import sys, struct
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import regdump
from swd import SWD

elf = sys.argv[1]
w = SWD(4444)
def sym(n): return regdump.elf_symbol(elf, n)
def rd(n, size): return w.mem(sym(n), size)
def u(n, size): return int.from_bytes(rd(n, size), 'little')

nf, nd, nt = u('bt_frames', 1), u('bt_draws', 1), u('bt_ticks', 1)
ANN = "SMPL IDLE AUTO LOP NULL DFILT MATH AZERO ERR INFO FRONT REAR SLOT LO_G RMT TLK LTN SRQ".split()
ev = []
fb = rd('bt_frame', 52 * nf) if nf else b''
for i in range(nf):
    r = fb[52 * i:52 * i + 52]
    ms = struct.unpack_from('<H', r)[0]
    t = r[2:49].decode('latin-1')
    bits = int.from_bytes(r[49:52], 'little')
    ann = ' '.join(a for k, a in enumerate(ANN) if bits >> k & 1)
    show = lambda s: ''.join(c if 32 <= ord(c) < 127 else f'\\x{ord(c):02x}' for c in s)
    ev.append((ms, 0, f'frame  [{show(t[:18])}] [{show(t[18:])}] {ann}'))
db = rd('bt_draw', 6 * nd) if nd else b''
for i in range(nd):
    ms, cells, ann, full = struct.unpack_from('<HHBB', db, 6 * i)
    ev.append((ms, 1, f'draw   {cells} cells, {ann} annunciators{" (full)" if full else ""}'))
ev.append((u('bt_timing_check_ms', 2), 2, f'timing-mode check: PA12 {"HIGH -> timing mode" if u("bt_timing_mode", 1) else "low -> live"}'))
for ms, _, s in sorted(ev):
    print(f'{ms:6d} ms  {s}')
ms0 = u('bt_pa12_ms0', 2)
pa = rd('bt_pa12', nt) if nt else b''
runs, prev, start = [], None, 0
for i, v in enumerate(pa):
    if v != prev:
        if prev is not None: runs.append((start, i - 1, prev))
        prev, start = v, i
if prev is not None: runs.append((start, nt - 1, prev))
print('PA11/PA12 by tick (35 ms):')
for a, b, v in runs:
    print(f'  {ms0 + a * 35:5d}-{ms0 + b * 35:5d} ms  PA11={(v >> 1) & 1} PA12={v & 1}')
for n in ['boot_ready_ms', 'first_frame_ms', 'timingModsOnBoot']:
    print(n, u(n, 1 if n == 'timingModsOnBoot' else 4))
