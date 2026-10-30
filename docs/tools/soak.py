"""Read the SDRAM retention soak of a -DSDRAM_SOAK=1 build over SWD (openocd on :4444). See docs/REWORK-PLAN.md.

  python soak.py --elf <firmware.elf>                 one status line per region
  python soak.py --elf <firmware.elf> --read 3        read region 3 (the request-only one) now, then report
  python soak.py --elf <firmware.elf> --rewrite       write fresh patterns (restarts the soak)
  python soak.py --elf <firmware.elf> --watch 60 --log soak.csv    report every 60 s, append to a CSV

Regions are read every 10 s, 60 s, 10 min, and only on request. Any bit error means the SDRAM lost data between reads.
verify_errs is the read-back straight after writing: it must be 0, or the read procedure itself is wrong.
"""
import argparse, os, struct, sys, time
from regdump import elf_symbol
from swd import SWD

PERIODS = ['10 s', '60 s', '10 min', 'request']
N = 4


def u32s(w, addr, n):
    return list(struct.unpack(f'<{n}I', w.mem(addr, 4 * n)))


def read_state(w, sym):
    s = dict(errs=u32s(w, sym['soak_errs'], N), errs_max=u32s(w, sym['soak_errs_max'], N),
             reads=u32s(w, sym['soak_reads'], N), read_ms=u32s(w, sym['soak_read_ms'], N),
             written_ms=u32s(w, sym['soak_written_ms'], 1)[0], verify_errs=u32s(w, sym['soak_verify_errs'], 1)[0],
             fifo_timeouts=u32s(w, sym['soak_fifo_timeouts'], 1)[0], peek=w.mem(sym['soak_peek'], 8).hex(' '),
             tick=u32s(w, sym['uwTick'], 1)[0])
    return s


def report(s):
    age = (s['tick'] - s['written_ms']) / 1000.0
    out = [f"patterns written {age:.0f} s ago ({age / 60:.1f} min); verify_errs={s['verify_errs']} "
           f"fifo_timeouts={s['fifo_timeouts']} peek={s['peek']}"]
    for i in range(N):
        out.append(f"  region {i} ({PERIODS[i]:>7}): reads {s['reads'][i]:5d}  bits wrong last {s['errs'][i]:5d}  "
                   f"max {s['errs_max'][i]:5d}  last read {(s['tick'] - s['read_ms'][i]) / 1000.0:6.0f} s ago")
    return '\n'.join(out)


def request(w, sym, value, s0):
    w.c.sendall(f"mwb 0x{sym['soak_request']:08x} {value}\r\n".encode()); w._read_until(b'> ')
    for _ in range(100):
        time.sleep(0.1)
        if w.mem(sym['soak_request'], 1)[0] == 0:
            return
    sys.exit('firmware did not answer the request (is it a -DSDRAM_SOAK=1 build?)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', required=True)
    ap.add_argument('--read', type=int, choices=range(N), help='read this region now')
    ap.add_argument('--rewrite', action='store_true', help='write fresh patterns (restart)')
    ap.add_argument('--watch', type=float, help='report every this many seconds')
    ap.add_argument('--log', help='append one CSV row per report')
    ap.add_argument('--port', type=int, default=4444)
    a = ap.parse_args()

    names = ['soak_errs', 'soak_errs_max', 'soak_reads', 'soak_read_ms', 'soak_written_ms', 'soak_verify_errs',
             'soak_fifo_timeouts', 'soak_request', 'soak_peek', 'uwTick']
    sym = {n: elf_symbol(a.elf, n) for n in names}
    w = SWD(a.port)
    if a.rewrite:
        request(w, sym, 0x80, None)
    if a.read is not None:
        request(w, sym, 1 << a.read, None)
    while True:
        s = read_state(w, sym)
        print(time.strftime('%H:%M:%S'), report(s), flush=True)
        if a.log:
            new = not os.path.exists(a.log)
            with open(a.log, 'a') as f:
                if new:
                    f.write('time,age_s,verify_errs,fifo_timeouts,' + ','.join(
                        f'r{i}_reads,r{i}_errs,r{i}_max' for i in range(N)) + '\n')
                f.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')},{(s['tick'] - s['written_ms']) / 1000.0:.0f},"
                        f"{s['verify_errs']},{s['fifo_timeouts']}," + ','.join(
                            f"{s['reads'][i]},{s['errs'][i]},{s['errs_max'][i]}" for i in range(N)) + '\n')
        if not a.watch:
            break
        time.sleep(a.watch)


if __name__ == '__main__':
    main()
