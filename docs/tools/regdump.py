"""Read and decode the LT7680 register dump of a -DLT_REGDUMP=1 build (Stage 0 of docs/REWORK-PLAN.md).

  python regdump.py --elf images/regdump-O2/firmware.elf            read lt_regdump[] over SWD (openocd on :4444) and decode it
  python regdump.py --elf ... --again                               ask the firmware to dump again first (e.g. after a soak)
  python regdump.py --elf ... --save dump.json                      also save the raw bytes and the decode
  python regdump.py --bytes "4c 54 52 44 01 ..."                    decode bytes given by hand (no hardware)

Clock decode assumes the board's 10 MHz crystal (XI). SDRAM minimums are typical SDR SDRAM figures, not the
LT7680A-R die's own (not published); treat them as a guide.
"""
import argparse, json, struct, sys, time

OFF = dict(MAGIC=0, SEQ=4, STSR=5, R00=6, R01=7, R03=8, R12=9, PLL=10, SDR=16, TIM=21, E4_TBANK=25, E4_AFTER=26,
           SDR_AGAIN=27, INIT_POLLS_LO=31, INIT_POLLS_HI=32, INIT_E4=33, INIT_STSR=34)
SIZE = 36
XI_MHZ = 10.0
TREF_MS, ROWS = 64.0, 4096          # LT7680A-R: 4,096 rows, 64 ms (datasheet REG[E2h-E3h])
TYPICAL_NS = dict(TRCD=15, TRAS=42, TRP=15, TRFC=60, TXSR=70)


def elf_symbol(path, name):
    d = open(path, 'rb').read()
    shoff = struct.unpack_from('<I', d, 0x20)[0]
    shentsize, shnum = struct.unpack_from('<HH', d, 0x2e)
    secs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shentsize) for i in range(shnum)]
    for s in secs:
        if s[1] != 2:                                   # SHT_SYMTAB
            continue
        st = secs[s[6]]
        for o in range(s[4], s[4] + s[5], 16):
            n, v = struct.unpack_from('<II', d, o)
            if d[st[4] + n:d.index(b'\x00', st[4] + n)].decode() == name:
                return v
    sys.exit(f'{name} not in {path}: is it a -DLT_REGDUMP=1 build?')


def pll(c1, c2):
    od = {0: 1, 1: 2, 2: 2, 3: 4}[c1 >> 6]
    r = (c1 >> 1) & 0x1F
    n = ((c1 & 1) << 8) | c2
    vco = XI_MHZ * n / r if r else 0.0
    return dict(OD=od, R=r, N=n, vco_mhz=round(vco, 2), out_mhz=round(vco / od, 2), vco_in_range=100 <= vco <= 500)


def decode(b):
    b = bytes(b)
    if len(b) < SIZE:
        sys.exit(f'need {SIZE} bytes, got {len(b)}')
    out = dict(magic_ok=b[0:4] == b'LTRD', dump_count=b[OFF['SEQ']])
    p = b[OFF['PLL']:OFF['PLL'] + 6]
    clk = dict(PCLK=pll(p[0], p[1]), MCLK=pll(p[2], p[3]), CCLK=pll(p[4], p[5]))
    out['clocks'] = clk
    mclk = clk['MCLK']['out_mhz']

    e0, e1, e2, e3, e4 = b[OFF['SDR']:OFF['SDR'] + 5]
    ref = e2 | (e3 << 8)
    need = TREF_MS * 1000.0 / ROWS                       # us per row
    out['sdram_normal'] = dict(
        E0=f'{e0:02X}', banks=4 if e0 & 0x20 else 2,
        rows={0: 2048, 1: 4096}.get((e0 >> 3) & 3, 8192),
        columns=[256, 512, 1024, 2048][e0 & 3] if not e0 & 4 else 4096,
        power_saving='self refresh' if e0 & 0x80 else 'power down',
        E1=f'{e1:02X}', cas_latency=e1 & 7,
        refresh_interval=ref,
        refresh_us=round(ref / mclk, 3) if ref and mclk else None,
        refresh_needed_us=round(need, 3),
        refresh_ok=bool(ref) and mclk > 0 and ref / mclk <= need,
        refresh_suggested=int(mclk * need) - 16 if mclk else None,
        E4=f'{e4:02X}')
    if not ref:
        out['sdram_normal']['refresh_note'] = '0000h: auto-refresh disabled'

    t0, t1, t2, t3 = b[OFF['TIM']:OFF['TIM'] + 4]
    clk_ns = 1000.0 / mclk if mclk else 0.0
    tim = dict(TMRD=(t0 & 15) + 1, TRFC=(t1 >> 4) + 1, TXSR=(t1 & 15) + 1, TRP=(t2 >> 4) + 1, TWR=(t2 & 15) + 1,
               TRCD=(t3 >> 4) + 1, TRAS=(t3 & 15) + 1)
    out['sdram_timing_clocks'] = tim
    out['sdram_timing_raw'] = ' '.join(f'{x:02X}' for x in (t0, t1, t2, t3))
    out['sdram_timing_ns'] = {k: round(v * clk_ns, 1) for k, v in tim.items()}
    out['sdram_timing_short'] = [f'{k} {tim[k]} clk = {tim[k] * clk_ns:.0f} ns < ~{ns} ns'
                                 for k, ns in TYPICAL_NS.items() if tim[k] * clk_ns < ns]
    again = b[OFF['SDR_AGAIN']:OFF['SDR_AGAIN'] + 4]
    out['bank_switch'] = dict(E4_while_timing=f"{b[OFF['E4_TBANK']]:02X}", E4_after=f"{b[OFF['E4_AFTER']]:02X}",
                              normal_restored=again == bytes((e0, e1, e2, e3)),
                              banks_differ=bytes((t0, t1, t2, t3)) != bytes((e0, e1, e2, e3)))

    def stsr(s):
        return dict(raw=f'{s:02X}', wfifo_full=bool(s & 0x80), wfifo_empty=bool(s & 0x40), rfifo_full=bool(s & 0x20),
                    rfifo_empty=bool(s & 0x10), core_busy=bool(s & 8), sdram_ready=bool(s & 4),
                    power_saving=bool(s & 2), int_pin=bool(s & 1))
    out['status'] = stsr(b[OFF['STSR']])
    out['init'] = dict(polls=b[OFF['INIT_POLLS_LO']] | (b[OFF['INIT_POLLS_HI']] << 8),
                       E4_at_ready=f"{b[OFF['INIT_E4']]:02X}", status_at_ready=stsr(b[OFF['INIT_STSR']]))
    out['other'] = {f'R{r}': f'{b[OFF["R" + r]]:02X}' for r in ('00', '01', '03', '12')}
    out['raw'] = b[:SIZE].hex(' ')
    return out


