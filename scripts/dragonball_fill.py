#!/usr/bin/env python3
# Gap-fill: targeted Cinemeta searches for the theatrical films the broad sweep missed,
# + AniList cover art for the curated manga shelf (s4.anilist.co is IPv4-pinned in main.cpp).
import json, urllib.request, urllib.parse

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ColosseumDBProbe/1.0"
def getj(url, data=None, headers=None, timeout=30):
    h = {"User-Agent": UA, "Accept": "application/json"}
    if headers: h.update(headers)
    req = urllib.request.Request(url, data=data, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))

def cine_movie(q):
    url = f"https://v3-cinemeta.strem.io/catalog/movie/top/search={urllib.parse.quote(q)}.json"
    try: return getj(url).get("metas") or []
    except Exception as e: return []

print("=== FILM GAP-FILL (targeted) ===")
for q in ["Dragon Ball Z Dead Zone", "Dragon Ball Sleeping Princess Devil Castle",
          "Dragon Ball Z Bardock Father of Goku", "Dragon Ball Z The Tree of Might"]:
    metas = cine_movie(q)
    print(f"\n  ? {q}")
    for m in metas[:6]:
        nm = m.get("name","")
        if "dragon ball" in nm.lower():
            print(f"      {m.get('id',''):14} {m.get('releaseInfo',''):8} {nm}")

# AniList covers for the curated manga shelf (by id)
IDS = [30042, 86508, 53446, 98030, 56373, 97900, 46110, 94109]
print("\n=== MANGA COVERS (AniList) ===")
Q = """query ($ids:[Int]) { Page(perPage:30){ media(id_in:$ids, type:MANGA){
  id title{romaji english} format startDate{year} chapters
  coverImage{large} } } }"""
data = getj("https://graphql.anilist.co",
            data=json.dumps({"query": Q, "variables": {"ids": IDS}}).encode(),
            headers={"Content-Type": "application/json"})
for m in data["data"]["Page"]["media"]:
    t = m["title"]["english"] or m["title"]["romaji"]
    print(f"  AL {m['id']:8} {m.get('format',''):9} {m['startDate'].get('year') or '':6} "
          f"ch={m.get('chapters')}  {t}")
    print(f"        cover: {m['coverImage']['large']}")
print("\n=== done ===")
