"""Deliberately local process/protocol fixture; never a catalogue or media test."""
import json
import sys
import time

req = json.load(sys.stdin)
op, value = req["operation"], req["value"]
if value in ("slow", "hang"):
    time.sleep(0.5 if value == "slow" else 5.0)
if value == "too-big":
    sys.stdout.write("X" * (4 * 1024 * 1024 + 100))
    sys.stdout.flush()
    sys.exit(0)
if value == "malformed":
    print("{invalid json")
    sys.exit(0)
if value == "missing-ok":
    print(json.dumps({"data": []}))
    sys.exit(0)
if value == "wrong-shape":
    print(json.dumps({"ok": True, "data": {"wrong": "shape"}}))
    sys.exit(0)
if value == "broken-search-row":
    print(json.dumps({"ok": True, "data": [{"title": "No ID"}]}))
    sys.exit(0)
if value == "broken-album":
    print(json.dumps({"ok": True, "data": {"id": "r", "browseId": "r",
                      "title": "A", "artists": [], "tracks": [{"title": "No artists"}]}}))
    sys.exit(0)
if value == "CCCCCCCCCCC" and op == "resolve":
    print(json.dumps({"ok": True, "data": {"videoId": value}}))
    sys.exit(0)
if value == "crash":
    print("https://secret.example/signed?token=PRIVATE", file=sys.stderr)
    sys.exit(9)
if value == "leak":
    print(json.dumps({"ok": False, "errorCode": "invalid_request",
                      "error": "https://secret.example/signed?token=PRIVATE"}))
    sys.exit(0)
if value == "dependency":
    print(json.dumps({"ok": False, "errorCode": "dependency_missing",
                      "error": "Vinyl's catalogue helper is not installed."}))
    sys.exit(0)
if op == "resolve":
    video = "BBBBBBBBBBB" if value == "AAAAAAAAAAA" else value
    data = {"videoId": video, "url": "https://r.googlevideo.com/audio?token=PRIVATE",
            "headers": {"Referer": "music"}, "qualityLabel": "OPUS"}
elif op == "album":
    data = {"id": value, "browseId": value, "title": "Album", "artists": [],
            "year": "", "cover": "", "tracks": [
                {"id": "track1", "videoId": "InEXiAJCOuA", "title": "Track",
                 "artists": [], "seconds": 75, "available": True}]}
else:
    data = [{"id": value, "title": value, "browseId": value, "artists": []}]
print(json.dumps({"ok": True, "data": data}))
