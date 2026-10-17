"""Per-digit update timing from a slow-motion clip of the R6581T TFT.

usage: python -I events240.py <video> <x> <y> <w> <h> [ncols] [--sheet N]
  (x y w h) = crop of the DIGIT area of the main line, in source-video pixels (the digits only, not the units).
Finds each reading change (frame-to-frame change of the digit mask), then for every digit column finds the first
frame in which that column has moved 25 % of the way to its final state. If the display updates atomically
(page flip) all columns start in the same frame; a progressive in-place redraw starts them one after another.
Frames are read with -fps_mode passthrough so the 120/240 fps mix of iPhone slo-mo is kept exactly.
"""
import sys, subprocess, os, tempfile
import numpy as np

vid = sys.argv[1]; x, y, w, h = map(int, sys.argv[2:6])
ncols = int(sys.argv[6]) if len(sys.argv) > 6 and not sys.argv[6].startswith("--") else 8
sheet_n = int(sys.argv[sys.argv.index("--sheet") + 1]) if "--sheet" in sys.argv else 0

tmp = tempfile.mkdtemp()
raw = os.path.join(tmp, "d.raw"); sw, sh = w // 2, h // 2
subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", vid, "-fps_mode", "passthrough",
                "-vf", f"crop={w}:{h}:{x}:{y},scale={sw}:{sh},format=gray", "-f", "rawvideo", "-pix_fmt", "gray", raw], check=True)
fr = np.fromfile(raw, dtype=np.uint8).reshape(-1, sh, sw)
pts = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "frame=pts_time", "-of", "csv=p=0", vid],
                     capture_output=True, text=True).stdout.split()
t = [float(p.strip().rstrip(",")) for p in pts]
N = len(fr)
M = np.array([f > (np.percentile(f, 99) + np.percentile(f, 20)) / 2 for f in fr]).astype(np.int8)   # midpoint between background and digit brightness
step = np.array([np.abs(M[i].astype(int) - M[i - 1]).mean() * 100 for i in range(1, N)])
thr = max(np.percentile(step, 90) * 3, 1.0)
ev = []; i = 0
while i < len(step):
    if step[i] > thr:
        j = i
        while j + 1 < len(step) and step[j + 1] > thr * 0.5: j += 1
        ev.append((i + 1, j + 1)); i = j + 1
    else: i += 1
# merge events closer than 12 frames (the fade of one reading is not a new reading)
merged = []
for a, b in ev:
    if merged and a - merged[-1][1] < 12: merged[-1] = (merged[-1][0], b)
    else: merged.append((a, b))

cw = sw // ncols
spreads = []; rows = []
for a, b in merged:
    lo, hi = a - 4, min(N - 1, b + 14)
    if lo < 2 or hi >= N or a < 4: continue
    old = M[lo]; new = M[hi]
    onset = []
    for c in range(ncols):
        sl = slice(c * cw, (c + 1) * cw)
        tot = np.abs(new[:, sl].astype(int) - old[:, sl]).sum()
        if tot < 0.02 * old[:, sl].size:    # column did not change (same digit) - skip
            continue
        reach = None
        for k in range(lo, hi + 1):
            if np.abs(M[k][:, sl].astype(int) - old[:, sl]).sum() >= 0.25 * tot:
                reach = k; break
        if reach is not None: onset.append((c, reach))
    if len(onset) >= 3:
        fs = [r for _, r in onset]
        spreads.append(max(fs) - min(fs)); rows.append((a, round(t[a], 2), [(c, r - min(fs)) for c, r in onset], max(fs) - min(fs)))

print(f"{os.path.basename(vid)}: {N} frames, {len(merged)} reading changes found, {len(spreads)} analysed (>=3 changed columns)")
if spreads:
    sp = np.array(spreads)
    print("onset spread across changed digit columns, in frames (1 frame = 4.2 ms at 240 fps):")
    print("  median %.1f   mean %.2f   max %d   share with spread 0 or 1: %.0f%%   share >=3: %.0f%%" %
          (np.median(sp), sp.mean(), sp.max(), 100 * (sp <= 1).mean(), 100 * (sp >= 3).mean()))
    import collections
    print("  histogram:", sorted(collections.Counter(sp.tolist()).items()))
    for r in rows[:6]: print("  event @frame", r[0], "t=%.2fs" % r[1], "col:frame-offset", r[2], "spread", r[3])
np.save(os.path.join(tmp, "events.npy"), np.array(merged))
print("events file:", os.path.join(tmp, "events.npy"))
