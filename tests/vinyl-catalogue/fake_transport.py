"""Deterministic stdio transport for native tests only. No network."""
import json
import sys
import time
import tempfile
import os
from datetime import datetime, timezone

request = json.load(sys.stdin)
operation, value = request["operation"], request.get("value", "")
counter_path = os.environ.get("VINYL_TEST_HOME_COUNTER")
if counter_path:
    with open(counter_path, "a", encoding="utf-8") as counter:
        counter.write("launch\n")
discover_env_path = os.environ.get("VINYL_TEST_DISCOVER_ENV_FILE")
if discover_env_path:
    with open(discover_env_path, "w", encoding="utf-8") as env_file:
        json.dump({"cacheDir": os.environ.get("COLOSSEUM_VINYL_DISCOVER_CACHE_DIR", ""),
                   "lastfmKeyFile": os.environ.get("COLOSSEUM_VINYL_LASTFM_KEY_FILE", "")}, env_file)
channel = "UCedvOgsKFzcK3hA5taf3KoQ"
artist_log_path = os.environ.get("VINYL_TEST_ARTIST_REQUEST_LOG")
if artist_log_path and operation in {"artist", "artistReleases", "artistVideos", "artistIdentity", "artistArtwork"}:
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", prefix="artist-request-",
                                     suffix=".json", dir=artist_log_path, delete=False) as artist_log:
        json.dump(request, artist_log)
if value == "slow":
    time.sleep(1)
if value == "oversize":
    print("x" * (4 * 1024 * 1024 + 1))
    sys.exit(0)
if operation == "browseCategories":
    data={"version":1,"source":"youtube-music","categories":[{"id":"source-category","title":"Rock","group":"Genres","params":"source-params"}],"fetchedAt":"2026-11-04T00:00:00Z"}
elif operation == "browseCatalogue":
    record={"id":"standalone-song","browseId":"ytm:track:abcdefghijk","type":"Song","title":"Track","origin":"youtube-music","artists":["Artist"],
            "tracks":[{"id":"provider-recording","videoId":"abcdefghijk","title":"Track","seconds":180}]}
    data={"version":1,"kind":value,"category":request.get("category",""),"region":request.get("region",""),"source":"youtube-music",
          "items":[{"id":"provider-recording","kind":"songs","albumId":"standalone-song","trackId":"provider-recording","trackIndex":0,"title":"Track"}],
          "records":[record],"offset":0,"nextOffset":1,"nextCursor":{},"complete":True,"fetchedAt":"2026-11-04T00:00:00Z","request":request}
elif operation == "resolveCatalogueItem":
    item=request.get("item") or {}
    record={"id":"discover-album-uuid","browseId":"MPRE_discover_album","type":"Song","title":item.get("title","Track"),
            "origin":"youtube-music","artists":["Artist"],"tracks":[{"id":"discover-track","videoId":"abcdefghijk",
            "title":item.get("title","Track"),"seconds":180}]}
    data={"version":1,"id":value,"kind":item.get("kind","songs"),"matched":True,"albumId":record["id"],
          "record":record,"cover":"https://lh3.googleusercontent.com/test","request":request}
elif operation == "resolveCatalogueArt":
    data={"version":1,"covers":{},"fetchedAt":"2026-11-04T00:00:00Z","request":request}
elif operation == "cataloguePage":
    cursor=request.get("cursor") or {}; offset=cursor.get("offset",0);kind=request.get("kind","songs")
    record={"id":"standalone-song","browseId":"ytm:track:abcdefghijk","type":"Song","title":"Track","origin":"youtube-music","artists":["Artist"],
            "tracks":[{"id":"provider-recording","videoId":"abcdefghijk","title":"Track","seconds":180}]}
    data={"version":1,"query":value,"kind":kind,"source":"youtube-music","offset":offset,"nextOffset":offset+1,
          "items":[{"id":"provider-recording","kind":"songs","albumId":"standalone-song","trackIndex":0,"title":"Track"}],
          "records":[record],"complete":True,"nextCursor":{},"request":request}
elif operation == "playlistPage":
    offset=(request.get("cursor") or {}).get("offset",0)
    data={"version":1,"id":"public-playlist","browseId":"ytm:playlist:"+value,"playlistId":value,"type":"Playlist",
          "title":"Chart","origin":"youtube-music","artists":[],"offset":offset,"nextOffset":offset+1,"complete":offset>0,
          "nextCursor":{} if offset else {"offset":1,"token":"next"},"totalTracks":5,"omittedTracks":3 if offset else 0,
          "tracks":[{"id":"provider-recording" if offset==0 else "later-recording","videoId":"abcdefghijk" if offset==0 else "lmnopqrstuv","title":"Track","seconds":180}]}
