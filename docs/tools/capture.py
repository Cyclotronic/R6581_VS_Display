"""Read the VFD capture counters (Stage 4 of docs/REWORK-PLAN.md) over SWD (openocd on :4444).

  python capture.py --elf <firmware.elf>                       one report: totals, and the decoded main/aux lines
  python capture.py --elf <firmware.elf> --watch 60 --log c.csv  report every 60 s with rates, append to a CSV

frames: whole frames the EXTI handler copied; short: frames cut off before the DMA had all 235 bytes;
unstable: frames that differed from the one before (each real change, plus any bad frame); decodes: frames decoded;
unmatched: decodes with a character not in the font table; draws / draw?: 35 ms redraws, and those with a '?' in G[].
CAPTURE_LEGACY=1 builds have frames, short, decodes (every main-loop pass), unmatched and the draw counters; the
others are linked out there and read as 0.
"""
import argparse, os, struct, time
from regdump import elf_symbol
from swd import SWD

NAMES = ['cap_seq', 'cap_short', 'cap_unstable', 'cap_decodes', 'cap_unmatched', 'draws', 'draw_unmatched',
         'main_loops', 'lt_busy_timeouts', 'uwTick']
SIZE = dict(lt_busy_timeouts=2)


def read(w, sym):
    s = {n: int.from_bytes(w.mem(sym[n], SIZE.get(n, 4)), 'little') if sym[n] is not None else 0 for n in NAMES}
    g = w.mem(sym['G'], 48)
    s['main'], s['aux'] = g[1:19].decode('latin1'), g[19:48].decode('latin1')
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', required=True)
    ap.add_argument('--watch', type=float, help='report every this many seconds')
    ap.add_argument('--log', help='append one CSV row per report')
    ap.add_argument('--port', type=int, default=4444)
    a = ap.parse_args()
    sym = {n: elf_symbol(a.elf, n, required=False) for n in NAMES}   # a counter a build never uses is linked out
    sym['G'] = elf_symbol(a.elf, 'G')
    w = SWD(a.port)
    prev = None
    while True:
        s = read(w, sym)
        line = (f"t={s['uwTick'] / 1000:.0f}s frames {s['cap_seq']} short {s['cap_short']} unstable {s['cap_unstable']} "
                f"decodes {s['cap_decodes']} unmatched {s['cap_unmatched']} draws {s['draws']} draw? {s['draw_unmatched']} "
                f"busy_to {s['lt_busy_timeouts']}")
        if prev:
            dt = (s['uwTick'] - prev['uwTick']) / 1000.0 or 1.0
            line += (f" | per s: frames {(s['cap_seq'] - prev['cap_seq']) / dt:.1f} "
                     f"decodes {(s['cap_decodes'] - prev['cap_decodes']) / dt:.1f} loops {(s['main_loops'] - prev['main_loops']) / dt:.0f}")
        print(time.strftime('%H:%M:%S'), line, f"| [{s['main']}] [{s['aux']}]", flush=True)
        if a.log:
            new = not os.path.exists(a.log)
            with open(a.log, 'a') as f:
                if new:
                    f.write('time,' + ','.join(NAMES) + ',main,aux\n')
                f.write(time.strftime('%Y-%m-%d %H:%M:%S,') + ','.join(str(s[n]) for n in NAMES) +
                        f",\"{s['main']}\",\"{s['aux']}\"\n")
        if not a.watch:
            break
        prev = s
        time.sleep(a.watch)


if __name__ == '__main__':
    main()
