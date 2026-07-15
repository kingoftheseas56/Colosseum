#!/usr/bin/env python3
# Enumerate Dragon Ball's REAL catalog straight from the live providers the app uses,
# so every universe pin is grounded in what Theatre/Biblio can actually open (Universe
# Page LAW: metadata id = the gate, verified live). Cinemeta = anime series + films;
# AniList = manga. Prints id + name + year, deduped, for curation.
import json, urllib.request, urllib.parse

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumDBProbe/1.0"
def getj(url, data=None, headers=None, timeout=30):
    h = {"User-Agent": UA, "Accept": "application/json"}
    if headers: h.update(headers)
    req = urllib.request.Request(url, data=data, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))

CINE = "https://v3-cinemeta.strem.io/catalog/{type}/top/search={q}.json"
def cine(type_, q):
    url = CINE.format(type=type_, q=urllib.parse.quote(q))
    try:
        return (getj(url).get("metas") or [])
    except Exception as e:
        print(f"  ! cinemeta {type_} '{q}' failed: {e}")
        return []

def show(metas, label):
    print(f"\n=== {label} ({len(metas)}) ===")
    seen = set()
    for m in metas:
        mid = m.get("id", "")
        if mid in seen: continue
        seen.add(mid)
        yr = m.get("releaseInfo") or m.get("year") or ""
        nm = m.get("name", "")
        # keep only Dragon Ball hits
        if "dragon ball" in nm.lower() or "dragonball" in nm.lower():
            print(f"  {mid:14} {yr:10} {nm}")

# ---- ANIME SERIES ----
series = []
for q in ["dragon ball", "dragon ball z", "dragon ball super", "dragon ball daima", "dragon ball kai"]:
    series += cine("series", q)
show(series, "SERIES (Cinemeta)")

# ---- FILMS ----
films = []
for q in ["dragon ball", "dragon ball z", "dragon ball super", "dragon ball broly",
          "dragon ball battle of gods", "dragon ball resurrection", "dragon ball super hero"]:
    films += cine("movie", q)
show(films, "FILMS (Cinemeta)")

# ---- MANGA (AniList) ----
print("\n=== MANGA (AniList) ===")
Q = """
query ($s:String) { Page(perPage:25){ media(search:$s, type:MANGA, sort:SEARCH_MATCH){
  id title{romaji english} format startDate{year} countryOfOrigin } } }"""
try:
    data = getj("https://graphql.anilist.co",
                data=json.dumps({"query": Q, "variables": {"s": "dragon ball"}}).encode(),
                headers={"Content-Type": "application/json"})
    for m in data["data"]["Page"]["media"]:
        t = m["title"]["english"] or m["title"]["romaji"]
        if "dragon ball" in (t or "").lower():
            print(f"  AL {m['id']:8} {m.get('format',''):10} {m['startDate'].get('year') or '':6} {t}")
except Exception as e:
    print(f"  ! anilist failed: {e}")
print("\n=== done ===")