elif operation == "discover":
    record = {"id": "standalone-song", "browseId": "ytm:track:abcdefghijk", "type": "Song",
              "title": "Track", "origin": "youtube-music", "artists": ["Artist"],
              "tracks": [{"id": "provider-recording", "videoId": "abcdefghijk", "title": "Track", "seconds": 180}]}
    if value == "charts":
        record = {"id": "public-playlist", "browseId": "ytm:playlist:PLtest", "playlistId": "PLtest",
                  "type": "Playlist", "title": "Chart", "origin": "youtube-music", "artists": [], "tracks": []}
    data = {"version": 1, "key": value, "kind": "catalogue-playlists" if value == "charts" else "songs",
            "title": "Discovery", "source": "youtube-music", "region": "IN", "fetchedAt": "test-revision",
            "complete": False, "items": [dict(record, kind="catalogue-playlists" if value == "charts" else "songs")], "records": [record]}
elif operation == "playlist":
    data = {"id": "public-playlist", "browseId": "ytm:playlist:" + value, "playlistId": value,
            "type": "Playlist", "title": "Chart", "origin": "youtube-music", "artists": [], "complete": True,
            "tracks": [{"id": "provider-recording", "videoId": "abcdefghijk", "title": "Track", "seconds": 180}]}
elif operation == "artistArtwork":
    data = {"artistMbid":"11111111-1111-4111-8111-111111111111","source":"theaudiodb","usage":"development",
            "termsUrl":"https://www.theaudiodb.com/docs_terms_of_use.php","fetchedAtEpoch":int(time.time()),
            "photos":[{"id":str(i),"url":"https://r2.theaudiodb.com/images/media/artist/fanart/%d.jpg"%i,
                       "attribution":"TheAudioDB test source"} for i in range(int(os.environ.get('VINYL_TEST_PHOTOS','4')))]}
    if os.environ.get("VINYL_TEST_ARTWORK_MALFORMED"):
        data["source"] = "unverified-source"
elif operation == "artist":
    record = {"id":"ytm:track:abcdefghijk","browseId":"ytm:track:abcdefghijk","type":"Song","title":"Artist song",
              "origin":"youtube-music","artists":["Artist"],"tracks":[{"id":"artist-recording","videoId":"abcdefghijk",
              "title":"Artist song","seconds":180}]}
    album = {"id":"provider-release","browseId":"MPRE_test","title":"Album","type":"Album"}
    single = {"id":"provider-single","browseId":"MPSP_test","title":"Single","type":"Single"}
    video = {"id":"abcdefghijk","videoId":"abcdefghijk","title":"Artist video","duration":"3:00","seconds":180,
             "thumbnail":"https://i.ytimg.com/vi/abcdefghijk/mqdefault.jpg","live":False}
    continuations = {"albums":None,"singles":None,"videos":None}
    complete = True
    if os.environ.get("VINYL_TEST_ARTIST_CONTINUATIONS"):
        continuations = {"albums":{"browseId":"MPRE_albums_next","params":"CgQ="},
                         "singles":{"browseId":"MPSP_singles_next","params":"CgU="},
                         "videos":{"browseId":"VLartistvideos"}}
        complete = False
    data = {"requestedChannelId": channel, "channelId": "UCaaaaaaaaaaaaaaaaaaaaaa",
            "name": "No MB mapping" if os.environ.get("VINYL_TEST_IDENTITY_UNAVAILABLE") else "Artist",
            "origin": "youtube-music", "songs":[{"id":"ytm:recording:abcdefghijk","record":record}],
            "continuations":continuations,
            "albumShelf":{"requestedChannelId":channel,"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa","kind":"albums","items":[album],"complete":complete},
            "singleShelf":{"requestedChannelId":channel,"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa","kind":"singles","items":[single],"complete":complete},
            "videoShelf":{"requestedChannelId":channel,"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa","items":[video],"videos":[video],
                          "live":[],"livePerformances":[],"complete":complete}}
    if os.environ.get("VINYL_TEST_ARTIST_CHANNEL_MISMATCH"):
        data["requestedChannelId"] = "UCbbbbbbbbbbbbbbbbbbbbbb"
elif operation == "artistReleases":
    if request.get("continuation"):
        if os.environ.get("VINYL_TEST_ARTIST_CONTINUATION_DELAY"):
            time.sleep(0.6)
        kind = request["kind"]
        item = {"id":"provider-release-more","browseId":"MPRE_album_more","title":"Later album","type":"Album"}
        if kind == "singles":
            item = {"id":"provider-single-more","browseId":"MPSP_single_more","title":"Later single","type":"Single"}
        requested = "UCbbbbbbbbbbbbbbbbbbbbbb" if kind == "albums" and os.environ.get("VINYL_TEST_ARTIST_RELEASE_MISMATCH") else channel
        data = {"requestedChannelId":requested,"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa","kind":kind,"complete":True,"items":[item]}
    else:
        data = {"requestedChannelId": channel, "kind": request["kind"], "complete": True,
                "items": [{"id": "provider-release", "browseId": "MPRE_test", "title": "Album"}]}
