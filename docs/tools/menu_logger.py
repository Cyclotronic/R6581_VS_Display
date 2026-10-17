"""Front-panel walk-through logger. Reads the Blue Pill's decoded display over SWD (non-halting); sends NOTHING to the meter.

usage: python -I menu_logger.py <out-dir>        (needs the openocd server on :4444; stop with Ctrl-C / kill)

Keeps a summary file (menuwalk_summary.json) rewritten every few seconds:
  screens    distinct screens (digits normalised to '#') with first-seen time, count and a raw example
  chars      every character that appeared on the MAIN/AUX lines, with counts
  unknown    5x7 bitmaps that stayed unmatched ('?') for >= PERSIST consecutive samples (real table gaps),
             with the screen text they appeared in. One-sample '?' (torn reads) are counted separately.
  annunciators  every annunciator seen lit
Raw state changes go to menuwalk_changes.jsonl.
"""
import sys, os, json, time, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swd
from swd import SWD, snap, ADDR
if os.environ.get('SWD_ELF'): swd.use_elf(os.environ['SWD_ELF'])   # variable addresses of the build running on the board

PERSIST = 3
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
w = SWD()
t0 = time.time()
screens, chars, unknown, anns = {}, {}, {}, {}
torn = 0
miss_run = {}          # cell -> consecutive '?' samples
last_key = None
fh = open(os.path.join(out, "menuwalk_changes.jsonl"), "w")

def norm(s): return re.sub(r"\d", "#", s)

def art(bm):
    return ["".join("#" if (b >> (4 - c)) & 1 else "." for c in range(5)) for b in bm]

def save():
    d = dict(elapsed_s=round(time.time() - t0, 1), samples=n, torn_single_sample_unknowns=torn,
             screens=screens, chars=dict(sorted(chars.items())), annunciators=anns, unknown=unknown)
    tmp = os.path.join(out, "menuwalk_summary.tmp");
    json.dump(d, open(tmp, "w"), indent=1); os.replace(tmp, os.path.join(out, "menuwalk_summary.json"))

n = 0; last_save = 0
G_cells = 47
try:
    while True:
        s = snap(w); n += 1; now = round(time.time() - t0, 2)
        text = s["main"] + "|" + s["aux"]
        for ch in text:
            if ch != " " and ch != "|": chars[ch] = chars.get(ch, 0) + 1
        for a in s["ann"]: anns[a] = anns.get(a, 0) + 1
        key = (norm(s["main"]), norm(s["aux"]), tuple(s["ann"]))
        # screens
        k2 = norm(s["main"]).strip() + " || " + norm(s["aux"]).strip()
        e = screens.setdefault(k2, dict(first_s=now, count=0, example=s["main"] + " || " + s["aux"], ann=s["ann"]))
        e["count"] += 1
        if key != last_key:
            fh.write(json.dumps(dict(t=now, main=s["main"], aux=s["aux"], ann=s["ann"])) + "\n"); fh.flush(); last_key = key
        # unmatched cells: '?' in G with the bitmap from the chars array
        cur = set(cell for cell, _ in s["miss"])
        for cell, bm in s["miss"]:
            miss_run[cell] = miss_run.get(cell, 0) + 1
            if miss_run[cell] == PERSIST:
                bmb = bytes(int(x, 16) for x in bm.split())
                k = bm
                u = unknown.setdefault(k, dict(count=0, cells=[], screens=[], art=art(bmb)))
                u["count"] += 1
                if cell not in u["cells"]: u["cells"].append(cell)
                scr = s["main"] + " || " + s["aux"]
                if scr not in u["screens"] and len(u["screens"]) < 4: u["screens"].append(scr)
        for cell in list(miss_run):
            if cell not in cur:
                if miss_run[cell] < PERSIST: torn += 1
                del miss_run[cell]
        if time.time() - last_save > 2: save(); last_save = time.time()
except KeyboardInterrupt:
    pass
finally:
    save(); fh.close()
