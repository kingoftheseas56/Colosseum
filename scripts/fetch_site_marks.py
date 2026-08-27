#!/usr/bin/env python3
# fetch_site_marks.py — sibling of fetch_addon_logos.py, for HOUSE WELLS.
#
# fetch_addon_logos.py reads a Stremio addon's manifest.json and takes its `logo`
# URL. Our own wells (WeebCentral, GetComics, LibGen, AudioBookBay, the torrent
# indexers) are in-app code, not hosted addons — there is no manifest and no `logo`
# field. But every one of these sites has its own iconography, and that mark is what
# belongs on the addon tile, exactly as Theatre's community addons show theirs.
#
# So this walks the site itself: apple-touch-icon (usually the largest and cleanest)
# → <link rel=icon> → og:image → /favicon.ico. Whatever fails keeps the letter-square
# fallback in AddonLogo.qml — honest, never a fake tile. Same rule as the sibling.
#
# Bundling (rather than hot-linking) is deliberate: several of these domains rotate
# (LibGen key rotation, AudioBookBay domain hopping), so a committed icon keeps
# working when the site moves or is unreachable.
#
# Run from the repo root:  python scripts/fetch_site_marks.py
import html as htmllib
import os, re, struct, sys, urllib.request, urllib.parse

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "assets", "addon-logos")
os.makedirs(OUT, exist_ok=True)

# slug -> homepage. Slug is what AddonLogos.js matches on.
TARGETS = {
    "weebcentral":   "https://weebcentral.com",
    "getcomics":     "https://getcomics.org",
    "libgen":        "https://libgen.li",
    "audiobookbay":  "https://audiobookbay.lu",
    "piratebay":     "https://thepiratebay.org",
    "exttorrents":   "https://extto.org",
    "torrentscsv":   "https://torrents-csv.com",
    "knaben":        "https://knaben.org",
    "applebooks":    "https://www.apple.com/apple-books/",
    # A house CATALOGUE, not a well — the manga search and series artwork behind
    # Tankoban. Same rule applies: it is a real site with real iconography, so it gets
    # its own mark rather than a letter square. (Hemanth, 2026-07-26.)
    "anilist":       "https://anilist.co",
}

UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

IMAGE_MAGIC = [
    (b"\x89PNG\r\n\x1a\n", ".png"),
    (b"\xff\xd8\xff",      ".jpg"),
    (b"GIF8",              ".gif"),
    (b"RIFF",              ".webp"),   # RIFF....WEBP
    (b"\x00\x00\x01\x00",  ".ico"),
    (b"<svg",              ".svg"),
    (b"<?xml",             ".svg"),
]


def get(url, timeout=20):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read(), r.headers.get("Content-Type", "")


def sniff(data):
    """Real extension from magic bytes. None = not an image (an HTML error page)."""
    head = data[:64].lstrip()
    for magic, ext in IMAGE_MAGIC:
        if head.startswith(magic):
            if ext == ".webp" and b"WEBP" not in data[:16]:
                continue
            return ext
    if b"<svg" in head[:200].lower():
        return ".svg"
    return None


def aspect(data, ext):
    """(w, h) for the formats we can read headerlessly. None = unknown, allow it."""
    try:
        if ext == ".png" and data[:8] == b"\x89PNG\r\n\x1a\n":
            return struct.unpack(">II", data[16:24])
        if ext == ".ico" and data[:4] == b"\x00\x00\x01\x00":
            return (data[6] or 256), (data[7] or 256)
    except Exception:
        # Unreadable image metadata falls back to unknown dimensions.
        pass
    return None


def square_enough(data, ext):
    """
    Reject wide images. og:image is very often a 1200x630 MARKETING BANNER, not a
    mark — Apple's Books page serves exactly that. AddonLogo.qml uses
    PreserveAspectFit inside a square plate, so a banner renders as a thin letterboxed
    strip: worse than the honest letter square. (Caught in review 2026-07-25.)
    """
    wh = aspect(data, ext)
    if not wh:
        return True, ""
    w, h = wh
    if h == 0:
        return False, "zero height"
    ar = w / h
    if 0.8 <= ar <= 1.25:
        return True, f"{w}x{h}"
    return False, f"{w}x{h} ar={ar:.2f} — a banner, not a mark"


