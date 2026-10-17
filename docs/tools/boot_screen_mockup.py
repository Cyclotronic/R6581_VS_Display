"""Mock-up v3 of the boot screen: notes in the middle, timing below, credits + small logo at the bottom. usage: python -I boot_screen_mockup3.py <boot_logo.h> out.png [boot] [speed]"""
import sys, re
from PIL import Image, ImageDraw, ImageFont
hdr, out = sys.argv[1], sys.argv[2]; boot = sys.argv[3] if len(sys.argv) > 3 else "5"; speed = sys.argv[4] if len(sys.argv) > 4 else "2300000"
t = open(hdr).read(); Wl = int(re.search(r"BOOT_LOGO_W (\d+)", t).group(1)); Hl = int(re.search(r"BOOT_LOGO_H (\d+)", t).group(1)); bpr = int(re.search(r"BOOT_LOGO_BPR (\d+)", t).group(1))
data = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", t)]
logo = Image.new("L", (Wl, Hl))
for r in range(Hl):
    for c in range(Wl): logo.putpixel((c, r), 255 if (data[r * bpr + (c >> 3)] >> (7 - (c & 7))) & 1 else 0)
LH = 48; logo = logo.resize((round(Wl * LH / Hl), LH), Image.LANCZOS).point(lambda v: 255 if v >= 90 else 0)
W, XMIN, XMAX, S = 960, 80, 399, 2
img = Image.new("RGB", (W * S, (XMAX - XMIN + 1) * S + 40), (40, 40, 40)); d = ImageDraw.Draw(img)
d.rectangle([0, 40, W * S - 1, 40 + (XMAX - XMIN + 1) * S - 1], fill=(0, 0, 0))
def line(s, x, pitch, col, y0=None):
    if y0 is None: y0 = (952 - pitch * len(s)) // 2
    f = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", int(pitch * S / 0.55))
    for i, ch in enumerate(s): d.text(((y0 + i * pitch) * S, 40 + (x - XMIN) * S), ch, font=f, fill=col)
    return f"X {x}..{x + int(pitch / 0.5)}"
rows = []
rows.append(line(f"BluePill speed = {speed}", 128, 12, (0xA0, 0xA0, 0xA0)))                      # 24-dot x1, grey, top
rows.append(line("Splash + timing test: first 10 boots only", 176, 16, (255, 255, 255)))       # 32-dot, white
rows.append(line("After that, hold DCV at power-up to reset", 214, 16, (255, 255, 255)))
rows.append(line(f"17 14 2 50 30 10 60 AdaF   boot {boot}/10", 266, 12, (0x90, 0x90, 0x90)))
cred = "Reverse engineering by MickleT / TFT LCD by Ian Johnston"; cw = 12 * len(cred); gap = 16; total = cw + gap + logo.width; y0 = (952 - total) // 2
rows.append(line(cred, 304 + (LH - 24) // 2, 12, (0, 255, 0), y0=y0))                          # credits vertically centred on the logo
lx, ly = 304, y0 + cw + gap
for r in range(logo.height):
    for c in range(logo.width):
        if logo.getpixel((c, r)):
            d.rectangle([(ly + c) * S, 40 + (lx + r - XMIN) * S, (ly + c + 1) * S - 1, 40 + (lx + r - XMIN + 1) * S - 1], fill=(0, 255, 0))
for xx in (128, 352):
    for yy in range(0, W * S, 16): d.line([yy, 40 + (xx - XMIN) * S, yy + 8, 40 + (xx - XMIN) * S], fill=(200, 120, 0))
d.rectangle([952 * S, 40, W * S - 1, 40 + (XMAX - XMIN + 1) * S - 1], fill=(70, 0, 0))
d.text((6, 6), f"Boot screen mock-up v3 (to scale, 2x). Content spans X 128..{lx + LH}; orange dashes = top of annunciators (128) and bottom of aux row (352), the live layout's visible extent; logo {logo.width}x{LH}, credits+logo group Y {y0}..{y0 + total}.", fill=(255, 255, 255))
img.save(out); print("logo", logo.width, "x", LH, "group Y", y0, y0 + total, "bottom X", lx + LH)
