#!/usr/bin/env python3
# One-shot helper: pull the real logo for each curated add-on that Harbor does
# not already bundle. Reads each add-on's manifest.json, takes its `logo` URL,
# downloads the icon into assets/addon-logos/<slug>.<ext>. Whatever fails to
# fetch simply keeps the letter fallback in the UI — honest, never a fake tile.
import json, os, urllib.request, urllib.parse

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "assets", "addon-logos")
os.makedirs(OUT, exist_ok=True)

# slug -> manifest.json  (slug is what AddonLogos.js will match to)
TARGETS = {
    "cinemeta":        "https://v3-cinemeta.strem.io/manifest.json",
    "peerflix":        "https://peerflix.mov/manifest.json",
    "notorrent":       "https://addon.notorrent2.workers.dev/manifest.json",
    "webstreamr":      "https://87d6a6ef6b58-webstreamrmbg.baby-beamup.club/manifest.json",
    "netflix-catalog": "https://7a82163c306e-stremio-netflix-catalog-addon.baby-beamup.club/manifest.json",
    "flixpatrol":      "https://top-streaming.stream/username=temporary_username/manifest.json",
    "aiolists":        "https://aiolists.elfhosted.com/manifest.json",
    "marvel":          "https://addon-marvel.onrender.com/manifest.json",
    "dc":              "https://addon-dc-cq85.onrender.com/manifest.json",
    "morelikethis":    "https://bbab4a35b833-more-like-this.baby-beamup.club/manifest.json",
    "subsource":       "https://subsource.strem.top/manifest.json",
    "subdl":           "https://subdl.strem.top/manifest.json",
    "ratings":         "https://72059fbbd1e5-stremio-addon-ratings.baby-beamup.club/manifest.json",
    "streailer":       "https://streailer.elfhosted.com/manifest.json",
    "meteor":          "https://meteorfortheweebs.midnightignite.me/stremio/manifest.json",
    "usatv":           "https://848b3516657c-usatv.baby-beamup.club/manifest.json",
}

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumStore/1.0"

def get(url, binary=False, timeout=20):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = r.read()
        return data if binary else data.decode("utf-8", "replace")

def ext_for(url, ctype):
    path = urllib.parse.urlparse(url).path.lower()
    for e in (".png", ".jpg", ".jpeg", ".webp", ".svg", ".ico", ".gif"):
        if path.endswith(e):
            return ".jpg" if e == ".jpeg" else e
    ct = (ctype or "").lower()
    if "svg" in ct: return ".svg"
    if "png" in ct: return ".png"
    if "webp" in ct: return ".webp"
    if "jpeg" in ct or "jpg" in ct: return ".jpg"
    if "gif" in ct: return ".gif"
    if "icon" in ct or "ico" in ct: return ".ico"
    return ".png"

ok, miss = [], []
for slug, murl in TARGETS.items():
    try:
        man = json.loads(get(murl))
    except Exception as e:
        miss.append(f"{slug}: manifest FAILED ({e.__class__.__name__}: {e})")
        continue
    logo = (man.get("logo") or "").strip()
    if not logo:
        miss.append(f"{slug}: manifest has no logo field")
        continue
    if logo.startswith("//"):
        logo = "https:" + logo
    if not logo.startswith(("http://", "https://", "data:")):
        logo = urllib.parse.urljoin(murl, logo)
    if logo.startswith("data:"):
        miss.append(f"{slug}: logo is a data: URI (skipped — inline)")
        continue
    try:
        req = urllib.request.Request(logo, headers={"User-Agent": UA, "Accept": "image/*,*/*"})
        with urllib.request.urlopen(req, timeout=20) as r:
            ctype = r.headers.get("Content-Type", "")
            blob = r.read()
        ext = ext_for(logo, ctype)
        dest = os.path.join(OUT, slug + ext)
        with open(dest, "wb") as f:
            f.write(blob)
        ok.append(f"{slug}{ext}  <- {logo}  ({len(blob)} bytes)")
    except Exception as e:
        miss.append(f"{slug}: logo download FAILED ({e.__class__.__name__}: {e}) [{logo}]")

print("=== FETCHED ({}) ===".format(len(ok)))
for l in ok: print("  " + l)
print("=== MISSED ({}) ===".format(len(miss)))
for l in miss: print("  " + l)
