#!/usr/bin/env python3
"""Builds the UI font for Flaux VR: a signed-distance-field atlas (assets/ui/font.bin, raw 8-bit)
and its glyph table (src/font_data.h).  Text comes from Inter, icons from DejaVu Sans.
Run:  python3 tools/make_font.py   (needs Pillow, numpy, scipy)"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt as edt

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEXT_FONT = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"
ICON_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
EM, SS, SPREAD, W = 48, 4, 6, 1024

text_chars = [chr(c) for c in range(32, 127)] + list("×°•…−←→↑↓")
icon_chars = list("⌂✚☰◉⇄✦▤⚙↶↷✕❐✓⌫⏎⇧▲▼◀▶★●◆■☀♪✎⚑⟲☾")

def render(font, ch):
    f = ImageFont.truetype(font, EM * SS)
    l, t, r, b = f.getbbox(ch)
    adv = f.getlength(ch) / SS
    if r <= l or b <= t:
        return None, 0, 0, adv
    pad = SPREAD * SS
    w, h = r - l + 2 * pad, b - t + 2 * pad
    w += (-w) % SS; h += (-h) % SS
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad - l, pad - t), ch, font=f, fill=255)
    a = np.asarray(img, dtype=np.float32) / 255.0
    inside = a > 0.5
    d = edt(inside) - edt(~inside)
    d = d.reshape(h // SS, SS, w // SS, SS).mean(axis=(1, 3)) / SS
    sdf = np.clip(0.5 + d / (2 * SPREAD), 0, 1)
    return (sdf * 255 + 0.5).astype(np.uint8), l / SS - SPREAD, t / SS - SPREAD, adv

glyphs, x, y, rowh = [], 1, 1, 0
rows = []
for font, chars in ((TEXT_FONT, text_chars), (ICON_FONT, icon_chars)):
    probe = ImageFont.truetype(font, EM)
    for ch in chars:
        img, xo, yo, adv = render(font, ch)
        if img is None:
            glyphs.append((ord(ch), 0, 0, 0, 0, 0.0, 0.0, adv)); continue
        h, w = img.shape
        if x + w + 1 > W:
            x, y, rowh = 1, y + rowh + 1, 0
        rows.append((x, y, img))
        glyphs.append((ord(ch), x, y, w, h, xo, yo, adv))
        x += w + 1; rowh = max(rowh, h)
H = 1
while H < y + rowh + 1: H *= 2
atlas = np.zeros((H, W), np.uint8)
for gx, gy, img in rows:
    atlas[gy:gy + img.shape[0], gx:gx + img.shape[1]] = img
glyphs.sort()
open(os.path.join(ROOT, "assets/ui/font.bin"), "wb").write(atlas.tobytes())
asc, desc = ImageFont.truetype(TEXT_FONT, EM).getmetrics()
with open(os.path.join(ROOT, "src/font_data.h"), "w") as f:
    f.write("/* Made by tools/make_font.py - don't edit by hand. */\n")
    f.write(f"#define FONT_W {W}\n#define FONT_H {H}\n#define FONT_EM {EM}\n#define FONT_SPREAD {SPREAD}\n")
    f.write(f"#define FONT_ASCENT {asc}\n#define FONT_DESCENT {desc}\n#define FONT_COUNT {len(glyphs)}\n")
    f.write("typedef struct { unsigned cp; unsigned short x, y, w, h; float xo, yo, adv; } FontGlyph;\n")
    f.write("static const FontGlyph FONT_GLYPHS[FONT_COUNT] = {\n")
    for g in glyphs:
        f.write("  {%d,%d,%d,%d,%d,%.3ff,%.3ff,%.3ff},\n" % g)
    f.write("};\n")
print("atlas", W, H, "glyphs", len(glyphs), "ascent", asc, "descent", desc)
