# bootrun.py <elf> <resets> <wait_s>: reset the board N times over openocd :4444 and read the boot timestamps after each.
import sys, time
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import regdump
from swd import SWD

elf, n, wait = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
names = ['boot_sleepout_ms', 'boot_lt_ms', 'boot_panel_ms', 'boot_ready_ms', 'first_frame_ms', 'lt_init_ms:2', 'lt_init_attempts:1', 'lt_fault:1',
         'boot_diag_number:1', 'cap_decodes', 'cap_short']
addr = {}
for x in names:
    nm, _, sz = x.partition(':')
    addr[nm] = (regdump.elf_symbol(elf, nm, required=False), int(sz or 4))
w = SWD(4444)
rows = []
for k in range(n):
    w.c.sendall(b'reset run\r\n'); w._read_until(b'> ')
    time.sleep(wait)
    r = {}
    for nm, (ad, sz) in addr.items():
        r[nm] = None if ad is None else int.from_bytes(w.mem(ad, sz), 'little')
    rows.append(r)
    print(' '.join(f'{k}={v}' for k, v in r.items()), flush=True)
for nm in ['boot_sleepout_ms', 'boot_lt_ms', 'boot_panel_ms', 'boot_ready_ms', 'first_frame_ms']:
    v = [r[nm] for r in rows if r[nm] is not None]
    if v: print(f'{nm}: min {min(v)} max {max(v)} avg {sum(v)/len(v):.1f}')