def summary(d):
    c, s = d['clocks'], d['sdram_normal']
    lines = [f"dump #{d['dump_count']}  magic {'ok' if d['magic_ok'] else 'MISSING (no dump yet?)'}"]
    for k in ('PCLK', 'MCLK', 'CCLK'):
        v = c[k]
        lines.append(f"{k}: {v['out_mhz']} MHz (N={v['N']} R={v['R']} OD=/{v['OD']}, VCO {v['vco_mhz']} MHz"
                     f"{'' if v['vco_in_range'] else ' OUTSIDE 100-500'})")
    lines.append(f"SDRAM E0={s['E0']}: {s['banks']} banks, {s['rows']} rows, {s['columns']} columns"
                 f"  (LT7680A-R wants 0x29: 4 banks, 4096 rows, 512 columns)")
    lines.append(f"CAS latency {s['cas_latency']}")
    if s['refresh_interval']:
        lines.append(f"refresh {s['refresh_interval']} clocks = {s['refresh_us']} us per row "
                     f"(needs <= {s['refresh_needed_us']} us) {'OK' if s['refresh_ok'] else 'TOO SLOW'}")
    else:
        lines.append(f"refresh 0000h: AUTO-REFRESH DISABLED (suggest ~{s['refresh_suggested']} at this MCLK)")
    lines.append(f"timing bank {d['sdram_timing_raw']}: " +
                 ', '.join(f'{k} {v}' for k, v in d['sdram_timing_clocks'].items()) + ' clocks')
    for x in d['sdram_timing_short']:
        lines.append('  SHORT: ' + x)
    bs = d['bank_switch']
    lines.append(f"bank switch: E4 {bs['E4_while_timing']} while timing bank, {bs['E4_after']} after; normal bank "
                 f"{'restored' if bs['normal_restored'] else 'CHANGED'}; banks {'differ' if bs['banks_differ'] else 'READ THE SAME (switch may not work)'}")
    st = d['status']
    lines.append(f"status {st['raw']}: sdram_ready={st['sdram_ready']} core_busy={st['core_busy']}")
    i = d['init']
    lines.append(f"init: {i['polls']} poll(s) of E4h, E4={i['E4_at_ready']}, status bit2 (SDRAM ready)="
                 f"{i['status_at_ready']['sdram_ready']}")
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', help='firmware.elf of the running -DLT_REGDUMP=1 build (for the variable addresses)')
    ap.add_argument('--bytes', help='decode these hex bytes instead of reading over SWD')
    ap.add_argument('--again', action='store_true', help='ask the firmware for a fresh dump first')
    ap.add_argument('--save', help='write the decode (with raw bytes) to this JSON file')
    ap.add_argument('--port', type=int, default=4444)
    a = ap.parse_args()

    if a.bytes:
        raw = bytes.fromhex(a.bytes.replace(',', ' '))
    else:
        if not a.elf:
            ap.error('--elf is needed to read over SWD')
        from swd import SWD
        addr, req = elf_symbol(a.elf, 'lt_regdump'), elf_symbol(a.elf, 'lt_regdump_request')
        w = SWD(a.port)
        if a.again:
            seq = w.mem(addr + OFF['SEQ'], 1)[0]
            w.c.sendall(f'mwb 0x{req:08x} 1\r\n'.encode()); w._read_until(b'> ')
            for _ in range(50):                          # the main loop picks it up within a pass or two
                time.sleep(0.1)
                if w.mem(addr + OFF['SEQ'], 1)[0] != seq:
                    break
            else:
                sys.exit('firmware did not answer the dump request')
        raw = w.mem(addr, SIZE)

    d = decode(raw)
    print(summary(d))
    if a.save:
        d['saved'] = time.strftime('%Y-%m-%d %H:%M:%S')
        json.dump(d, open(a.save, 'w'), indent=1)
        print('saved', a.save)


if __name__ == '__main__':
    main()
