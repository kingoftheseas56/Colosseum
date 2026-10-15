#!/usr/bin/env python3
"""Task 0 probe — do manga source searches ever return multi-volume packs?

Mirrors the engine EXACTLY so the answer describes what Colosseum really sees:
  - queryVariants()  native/torrent/MangaNyaaSource.cpp:195-219
  - the RSS endpoint native/torrent/MangaNyaaSource.cpp:391-400 (c=3_1, s=seeders)
  - detectCoverage() native/torrent/MangaNyaaSource.cpp:71-95
  - coverageIncludesTarget() / isChapterPack() reject rules

Builds nothing. Prints a table and writes the raw rows for the findings note.
"""
import json
import re
import sys
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

ENDPOINT = "https://nyaa.si/"
NS = {"nyaa": "https://nyaa.si/xmlns/nyaa"}

# ── detectCoverage(), ported verbatim from the C++ regexes ──────────────────
RANGE_RE = re.compile(
    r"(?:\bv|\bvol\.?|\bvolumes?)\s*0*([0-9]+)\s*-\s*(?:v|vol\.?|volume)?\s*0*([0-9]+)",
    re.IGNORECASE)
SINGLE_RE = re.compile(r"(?:\bv|\bvol\.?\s*|\bvolume\s*)0*([0-9]+)", re.IGNORECASE)
CHAPTER_RE = re.compile(r"\b(?:chapters?|ch\.?)\s*0*[0-9]+", re.IGNORECASE)


def detect_coverage(title):
    m = RANGE_RE.search(title)
    if m:
        return str(int(m.group(1))), str(int(m.group(2)))
    m = SINGLE_RE.search(title)
    if m:
        n = str(int(m.group(1)))
        return n, n
    return "", ""


def covers(lo, hi, target):
    if not lo or not hi:
        return False
    try:
        return int(lo) <= int(target) <= int(hi)
    except ValueError:
        return False


def query_variants(title, n):
    """native/torrent/MangaNyaaSource.cpp:195-219"""
    out = []
    for q in ("%s %d" % (title, n), "%s %02d" % (title, n),
              "%s %03d" % (title, n), "%s Vol %d" % (title, n), title):
        q = " ".join(q.split())
        if q and q not in out:
            out.append(q)
    return out


def fetch(query):
    url = ENDPOINT + "?" + urllib.parse.urlencode(
        {"page": "rss", "c": "3_1", "s": "seeders", "o": "desc", "q": query})
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def rows_for(title, volume):
    """Union every query variant by infoHash, exactly as the engine does."""
    seen = {}
    for q in query_variants(title, volume):
        try:
            root = ET.fromstring(fetch(q))
        except Exception as exc:                      # noqa: BLE001
            print("    ! query %r failed: %s" % (q, exc))
            continue
        for item in root.iter("item"):
            ih = item.findtext("nyaa:infoHash", "", NS)
            if not ih or ih in seen:
                continue
            seen[ih] = {
                "title": item.findtext("title", ""),
                "size": item.findtext("nyaa:size", "", NS),
                "seeders": int(item.findtext("nyaa:seeders", "0", NS) or 0),
                "trusted": item.findtext("nyaa:trusted", "No", NS),
            }
        time.sleep(1.5)                               # polite pacing
    return seen


SERIES = [
    ("One Piece", 35), ("Bleach", 5), ("Naruto", 20), ("Vagabond", 10),
    ("My Hero Academia", 12), ("Death Note", 3), ("Fullmetal Alchemist", 7),
    ("20th Century Boys", 10),
]


def main():
    report = []
    for title, vol in SERIES:
        print("\n=== %s — target volume %d ===" % (title, vol))
        rows = rows_for(title, vol)
        packs, singles, nocov = [], [], []
        for ih, r in rows.items():
            lo, hi = detect_coverage(r["title"])
            # engine: chapter-pack reject applies ONLY when coverage is empty
            if not lo and CHAPTER_RE.search(r["title"]):
                continue
            if not lo:
                nocov.append(r)
            elif lo != hi:
                packs.append((lo, hi, r, covers(lo, hi, vol)))
            else:
                singles.append((lo, r, lo == str(vol)))

        on_target_packs = [p for p in packs if p[3]]
        on_target_singles = [s for s in singles if s[2]]
        print("  rows=%d  packs=%d (on-target %d)  singles=%d (on-target %d)  no-coverage=%d"
              % (len(rows), len(packs), len(on_target_packs),
                 len(singles), len(on_target_singles), len(nocov)))
        for lo, hi, r, ok in packs[:6]:
            print("    PACK  v%s-v%s %-9s S=%-4d %s %s"
                  % (lo, hi, r["size"], r["seeders"],
                     "COVERS" if ok else "misses", r["title"][:78]))
        for lo, r, ok in on_target_singles[:3]:
            print("    ONE   v%-3s %-9s S=%-4d %s" % (lo, r["size"], r["seeders"], r["title"][:78]))

        report.append({
            "series": title, "targetVolume": vol, "totalRows": len(rows),
            "packs": len(packs), "packsCoveringTarget": len(on_target_packs),
            "singles": len(singles), "singlesOnTarget": len(on_target_singles),
            "noCoverage": len(nocov),
            "packTitles": [{"lo": lo, "hi": hi, "size": r["size"],
                            "seeders": r["seeders"], "coversTarget": ok,
                            "title": r["title"]} for lo, hi, r, ok in packs],
        })

    with open("tests/manga_volume_pack_probe.json", "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print("\n\n=== SUMMARY ===")
    print("%-22s %6s %6s %8s %8s" % ("series", "rows", "packs", "on-tgt", "singles"))
    for r in report:
        print("%-22s %6d %6d %8d %8d" % (r["series"], r["totalRows"], r["packs"],
                                         r["packsCoveringTarget"], r["singlesOnTarget"]))
    with_packs = sum(1 for r in report if r["packsCoveringTarget"] > 0)
    print("\n%d of %d series returned at least one pack COVERING the target volume."
          % (with_packs, len(report)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
