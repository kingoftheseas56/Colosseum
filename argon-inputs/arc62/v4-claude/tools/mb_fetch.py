"""MusicBrainz release-group browse for an artist MBID (or name search). Usage: mb_fetch.py <mbid|name:Foo> <out.json>"""
import json, sys, time, urllib.request, urllib.parse
UA = {"User-Agent": "ColosseumVinylPrototype/0.1 (artist-page catalogue check)"}
def get(url):
    for i in range(6):
        time.sleep(1.5 + i * 2)
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30) as r:
                return json.load(r)
        except Exception as e:
            err = e
    raise err
key, out = sys.argv[1], sys.argv[2]
res = {}
if key.startswith("name:"):
    q = get("https://musicbrainz.org/ws/2/artist/?fmt=json&limit=5&query=" + urllib.parse.quote('artist:"%s"' % key[5:]))
    res["search"] = [{"id": a["id"], "name": a["name"], "score": a.get("score"), "country": a.get("country"), "disambiguation": a.get("disambiguation")} for a in q.get("artists", [])]
    if not res["search"]:
        json.dump(res, open(out, "w", encoding="utf-8"), indent=1); print("no artist"); sys.exit()
    mbid = res["search"][0]["id"]
else:
    mbid = key
res["mbid"] = mbid
rgs, off = [], 0
while True:
    page = get(f"https://musicbrainz.org/ws/2/release-group?artist={mbid}&fmt=json&limit=100&offset={off}")
    rgs += page["release-groups"]; off += 100
    if off >= page["release-group-count"]: break
res["releaseGroups"] = [{"id": g["id"], "title": g["title"], "primary": g.get("primary-type"), "secondary": g.get("secondary-types", []), "first": g.get("first-release-date", "")} for g in rgs]
res["count"] = len(rgs)
json.dump(res, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(mbid, len(rgs), res.get("search", "")[:2] if res.get("search") else "")
