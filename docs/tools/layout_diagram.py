"""To-scale layout of the R6581T TFT in three versions: Ian's original, ours (master), Hydron's.
The panel is 400 x 960 in memory; the left 80 'X' pixels are overscan (not visible), so the visible area is 960 wide
(Y, along the text) by 320 tall (X = 80..399). Text runs along Y; X is the vertical position on the screen.
All numbers come from the constants in each version's display.h / display.c.  usage: python -I layout_diagram.py out.png"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, XMIN, XMAX = 960, 80, 399
ANN_NAMES = ["SMPL","IDLE","AUTO","LOP","NULL","DFILT","MATH","AZERO","ERR","INFO","FRONT","REAR","SLOT","LO_G","RMT","TLK","LTN","SRQ"]

def ann_boxes(kind):
    """returns [(y0, width)] for the annunciators"""
    out = []
    if kind == "ian":
        ys = [10,62,114,166,218,270,322,374,426,478,530,582,634,686,738,790,842,900]
        for n, y in zip(ANN_NAMES, ys): out.append((y, 8 * len(n), n))
    else:
        chars = [0,4,8,12,15,19,24,28,33,38,38,43,47,47,51,54,57,60]
        gaps  = [0,1,2,3,4,5,6,7,8,9,9,10,11,11,12,13,14,15]
        names = list(ANN_NAMES); names[8] = "ERROR"; names[9] = ""; names[12] = ""
        for n, c, g in zip(names, chars, gaps): out.append((2 + c * 12 + g * 13, 12 * len(n), n))
    return out

def cells(y0, n, pitch, glyph_w):
    return [(y0 + i * pitch, glyph_w) for i in range(n)]

LAYOUTS = {
 "Ours (master)":            dict(kind="new", ann_x=126, ann_h=48, main_x=182, main_y=0,  main_pitch=52, aux_x=280, aux_y=60, aux_pitch=24, aux_gw=24, aux_h=48,
                                   splash=[(330,100,696),(352,640,288)]),
 "Hydron's build":           dict(kind="new", ann_x=120, ann_h=48, main_x=176, main_y=3,  main_pitch=53, aux_x=280, aux_y=0,  aux_pitch=33, aux_gw=32, aux_h=64,
                                   splash=[(330,100,696),(130,640,288)]),
 "Proposed: Hydron's sizes, balanced spacing (boot screen no longer shares the live screen)":
                            dict(kind="new", ann_x=120, ann_h=48, main_x=184, main_y=3,  main_pitch=53, aux_x=296, aux_y=0,  aux_pitch=33, aux_gw=32, aux_h=64, splash=[]),
}
LAYOUTS["Taller main (current build): main 48x128, ~20 px gaps"] = dict(kind="new", ann_x=100, ann_h=48, main_x=168, main_h=128, main_y=3, main_pitch=53, aux_x=316, aux_y=0, aux_pitch=33, aux_gw=32, aux_h=64, splash=[])

def draw_panel(d, oy, title, L):
    d.rectangle([0, oy, W, oy + (XMAX - XMIN)], outline=(120,120,120), fill=(8,8,16))
    d.text((6, oy + 3), title, fill=(255,255,255))
    def rect(y0, w, x0, h, col, label=None):
        d.rectangle([y0, oy + x0 - XMIN, y0 + w - 1, oy + x0 - XMIN + h - 1], outline=col, fill=None)
    for y0, w, n in ann_boxes(L["kind"]):
        if w: rect(y0, w, L["ann_x"], L["ann_h"], (0,200,0))
    for y0, w in cells(L["main_y"], 18, L["main_pitch"], 48): rect(y0, w, L["main_x"], L.get("main_h", 96), (230,230,230))
    for y0, w in cells(L["aux_y"], 29, L["aux_pitch"], L["aux_gw"]): rect(y0, w, L["aux_x"], L["aux_h"], (230,200,0))
    for k, (sx, sy, sw) in enumerate(L["splash"]): rect(sy, sw, sx, 16, (0,200,220) if k == 0 else (140,140,140))
    # right-edge wipe zone (Ian's 8 black lines at Y 952..959)
    d.rectangle([952, oy, 959, oy + (XMAX - XMIN)], fill=(70,0,0))
    # vertical extents
    top = min(L["ann_x"], L["main_x"]); bot = max([L["aux_x"] + L["aux_h"]] + [sx + 16 for sx, _, _ in L["splash"]])
    gaps = f"gaps: annunciators-main {L['main_x']-(L['ann_x']+L['ann_h'])}, main-aux {L['aux_x']-(L['main_x']+L.get('main_h',96))}, top margin {top-XMIN}, bottom margin {XMAX+1-bot}"
    d.text((6, oy + (XMAX - XMIN) - 28), gaps, fill=(255,220,120))
    d.text((6, oy + (XMAX - XMIN) - 14), f"stack X {top}..{bot}  | annunciators {L['ann_x']}..{L['ann_x']+L['ann_h']} | main {L['main_x']}..{L['main_x']+L.get('main_h',96)} | aux {L['aux_x']}..{L['aux_x']+L['aux_h']}  (visible X 80..399)", fill=(180,180,180))

out = sys.argv[1]
ph = XMAX - XMIN + 1
img = Image.new("RGB", (W, len(LAYOUTS) * (ph + 22) + 6), (30,30,30)); d = ImageDraw.Draw(img)
for i, (title, L) in enumerate(LAYOUTS.items()):
    draw_panel(d, i * (ph + 22) + 18, title, L)
d.text((6, 3), "To scale (Ian's original dropped): green = annunciators, white = main line cells, yellow = aux cells, cyan/grey = splash text, red strip = right-edge wipe (Y 952..959).  Visible area only (overscan X<80 not shown).", fill=(255,255,255))
img.save(out); print("saved", out)
