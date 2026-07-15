#!/usr/bin/env python3
# Live-source assertion gate for the keyless anime ordering/merge stack
# (spec 2026-07-15). It fetches the EXACT two datasets the app fetches at runtime
# and asserts the identity + mapping facts the deterministic fixtures encode, for
# the approved long-runners. It does NOT call XEM and does NOT count a
# <mapping-list> element as a mapping node — it parses real <mapping> children
# with xml.etree.ElementTree. Any failed show/field exits non-zero.
#
# This is an OPT-IN gate (it hits the network). The deterministic native and QML
# harnesses are the source of truth; this only guards against an upstream schema
# or mapping drift. If it fails, STOP and inspect the live source — never weaken
# the fixtures to follow a broken upstream.
import json
import sys
import urllib.request
import xml.etree.ElementTree as ET

# The exact runtime sources (must match AnimeOrderService's production URLs).
FRIBB = "https://raw.githubusercontent.com/Fribb/anime-lists/master/anime-list-mini.json"
MAPPINGS = "https://raw.githubusercontent.com/Anime-Lists/anime-lists/master/anime-list-master.xml"

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumAnimeProbe/2.0"

# MAL id -> the identity + mapping facts the fixtures assert.
EXPECTED = {
    21:   {"name": "One Piece",         "anidb": 69,   "tvdb": 81797},
    918:  {"name": "Gintama (2006)",    "anidb": 3468, "tvdb": 79895},
    20:   {"name": "Naruto",            "anidb": 239,  "tvdb": 78857},
    1735: {"name": "Naruto: Shippuden", "anidb": 4880, "tvdb": 79824},
}


def fetch(url, timeout=90):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def load_fribb():
    print(f"=== Fribb identity dataset ===\n  {FRIBB}")
    data = json.loads(fetch(FRIBB))
    by_mal = {}
    for entry in data:
        mal = entry.get("mal_id")
        if mal is not None:
            try:
                by_mal[int(mal)] = entry
            except (TypeError, ValueError):
                pass
    print(f"  {len(data)} entries, {len(by_mal)} with MAL ids\n")
    return by_mal


def load_mappings():
    print(f"=== Anime-Lists mapping dataset ===\n  {MAPPINGS}")
    raw = fetch(MAPPINGS)
    root = ET.fromstring(raw)
    if root.tag != "anime-list":
        raise SystemExit(f"FAIL: mapping xml root is <{root.tag}>, expected <anime-list>")
    by_anidb = {}
    for anime in root.findall("anime"):
        aid = anime.get("anidbid")
        if not aid:
            continue
        # Count REAL regular <mapping> children (anidbseason != 0), not the
        # single <mapping-list> wrapper the old regex miscounted.
        regular = 0
        mapping_list = anime.find("mapping-list")
        if mapping_list is not None:
            for mapping in mapping_list.findall("mapping"):
                if mapping.get("anidbseason") != "0":
                    regular += 1
        try:
            key = int(aid)
        except ValueError:
            continue
        by_anidb[key] = {
            "tvdb": anime.get("tvdbid"),
            "default": anime.get("defaulttvdbseason"),
            "offset": anime.get("episodeoffset"),
            "regular_mappings": regular,
        }
    print(f"  {len(by_anidb)} <anime> entries parsed\n")
    return by_anidb


def main():
    by_mal = load_fribb()
    by_anidb = load_mappings()

    failures = []
    for mal, exp in EXPECTED.items():
        name = exp["name"]
        entry = by_mal.get(mal)
        if not entry:
            failures.append(f"{name} (MAL {mal}): not present in Fribb mini")
            print(f"FAIL {name}: not in Fribb mini")
            continue
        anidb = entry.get("anidb_id")
        if anidb != exp["anidb"]:
            failures.append(f"{name}: Fribb anidb {anidb} != expected {exp['anidb']}")
            print(f"FAIL {name}: Fribb anidb {anidb} != {exp['anidb']}")
            continue
        mapping = by_anidb.get(anidb)
        if not mapping:
            failures.append(f"{name}: no Anime-Lists entry for anidb {anidb}")
            print(f"FAIL {name}: no Anime-Lists entry for anidb {anidb}")
            continue

        show_fail = []
        if str(mapping["tvdb"]) != str(exp["tvdb"]):
            show_fail.append(f"tvdb {mapping['tvdb']} != {exp['tvdb']}")
        if mapping["default"] != "a":
            show_fail.append(f"defaulttvdbseason {mapping['default']!r} != 'a'")
        if mapping["regular_mappings"] < 1:
            show_fail.append("no regular <mapping> path")

        if show_fail:
            for f in show_fail:
                failures.append(f"{name}: {f}")
            print(f"FAIL {name}: " + "; ".join(show_fail))
        else:
            print(f"PASS {name}: mal {mal} -> anidb {anidb} -> tvdb {mapping['tvdb']} "
                  f"(default 'a', {mapping['regular_mappings']} regular mappings)")

    print()
    if failures:
        print(f"=== {len(failures)} assertion(s) FAILED ===")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print("=== all live long-runner assertions passed ===")
    sys.exit(0)


if __name__ == "__main__":
    main()