def candidates(html, base):
    """Icon URLs from the page, best-guess first."""
    found = []

    def add(u, score):
        if not u:
            return
        # Unescape FIRST. WordPress sites emit icon hrefs with &#038; for & (GetComics
        # does exactly this on its i0.wp.com URLs), and fetching the raw href 404s.
        u = htmllib.unescape(u.strip())
        found.append((score, urllib.parse.urljoin(base, u)))

    # apple-touch-icon — normally 180px+ and already square/padded
    for m in re.finditer(r'<link[^>]+rel=["\']?[^"\'>]*apple-touch-icon[^"\'>]*["\']?[^>]*>',
                         html, re.I):
        tag = m.group(0)
        href = re.search(r'href=["\']([^"\']+)', tag, re.I)
        size = re.search(r'sizes=["\']?(\d+)', tag, re.I)
        if href:
            add(href.group(1), 100 + (int(size.group(1)) if size else 0))

    # explicit icon links, biggest declared size wins
    for m in re.finditer(r'<link[^>]+rel=["\']?(?:shortcut )?icon["\']?[^>]*>', html, re.I):
        tag = m.group(0)
        href = re.search(r'href=["\']([^"\']+)', tag, re.I)
        size = re.search(r'sizes=["\']?(\d+)', tag, re.I)
        if href:
            add(href.group(1), 50 + (int(size.group(1)) if size else 0))

    # og:image — often the site's wordmark
    for m in re.finditer(r'<meta[^>]+(?:property|name)=["\']og:image["\'][^>]*>', html, re.I):
        href = re.search(r'content=["\']([^"\']+)', m.group(0), re.I)
        if href:
            add(href.group(1), 30)

    # Last resort: an <img> the page itself calls a logo. Often a wide wordmark rather
    # than a mark, so it scores below every declared icon and still has to clear
    # square_enough() — ExtTorrents publishes only this (its apple-icon.png 404s).
    for m in re.finditer(r'<img[^>]+>', html, re.I):
        tag = m.group(0)
        if not re.search(r'(logo|brand|site-?icon)', tag, re.I):
            continue
        src = re.search(r'src=["\']([^"\']+)', tag, re.I)
        if src:
            add(src.group(1), 10)

    # Jetpack CDN unwrap: WordPress sites (GetComics) point icons at
    # i0.wp.com/<origin-host>/<path>, which 404s for the long tail — the same host
    # A0 already found unreliable for poster pinning (2026-07-25). The origin serves
    # the file fine, so try the unwrapped URL right after the CDN one.
    for score, u in list(found):
        m = re.match(r'https?://i\d+\.wp\.com/(.+)', u, re.I)
        if m:
            origin = "https://" + m.group(1).split("?", 1)[0]
            found.append((score - 1, origin))

    found.sort(key=lambda t: -t[0])
    seen, ordered = set(), []
    for _, u in found:
        if u not in seen:
            seen.add(u)
            ordered.append(u)
    return ordered


def existing(slug):
    for f in os.listdir(OUT):
        if os.path.splitext(f)[0] == slug:
            return f
    return None


def fetch(slug, home):
    have = existing(slug)
    if have:
        print(f"  {slug:14s} already bundled as {have} — left alone")
        return "kept"

    urls = []
    try:
        html, _ = get(home)
        urls = candidates(html.decode("utf-8", "replace"), home)
    except Exception as e:
        print(f"  {slug:14s} homepage unreadable ({type(e).__name__}) — trying /favicon.ico")
    urls.append(urllib.parse.urljoin(home, "/favicon.ico"))

    for u in urls[:6]:
        try:
            data, _ = get(u)
        except Exception as e:
            continue
        if len(data) < 100:
            continue
        ext = sniff(data)
        if not ext:
            continue                      # an HTML error page wearing an image URL
        ok, note = square_enough(data, ext)
        if not ok:
            print(f"  {slug:14s} skip {note}")
            continue
        path = os.path.join(OUT, slug + ext)
        with open(path, "wb") as f:
            f.write(data)
        print(f"  {slug:14s} OK  {len(data):>7,d}b  {ext:5s} {note:9s} <- {u}")
        return "ok"

    print(f"  {slug:14s} NO MARK FOUND — keeps the letter square (honest, not a fake tile)")
    return "fail"


if __name__ == "__main__":
    only = sys.argv[1:] or list(TARGETS)
    print(f"Fetching site marks into {OUT}\n")
    tally = {"ok": 0, "kept": 0, "fail": 0}
    for slug in only:
        if slug not in TARGETS:
            print(f"  {slug}: unknown slug")
            continue
        tally[fetch(slug, TARGETS[slug])] += 1
    print(f"\n{tally['ok']} fetched · {tally['kept']} already bundled · {tally['fail']} keeping the letter square")
