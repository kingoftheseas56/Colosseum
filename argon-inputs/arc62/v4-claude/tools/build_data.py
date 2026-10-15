"""Normalize raw YouTube Music + MusicBrainz pulls into the artist-page data file, and download artwork.
Usage: build_data.py <rawdir> <outdir>
Writes <outdir>/data/artists.js (page data) and <outdir>/catalogue-evidence.json (full manifest).
"""
import json, sys, re, hashlib, pathlib, shutil, unicodedata, urllib.request, datetime

RAW, OUT = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
ARTISTS = [("eminem", "Rich catalogue"), ("lifafa", "Sparse catalogue"), ("easywanderlings", "No eligible artist image")]
HERO_MIN_W, HERO_MIN_ASPECT = 1440, 1.6
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumVinylPrototype/0.1"}
provenance = []

def download(url, rel, kind, entity):
    dest = OUT / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=40) as r:
            dest.write_bytes(r.read())
    b = dest.read_bytes()
    provenance.append({"file": rel, "kind": kind, "entity": entity, "sourceUrl": url, "bytes": len(b),
                       "sha256": hashlib.sha256(b).hexdigest(),
                       "rights": "Provider-returned image. Reference use in this prototype only; not rights-cleared for production (see ARTIST-ARTWORK-SOURCE-STRATEGY)."})
    return rel

def lh3(url, spec):
    return re.sub(r"=w\d+-h\d+[^/]*$", "=" + spec, url) if "googleusercontent" in url else url

def biggest(thumbs):
    return max(thumbs or [{}], key=lambda t: t.get("width", 0) or 0)

def fold(s):
    s = unicodedata.normalize("NFKD", s.replace("\u2019", "'").replace("\u2010", "-"))
    return "".join(c for c in s if not unicodedata.combining(c)).lower()

def key1(t):  # drop bracketed edition text and punctuation
    return re.sub(r"[^a-z0-9\u0900-\u097f]", "", re.sub(r"\s*[\(\[].*?[\)\]]", "", fold(t)))

def key2(t):  # additionally drop ": subtitle" / " - subtitle" edition tails
    base = re.sub(r"\s*[\(\[].*?[\)\]]", "", fold(t))
    base = re.split(r"\s+[-\u2013]\s+|:\s", base)[0]
    return re.sub(r"[^a-z0-9\u0900-\u097f]", "", base)

def key3(t):  # Latin letters/digits only: matches bilingual titles such as "Jaago जागो" to "Jaago"
    return re.sub(r"[^a-z0-9]", "", key2(t))

SKIP_SECONDARY = {"Remix", "DJ-mix", "Mixtape/Street", "Interview", "Demo", "Spokenword"}

def mb_match(title, primaries, groups):
    cands = [g for g in groups if g["primary"] in primaries and not (set(g["secondary"]) & SKIP_SECONDARY)]
    for kf in (key1, key2, key3):
        if not kf(title): continue
        hits = [g for g in cands if kf(g["title"]) == kf(title)]
        if hits:
            return sorted(hits, key=lambda g: g["first"] or "9999")[0], kf.__name__
    return None, None

def category(g, ytype):
    if not g:
        return "other"
    sec = set(g["secondary"])
    if g["primary"] in ("Single", "EP"):
        return "singles"
    if "Soundtrack" in sec: return "soundtracks"
    if "Compilation" in sec: return "compilations"
    if "Live" in sec: return "live"
    return "albums"

EDITION = r"(edition|anniversary|deluxe|expanded|remaster(ed)?)"
def display_title(t):  # provider title of the representative edition, minus edition qualifiers
    t = re.sub(r"\s*[\(\[][^\)\]]*" + EDITION + r"[^\)\]]*[\)\]]", "", t, flags=re.I)
    t = re.sub(r"\s*(:|\s-)\s[^:]*" + EDITION + r".*$", "", t, flags=re.I)
    return t.strip()

SHELF_TITLES = {"albums": "Albums", "singles": "Singles & EPs", "compilations": "Compilations",
                "soundtracks": "Soundtracks", "live": "Live albums", "other": "Other releases"}
SHELF_ORDER = ["albums", "singles", "compilations", "soundtracks", "live", "other"]

def secs(d):
    if not d: return None
    p = [int(x) for x in d.split(":")]
    return sum(v * 60 ** i for i, v in enumerate(reversed(p)))

page_artists, evidence = {}, {"generatedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                              "primaryProvider": "YouTube Music (ytmusicapi 1.12.3, unauthenticated, location=IN)",
                              "identityProvider": "MusicBrainz web service v2", "heroRule": f"artist header image is used only when its largest provider size is >= {HERO_MIN_W}px wide and >= {HERO_MIN_ASPECT}:1; otherwise the page uses the current Colosseum wallpaper", "artists": {}}

