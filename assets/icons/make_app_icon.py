#!/usr/bin/env python
# Generate the Colosseum Windows app icon (assets/icons/colosseum.ico) from the
# amphitheatre glyph — full-bleed, no tile, like every other taskbar icon
# (Hemanth's call, 2026-07-10: "look like how all the other icons there look").
#
# Regenerate:  python assets/icons/make_app_icon.py   (run from the repo root)
#
# Geometry is the exact path data from colosseum.svg. Strokes are rendered by
# STAMPING round brushes densely along the parametric path — perfectly centered
# strokes with round caps, unlike ImageDraw.arc whose width grows inward and
# kinked the arch shoulders (the "crooked" v1). 4x supersampled, LANCZOS down.
import math
from PIL import Image, ImageDraw

GOLD = (239, 193, 90, 255)     # #efc15a — same gold as the in-app glyph

# glyph coordinate space: the 48x48 viewBox of colosseum.svg, centered on (24,24)
# bbox incl. stroke: x 7.5..40.5 (33 wide) — width-limited when filling the canvas
FILL = 0.92                    # glyph bbox fills 92% of the canvas, like neighbors

def build(px):
    ss = 4
    RS = px * ss
    S = (px * FILL / 33.0) * ss          # glyph-unit -> supersampled-px scale
    img = Image.new("RGBA", (RS, RS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def T(x, y):                          # glyph coords -> canvas px, centered
        return ((x - 24.0) * S + RS / 2.0, (y - 24.0) * S + RS / 2.0)

    def stamp(points, w):                 # centered stroke: dense round brushes
        r = (w * S) / 2.0
        for (x, y) in points:
            cx, cy = T(x, y)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GOLD)

    def line_pts(x0, y0, x1, y1):
        n = max(2, int(math.hypot((x1 - x0) * S, (y1 - y0) * S)))   # ~1px steps
        return [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n + 1)]

    def arch_pts(cx, rx, ry, cy, base):
        # leg up, top half-ellipse (180..360 deg), leg down — one continuous path
        pts = line_pts(cx - rx, base, cx - rx, cy)
        n = max(24, int(math.pi * max(rx, ry) * S))
        for i in range(n + 1):
            th = math.radians(180 + 180.0 * i / n)
            pts.append((cx + rx * math.cos(th), cy + ry * math.sin(th)))
        pts += line_pts(cx + rx, cy, cx + rx, base)
        return pts

    # the five strokes of colosseum.svg (outer arch -> inner arch, stage, ground)
    stamp(arch_pts(24, 10.0, 9.8, 20.8, 32.5), 2.0)
    stamp(arch_pts(24,  6.0, 5.7, 20.9, 32.5), 1.6)
    stamp(arch_pts(24,  1.8, 2.0, 22.6, 32.5), 1.4)
    stamp(line_pts(10.0, 32.5, 38.0, 32.5), 2.0)
    stamp(line_pts(8.5, 37.0, 39.5, 37.0), 2.0)
    return img.resize((px, px), Image.LANCZOS)

master = build(256)
# Windows .ico: pack the standard sizes (Pillow downscales the master per size)
master.save("assets/icons/colosseum.ico",
            sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print("wrote assets/icons/colosseum.ico")
