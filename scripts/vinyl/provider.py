"""Anonymous Vinyl provider. One JSON request on stdin, one JSON response on stdout.

Catalogue identities are durable; resolved URLs and headers are transient. Never
write the resolver response to the library, progress store, logs, or a snapshot.
"""
import json
import re
import sys
import uuid
from urllib.parse import urlparse

NAMESPACE = uuid.UUID("9b7255c4-9aec-51d6-8905-e55de3fa8209")


def identity(kind, source_key):
    return str(uuid.uuid5(NAMESPACE, "youtube-music:" + kind + ":" + source_key))


def artists(record):
    return [a["name"] for a in record.get("artists", []) if a.get("name")]


def cover(record):
    images = record.get("thumbnails") or []
    return max(images, key=lambda x: x.get("width", 0)).get("url", "") if images else ""


def album_card(record, browse_id=None):
    key = browse_id or record.get("browseId")
    if not key:
        raise ValueError("Album has no source identity")
    return {"id": identity("release", key), "browseId": key,
            "origin": "youtube-music", "title": record.get("title", ""),
            "artists": artists(record), "year": str(record.get("year") or ""),
            "cover": cover(record), "explicit": bool(record.get("isExplicit")),
            "type": record.get("type", "Album")}


def normalize_album(record, key):
    result = album_card(record, key)
    result["tracks"] = []
    for index, track in enumerate(record.get("tracks") or []):
        video = track.get("videoId") or ""
        result["tracks"].append({
            "id": identity("recording", video) if video else identity("unavailable", key + ":" + str(index)),
            "videoId": video, "title": track.get("title") or "Unavailable track",
            "artists": artists(track) or result["artists"],
            "seconds": int(track.get("duration_seconds") or 0),
            "available": bool(video) and track.get("isAvailable", True),
            "explicit": bool(track.get("isExplicit"))})
    # A truncated album is not silently presented as a complete release.
    expected = record.get("trackCount")
    if isinstance(expected, int) and expected > len(result["tracks"]):
        raise ValueError("Provider returned an incomplete album")
    if not result["tracks"]:
        raise ValueError("This album has no available track listing")
    return result


def resolve(video):
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video):
        raise ValueError("Invalid YouTube video ID")
    from yt_dlp import YoutubeDL
    class QuietLogger:
        def debug(self, message): pass
        def warning(self, message): pass
        def error(self, message): pass
    with YoutubeDL({"quiet": True, "no_warnings": True, "logger": QuietLogger(),
                    "format": "bestaudio", "skip_download": True, "noplaylist": True,
                    "socket_timeout": 20, "retries": 1, "extractor_retries": 1}) as client:
        data = client.extract_info("https://music.youtube.com/watch?v=" + video, download=False)
    url = data.get("url", "")
    parsed = urlparse(url)
    if parsed.scheme != "https" or not (parsed.hostname or "").endswith(".googlevideo.com"):
        raise ValueError("Provider returned an unsupported audio location")
    if data.get("id") != video:
        raise ValueError("Provider returned a different track")
    codec = data.get("acodec") or "Audio"
    bitrate = data.get("abr")
    return {"videoId": video, "url": url, "headers": data.get("http_headers") or {},
            "qualityLabel": codec.upper() + (" · " + str(round(bitrate)) + " kbps" if bitrate else "")}


def handle(request):
    operation, value = request["operation"], str(request.get("value", "")).strip()
    if operation == "resolve":
        return resolve(value)
    from ytmusicapi import YTMusic
    client = YTMusic(location=request.get("region", "IN"))
    if operation == "search":
        if not 1 <= len(value) <= 300:
            raise ValueError("Enter an album or artist name")
        return [album_card(a) for a in client.search(value, filter="albums", limit=24)
                if a.get("browseId")][:24]
    if operation == "album":
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,160}", value):
            raise ValueError("Invalid album identity")
        return normalize_album(client.get_album(value), value)
    raise ValueError("Unknown operation")


def main():
    try:
        request = json.loads(sys.stdin.buffer.read(8193))
        response = {"ok": True, "data": handle(request)}
    except ImportError:
        response = {"ok": False, "error": "Vinyl's catalogue helper is not installed."}
    except ValueError as exc:
        response = {"ok": False, "error": str(exc)}
    except Exception:
        # Upstream errors may contain signed URLs, cookies or verbose internals.
        response = {"ok": False, "error": "YouTube is unavailable for this request. Try again."}
    sys.stdout.buffer.write(json.dumps(response, ensure_ascii=True).encode("utf-8"))


if __name__ == "__main__":
    main()
