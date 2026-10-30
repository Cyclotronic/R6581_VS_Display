# sweep.py <elf>: step the R6581T's integration time over GPIB and measure the display board's capture and drawing at each
import sys, time, json
import pyvisa
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import regdump
from swd import SWD

elf = sys.argv[1]
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 30
w = SWD(4444)
VARS = ['cap_seq', 'cap_short', 'cap_unstable', 'cap_heldback', 'cap_decodes', 'cap_unmatched', 'draws', 'draw_unmatched',
        'tp_lat_sum', 'tp_lat_n', 'tp_lat_max', 'tp_draw_max', 'SystemCoreClock']
addr = {n: regdump.elf_symbol(elf, n) for n in VARS}
def snap(): return {n: int.from_bytes(w.mem(a, 4), 'little') for n, a in addr.items()}
def reset_max():   # zero the max trackers so each step reports its own
    for n in ['tp_lat_max', 'tp_draw_max']:
        w.c.sendall(f'mww 0x{addr[n]:08x} 0\r\n'.encode()); w._read_until(b'> ')

rm = pyvisa.ResourceManager()
m = rm.open_resource('GPIB0::27::INSTR'); m.timeout = 10000
steps = [
    ('10 PLC, 7 dig, AZ on', ['VOLT:DC:NPLC 10', 'VOLT:DC:DIG 7', 'ZERO:AUTO ON']),
    ('1 PLC, 7 dig, AZ on', ['VOLT:DC:NPLC 1', 'VOLT:DC:DIG 7']),
    ('0.1 PLC, 6 dig, AZ on', ['VOLT:DC:NPLC 0.1', 'VOLT:DC:DIG 6']),
    ('0.01 PLC, 5 dig, AZ on', ['VOLT:DC:NPLC 0.01', 'VOLT:DC:DIG 5']),
    ('MIN PLC, 4 dig, AZ on', ['VOLT:DC:NPLC MIN', 'VOLT:DC:DIG 4']),
    ('MIN PLC, 4 dig, AZ off', ['ZERO:AUTO OFF']),
]
m.write('VOLT:DC:RANG 1')   # fixed range: autorange steps would muddle the rates
rows = []
for label, cmds in steps:
    for c in cmds: m.write(c)
    err = m.query('SYST:ERR?').strip()
    got = m.query('VOLT:DC:NPLC?').strip() + ' PLC, ' + m.query('VOLT:DC:DIG?').strip() + ' dig'
    time.sleep(3)
    reset_max(); a = snap(); t0 = time.time()
    time.sleep(SECS)
    b = snap(); dt = time.time() - t0
    d = {n: b[n] - a[n] for n in VARS}
    hz = b['SystemCoreClock'] or 72000000
    r = dict(step=label, meter=got, err=err,
             frames_s=round(d['cap_seq'] / dt, 1), short=d['cap_short'], unstable_s=round(d['cap_unstable'] / dt, 1), heldback=d['cap_heldback'],
             decodes_s=round(d['cap_decodes'] / dt, 2), unmatched=d['cap_unmatched'], draws_s=round(d['draws'] / dt, 2),
             draw_q=d['draw_unmatched'],
             lat_avg_ms=round(d['tp_lat_sum'] / d['tp_lat_n'] / 1000, 2) if d['tp_lat_n'] else None,
             lat_max_ms=round(b['tp_lat_max'] / 1000, 2), draw_max_ms=round(b['tp_draw_max'] * 1000 / hz, 2))
    rows.append(r); print(json.dumps(r), flush=True)
json.dump(rows, open('sweep_results.json', 'w'), indent=1)
