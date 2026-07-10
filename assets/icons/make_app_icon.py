#!/usr/bin/env python
# Generate the Colosseum Windows app icon (assets/icons/colosseum.ico) from the
# amphitheatre glyph, on the app's dark rounded tile.
#
# Regenerate:  python assets/icons/make_app_icon.py   (run from the repo root)
#
# The glyph geometry is the exact path data from colosseum.svg, reconstructed as
# lines + elliptical arches so no SVG rasterizer is needed (we only have Pillow).
# Rendered 4x-supersampled, then downscaled with LANCZOS; Pillow packs the .ico.
from PIL import Image, ImageDraw

GOLD = (239, 193, 90, 255)     # #efc15a
TILE = (11, 13, 19, 255)       # #0b0d13
EDGE = (42, 45, 56, 255)       # #2a2d38

def build(px):
    ss = 4
    RS = px * ss
    s = RS / 256.0                      # scale vs the 256 design canvas
    img = Image.new("RGBA", (RS, RS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # dark rounded tile (10px margin, rx52, 2px edge — all on the 256 canvas)
    d.rounded_rectangle([10*s, 10*s, 246*s, 246*s], radius=52*s,
                        fill=TILE, outline=EDGE, width=max(1, round(2*s)))
    # glyph transform: translate(58,58) scale(2.92) on the 256 canvas
    def X(x): return s * (58 + x * 2.92)
    def Y(y): return s * (58 + y * 2.92)
    def W(w): return max(1, round(w * 2.92 * s))
    def cap(x, y, w):                   # round line cap
        r = w / 2.0
        d.ellipse([x-r, y-r, x+r, y+r], fill=GOLD)
    def hline(x0, x1, y, w):
        pw = W(w); d.line([X(x0), Y(y), X(x1), Y(y)], fill=GOLD, width=pw)
        cap(X(x0), Y(y), pw); cap(X(x1), Y(y), pw)
    def arch(cx, rx, ry, cy, base, w):
        pw = W(w)
        # top half-ellipse (shoulders at y=cy, peak at y=cy-ry)
        d.arc([X(cx-rx), Y(cy-ry), X(cx+rx), Y(cy+ry)], 180, 360, fill=GOLD, width=pw)
        # the two vertical legs down to the stage
        d.line([X(cx-rx), Y(cy), X(cx-rx), Y(base)], fill=GOLD, width=pw)
        d.line([X(cx+rx), Y(cy), X(cx+rx), Y(base)], fill=GOLD, width=pw)
        cap(X(cx-rx), Y(base), pw); cap(X(cx+rx), Y(base), pw)
    # nested arches (outer -> inner), then the two rails
    arch(24, 10.0, 9.8, 20.8, 32.5, 2.2)
    arch(24,  6.0, 5.7, 20.9, 32.5, 1.8)
    arch(24,  1.8, 2.0, 22.6, 32.5, 1.6)
    hline(10.0, 38.0, 32.5, 2.2)        # stage
    hline(8.5, 39.5, 37.0, 2.2)         # ground
    return img.resize((px, px), Image.LANCZOS)

master = build(256)
# Windows .ico: pack the standard sizes (Pillow downscales the master per size)
master.save("assets/icons/colosseum.ico",
            sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print("wrote assets/icons/colosseum.ico")
