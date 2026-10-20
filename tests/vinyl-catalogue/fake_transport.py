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
if operation == "artist":
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
