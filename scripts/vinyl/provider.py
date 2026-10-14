"""Anonymous Vinyl provider. One JSON request on stdin, one JSON response on stdout.

Catalogue identities are durable; resolved URLs and headers are transient. Never
write the resolver response to the library, progress store, logs, or a snapshot.
"""
import json
import re
import sys
import uuid
from urllib.parse import urlparse


class ProviderError(ValueError):
    """Only locally authored, category-tagged messages may cross the process boundary."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


MAX_REQUEST_BYTES = 8192

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
        raise ProviderError("invalid_response", "Album has no source identity")
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
        raise ProviderError("incomplete_album", "Provider returned an incomplete album")
    if not result["tracks"]:
        raise ProviderError("not_found", "This album has no available track listing")
    return result


def resolve(video):
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", video):
        raise ProviderError("invalid_request", "Invalid YouTube video ID")
    from yt_dlp import YoutubeDL
    class QuietLogger:
        def debug(self, message): pass
        def warning(self, message): pass
        def error(self, message): pass
    with YoutubeDL({"quiet": True, "no_warnings": True, "logger": QuietLogger(),
                    "format": "bestaudio", "skip_download": True, "noplaylist": True,
                    "socket_timeout": 20, "retries": 1, "extractor_retries": 1}) as client:
        data = client.extract_info("https://music.youtube.com/watch?v=" + video, download=False)
    if not isinstance(data, dict):
        raise ProviderError("invalid_response", "Provider returned an invalid catalogue entry.")
    url = data.get("url", "")
    parsed = urlparse(url) if isinstance(url, str) else urlparse("")
    if parsed.scheme != "https" or not (parsed.hostname or "").endswith(".googlevideo.com"):
        raise ProviderError("invalid_response", "Provider returned an invalid catalogue entry.")
    if data.get("id") != video:
        raise ProviderError("identity_mismatch", "Provider returned a different track")
    codec = data.get("acodec") or "Audio"
    bitrate = data.get("abr")
    return {"videoId": video, "url": url, "headers": data.get("http_headers") or {},
            "qualityLabel": codec.upper() + (" · " + str(round(bitrate)) + " kbps" if bitrate else "")}


def handle(request):
    if not isinstance(request, dict) or request.get("operation") not in ("search", "album", "resolve"):
        raise ProviderError("invalid_request", "Unknown operation")
    operation, value = request["operation"], request.get("value")
    if not isinstance(value, str):
        raise ProviderError("invalid_request", "Invalid catalogue request.")
    value = value.strip()
    if operation == "resolve":
        return resolve(value)
    if operation == "search" and not 1 <= len(value) <= 300:
        raise ProviderError("invalid_request", "Enter an album or artist name")
    if operation == "album" and not re.fullmatch(r"[A-Za-z0-9_-]{1,160}", value):
        raise ProviderError("invalid_request", "Invalid album identity")
    region = request.get("region", "IN")
    if not isinstance(region, str) or not re.fullmatch(r"[A-Z]{2}", region):
        raise ProviderError("invalid_request", "Invalid catalogue request.")
    from ytmusicapi import YTMusic
    client = YTMusic(location=region)
    if operation == "search":
        records = client.search(value, filter="albums", limit=24)
        if not isinstance(records, list):
            raise ProviderError("invalid_response", "Provider returned an invalid catalogue entry.")
        return [album_card(a) for a in records if a.get("browseId")][:24]
    return normalize_album(client.get_album(value), value)


def main():
    try:
        raw = sys.stdin.buffer.read(MAX_REQUEST_BYTES + 1)
        if len(raw) > MAX_REQUEST_BYTES:
            raise ProviderError("invalid_request", "Invalid catalogue request.")
        try:
            request = json.loads(raw)
        except (ValueError, UnicodeError):
            raise ProviderError("invalid_request", "Invalid catalogue request.") from None
        response = {"ok": True, "data": handle(request)}
    except ProviderError as exc:
        response = {"ok": False, "errorCode": exc.code, "error": str(exc)}
    except ImportError:
        response = {"ok": False, "errorCode": "dependency_missing",
                    "error": "Vinyl's catalogue helper is not installed."}
    except TimeoutError:
        response = {"ok": False, "errorCode": "timeout",
                    "error": "YouTube took too long. Try again."}
    except Exception:
        # Upstream errors (including ValueError) can expose signed URLs or cookies.
        response = {"ok": False, "errorCode": "provider_unavailable",
                    "error": "YouTube is unavailable for this request. Try again."}
    sys.stdout.buffer.write(json.dumps(response, ensure_ascii=True).encode("utf-8"))


if __name__ == "__main__":
    main()
