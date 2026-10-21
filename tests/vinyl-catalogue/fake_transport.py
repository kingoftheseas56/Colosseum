"""Deterministic stdio transport for native tests only. No network."""
import json
import sys
import time
import os

request = json.load(sys.stdin)
operation, value = request["operation"], request.get("value", "")
channel = "UCedvOgsKFzcK3hA5taf3KoQ"
if value == "slow":
    time.sleep(1)
if value == "oversize":
    print("x" * (4 * 1024 * 1024 + 1))
    sys.exit(0)
if operation == "discover":
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
            "type": "Playlist", "title": "Chart", "origin": "youtube-music", "artists": [],
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
elif operation == "album":
    data = {"id": "provider-release", "browseId": value, "title": "Album", "artists": ["Artist"],
            "origin": "youtube-music", "tracks": [{"id": "provider-recording", "videoId": "abcdefghijk", "title": "Track"}]}
elif operation == "resolve":
    data = {"videoId": value, "url": "https://audio.googlevideo.com/test?secret=TRANSIENT_ONLY", "headers": {}}
else:
    data = {"artists": [], "albums": [], "request": request, "complete": False}
print(json.dumps({"ok": True, "data": data}))