for slug, state in ARTISTS:
    cat = json.loads((RAW / slug / "catalogue.json").read_text(encoding="utf-8"))
    flog = json.loads((RAW / slug / "fetchlog.json").read_text(encoding="utf-8"))
    mb = json.loads((RAW / slug / "mb.json").read_text(encoding="utf-8"))
    a = cat["artist"]; groups = mb["releaseGroups"]; cid = a["channelId"]
    ev = {"state": state, "name": a["name"], "channelId": cid, "sourceUrl": f"https://music.youtube.com/channel/{cid}",
          "fetchedAt": flog["fetchedAt"], "calls": flog["calls"], "musicbrainz": {"artistMbid": mb["mbid"], "releaseGroupsBrowsed": len(groups),
          "matchedBy": "MBID browse" if "search" not in mb else "name search, top score 100 (verify)"}}

    # hero eligibility
    big = biggest(a.get("thumbnails"))
    w, h = big.get("width", 0), big.get("height", 1)
    eligible = w >= HERO_MIN_W and w / h >= HERO_MIN_ASPECT
    hero = None
    if eligible:
        hero = download(lh3(big["url"], "w1920-h800-p-l90-rj"), f"assets/{slug}/hero.jpg", "artist-hero", cid)
    ev["hero"] = {"largestProviderSize": [w, h], "eligible": eligible, "file": hero,
                  "reason": "meets size rule; rights review still pending" if eligible else f"largest provider image is {w}x{h}, below the {HERO_MIN_W}px rule -> current wallpaper"}

    # popular songs: artist shelf order, continued by the provider's 'all songs' playlist
    shelf = a.get("songs", {}).get("results", [])
    pl = (cat.get("songsPlaylist") or {}).get("tracks") or []
    seen, popular = set(), []
    for i, t in enumerate(pl):
        k = key1(t["title"])
        if k in seen or not t.get("videoId"): continue
        seen.add(k)
        others = [x["name"] for x in t.get("artists", []) if x["name"] != a["name"]]
        art = lh3(biggest(t.get("thumbnails"))["url"], "w120-h120-l90-rj") if t.get("thumbnails") else None
        popular.append({"rank": len(popular) + 1, "videoId": t["videoId"], "title": t["title"],
                        "album": (t.get("album") or {}).get("name"), "albumId": (t.get("album") or {}).get("id"),
                        "with": others, "duration": t.get("duration"), "seconds": secs(t.get("duration")),
                        "explicit": bool(t.get("isExplicit")),
                        "art": download(art, f"assets/{slug}/songs/{t['videoId']}.jpg", "song-art", t["videoId"]) if art else None,
                        "provider": "artist songs shelf" if i < len(shelf) else "artist songs playlist (Show all continuation)"})
        if len(popular) == 10: break
    shelf_ids = [s.get("videoId") for s in shelf]
    ev["popularSongs"] = {"artistShelfCount": len(shelf), "playlistTracksFetched": len(pl), "shown": len(popular),
                          "shelfMatchesPlaylistHead": shelf_ids == [t.get("videoId") for t in pl[:len(shelf)]],
                          "rankMeaning": "provider order of the YouTube Music artist Songs shelf; not a Colosseum popularity algorithm; no play counts shown"}

    # releases
    rel_items, unmatched = {}, []
    for ytk in ("albums", "singles"):
        for x in cat[ytk]["items"] or []:
            prim = ("Single", "EP") if ytk == "singles" else ("Album",)
            g, how = mb_match(x["title"], prim, groups)
            c = "singles" if ytk == "singles" else category(g, x.get("type"))
            gid = g["id"] if g else "yt:" + ytk + ":" + key1(x["title"]) + (":" + str(x.get("year")) if ytk == "singles" else "")
            if not g: unmatched.append({"title": x["title"], "browseId": x["browseId"], "ytShelf": ytk})
            item = rel_items.setdefault(gid, {"id": gid, "mbid": g["id"] if g else None, "category": c,
                                             "title": g["title"] if g else x["title"],
                                             "year": (g["first"][:4] if g and g["first"] else str(x.get("year") or "")),
                                             "ytType": x.get("type") or ytk[:-1].title(), "editions": [], "matchedBy": how})
            item["editions"].append({"browseId": x["browseId"], "title": x["title"], "year": x.get("year"),
                                     "explicit": x.get("isExplicit"), "thumb": biggest(x.get("thumbnails")).get("url")})
    shelves = {}
    for it in rel_items.values():
        eds = it["editions"]
        rep = next((e for e in eds if key1(e["title"]) == key1(it["title"]) and fold(e["title"]).strip() == fold(re.sub(r"\s*[\(\[].*?[\)\]]", "", e["title"])).strip()), None) \
              or sorted(eds, key=lambda e: str(e["year"]))[0]
        it["browseId"] = rep["browseId"]
        it["title"] = display_title(rep["title"])
        it["cover"] = download(lh3(rep["thumb"], "w300-h300-l90-rj"), f"assets/{slug}/covers/{rep['browseId']}.jpg", "release-cover", rep["browseId"])
        if it["ytType"] == "EP" and it["category"] == "singles": it["kind"] = "EP"
        shelves.setdefault(it["category"], []).append(it)
    page_shelves = []
    for k in SHELF_ORDER:
        if k not in shelves: continue
        items = sorted(shelves[k], key=lambda i: (i["year"] or "0", i["title"]), reverse=True)
        page_shelves.append({"key": k, "title": SHELF_TITLES[k], "items": [
            {"id": i["browseId"], "mbid": i["mbid"], "title": i["title"], "year": i["year"], "cover": i["cover"],
             "editions": len(i["editions"]), "kind": i.get("kind", "")} for i in items]})
    ev["releases"] = {"albumsShelf": {"source": cat["albums"]["source"], "entries": len(cat["albums"]["items"] or [])},
                      "singlesShelf": {"source": cat["singles"]["source"], "entries": len(cat["singles"]["items"] or [])},
                      "afterEditionGrouping": {s["key"]: len(s["items"]) for s in page_shelves},
                      "groups": list(rel_items.values()), "unmatchedInMusicBrainz": unmatched}

    # videos
    vpl = (cat.get("videosPlaylist") or {}).get("tracks")
    vsrc = "artist videos playlist" if vpl else "artist page videos shelf (no playlist handle returned)"
    vlist = vpl if vpl else a.get("videos", {}).get("results", [])
    videos, live = [], []
    for v in vlist:
        if not v.get("videoId"): continue
        thumb = download(f"https://i.ytimg.com/vi/{v['videoId']}/mqdefault.jpg", f"assets/{slug}/videos/{v['videoId']}.jpg", "video-still", v["videoId"])
        rec = {"id": v["videoId"], "title": v["title"], "duration": v.get("duration"), "still": thumb,
               "live": bool(re.search(r"\blive\b", v["title"], re.I))}
        (live if rec["live"] else videos).append(rec)
    if len(live) < 2:
        videos, live = sorted(videos + live, key=lambda r: [x.get("videoId") for x in vlist].index(r["id"])), []
    ev["videos"] = {"source": vsrc, "fetched": len(vlist), "musicVideos": len(videos), "livePerformances": len(live),
                    "liveRule": "title contains the word 'Live' (provider title text); separate rail only when 2+ exist",
                    "durationsMissing": sum(1 for r in videos + live if not r["duration"])}

    # similar artists
    related = []
    for r in a.get("related", {}).get("results", []):
        t = biggest(r.get("thumbnails")).get("url")
        related.append({"id": r["browseId"], "name": r["title"],
                        "photo": download(lh3(t, "w240-h240-p-l90-rj"), f"assets/{slug}/related/{r['browseId']}.jpg", "artist-thumb", r["browseId"]) if t else None})
    ev["related"] = {"source": "artist page related shelf", "count": len(related)}

    counts = {s["key"]: len(s["items"]) for s in page_shelves}
    page_artists[slug] = {"slug": slug, "state": state, "name": a["name"], "channelId": cid, "mbid": mb["mbid"],
                          "hero": hero, "counts": counts, "popular": popular, "shelves": page_shelves,
                          "videos": videos, "live": live, "related": related,
                          "snapshot": flog["fetchedAt"][:10]}
    evidence["artists"][slug] = ev