elif operation == "artistVideos":
    if request.get("continuation"):
        if os.environ.get("VINYL_TEST_ARTIST_CONTINUATION_DELAY"):
            time.sleep(0.6)
        video = {"id":"lmnopqrstuv","videoId":"lmnopqrstuv","title":"Later artist video","duration":"2:30","seconds":150,
                 "thumbnail":"https://i.ytimg.com/vi/lmnopqrstuv/mqdefault.jpg","live":False}
        data = {"requestedChannelId":channel,"channelId":"UCaaaaaaaaaaaaaaaaaaaaaa","complete":True,
                "items":[video],"videos":[video],"live":[],"livePerformances":[]}
    else:
        data = {"requestedChannelId": channel, "complete": True, "videos": [], "livePerformances": []}
elif operation == "artistIdentity":
    if os.environ.get("VINYL_TEST_IDENTITY_UNAVAILABLE"):
        print(json.dumps({"ok": False, "error": "The optional identity service is unavailable.", "errorCode": "unavailable"}))
        sys.exit(0)
    data = {"mbid": "11111111-1111-4111-8111-111111111111", "source": "musicbrainz", "complete": True,
            "fetchedAt": "test-revision", "releaseGroups": [
                {"id": "22222222-2222-4222-8222-222222222222", "title": "Album"}]}
elif operation in ("artistCandidates", "recommendationIdentity"):
    if os.environ.get("VINYL_TEST_HOME_SLOW"):
        time.sleep(0.5)
    if os.environ.get("VINYL_TEST_HOME_FAIL"):
        sys.exit(2)
    seed = dict(request.get("seed") or {})
    if operation == "artistCandidates":
        card_seed = dict(seed)
        item = {"provider": "youtube-music", "providerId": "MPRE_test", "kind": "album",
                "id": "ytm:album:MPRE_test", "title": "Album", "artistRefs": [card_seed],
                "relation": "own-release", "seedArtistRef": card_seed}
        if os.environ.get("VINYL_TEST_HOME_SEED_MISMATCH"):
            seed["providerId"] = "UC_mismatched"
        if os.environ.get("VINYL_TEST_HOME_OVERSIZE"):
            item["title"] = "A" * (512 * 1024)
        data = {"version": 1, "seed": seed, "items": [item],
                "provenance": {"source": "youtube-music", "generator": "artist-catalogue"},
                "fetchedAt": datetime.now(timezone.utc).isoformat(timespec="milliseconds"), "complete": False, "status": "ready"}
        if os.environ.get("VINYL_TEST_HOME_UNAVAILABLE"):
            data.update({"items": [], "status": "unavailable", "error": "Optional catalogue is unavailable.",
                         "sourceStatus": "unavailable"})
        if os.environ.get("VINYL_TEST_HOME_EMPTY"):
            data.update({"items": [], "complete": False, "status": "empty"})
        if os.environ.get("VINYL_TEST_HOME_RATE"):
            data.update({"items": [], "complete": False, "status": "unavailable",
                         "sourceStatus": "rate_limited", "retryAfterSeconds": 60,
                         "error": "The optional catalogue is rate limited."})
        if os.environ.get("VINYL_TEST_HOME_WRONG_SHAPE"):
            data["items"] = {"not-a-list": True}
    else:
        data = {"version": 1, "seed": seed, "mappingStatus": "unresolved", "complete": True,
                "status": "unresolved", "items": [], "fetchedAt": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                "provenance": {"source": "musicbrainz"}}
        if os.environ.get("VINYL_TEST_HOME_UNAVAILABLE"):
            data.update({"mappingStatus": "unavailable", "status": "unavailable",
                         "error": "Optional identity service is unavailable.", "sourceStatus": "unavailable"})
elif operation == "recordingMatch":
    recording = request["recording"]
    item = {"provider": "youtube-music", "providerId": "abcdefghijk", "kind": "song",
            "id": "ytm:track:abcdefghijk", "videoId": "abcdefghijk", "title": recording["title"],
            "seconds": recording["seconds"], "artistRefs": recording["artistRefs"], "relation": "recording-match",
            "playbackMatch": {"status": "verified", "provider": "youtube-music", "providerId": "abcdefghijk",
                              "artistMatched": True, "versionMatched": True, "durationMatched": True,
                              "method": "exact-metadata", "fetchedAtMs": int(time.time()*1000)}}
    data = {"version": 1, "seed": request.get("seed", {}), "items": [item], "complete": True,
            "status": "verified", "videoId": "abcdefghijk", "provenance": {"source": "youtube-music"},
            "fetchedAt": datetime.now(timezone.utc).isoformat(timespec="milliseconds")}
elif operation == "album":
    data = {"id": "provider-release", "browseId": value, "title": "Album", "artists": ["Artist"],
            "origin": "youtube-music", "tracks": [{"id": "provider-recording", "videoId": "abcdefghijk", "title": "Track"}]}
elif operation == "resolve":
    data = {"videoId": value, "url": "https://audio.googlevideo.com/test?secret=TRANSIENT_ONLY", "headers": {}}
else:
    data = {"artists": [], "albums": [], "request": request, "complete": False}
print(json.dumps({"ok": True, "data": data}))
