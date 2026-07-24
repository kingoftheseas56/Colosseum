#!/usr/bin/env python3
"""Generate a decodable demo chapter for the Comic Reader first-render preview.

Produces 18 pages (page_000.png .. page_017.png) into ./pages/ in reading order:
  * 16 portrait pages (800x1200)
  * 2 landscape spreads (1600x1200) at indices 5 and 12, so double-page pairing,
    spread rendering, and the gutter shadow are all visible mid-chapter.

Each page is a solid-ish coloured background with a BIG page number and a couple of
panel rectangles, so pages are visually distinct and navigation is obvious. Solid-fill
PNGs compress tiny — the whole chapter lands well under ~1.5MB.

Run:  python tools/comicreader-preview/gen_pages.py
Needs Pillow (PIL). No network. Deterministic output.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "pages")

PORTRAIT = (800, 1200)
SPREAD = (1600, 1200)
SPREAD_INDICES = {5, 12}
TOTAL = 18

# A calm, distinct-per-page palette (deep inks with a warm accent), so flipping
# through the chapter reads as clearly different pages, not a wall of one colour.
BG = [
    "#1b2330", "#241b30", "#30231b", "#1b3026", "#2c1b30", "#1b2c30",
    "#302a1b", "#221b30", "#1b3021", "#301b26", "#1b2833", "#2f301b",
    "#261b30", "#1b3030", "#301b1f", "#1b2530", "#28301b", "#301b2c",
]
PANEL = "#0e1219"
STROKE = "#3a4657"
ACCENT = "#f0c44a"   # the reader's gold thread, echoed on the page for warmth
INK = "#e8ecf3"
DIM = "#8b93a3"


def _font(size):
    for name in ("segoeuib.ttf", "arialbd.ttf", "arial.ttf", "segoeui.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    try:
        return ImageFont.load_default(size=size)   # Pillow >= 10 scales the default
    except TypeError:
        return ImageFont.load_default()


def _centered(draw, cx, cy, text, font, fill):
    l, t, r, b = draw.textbbox((0, 0), text, font=font)
    draw.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), text, font=font, fill=fill)


def portrait(idx):
    w, h = PORTRAIT
    img = Image.new("RGB", (w, h), BG[idx % len(BG)])
    d = ImageDraw.Draw(img)
    m = 40
    d.rectangle([m, m, w - m, h - m], outline=STROKE, width=3)
    # top panel
    d.rectangle([m + 24, m + 24, w - m - 24, 300], fill=PANEL, outline=STROKE, width=2)
    _centered(d, w / 2, 190, "PORTRAIT", _font(34), DIM)
    # big page number band
    d.rectangle([m + 24, 340, w - m - 24, 820], fill=PANEL, outline=STROKE, width=2)
    _centered(d, w / 2, 560, str(idx), _font(300), INK)
    _centered(d, w / 2, 760, "page %02d / %d" % (idx, TOTAL - 1), _font(28), DIM)
    # two bottom panels
    d.rectangle([m + 24, 860, (w / 2) - 8, h - m - 24], fill=PANEL, outline=STROKE, width=2)
    d.rectangle([(w / 2) + 8, 860, w - m - 24, h - m - 24], fill=PANEL, outline=STROKE, width=2)
    d.rectangle([m + 24, 860, m + 24 + 10, h - m - 24], fill=ACCENT)   # gold spine sliver
    return img


def spread(idx):
    w, h = SPREAD
    img = Image.new("RGB", (w, h), BG[idx % len(BG)])
    d = ImageDraw.Draw(img)
    m = 44
    d.rectangle([m, m, w - m, h - m], outline=STROKE, width=3)
    # one wide cinematic panel across the whole spread
    d.rectangle([m + 30, m + 30, w - m - 30, h - m - 30], fill=PANEL, outline=STROKE, width=2)
    _centered(d, w / 2, 300, "DOUBLE-PAGE SPREAD", _font(60), ACCENT)
    _centered(d, w / 2, 620, str(idx), _font(320), INK)
    _centered(d, w / 2, 880, "landscape page %02d / %d" % (idx, TOTAL - 1), _font(34), DIM)
    # faint centre seam so the spread reads as one wide image
    d.line([(w / 2, m + 30), (w / 2, h - m - 30)], fill=STROKE, width=1)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    total_bytes = 0
    for i in range(TOTAL):
        img = spread(i) if i in SPREAD_INDICES else portrait(i)
        path = os.path.join(OUT, "page_%03d.png" % i)
        img.save(path, "PNG", optimize=True)
        sz = os.path.getsize(path)
        total_bytes += sz
        kind = "SPREAD  " if i in SPREAD_INDICES else "portrait"
        print("  page_%03d.png  %s  %6d bytes" % (i, kind, sz))
    print("TOTAL %d pages, %.1f KB" % (TOTAL, total_bytes / 1024.0))


if __name__ == "__main__":
    main()
