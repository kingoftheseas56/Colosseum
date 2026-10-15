#!/usr/bin/env python3
# Enumerate One Piece's REAL catalog straight from the live providers, so every universe
# pin is grounded in what Theatre/Biblio can open (Universe Page LAW: id = the gate).
# One Piece = ONE continuous anime + ONE manga + many films + specials + spin-off manga.
import json, urllib.request, urllib.parse

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumOPProbe/1.0"
def getj(url, data=None, headers=None, timeout=30):
    h = {"User-Agent": UA, "Accept": "application/json"}
    if headers: h.update(headers)
    req = urllib.request.Request(url, data=data, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))

def cine(type_, q):
    url = "https://v3-cinemeta.strem.io/catalog/%s/top/search=%s.json" % (type_, urllib.parse.quote(q))
    try: return getj(url).get("metas") or []
    except Exception as e:
        print("  ! cinemeta %s '%s' failed: %s" % (type_, q, e)); return []

def show(metas, label):
    print("\n=== %s ===" % label)
    seen = set()
    for m in metas:
        mid = m.get("id", "")
        if mid in seen: continue
        seen.add(mid)
        nm = m.get("name", "")
        if "one piece" in nm.lower() or "one-piece" in nm.lower():
            print("  %-14s %-10s %s" % (mid, m.get("releaseInfo") or m.get("year") or "", nm))

# ANIME (series + specials)
series = []
for q in ["one piece", "one piece episode of", "one piece special"]:
    series += cine("series", q)
show(series, "SERIES / SPECIALS (Cinemeta)")

# FILMS
films = []
for q in ["one piece film", "one piece movie", "one piece z", "one piece gold",
          "one piece red", "one piece stampede", "one piece strong world", "one piece"]:
    films += cine("movie", q)
show(films, "FILMS (Cinemeta)")

# MANGA (AniList)
print("\n=== MANGA (AniList) ===")
Q = """query ($s:String){ Page(perPage:30){ media(search:$s, type:MANGA, sort:SEARCH_MATCH){
  id title{romaji english} format startDate{year} chapters countryOfOrigin
  coverImage{large} } } }"""
try:
    data = getj("https://graphql.anilist.co",
                data=json.dumps({"query": Q, "variables": {"s": "one piece"}}).encode(),
                headers={"Content-Type": "application/json"})
    for m in data["data"]["Page"]["media"]:
        t = m["title"]["english"] or m["title"]["romaji"]
        if "one piece" in (t or "").lower():
            print("  AL %-8s %-9s %-6s ch=%s  %s" % (m["id"], m.get("format",""),
                  m["startDate"].get("year") or "", m.get("chapters"), t))
            print("       cover: %s" % m["coverImage"]["large"])
except Exception as e:
    print("  ! anilist failed: %s" % e)
print("\n=== done ===")
