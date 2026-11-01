"""Deterministic stdio transport for native tests only. No network."""
import json
import sys
import time
import os
from datetime import datetime, timezone

request = json.load(sys.stdin)
operation, value = request["operation"], request.get("value", "")
counter_path = os.environ.get("VINYL_TEST_HOME_COUNTER")
if counter_path:
    with open(counter_path, "a", encoding="utf-8") as counter:
        counter.write("launch\n")
channel = "UCedvOgsKFzcK3hA5taf3KoQ"
if value == "slow":
    time.sleep(1)
if value == "oversize":
    print("x" * (4 * 1024 * 1024 + 1))
    sys.exit(0)
if operation == "cataloguePage":
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
elif operation == "artist":
    data = {"requestedChannelId": channel, "channelId": channel, "name": "No MB mapping" if os.environ.get("VINYL_TEST_IDENTITY_UNAVAILABLE") else "Artist", "origin": "youtube-music"}
elif operation == "artistReleases":
    data = {"requestedChannelId": channel, "kind": request["kind"], "complete": True,
            "items": [{"id": "provider-release", "browseId": "MPRE_test", "title": "Album"}]}
elif operation == "artistVideos":
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
