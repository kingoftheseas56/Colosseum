#!/usr/bin/env python3
# squarify_addon_logo.py — reframe a wide addon logo into the square plate.
#
# AddonLogo.qml draws every mark inside a square rounded plate with
# Image.PreserveAspectFit, so a wide wordmark renders as a thin horizontal strip
# floating in the middle — visibly broken next to the square logos beside it.
# Three ALREADY-SHIPPED marks were like this (found 2026-07-25). Only two of them
# can ever actually appear, so only those two are reframed:
#
#   opensubtitles.png  602x151  ar 3.99   [circular monitor symbol][" Open Subtitles"]
#     -> SEEDED. org.stremio.opensubtitlesv3 is one of the four house extensions,
#        so this strip is on screen in the Installed pane today.
#   streailer.png      640x463  ar 1.38   neon popcorn art, centred, wide margins
#     -> CURATED. org.streailer.trailer is in ExtensionsCatalog.js's rails.
#
#   eztv.png           303x115  ar 2.63   NOT TOUCHED — Hemanth caught this: there is
#        no EZTV extension. It appears nowhere in ExtensionsCatalog.js and nowhere in
#        the seed; the file and its AddonLogos.js matcher are inherited from Harbor's
#        bundled array. It can only ever render if someone installs an eztv-named addon
#        by link. Reframing a logo nothing can show is busywork; if such an addon ever
#        lands, add it to PLAN below and re-run.
#
# Nothing here invents artwork. Each mode reframes the vendor's OWN image:
#
#   symbol-left  a logo built as [symbol][wordmark]: find the gap after the symbol
#                and keep just the symbol. This is the real fix when it applies —
#                you end up with the mark the vendor already uses as their icon.
#   center-crop  content centred with decoration bleeding off both sides: take the
#                largest centred square.
#   trim-pad     art already centred with transparent margins: trim to the content,
#                then pad to a square canvas so it centres in the plate.
#
# Everything is written back as RGBA PNG so transparency survives. Run from repo root:
#   python scripts/squarify_addon_logo.py                 # the three known wide marks
#   python scripts/squarify_addon_logo.py --check         # report only, change nothing
import os
import sys

from PIL import Image

ASSETS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "assets", "addon-logos")

# file -> mode. Modes chosen by eye from a dark-composited preview, not guessed.
# Only marks a user can actually see. See the eztv note above.
PLAN = {
    "opensubtitles.png": "symbol-left",
    "streailer.png":     "trim-pad",
}

GATE_LO, GATE_HI = 0.8, 1.25       # same window fetch_site_marks.py enforces
ALPHA_FLOOR = 16                   # below this a pixel counts as empty


def content_cols(alpha):
    """Per-column count of visible pixels."""
    w, h = alpha.size
    px = alpha.load()
    return [sum(1 for y in range(h) if px[x, y] > ALPHA_FLOOR) for x in range(w)]


def symbol_left_box(im):
    """
    [symbol][wordmark] -> the symbol's box. Walks columns from the content's left
    edge and stops at the first run of empty columns wide enough to be a real gap
    rather than the space inside a glyph. Returns None if no such gap exists (a
    solid wordmark like EZTV), so the caller can fall back.
    """
    w, h = im.size
    cols = content_cols(im.split()[3])
    bbox = im.split()[3].getbbox()
    if not bbox:
        return None
    left = bbox[0]
    gap_needed = max(3, int(w * 0.015))

    x, run, gap_start = left, 0, None
    while x < w:
        if cols[x] <= ALPHA_FLOOR // 8:
            if run == 0:
                gap_start = x
            run += 1
            if run >= gap_needed and gap_start > left:
                break
        else:
            run, gap_start = 0, None
        x += 1
    else:
        return None
    box = (left, bbox[1], gap_start, bbox[3])
    bw, bh = box[2] - box[0], box[3] - box[1]
    if bh == 0:
        return None
    ar = bw / bh
    # only accept a symbol that is itself roughly square — otherwise we'd just be
    # cropping the wordmark in half
    if not (0.6 <= ar <= 1.6):
        return None
    return box


def center_crop_box(im):
    w, h = im.size
    side = min(w, h)
    return ((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2)


def trim_pad(im):
    bbox = im.split()[3].getbbox() or (0, 0, *im.size)
    art = im.crop(bbox)
    side = max(art.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.alpha_composite(art, ((side - art.width) // 2, (side - art.height) // 2))
    return canvas


def ar_of(im):
    return im.width / im.height if im.height else 0


def process(fn, mode, dry):
    path = os.path.join(ASSETS, fn)
    if not os.path.exists(path):
        print(f"  {fn:22s} missing — skipped")
        return False
    im = Image.open(path).convert("RGBA")
    before = f"{im.width}x{im.height} ar={ar_of(im):.2f}"

    if GATE_LO <= ar_of(im) <= GATE_HI:
        print(f"  {fn:22s} {before} already square — left alone")
        return False

    used = mode
    if mode == "symbol-left":
        box = symbol_left_box(im)
        if box:
            out = im.crop(box)
        else:
            print(f"  {fn:22s} no symbol/wordmark gap found — falling back to trim-pad")
            out, used = trim_pad(im), "trim-pad (fallback)"
    elif mode == "center-crop":
        out = im.crop(center_crop_box(im))
    else:
        out = trim_pad(im)

    # a reframe that is still not square is not a fix; pad whatever is left
    if not (GATE_LO <= ar_of(out) <= GATE_HI):
        side = max(out.size)
        canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        canvas.alpha_composite(out, ((side - out.width) // 2, (side - out.height) // 2))
        out, used = canvas, used + " + pad"

    after = f"{out.width}x{out.height} ar={ar_of(out):.2f}"
    verb = "would write" if dry else "wrote"
    print(f"  {fn:22s} {before:20s} -> {after:20s} [{used}] {verb}")
    if not dry:
        out.save(path, "PNG")
    return True


if __name__ == "__main__":
    dry = "--check" in sys.argv
    print(f"{'Checking' if dry else 'Reframing'} wide addon logos in {ASSETS}\n")
    n = sum(process(f, m, dry) for f, m in PLAN.items())
    print(f"\n{n} reframed" if not dry else f"\n{n} would change")
