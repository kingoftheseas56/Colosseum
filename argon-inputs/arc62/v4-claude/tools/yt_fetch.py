"""Fetch a YouTube Music artist catalogue (account-free) and dump raw provider JSON.
Usage: python yt_fetch.py <channelId> <outdir>
"""
import json, sys, time, datetime, pathlib
from ytmusicapi import YTMusic

cid, out = sys.argv[1], pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
yt = YTMusic(location="IN")
log = {"channelId": cid, "fetchedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(), "calls": []}

def call(name, fn, *a, **k):
    t = time.time()
    try:
        r = fn(*a, **k)
        log["calls"].append({"call": name, "ok": True, "ms": int((time.time() - t) * 1000)})
        return r
    except Exception as e:
        log["calls"].append({"call": name, "ok": False, "error": repr(e)[:300]})
        return None

artist = call("get_artist", yt.get_artist, cid)
(out / "artist.json").write_text(json.dumps(artist, indent=1, ensure_ascii=False), encoding="utf-8")
res = {"artist": artist}
for key in ("albums", "singles"):
    sec = (artist or {}).get(key) or {}
    if sec.get("browseId") and sec.get("params"):
        full = call(f"get_artist_albums:{key}", yt.get_artist_albums, sec["browseId"], sec["params"], limit=None)
        res[key] = {"source": "get_artist_albums", "complete": True, "items": full}
    else:
        res[key] = {"source": "get_artist inline shelf", "complete": not sec.get("browseId"), "items": sec.get("results", [])}
songs = (artist or {}).get("songs") or {}
if songs.get("browseId"):
    pl = call("get_playlist:songs", yt.get_playlist, songs["browseId"], limit=25)
    res["songsPlaylist"] = pl
vids = (artist or {}).get("videos") or {}
if vids.get("browseId"):
    pl = call("get_playlist:videos", yt.get_playlist, vids["browseId"], limit=60)
    res["videosPlaylist"] = pl
(out / "catalogue.json").write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
(out / "fetchlog.json").write_text(json.dumps(log, indent=1), encoding="utf-8")
print(json.dumps(log["calls"]))
for key in ("albums", "singles"):
    print(key, len(res[key]["items"] or []), res[key]["source"])
print("songsPlaylist", len((res.get("songsPlaylist") or {}).get("tracks", []) or []), "videosPlaylist", len((res.get("videosPlaylist") or {}).get("tracks", []) or []))
print("related", len(((artist or {}).get("related") or {}).get("results", [])))
print("sections", [k for k in (artist or {}).keys()])