# wallpaper: the user's current Colosseum home pick, falling back to the shipped default
wp = {"default": "assets/wallpaper/cold-ripple.jpg"}
(OUT / "assets/wallpaper").mkdir(parents=True, exist_ok=True)
shutil.copy("C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/assets/wallpaper/cold-ripple.jpg", OUT / wp["default"])
ini = pathlib.Path("C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/wallpapers.ini").read_text(encoding="utf-8")
m = re.search(r'^homePick="(.*)"\s*$', ini, re.M)
if m:
    pick = json.loads(json.loads('"' + m.group(1) + '"'))
    wp["current"] = download(pick["image_url"], "assets/wallpaper/current-home-pick.jpg", "wallpaper", pick.get("source_url"))
    wp["currentSource"] = {"from": "Colosseum/wallpapers.ini [wallpapers] homePick", "title": pick.get("title"), "sourceUrl": pick.get("source_url"), "attribution": pick.get("attribution")}
evidence["wallpaper"] = wp
evidence["assets"] = provenance

(OUT / "data").mkdir(parents=True, exist_ok=True)
(OUT / "data/artists.js").write_text(
    "// Generated by build_data.py from live YouTube Music + MusicBrainz pulls; see catalogue-evidence.json.\n"
    "window.VINYL_ARTIST_DATA = " + json.dumps({"wallpaper": {"current": wp.get("current"), "default": wp["default"]},
                                                 "artists": page_artists}, ensure_ascii=False, indent=1) + ";\n", encoding="utf-8")
(OUT / "catalogue-evidence.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=1), encoding="utf-8")
for s, p in page_artists.items():
    print(s, p["counts"], "popular", len(p["popular"]), "videos", len(p["videos"]), "live", len(p["live"]), "related", len(p["related"]), "hero", p["hero"])
print("unmatched", {s: [u["title"] for u in e["releases"]["unmatchedInMusicBrainz"]] for s, e in evidence["artists"].items()})
print("assets", len(provenance))
