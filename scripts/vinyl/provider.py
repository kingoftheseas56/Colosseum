"""Anonymous Vinyl provider. One JSON request on stdin, one JSON response on stdout.

Catalogue identities are durable; resolved URLs and headers are transient. Never
write the resolver response to the library, progress store, logs, or a snapshot.
"""
import datetime
import json
import math
import re
import socket
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import uuid
from urllib.parse import urlparse

NAMESPACE = uuid.UUID("9b7255c4-9aec-51d6-8905-e55de3fa8209")
MBID_RE = re.compile(r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")
MB_USER_AGENT = "ColosseumVinylPrototype/0.1 (artist-page catalogue check)"
MB_MIN_INTERVAL_SECONDS = 1.1
SKIP_SECONDARY = {"Remix", "DJ-mix", "Mixtape/Street", "Interview", "Demo", "Spokenword"}
EDITION_RE = r"(edition|anniversary|deluxe|expanded|remaster(ed)?)"
SHELF_TITLES = {
    "albums": "Albums",
    "singles": "Singles & EPs",
    "compilations": "Compilations",
    "soundtracks": "Soundtracks",
    "live": "Live albums",
    "other": "Other releases",
}
SHELF_ORDER = ["albums", "singles", "compilations", "soundtracks", "live", "other"]
YTM_VIDEO_TYPES = frozenset({
    "MUSIC_VIDEO_TYPE_ATV", "MUSIC_VIDEO_TYPE_OMV", "MUSIC_VIDEO_TYPE_UGC",
})
LRCLIB_ENDPOINT = "https://lrclib.net/api/get"
LRCLIB_USER_AGENT = "Colosseum Vinyl/2.0 (first-party lyrics lookup)"
LRCLIB_TIMEOUT_SECONDS = 8
LRCLIB_MAX_RESPONSE_BYTES = 512 * 1024
LRCLIB_MAX_METADATA_CHARS = 300
LRCLIB_MAX_LYRICS_CHARS = 256 * 1024

VIDEO_FORMAT_SELECTOR = ("bv[height<=1080][protocol=https]+ba[protocol=https]"
                         "/b[height<=1080][protocol=https]")
VIDEO_URL_MAX_CHARS = 8192
VIDEO_HEADER_TOKEN_RE = re.compile(r"[!#$%&'*+\-.^_`|~0-9A-Za-z]+")
VIDEO_MAX_HEADERS = 32
VIDEO_MAX_HEADER_NAME_CHARS = 128
VIDEO_MAX_HEADER_VALUE_CHARS = 4096
VIDEO_MAX_TOTAL_HEADER_CHARS = 16384


class ProviderError(ValueError):
    """Typed provider failure carrying a stable error code."""

    def __init__(self, message, code="provider_error"):
        super().__init__(message)
        self.code = code

    def to_dict(self):
        return {"ok": False, "error": str(self), "errorCode": self.code}


class InvalidArgumentError(ProviderError):
    def __init__(self, message="Invalid provider request"):
        super().__init__(message, code="invalid_argument")


class ArtistNotFoundError(ProviderError):
    def __init__(self, message="Artist channel not found"):
        super().__init__(message, code="not_found")


class ProviderTimeoutError(ProviderError):
    def __init__(self, message="Provider request timed out"):
        super().__init__(message, code="timeout")


class ProviderUnavailableError(ProviderError):
    def __init__(self, message="Provider is unavailable"):
        super().__init__(message, code="unavailable")


class RateLimitError(ProviderError):
    def __init__(self, message="Provider rate limit exceeded", retry_after=60):
        super().__init__(message, code="rate_limited")
        self.retry_after = max(1, min(3600, int(retry_after)))

    def to_dict(self):
        return dict(super().to_dict(), retryAfterSeconds=self.retry_after)


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
            "type": record.get("type", "Album"),
            "artistIds": [("ytm:artist:" + a["id"]) if a.get("id") else ""
                          for a in record.get("artists", []) if a.get("name")],
            "providerIds": {"youtube-music": key}}


def normalize_album(record, key, track_offset=0):
    result = album_card(record, key)
    result["tracks"] = []
    for index, track in enumerate(record.get("tracks") or []):
        video = track.get("videoId") or ""
        normalized = {
            "id": identity("recording", video) if video else identity("unavailable", key + ":" + str(track_offset + index)),
            "videoId": video, "title": track.get("title") or "Unavailable track",
            "artists": artists(track) or result["artists"],
            "artistIds": [("ytm:artist:" + a["id"]) if a.get("id") else ""
                          for a in track.get("artists", []) if a.get("name")],
            "seconds": int(track.get("duration_seconds") or 0),
            "available": bool(video) and track.get("isAvailable", True),
            "explicit": bool(track.get("isExplicit"))}
        track_album = track.get("album")
        album_name = track_album.get("name") if isinstance(track_album, dict) else None
        if not isinstance(album_name, str) or not album_name.strip():
            album_name = record.get("title") if record.get("type", "Album") == "Album" else None
        if isinstance(album_name, str) and album_name.strip():
            normalized["albumName"] = album_name
        video_type = track.get("videoType")
        if isinstance(video_type, str) and video_type in YTM_VIDEO_TYPES:
            normalized["videoType"] = video_type
        result["tracks"].append(normalized)
    # A truncated album is not silently presented as a complete release.
    expected = record.get("trackCount")
    if isinstance(expected, int) and expected > len(result["tracks"]):
        raise ValueError("Provider returned an incomplete album")
    if not result["tracks"]:
        raise ValueError("This album has no available track listing")
    return result


DISCOVERY_KEYS = {"new-releases": ("New releases", "albums"),
                  "trending": ("Trending", "songs"),
                  "artists": ("Chart artists", "artists"),
                  "charts": ("Charts", "catalogue-playlists")}


def requested_region(request):
    region = request.get("region", "")
    if region is None or region == "":
        return ""
    if not isinstance(region, str) or not re.fullmatch(r"[A-Z]{2}", region):
        raise InvalidArgumentError("Market must be a two-letter uppercase code")
    return region


def ytmusic_location_options(region):
    return {"location": region} if region else {}


def source_key(value):
    return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,160}", value)


def song_record(raw):
    video = raw.get("videoId", "")
    if any("podcast" in str(raw.get(field, "")).lower() for field in ("resultType", "type", "category")):
        return None
    video_type = raw.get("videoType")
    if (not isinstance(video, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", video)
            or not raw.get("title") or raw.get("isAvailable") is False
            or ("videoType" in raw and (not isinstance(video_type, str) or video_type not in YTM_VIDEO_TYPES))):
        return None
    record = normalize_album({"title": raw["title"], "artists": raw.get("artists", []),
                              "thumbnails": raw.get("thumbnails", []), "type": "Song",
                              "tracks": [raw]}, "track:" + video)
    record["browseId"] = "ytm:track:" + video
    # Standalone catalogue recordings are not fabricated album releases.
    record["providerIds"] = {"youtube-music": video}
    return record


def playlist_card(raw):
    key = raw.get("playlistId") or raw.get("browseId", "")
    if isinstance(key, str) and key.startswith("VL"):
        key = key[2:]
    if not source_key(key) or not raw.get("title"):
        return None
    return {"id": identity("playlist", key), "browseId": "ytm:playlist:" + key,
            "playlistId": key, "origin": "youtube-music", "type": "Playlist",
            "title": raw["title"], "artists": [], "artistIds": [], "cover": cover(raw),
            "providerIds": {"youtube-music": key}, "tracks": []}


def fetch_discovery(client, key, region, fetched_at=None):
    if key in {"artists", "charts"} and not region:
        raise InvalidArgumentError("Market-specific discovery requires an explicit market")
    title, kind = DISCOVERY_KEYS[key]
    raw = _call_provider(client.get_explore) if key in {"new-releases", "trending"} else _call_provider(client.get_charts, region)
    if not isinstance(raw, dict):
        raise ProviderUnavailableError("The discovery response is invalid. Retry the catalogue.")
    values = raw.get({"new-releases": "new_releases", "trending": "trending",
                      "artists": "artists", "charts": "videos"}[key], [])
    if key == "trending" and isinstance(values, dict):
        values = values.get("items", [])
    if not isinstance(values, list):
        raise ProviderUnavailableError("The discovery shelf is invalid. Retry the catalogue.")
    items, records, seen = [], [], set()
    for value in values:
        if not isinstance(value, dict):
            continue
        record = None
        if key == "new-releases":
            if not source_key(value.get("browseId")) or not value.get("title"):
                continue
            record = album_card(value)
            item = dict(record, kind=kind, albumId=record["id"], subtitle=" · ".join(record["artists"]))
        elif key == "trending":
            record = song_record(value)
            if not record:
                continue
            track = record["tracks"][0]
            item = {"id": track["id"], "kind": kind, "albumId": record["id"],
                    "trackId": track["id"], "trackIndex": 0, "title": track["title"],
                    "cover": record["cover"], "subtitle": " · ".join(record["artists"]),
                    "origin": "youtube-music"}
        elif key == "artists":
            channel = value.get("browseId", "")
            if not isinstance(channel, str) or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", channel) or not value.get("title"):
                continue
            item = {"id": "ytm:artist:" + channel, "artistId": "ytm:artist:" + channel,
                    "kind": kind, "title": value["title"], "artist": value["title"],
                    "cover": cover(value), "round": True, "origin": "youtube-music"}
        else:
            record = playlist_card(value)
            if not record:
                continue
            item = dict(record, kind=kind, albumId=record["id"], subtitle="YouTube Music")
        if item["id"] in seen:
            continue
        seen.add(item["id"])
        items.append(item)
        if record:
            records.append(record)
        if len(items) == 40:
            break
    return {"version": 1, "key": key, "title": title, "kind": kind, "items": items,
            "records": records, "source": "youtube-music", "region": region,
            "fetchedAt": now_iso(fetched_at), "complete": False}


def fetch_playlist(client, key, region=""):
    cursor, tracks, seen = {}, [], set()
    while True:
        page = fetch_playlist_page(client, {"cursor": cursor}, key, region)
        tracks.extend(page["tracks"])
        if page["complete"]:
            page["tracks"] = tracks
            page["offset"] = 0
            return page
        cursor = page["nextCursor"]
        if cursor["token"] in seen:
            raise ProviderUnavailableError("The playlist continuation repeated. Open it again.")
        seen.add(cursor["token"])


PAGE_KINDS = {"albums", "artists", "songs", "playlists"}


def renderer_nodes(value, name):
    if isinstance(value, dict):
        if name in value:
            yield value[name]
        for child in value.values():
            yield from renderer_nodes(child, name)
    elif isinstance(value, list):
        for child in value:
            yield from renderer_nodes(child, name)


def first_page(client, method, *args, **kwargs):
    # The bundled parser returns the initial server page even at limit=0.
    # Capture its continuation while preventing automatic follow-up requests.
    responses, original = [], client._send_request
    def request(*request_args, **request_kwargs):
        response = original(*request_args, **request_kwargs)
        responses.append(response)
        return response
    client._send_request = request
    try:
        parsed = method(*args, limit=0, **kwargs)
    finally:
        client._send_request = original
    if len(responses) != 1:
        raise ProviderUnavailableError("The catalogue paging protocol changed. Retry the catalogue.")
    return parsed, responses[0]


def read_search_page(client, query, kind, token):
    from ytmusicapi.parsers.search import parse_search_results, get_search_params
    from ytmusicapi.continuations import get_continuation_string
    if token:
        response = client._send_request("search", {"query": query, "params": get_search_params(kind, None, False)}, get_continuation_string(token))
        shelf = response.get("continuationContents", {}).get("musicShelfContinuation")
        if not isinstance(shelf, dict):
            raise ProviderUnavailableError("The search page expired. Search again to refresh it.")
        items = parse_search_results(shelf.get("contents", []), kind[:-1], kind.title())
    else:
        items, response = first_page(client, client.search, query, filter=kind)
        shelves = list(renderer_nodes(response, "musicShelfRenderer"))
        shelf = next((s for s in shelves if s.get("continuations")), {})
    continuations = shelf.get("continuations", [])
    continuation = continuations[0].get("nextContinuationData", {}).get("continuation", "") if continuations else ""
    return {"items": items, "continuation": continuation}


def read_playlist_page(client, key, token):
    from ytmusicapi.continuations import get_continuation_token
    if token:
        response = client._send_request("browse", {"continuation": token})
        actions = response.get("onResponseReceivedActions", [])
        contents = next((a["appendContinuationItemsAction"]["continuationItems"] for a in actions if "appendContinuationItemsAction" in a), None)
        if contents is None:
            raise ProviderUnavailableError("The playlist page expired. Open the playlist again.")
        items, meta = playlist_page_items(contents), {}
    else:
        meta, response = first_page(client, client.get_playlist, key)
        items = meta.get("tracks", [])
        shelves = list(renderer_nodes(response, "musicPlaylistShelfRenderer"))
        contents = shelves[0].get("contents", []) if shelves else []
        items = playlist_page_items(contents)
    continuation = get_continuation_token(contents) if contents else ""
    return {"items": items, "meta": meta, "continuation": continuation or ""}


def playlist_page_items(contents):
    from ytmusicapi.parsers.playlists import parse_playlist_items
    result = []
    for row in contents:
        parsed = parse_playlist_items([row])
        if parsed:
            result.extend(parsed)
        elif "musicResponsiveListItemRenderer" in row:
            # The upstream parser skips deleted/private rows. Keep any row the
            # server actually supplies at its original playlist position.
            result.append({"videoId": None, "title": "Unavailable track", "isAvailable": False})
    return result


def page_cursor(operation, value, kind, region, token, offset, meta=None):
    if not token:
        return {}
    return {"version": 1, "operation": operation, "value": value, "kind": kind,
            "region": region, "token": token, "offset": offset, "meta": meta or {}}


def validate_page_request(request, operation, value, kind, region):
    cursor = request.get("cursor") or {}
    if not isinstance(cursor, dict) or not isinstance(region, str) or (region and not re.fullmatch(r"[A-Z]{2}", region)):
        raise InvalidArgumentError("Invalid catalogue page")
    if not cursor:
        return "", 0, {}
    token, offset = cursor.get("token"), cursor.get("offset")
    if (cursor.get("version") != 1 or cursor.get("operation") != operation
            or cursor.get("value") != value or cursor.get("kind") != kind or cursor.get("region") != region
            or not isinstance(token, str) or not re.fullmatch(r"[A-Za-z0-9_+%./=,-]{1,16384}", token)
            or not isinstance(offset, int) or isinstance(offset, bool) or not 0 < offset < 10000
            or not isinstance(cursor.get("meta", {}), dict)):
        raise InvalidArgumentError("This page belongs to a different catalogue request. Search or open the playlist again.")
    return token, offset, cursor.get("meta", {})


def fetch_catalogue_page(client, request, value, region):
    kind = request["kind"]
    token, offset, _ = validate_page_request(request, "cataloguePage", value, kind, region)
    raw = _call_provider(read_search_page, client, value, kind, token)
    if not isinstance(raw, dict):
        raise ProviderUnavailableError("The search page is invalid.")
    values, continuation = raw.get("items"), raw.get("continuation", "")
    if not isinstance(values, list) or len(values) > 200 or not isinstance(continuation, str) or (token and token == continuation):
        raise ProviderUnavailableError("The catalogue page did not advance. Search again.")
    items, records, seen = [], [], set()
    for entry in values:
        if not isinstance(entry, dict):
            continue
        record = None
        if kind == "songs":
            record = song_record(entry)
            if not record:
                continue
            track = record["tracks"][0]
            item = {"id": track["id"], "kind": "songs", "albumId": record["id"], "trackId": track["id"], "trackIndex": 0,
                    "title": track["title"], "cover": record["cover"], "subtitle": " · ".join(record["artists"])}
        elif kind == "artists":
            channel = entry.get("browseId", "")
            if not isinstance(channel, str) or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", channel):
                continue
            item = {"id": "ytm:artist:" + channel, "artistId": "ytm:artist:" + channel, "kind": "artists",
                    "title": entry.get("artist") or entry.get("title") or "", "cover": cover(entry), "round": True}
            item["artist"] = item["title"]
        elif kind == "playlists":
            record = playlist_card(entry)
            if not record:
                continue
            item = dict(record, kind="catalogue-playlists", albumId=record["id"], subtitle="YouTube Music")
        else:
            if not source_key(entry.get("browseId")) or not entry.get("title"):
                continue
            record = album_card(entry)
            item = dict(record, kind="albums", albumId=record["id"], subtitle=" · ".join(record["artists"]))
        if not item["title"] or item["id"] in seen:
            continue
        seen.add(item["id"]);items.append(item)
        if record:
            records.append(record)
    next_offset = offset + len(values)
    if continuation and (not values or next_offset >= 10000):
        raise ProviderUnavailableError("The catalogue page limit was reached. Refine your search.")
    return {"version": 1, "query": value, "kind": kind, "source": "youtube-music", "region": region,
            "items": items, "records": records, "offset": offset, "nextOffset": next_offset,
            "nextCursor": page_cursor("cataloguePage", value, kind, region, continuation, next_offset),
            "complete": not bool(continuation), "fetchedAt": now_iso(request.get("fetchedAt"))}


def fetch_playlist_page(client, request, key, region):
    token, offset, prior_meta = validate_page_request(request, "playlistPage", key, "tracks", region)
    raw = _call_provider(read_playlist_page, client, key, token)
    if not isinstance(raw, dict):
        raise ProviderUnavailableError("The playlist page is invalid.")
    tracks, continuation = raw.get("items"), raw.get("continuation", "")
    if not isinstance(tracks, list) or len(tracks) > 200 or not isinstance(continuation, str) or (token and token == continuation):
        raise ProviderUnavailableError("The playlist page did not advance. Open it again.")
    meta = prior_meta if token else raw.get("meta", {})
    if not isinstance(meta, dict) or not meta.get("title"):
        raise ProviderUnavailableError("The playlist page has no metadata.")
    total = meta.get("trackCount")
    if total is not None and (not isinstance(total, int) or isinstance(total, bool) or total < 0):
        raise ProviderUnavailableError("The playlist track count is invalid.")
    if total is not None and total > 10000:
        raise ProviderUnavailableError("This playlist exceeds the 10,000-track queue limit.")
    next_offset = offset + len(tracks)
    if next_offset > 10000 or (continuation and (not tracks or next_offset >= 10000)):
        raise ProviderUnavailableError("This playlist exceeds the 10,000-track queue limit.")
    # Audio-list parsers sometimes derive trackCount from the first page itself.
    if continuation and total is not None and total <= next_offset:
        total = None
    omitted = max(0, total - next_offset) if not continuation and total is not None else 0
    record = normalize_album({"title": meta["title"], "thumbnails": meta.get("thumbnails", []), "tracks": tracks}, key, offset) if tracks else dict(playlist_card({"playlistId": key, "title": meta["title"]}), tracks=[])
    public_meta = {"title": meta["title"], "thumbnails": meta.get("thumbnails", []), "trackCount": total}
    record.update({"version": 1, "id": identity("playlist", key), "browseId": "ytm:playlist:" + key,
                   "playlistId": key, "type": "Playlist", "offset": offset, "nextOffset": next_offset,
                   "totalTracks": total if total is not None else (next_offset if not continuation else None),
                   "omittedTracks": omitted,
                   "source": "youtube-music", "region": region,
                   "nextCursor": page_cursor("playlistPage", key, "tracks", region, continuation, next_offset, public_meta),
                   "complete": not bool(continuation)})
    return record


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


def resolve_video(video):
    """Qualify one exact caller-supplied YouTube video ID for native playback.

    The caller owns matching; this transport never searches or substitutes.
    """
    if not isinstance(video, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", video):
        raise InvalidArgumentError("Invalid YouTube video ID")
    from yt_dlp import YoutubeDL
    class QuietLogger:
        def debug(self, message): pass
        def warning(self, message): pass
        def error(self, message): pass
    with YoutubeDL({"quiet": True, "no_warnings": True, "logger": QuietLogger(),
                    "format": VIDEO_FORMAT_SELECTOR, "skip_download": True,
                    "noplaylist": True, "socket_timeout": 20, "retries": 1,
                    "extractor_retries": 1}) as client:
        data = client.extract_info("https://www.youtube.com/watch?v=" + video, download=False)
    return _validated_video_resolution(video, data)


def _video_codec(value):
    return isinstance(value, str) and value not in ("", "none")


def _video_stream_kind(fmt):
    has_video, has_audio = _video_codec(fmt.get("vcodec")), _video_codec(fmt.get("acodec"))
    if has_video and has_audio:
        return "muxed"
    if has_video:
        return "video"
    if has_audio:
        return "audio"
    return None


def _video_headers(raw):
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError("Provider returned unsupported HTTP headers")
    if len(raw) > VIDEO_MAX_HEADERS:
        raise ValueError("Provider returned unsupported HTTP headers")
    total = 0
    names = set()
    for name, value in raw.items():
        if (not isinstance(name, str) or not name or len(name) > VIDEO_MAX_HEADER_NAME_CHARS
                or not VIDEO_HEADER_TOKEN_RE.fullmatch(name)):
            raise ValueError("Provider returned unsupported HTTP headers")
        if name.lower() in names:
            raise ValueError("Provider returned duplicate HTTP headers")
        names.add(name.lower())
        if (not isinstance(value, str) or not value.strip() or len(value) > VIDEO_MAX_HEADER_VALUE_CHARS
                or any(ord(ch) < 0x20 or 0x7f <= ord(ch) <= 0x9f for ch in value)):
            raise ValueError("Provider returned unsupported HTTP headers")
        total += len(name) + len(value)
    if total > VIDEO_MAX_TOTAL_HEADER_CHARS:
        raise ValueError("Provider returned unsupported HTTP headers")
    return dict(raw)


def _video_stream_headers(data, fmt):
    # A requested format inherits the info dict's common headers when it carries none.
    raw = fmt["http_headers"] if "http_headers" in fmt else data.get("http_headers")
    return _video_headers(raw)


def _video_media_url(url):
    if (not isinstance(url, str) or not url or len(url) > VIDEO_URL_MAX_CHARS
            or re.search(r"[\s\x00-\x1f\x7f-\x9f]", url)):
        raise ValueError("Provider returned an unsupported video location")
    parsed = urlparse(url)
    if (parsed.scheme != "https" or not (parsed.hostname or "").endswith(".googlevideo.com")
            or parsed.username is not None or parsed.password is not None
            or parsed.fragment or parsed.port not in (None, 443)):
        raise ValueError("Provider returned an unsupported video location")
    return url


def _video_expiry_ms(urls, now_ms):
    earliest = None
    for url in urls:
        for raw in urllib.parse.parse_qs(urlparse(url).query, keep_blank_values=True).get("expire", []):
            try:
                seconds = float(raw)
            except ValueError:
                raise ValueError("Provider returned an invalid stream expiry") from None
            # A known expiry must be a plausible future epoch timestamp, never invented.
            if not math.isfinite(seconds) or not 0 < seconds < 1e12:
                raise ValueError("Provider returned an invalid stream expiry")
            if earliest is None or seconds < earliest:
                earliest = seconds
    if earliest is None:
        return None
    expires_ms = int(earliest * 1000)
    if expires_ms <= now_ms:
        raise ValueError("Provider returned an expired stream location")
    return expires_ms


def _validated_video_resolution(video, data):
    if not isinstance(data, dict):
        raise ValueError("Provider returned an invalid video response")
    if data.get("id") != video:
        raise ValueError("Provider returned a different video")
    if data.get("_type") not in (None, "video") or "entries" in data:
        raise ValueError("Provider returned more than a single video")
    if data.get("is_live") or data.get("live_status") == "is_live":
        raise ValueError("Provider returned an ongoing live stream")
    requested = data.get("requested_formats")
    separate_streams = False
    if isinstance(requested, list) and requested:
        if len(requested) > 2 or any(not isinstance(fmt, dict) for fmt in requested):
            raise ValueError("Provider returned an ambiguous stream selection")
        kinds = [_video_stream_kind(fmt) for fmt in requested]
        separate_streams = len(requested) == 2
        if separate_streams:
            if set(kinds) != {"audio", "video"}:
                raise ValueError("Provider did not return one video and one audio stream")
            video_format = requested[kinds.index("video")]
            audio_format = requested[kinds.index("audio")]
        elif kinds != ["muxed"]:
            raise ValueError("Provider returned an ambiguous stream selection")
        else:
            video_format = audio_format = requested[0]
        for fmt in (video_format, audio_format):
            if fmt.get("protocol") not in (None, "https"):
                raise ValueError("Provider returned an unsupported stream transport")
        url = _video_media_url(video_format.get("url"))
        audio_url = _video_media_url(audio_format.get("url"))
        height = video_format.get("height")
        headers = _video_stream_headers(data, video_format)
        # The mpv core installs one header map for both resources.
        if {name.lower(): value for name, value in headers.items()} != {
                name.lower(): value for name, value in _video_stream_headers(data, audio_format).items()}:
            raise ValueError("The separate streams require different HTTP headers")
    else:
        if _video_stream_kind(data) != "muxed":
            raise ValueError("Provider returned no playable video stream")
        if data.get("protocol") not in (None, "https"):
            raise ValueError("Provider returned an unsupported stream transport")
        url = _video_media_url(data.get("url"))
        audio_url = url
        height = data.get("height")
        headers = _video_headers(data.get("http_headers"))
    if (not isinstance(height, (int, float)) or isinstance(height, bool)
            or not math.isfinite(height) or not 0 < height <= 1080):
        raise ValueError("Provider returned an unbounded video resolution")
    now_ms = int(time.time() * 1000)
    result = {"version": 1, "videoId": video, "url": url,
              "audioUrl": audio_url if separate_streams else "",
              "headers": headers, "hasVideo": True, "hasAudio": True,
              "qualityLabel": str(int(height)) + "p", "resolvedAtMs": now_ms}
    expires_ms = _video_expiry_ms(dict.fromkeys((url, audio_url)), now_ms)
    if expires_ms is not None:
        result["expiresAtMs"] = expires_ms
    duration = data.get("duration")
    if (isinstance(duration, (int, float)) and not isinstance(duration, bool)
            and math.isfinite(duration) and duration > 0):
        result["durationMs"] = int(round(duration * 1000))
    return result


def now_iso(fetched_at=None):
    if fetched_at:
        return str(fetched_at)
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def biggest(thumbs):
    valid = [t for t in (thumbs or []) if isinstance(t, dict)]
    return max(valid, key=lambda t: t.get("width", 0) or 0) if valid else {}


def secs(duration_text):
    if not duration_text:
        return None
    if isinstance(duration_text, (int, float)):
        return int(duration_text)
    parts = [int(x) for x in str(duration_text).split(":")]
    return sum(v * (60 ** i) for i, v in enumerate(reversed(parts)))


def fold(text):
    s = unicodedata.normalize("NFKD", str(text or "").replace("\u2019", "'").replace("\u2010", "-"))
    return "".join(c for c in s if not unicodedata.combining(c)).lower()


def key1(title):
    return re.sub(r"[^a-z0-9\u0900-\u097f]", "", re.sub(r"\s*[\(\[].*?[\)\]]", "", fold(title)))


def key2(title):
    base = re.sub(r"\s*[\(\[].*?[\)\]]", "", fold(title))
    base = re.split(r"\s+[-\u2013]\s+|:\s", base)[0]
    return re.sub(r"[^a-z0-9\u0900-\u097f]", "", base)


def key3(title):
    return re.sub(r"[^a-z0-9]", "", key2(title))


def mb_match(title, primaries, groups):
    cands = [
        g for g in (groups or [])
        if g.get("primary") in primaries and not (set(g.get("secondary") or []) & SKIP_SECONDARY)
    ]
    for kf in (key1, key2, key3):
        target = kf(title)
        if not target:
            continue
        hits = [g for g in cands if kf(g.get("title") or "") == target]
        if hits:
            return sorted(hits, key=lambda g: g.get("first") or "9999")[0], kf.__name__
    return None, None


def category(group, ytype=None):
    if not group:
        return "other"
    sec = set(group.get("secondary") or [])
    if group.get("primary") in ("Single", "EP"):
        return "singles"
    if "Soundtrack" in sec:
        return "soundtracks"
    if "Compilation" in sec:
        return "compilations"
    if "Live" in sec:
        return "live"
    return "albums"


def display_title(title):
    t = re.sub(r"\s*[\(\[][^\)\]]*" + EDITION_RE + r"[^\)\]]*[\)\]]", "", str(title or ""), flags=re.I)
    t = re.sub(r"\s*(:|\s-)\s[^:]*" + EDITION_RE + r".*$", "", t, flags=re.I)
    return t.strip()


def validate_channel_id(channel_id):
    cid = str(channel_id or "").strip()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,160}", cid):
        raise InvalidArgumentError("Invalid artist channel ID")
    return cid


def _is_timeout_exception(exc):
    if isinstance(exc, (TimeoutError, socket.timeout)):
        return True
    if isinstance(exc, urllib.error.URLError) and isinstance(getattr(exc, "reason", None), (TimeoutError, socket.timeout)):
        return True
    msg = f"{type(exc).__name__} {exc}".lower()
    return "timeout" in msg or "timed out" in msg


def _is_not_found_exception(exc):
    if isinstance(exc, (LookupError, KeyError)):
        return True
    if isinstance(exc, urllib.error.HTTPError) and exc.code == 404:
        return True
    msg = str(exc).lower()
    return any(tok in msg for tok in ("404", "not found", "unable to find", "no such artist", "invalid channel", "does not exist"))


def _call_provider(fn, *args, not_found_on_error=False, **kwargs):
    try:
        return fn(*args, **kwargs)
    except ProviderError:
        raise
    except Exception as exc:
        status = getattr(exc, "code", None) if isinstance(exc, urllib.error.HTTPError) else getattr(getattr(exc, "response", None), "status_code", None)
        if status in (429, 503):
            headers = getattr(exc, "headers", None) or getattr(getattr(exc, "response", None), "headers", {})
            try:
                retry = int(headers.get("Retry-After", 60))
            except (TypeError, ValueError):
                retry = 60
            raise RateLimitError(retry_after=retry) from exc
        if _is_timeout_exception(exc):
            raise ProviderTimeoutError("YouTube Music request timed out.") from exc
        if _is_not_found_exception(exc):
            raise ArtistNotFoundError("Artist channel not found.") from exc
        if not_found_on_error and isinstance(exc, ValueError):
            raise ArtistNotFoundError("Artist channel not found.") from exc
        raise ProviderUnavailableError("YouTube Music is unavailable for this request.") from exc


def _continuation_handle(section, require_params=False):
    if not isinstance(section, dict):
        return None
    browse_id = section.get("browseId")
    params = section.get("params")
    if not browse_id:
        return None
    if require_params and not params:
        return None
    return {"browseId": browse_id, "params": params}


def normalize_artist(raw_artist, songs_playlist=None, requested_channel_id=None, fetched_at=None):
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    resolved_cid = raw_artist.get("channelId") or requested_channel_id
    if not resolved_cid:
        raise ArtistNotFoundError("Artist has no channel identity.")
    artist_name = raw_artist["name"]

    images = []
    for thumb in raw_artist.get("thumbnails") or []:
        if not isinstance(thumb, dict) or not thumb.get("url"):
            continue
        images.append({
            "url": thumb["url"],
            "width": int(thumb["width"]) if thumb.get("width") is not None else None,
            "height": int(thumb["height"]) if thumb.get("height") is not None else None,
        })

    songs_sec = raw_artist.get("songs") if isinstance(raw_artist.get("songs"), dict) else {}
    shelf_results = songs_sec.get("results") or []
    pl_tracks = (songs_playlist or {}).get("tracks") if isinstance(songs_playlist, dict) else None
    pl_list = pl_tracks if isinstance(pl_tracks, list) else []

    if pl_list:
        pl_by_vid = {t.get("videoId"): t for t in pl_list if isinstance(t, dict) and t.get("videoId")}
        shelf_vids = set()
        candidates = []
        for s in shelf_results:
            if not isinstance(s, dict):
                continue
            vid = s.get("videoId")
            if vid:
                shelf_vids.add(vid)
            candidates.append(pl_by_vid.get(vid, s) if vid else s)
        for t in pl_list:
            if isinstance(t, dict) and t.get("videoId") not in shelf_vids:
                candidates.append(t)
    else:
        candidates = list(shelf_results)

    seen_titles = set()
    songs = []
    for index, track in enumerate(candidates):
        if not isinstance(track, dict):
            continue
        video_id = track.get("videoId")
        title = track.get("title") or ""
        norm_key = key1(title)
        if not video_id or not norm_key or norm_key in seen_titles:
            continue
        seen_titles.add(norm_key)
        album_obj = track.get("album") if isinstance(track.get("album"), dict) else {}
        dur_text = track.get("duration")
        dur_seconds = secs(dur_text) if dur_text else (
            int(track["duration_seconds"]) if track.get("duration_seconds") is not None else None
        )
        other_artists = [
            a["name"] for a in (track.get("artists") or [])
            if isinstance(a, dict) and a.get("name") and a["name"] != artist_name
        ]
        thumb_url = biggest(track.get("thumbnails")).get("url") if track.get("thumbnails") else None
        songs.append({
            "id": identity("recording", video_id),
            "rank": len(songs) + 1,
            "videoId": video_id,
            "title": title,
            "album": album_obj.get("name"),
            "albumName": album_obj.get("name"),
            "albumId": album_obj.get("id"),
            "duration": dur_text,
            "seconds": dur_seconds,
            "explicit": bool(track.get("isExplicit")),
            "artists": artists(track),
            "with": other_artists,
            "thumbnail": thumb_url,
            "provider": "artist songs shelf" if index < len(shelf_results) else "artist songs playlist (Show all continuation)",
        })
        if len(songs) == 10:
            break

    related = []
    related_sec = raw_artist.get("related") if isinstance(raw_artist.get("related"), dict) else {}
    for rel in related_sec.get("results") or []:
        if not isinstance(rel, dict) or not rel.get("browseId"):
            continue
        rel_name = rel.get("title") or rel.get("name") or ""
        if not rel_name:
            continue
        rel_thumb = biggest(rel.get("thumbnails")).get("url") if rel.get("thumbnails") else None
        related.append({
            "id": rel["browseId"],
            "browseId": rel["browseId"],
            "name": rel_name,
            "thumbnail": rel_thumb,
        })

    albums_handle = _continuation_handle(raw_artist.get("albums"), require_params=True)
    singles_handle = _continuation_handle(raw_artist.get("singles"), require_params=True)
    videos_handle = _continuation_handle(raw_artist.get("videos"), require_params=False)
    songs_handle = _continuation_handle(songs_sec, require_params=False)
    continuations = {
        "albums": albums_handle,
        "singles": singles_handle,
        "releases": {
            "albums": albums_handle,
            "singles": singles_handle,
        },
        "videos": videos_handle,
        "songs": songs_handle,
    }

    songs_complete = bool(pl_list) or not bool(songs_sec.get("browseId"))
    return {
        "id": identity("artist", resolved_cid),
        "channelId": resolved_cid,
        "requestedChannelId": requested_channel_id or resolved_cid,
        "name": artist_name,
        "biography": str(raw_artist.get("description") or "")[:50000],
        "images": images,
        "thumbnails": images,
        "songs": songs,
        "related": related,
        "relatedArtists": related,
        "continuations": continuations,
        "continuationHandles": continuations,
        "source": "get_artist",
        "origin": "youtube-music",
        "fetchedAt": now_iso(fetched_at),
        "complete": songs_complete,
    }


def fetch_artist(client, channel_id, fetched_at=None):
    cid = validate_channel_id(channel_id)
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    songs_sec = raw_artist.get("songs") if isinstance(raw_artist.get("songs"), dict) else {}
    songs_pl = None
    if songs_sec.get("browseId") and hasattr(client, "get_playlist"):
        songs_pl = _call_provider(client.get_playlist, songs_sec["browseId"], limit=25)
        if songs_pl is None or not isinstance(songs_pl, dict) or not isinstance(songs_pl.get("tracks"), list):
            raise ProviderUnavailableError("Failed to fetch artist songs playlist.")
    return normalize_artist(raw_artist, songs_playlist=songs_pl, requested_channel_id=cid, fetched_at=fetched_at)


def fetch_artist_releases(client, channel_id, kind, fetched_at=None):
    cid = validate_channel_id(channel_id)
    norm_kind = str(kind or "").strip().lower()
    if norm_kind not in ("albums", "singles"):
        raise InvalidArgumentError("Invalid release kind; expected 'albums' or 'singles'.")
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    resolved_cid = raw_artist.get("channelId") or cid
    sec = raw_artist.get(norm_kind)
    if not isinstance(sec, dict):
        raise ProviderUnavailableError(f"Artist response missing '{norm_kind}' shelf.")

    if sec.get("browseId") and sec.get("params"):
        raw_items = _call_provider(
            client.get_artist_albums,
            sec["browseId"],
            sec["params"],
            limit=None,
        )
        if not isinstance(raw_items, list):
            raise ProviderUnavailableError(f"Provider returned no '{norm_kind}' continuation list.")
        source = "get_artist_albums"
        complete = True
    else:
        raw_items = sec.get("results")
        if not isinstance(raw_items, list):
            raise ProviderUnavailableError(f"Provider returned an invalid '{norm_kind}' inline shelf.")
        source = "get_artist inline shelf"
        complete = not bool(sec.get("browseId"))

    default_type = "Album" if norm_kind == "albums" else "Single"
    items = []
    for entry in raw_items:
        if not isinstance(entry, dict) or not entry.get("browseId"):
            continue
        browse_id = entry["browseId"]
        thumb_url = biggest(entry.get("thumbnails")).get("url") if entry.get("thumbnails") else None
        raw_year = entry.get("year")
        year_str = str(raw_year) if raw_year is not None and str(raw_year) != "" else None
        items.append({
            "id": identity("release", browse_id),
            "browseId": browse_id,
            "title": entry.get("title") or "",
            "year": year_str,
            "type": entry.get("type") or default_type,
            "thumbnail": thumb_url,
            "cover": thumb_url or "",
            "explicit": bool(entry["isExplicit"]) if entry.get("isExplicit") is not None else None,
            "thumbnails": [
                {
                    "url": t["url"],
                    "width": int(t["width"]) if t.get("width") is not None else None,
                    "height": int(t["height"]) if t.get("height") is not None else None,
                }
                for t in (entry.get("thumbnails") or [])
                if isinstance(t, dict) and t.get("url")
            ],
        })

    return {
        "channelId": resolved_cid,
        "requestedChannelId": cid,
        "kind": norm_kind,
        "items": items,
        "releases": items,
        "source": source,
        "origin": "youtube-music",
        "fetchedAt": now_iso(fetched_at),
        "complete": complete,
    }


def fetch_artist_videos(client, channel_id, fetched_at=None):
    cid = validate_channel_id(channel_id)
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    resolved_cid = raw_artist.get("channelId") or cid
    vsec = raw_artist.get("videos")
    if not isinstance(vsec, dict):
        raise ProviderUnavailableError("Artist response missing 'videos' section.")

    playlist_id = vsec.get("browseId")
    if playlist_id:
        vpl = _call_provider(client.get_playlist, playlist_id, limit=None)
        if not isinstance(vpl, dict) or not isinstance(vpl.get("tracks"), list):
            raise ProviderUnavailableError("Failed to fetch artist videos playlist.")
        raw_videos = vpl["tracks"]
        source = "artist videos playlist"
        fallback = False
        fallback_reason = None
        complete = True
    else:
        raw_videos = vsec.get("results")
        if not isinstance(raw_videos, list):
            raise ProviderUnavailableError("Provider returned an invalid 'videos' inline shelf.")
        source = "artist page videos shelf (no playlist handle returned)"
        fallback = True
        fallback_reason = "no playlist handle returned"
        complete = False

    items, videos, live = [], [], []
    for v in raw_videos:
        if not isinstance(v, dict) or not v.get("videoId"):
            continue
        vid = v["videoId"]
        title = v.get("title") or ""
        dur_text = v.get("duration")
        dur_seconds = secs(dur_text) if dur_text else (
            int(v["duration_seconds"]) if v.get("duration_seconds") is not None else None
        )
        thumb_url = (
            biggest(v.get("thumbnails")).get("url")
            if v.get("thumbnails")
            else f"https://i.ytimg.com/vi/{vid}/mqdefault.jpg"
        )
        is_live = bool(re.search(r"\blive\b", title, re.I))
        rec = {
            "id": vid,
            "videoId": vid,
            "recordingId": identity("recording", vid),
            "title": title,
            "duration": dur_text,
            "seconds": dur_seconds,
            "thumbnail": thumb_url,
            "live": is_live,
        }
        items.append(rec)
        (live if is_live else videos).append(rec)

    return {
        "channelId": resolved_cid,
        "requestedChannelId": cid,
        "playlistId": playlist_id,
        "items": items,
        "videos": videos,
        "live": live,
        "livePerformances": live,
        "fallback": fallback,
        "fallbackReason": fallback_reason,
        "source": source,
        "origin": "youtube-music",
        "fetchedAt": now_iso(fetched_at),
        "complete": complete,
    }


class MusicBrainzClient:
    """Rate-limited MusicBrainz web service v2 client (>=1.1s interval, User-Agent, 503 backoff)."""

    def __init__(
        self,
        user_agent=MB_USER_AGENT,
        min_interval=MB_MIN_INTERVAL_SECONDS,
        urlopen_fn=None,
        sleep_fn=None,
        clock_fn=None,
        max_retries=5,
        backoff_base=1.5,
        timeout=30,
    ):
        if not user_agent or not str(user_agent).strip():
            raise InvalidArgumentError("MusicBrainz User-Agent header is required.")
        self.user_agent = str(user_agent).strip()
        self.min_interval = max(float(min_interval), MB_MIN_INTERVAL_SECONDS)
        self._urlopen = urlopen_fn or urllib.request.urlopen
        self._sleep = sleep_fn or time.sleep
        self._clock = clock_fn or time.monotonic
        self.max_retries = int(max_retries)
        self.backoff_base = float(backoff_base)
        self.timeout = timeout
        self._last_request_time = None
        self.request_log = []

    def _respect_rate_limit(self):
        if self._last_request_time is not None:
            elapsed = self._clock() - self._last_request_time
            if elapsed < self.min_interval:
                delay = self.min_interval - elapsed
                self._sleep(delay)

    def get_json(self, url):
        last_error = None
        for attempt in range(self.max_retries):
            self._respect_rate_limit()
            req = urllib.request.Request(
                url,
                headers={
                    "User-Agent": self.user_agent,
                    "Accept": "application/json",
                },
            )
            call_ts = self._clock()
            self._last_request_time = call_ts
            self.request_log.append({
                "url": url,
                "userAgent": req.get_header("User-agent"),
                "timestamp": call_ts,
                "attempt": attempt + 1,
            })
            try:
                with self._urlopen(req, timeout=self.timeout) as resp:
                    status = getattr(resp, "status", 200)
                    if status == 503:
                        raise urllib.error.HTTPError(url, 503, "Service Unavailable", {}, None)
                    body = resp.read(4 * 1024 * 1024 + 1)
                    if len(body) > 4 * 1024 * 1024:
                        raise ProviderUnavailableError("MusicBrainz response exceeded its limit.")
                    return json.loads(body)
            except urllib.error.HTTPError as exc:
                last_error = exc
                if exc.code == 429:
                    try:
                        retry = int((exc.headers or {}).get("Retry-After", 60))
                    except (TypeError, ValueError):
                        retry = 60
                    raise RateLimitError(retry_after=retry) from exc
                if exc.code == 503:
                    if attempt + 1 >= self.max_retries:
                        raise RateLimitError("MusicBrainz service unavailable (HTTP 503) after backoff.") from exc
                    self._sleep(self.backoff_base * (2 ** attempt))
                    continue
                if exc.code == 404:
                    raise ArtistNotFoundError("Artist not found in MusicBrainz.") from exc
                raise ProviderUnavailableError(f"MusicBrainz returned HTTP {exc.code}.") from exc
            except ProviderError:
                raise
            except Exception as exc:
                last_error = exc
                if _is_timeout_exception(exc):
                    raise ProviderTimeoutError("MusicBrainz request timed out.") from exc
                if _is_not_found_exception(exc):
                    raise ArtistNotFoundError("Artist not found in MusicBrainz.") from exc
                raise ProviderUnavailableError("MusicBrainz is unavailable for this request.") from exc
        raise ProviderUnavailableError("MusicBrainz request failed.") from last_error

    def fetch_release_groups(self, identifier):
        raw_id = str(identifier or "").strip()
        if not raw_id:
            raise InvalidArgumentError("Provide an artist MBID or name.")
        search_entries = None
        if raw_id.startswith("name:"):
            name_query = raw_id[5:].strip()
            if not name_query:
                raise InvalidArgumentError("Provide a non-empty artist name.")
            mbid = None
        elif MBID_RE.match(raw_id):
            mbid = raw_id.lower()
            name_query = None
        else:
            name_query = raw_id
            mbid = None

        if name_query is not None:
            encoded = urllib.parse.quote(f'artist:"{name_query}"')
            q = self.get_json(f"https://musicbrainz.org/ws/2/artist/?fmt=json&limit=5&query={encoded}")
            artists_found = q.get("artists") if isinstance(q, dict) else None
            if not isinstance(artists_found, list) or not artists_found:
                raise ArtistNotFoundError(f"Artist '{name_query}' not found in MusicBrainz.")
            search_entries = [
                {
                    "id": a["id"],
                    "name": a.get("name") or "",
                    "score": a.get("score"),
                    "country": a.get("country"),
                    "disambiguation": a.get("disambiguation"),
                }
                for a in artists_found
                if isinstance(a, dict) and a.get("id")
            ]
            if not search_entries:
                raise ArtistNotFoundError(f"Artist '{name_query}' not found in MusicBrainz.")
            exact_name = lambda text: unicodedata.normalize("NFKC", text).casefold().strip()
            exact = [a for a in search_entries if exact_name(a["name"]) == exact_name(name_query)
                     and str(a.get("score")) == "100"]
            if len(exact) != 1:
                raise ProviderUnavailableError("Artist identity is ambiguous. Catalogue browsing is still available.")
            mbid = exact[0]["id"]

        rgs = []
        offset = 0
        while True:
            page = self.get_json(
                f"https://musicbrainz.org/ws/2/release-group?artist={mbid}&fmt=json&limit=100&offset={offset}"
            )
            if not isinstance(page, dict) or not isinstance(page.get("release-groups"), list):
                raise ProviderUnavailableError("MusicBrainz returned an invalid release-group response.")
            page_items = page["release-groups"]
            rgs.extend(page_items)
            offset += 100
            total_count = page.get("release-group-count")
            if not page_items or (isinstance(total_count, int) and offset >= total_count):
                break

        normalized_rgs = [
            {
                "id": g["id"],
                "title": g.get("title") or "",
                "primary": g.get("primary") if "primary" in g else g.get("primary-type"),
                "secondary": g.get("secondary") if "secondary" in g else g.get("secondary-types", []),
                "first": g.get("first") if "first" in g else g.get("first-release-date", ""),
            }
            for g in rgs
            if isinstance(g, dict) and g.get("id")
        ]
        res = {
            "mbid": mbid,
            "releaseGroups": normalized_rgs,
            "count": len(normalized_rgs),
        }
        if search_entries is not None:
            res["search"] = search_entries
        return res


_DEFAULT_MB_CLIENT = MusicBrainzClient()


def map_releases_to_musicbrainz(mb_record, albums_items=None, singles_items=None, fetched_at=None):
    if not isinstance(mb_record, dict) or not mb_record.get("mbid"):
        raise ArtistNotFoundError("MusicBrainz artist identity is missing.")
    groups = mb_record.get("releaseGroups")
    if not isinstance(groups, list):
        raise ProviderUnavailableError("MusicBrainz release groups list is missing.")

    albums_list = list(albums_items or [])
    singles_list = list(singles_items or [])

    rel_items = {}
    unmatched = []
    mapped_releases = []

    for ytk, items_for_shelf in (("albums", albums_list), ("singles", singles_list)):
        primaries = ("Single", "EP") if ytk == "singles" else ("Album",)
        for x in items_for_shelf:
            if not isinstance(x, dict) or not x.get("browseId"):
                continue
            title = x.get("title") or ""
            browse_id = x["browseId"]
            g, how = mb_match(title, primaries, groups)
            cat_key = "singles" if ytk == "singles" else category(g, x.get("type"))
            gid = (
                g["id"]
                if g
                else "yt:" + ytk + ":" + key1(title) + (":" + str(x.get("year")) if ytk == "singles" else "")
            )
            match_label = how if g else "unmatched"
            if not g:
                unmatched.append({
                    "title": title,
                    "browseId": browse_id,
                    "ytShelf": ytk,
                    "matchedBy": "unmatched",
                })
            thumb_url = (
                x.get("thumbnail")
                or x.get("cover")
                or (biggest(x.get("thumbnails")).get("url") if x.get("thumbnails") else None)
            )
            yt_type = x.get("type") or ("Album" if ytk == "albums" else "Single")
            group_entry = rel_items.setdefault(
                gid,
                {
                    "id": gid,
                    "mbid": g["id"] if g else None,
                    "category": cat_key,
                    "title": g["title"] if g else title,
                    "mbTitle": g["title"] if g else None,
                    "year": (g["first"][:4] if g and g.get("first") else str(x.get("year") or "")),
                    "ytType": yt_type,
                    "editions": [],
                    "matched": bool(g is not None),
                    "matchedBy": match_label,
                    "matchKey": how,
                },
            )
            edition_rec = {
                "browseId": browse_id,
                "title": title,
                "year": str(x["year"]) if x.get("year") is not None and str(x.get("year")) != "" else None,
                "explicit": x.get("explicit") if "explicit" in x else x.get("isExplicit"),
                "thumb": thumb_url,
                "thumbnail": thumb_url,
                "matchedBy": match_label,
            }
            group_entry["editions"].append(edition_rec)
            mapped_releases.append({
                "browseId": browse_id,
                "title": title,
                "ytShelf": ytk,
                "type": yt_type,
                "year": edition_rec["year"],
                "thumbnail": thumb_url,
                "releaseGroupId": gid,
                "mbid": g["id"] if g else None,
                "mbTitle": g["title"] if g else None,
                "category": cat_key,
                "matched": bool(g is not None),
                "matchedBy": match_label,
                "matchKey": how,
            })

    shelves_by_cat = {}
    for it in rel_items.values():
        eds = it["editions"]
        rep = next(
            (
                e for e in eds
                if key1(e["title"]) == key1(it["title"])
                and fold(e["title"]).strip() == fold(re.sub(r"\s*[\(\[].*?[\)\]]", "", e["title"])).strip()
            ),
            None,
        ) or sorted(eds, key=lambda e: str(e.get("year") or ""))[0]
        it["browseId"] = rep["browseId"]
        it["title"] = display_title(rep["title"])
        it["thumbnail"] = rep["thumb"]
        it["editionCount"] = len(eds)
        if it["ytType"] == "EP" and it["category"] == "singles":
            it["kind"] = "EP"
        shelves_by_cat.setdefault(it["category"], []).append(it)

    page_shelves = []
    for k in SHELF_ORDER:
        if k not in shelves_by_cat:
            continue
        sorted_items = sorted(shelves_by_cat[k], key=lambda i: (i["year"] or "0", i["title"]), reverse=True)
        page_shelves.append({
            "key": k,
            "title": SHELF_TITLES[k],
            "items": [
                {
                    "id": i["browseId"],
                    "browseId": i["browseId"],
                    "mbid": i["mbid"],
                    "title": i["title"],
                    "year": i["year"],
                    "thumbnail": i["thumbnail"],
                    "editions": len(i["editions"]),
                    "kind": i.get("kind", ""),
                    "matched": i["matched"],
                    "matchedBy": i["matchedBy"],
                    "matchKey": i["matchKey"],
                }
                for i in sorted_items
            ],
        })

    after_grouping = {s["key"]: len(s["items"]) for s in page_shelves}
    return {
        "mbid": mb_record["mbid"],
        "matchedArtistBy": "name search, top score 100 (verify)" if mb_record.get("search") else "MBID browse",
        "search": mb_record.get("search"),
        "releaseGroupsBrowsed": len(groups),
        "releaseGroups": groups,
        "groups": list(rel_items.values()),
        "releases": mapped_releases,
        "shelves": page_shelves,
        "afterEditionGrouping": after_grouping,
        "unmatched": unmatched,
        "unmatchedInMusicBrainz": [
            {"title": u["title"], "browseId": u["browseId"], "ytShelf": u["ytShelf"]}
            for u in unmatched
        ],
        "source": "musicbrainz",
        "fetchedAt": now_iso(fetched_at),
        "complete": True,
    }


def fetch_artist_identity(
    identifier,
    client=None,
    channel_id=None,
    releases=None,
    mb_client=None,
    mb_data=None,
    fetched_at=None,
):
    active_mb = mb_client or _DEFAULT_MB_CLIENT
    mb_record = mb_data if isinstance(mb_data, dict) else active_mb.fetch_release_groups(identifier)

    albums_items, singles_items = [], []
    if isinstance(releases, dict):
        albums_items = releases.get("albums") or []
        singles_items = releases.get("singles") or []
        if isinstance(albums_items, dict):
            albums_items = albums_items.get("items") or []
        if isinstance(singles_items, dict):
            singles_items = singles_items.get("items") or []
    elif isinstance(releases, list):
        for r in releases:
            if not isinstance(r, dict):
                continue
            if r.get("type") in ("Single", "EP") or r.get("ytShelf") == "singles":
                singles_items.append(r)
            else:
                albums_items.append(r)
    else:
        target_cid = channel_id
        if not target_cid and client is not None and hasattr(client, "resolve_channel_id"):
            target_cid = client.resolve_channel_id(identifier, mb_record.get("mbid"))
        if target_cid and client is not None:
            albums_items = fetch_artist_releases(client, target_cid, "albums", fetched_at=fetched_at)["items"]
            singles_items = fetch_artist_releases(client, target_cid, "singles", fetched_at=fetched_at)["items"]

    return map_releases_to_musicbrainz(
        mb_record,
        albums_items=albums_items,
        singles_items=singles_items,
        fetched_at=fetched_at,
    )


def _parse_releases_request(request, raw_value):
    channel_id = request.get("channelId")
    kind = request.get("kind")
    if not kind and raw_value.startswith("{"):
        try:
            parsed = json.loads(raw_value)
            if isinstance(parsed, dict):
                channel_id = channel_id or parsed.get("channelId") or parsed.get("value")
                kind = kind or parsed.get("kind")
        except Exception:
            pass
    if not kind and ":" in raw_value:
        left, right = raw_value.rsplit(":", 1)
        if right.lower() in ("albums", "singles"):
            channel_id = channel_id or left
            kind = right.lower()
    return channel_id or raw_value, kind


def fetch_artist_artwork(mbid, get_json=None):
    """Public artist fanart metadata; media bytes use Colosseum's image owner."""
    if not MBID_RE.fullmatch(mbid) or uuid.UUID(mbid).int == 0:
        raise InvalidArgumentError("Artwork needs a canonical artist identity")
    mbid = mbid.lower()
    url = 'https://www.theaudiodb.com/api/v1/json/123/artist-mb.php?i=' + mbid
    if get_json is None:
        def get_json(target):
            import requests
            try:
                with requests.get(target, headers={'User-Agent': MB_USER_AGENT}, timeout=20, stream=True) as response:
                    response.raise_for_status()
                    chunks = bytearray()
                    for chunk in response.iter_content(16384):
                        chunks.extend(chunk)
                        if len(chunks) > 1024 * 1024:
                            raise ProviderUnavailableError('Artist artwork response exceeded its limit')
                    return json.loads(chunks)
            except (requests.RequestException, ValueError):
                raise ProviderUnavailableError('Artist artwork is temporarily unavailable') from None
    result = {'artistMbid':mbid, 'source':'theaudiodb', 'usage':'development',
              'termsUrl':'https://www.theaudiodb.com/docs_terms_of_use.php',
              'fetchedAtEpoch':int(time.time()), 'photos':[]}
    records = get_json(url).get('artists') or []
    matches = [a for a in records if str(a.get('strMusicBrainzID', '')).lower() == mbid]
    if len(matches) != 1:
        return result
    artist = matches[0]
    seen = set()
    for slot in ('strArtistFanart', 'strArtistFanart2', 'strArtistFanart3', 'strArtistFanart4'):
        photo = str(artist.get(slot) or '')
        parsed = urlparse(photo)
        if (parsed.scheme != 'https' or parsed.hostname not in ('www.theaudiodb.com', 'r2.theaudiodb.com')
                or not parsed.path.startswith('/images/media/artist/fanart/') or parsed.query
                or parsed.username or parsed.password or parsed.port not in (None,443) or photo in seen):
            continue
        seen.add(photo)
        identifier = str(artist.get('idArtist') or '')
        result['photos'].append({'url':photo, 'id':parsed.path.rsplit('/',1)[-1],
            'sourceUrl':'https://www.theaudiodb.com/artist/' + identifier if identifier.isdigit() else url,
            'attribution':'Artist fanart supplied by TheAudioDB; individual creator not supplied'})
    return result


# Home operations are deliberately separate from full artist-page enumeration.
HOME_OPERATIONS = {"artistCandidates", "recommendationIdentity", "recordingMatch", "populationCandidates"}
LB_ARTIST_EVIDENCE_QUALIFIED = True  # Public artist-radio route qualified 2026-10-22; failures remain optional.


def home_envelope(request, source, status="unavailable", complete=False):
    return {"version": 1, "seed": request.get("seed") or {}, "items": [],
            "provenance": {"source": source, "generator": "artist-catalogue" if source == "youtube-music" else "artist-evidence"},
            "fetchedAt": now_iso(request.get("fetchedAt")), "complete": complete, "status": status}


def home_seed(request):
    seed = request.get("seed")
    if (not isinstance(seed, dict) or seed.get("provider") != "youtube-music"
            or seed.get("kind") != "artist" or seed.get("providerId") != request.get("value")):
        raise InvalidArgumentError("A provider-bound artist selection is required.")
    validate_channel_id(seed.get("providerId"))
    if len(str(seed.get("name", ""))) > 300:
        raise InvalidArgumentError("Artist name is oversized.")
    return seed


def home_raw_artist(client, seed):
    raw = _call_provider(client.get_artist, seed["providerId"], not_found_on_error=True)
    if (not isinstance(raw, dict)
            or not isinstance(raw.get("name"), str) or not raw["name"].strip()
            or len(json.dumps(raw, ensure_ascii=True).encode()) > 4 * 1024 * 1024):
        raise ProviderUnavailableError("The artist response is invalid or belongs to another artist.")
    # ytmusicapi's returned channelId is a subscription channel, explicitly
    # documented as different from its input artist browse ID. Never alias it.
    for section in ("albums", "singles"):
        shelf = raw.get(section)
        owner = shelf.get("browseId") if isinstance(shelf, dict) else None
        if owner is None:
            continue
        if not isinstance(owner, str):
            raise ProviderUnavailableError("The release shelf owner identity is invalid.")
        if owner.startswith("MPAD"):
            owner = owner[4:]
        if owner.startswith("UC") and owner != seed["providerId"]:
            raise ProviderUnavailableError("The release shelf belongs to another artist.")
    return raw


def home_artist_refs(raw):
    out, seen = [], set()
    if not isinstance(raw, list) or len(raw) > 64:
        return out
    for artist in raw:
        if not isinstance(artist, dict):
            continue
        cid = artist.get("id") or artist.get("channelId")
        if isinstance(cid, str) and re.fullmatch(r"UC[A-Za-z0-9_-]{22}", cid) and cid not in seen:
            seen.add(cid)
            out.append({"kind": "artist", "provider": "youtube-music", "providerId": cid,
                        "id": "ytm:artist:" + cid, "name": str(artist.get("name") or "")[:300]})
    return out


def home_card(kind, raw, relation):
    if not isinstance(raw, dict):
        return None
    key = raw.get("videoId") if kind == "song" else raw.get("browseId")
    title = raw.get("title") or raw.get("name") or ""
    pattern = r"UC[A-Za-z0-9_-]{22}" if kind == "artist" else r"[A-Za-z0-9_-]{11}" if kind == "song" else r"[A-Za-z0-9_-]{1,160}"
    if not isinstance(key, str) or not re.fullmatch(pattern, key) or not isinstance(title, str) or not title.strip() or len(title) > 4096:
        return None
    if raw.get("isAvailable") is False:
        return None
    prefix = "artist" if kind == "artist" else "track" if kind == "song" else "album"
    out = {"kind": kind, "provider": "youtube-music", "providerId": key, "id": "ytm:" + prefix + ":" + key,
           "title": title, "name": title, "image": cover(raw), "cover": cover(raw), "relation": relation,
           "artistRefs": home_artist_refs(raw.get("artists", [])), "mappingStatus": "unresolved"}
    year = str(raw.get("year") or "")
    if re.fullmatch(r"\d{4}", year):
        out["year"] = year
    release_date = raw.get("releaseDate")
    if isinstance(release_date, str):
        try:
            datetime.date.fromisoformat(release_date)
            out["releaseDate"] = release_date
            out["dateSource"] = "youtube-music"
        except ValueError:
            pass
    if raw.get("type"):
        out["releaseType"] = str(raw["type"])[:80]
    if kind == "song":
        out["seconds"] = home_duration(raw)
        out["videoId"] = key
        if raw.get("version"):
            out["version"] = str(raw["version"])[:300]
    return out


def home_duration(raw):
    duration = raw.get("duration_seconds") or raw.get("seconds")
    if isinstance(duration, (int, float)) and not isinstance(duration, bool) and 0 < duration <= 86400:
        return int(duration)
    return secs(raw.get("duration")) or 0


def artist_candidates(request, client):
    result = home_envelope(request, "youtube-music")
    seed = home_seed(request)
    raw = home_raw_artist(client, seed)
    seen = set()
    for section, kind, relation, limit in [("albums", "album", "own-release", 20), ("singles", "album", "own-release", 20),
                                            ("songs", "song", "top-recording", 10), ("related", "artist", "related-artist", 10)]:
        shelf = raw.get(section)
        values = shelf.get("results", []) if isinstance(shelf, dict) else []
        if not isinstance(values, list):
            raise ProviderUnavailableError("The artist shelf has an invalid shape.")
        for value in values[:limit]:
            card = home_card(kind, value, relation)
            if card and card["id"] not in seen:
                card["seedArtistRef"] = seed
                seen.add(card["id"])
                result["items"].append(card)
    result["status"] = "ready" if result["items"] else "empty"
    result["capabilities"] = {name: isinstance(raw.get(name), dict) for name in ("albums", "singles", "songs", "related")}
    result["artist"] = {"name": raw["name"], "providerId": seed["providerId"], "provider": "youtube-music",
                        "biography": str(raw.get("description") or "")[:10000], "image": cover(raw)}
    return result


def channel_relation(detail, channel):
    for relation in detail.get("relations", []):
        if not isinstance(relation, dict):
            continue
        target = (relation.get("url") or {}).get("resource", "")
        parsed = urllib.parse.urlparse(target)
        if parsed.hostname in {"youtube.com", "www.youtube.com", "music.youtube.com", "m.youtube.com"} and parsed.path.rstrip("/") == "/channel/" + channel:
            return target
    return None


def recommendation_identity(request, client, mb_client=None):
    result = home_envelope(request, "musicbrainz", "unresolved", True)
    seed = home_seed(request)
    raw = home_raw_artist(client, seed)
    mb = mb_client or MusicBrainzClient(max_retries=1, timeout=2.5)
    name = raw["name"].strip()[:300]
    query = urllib.parse.urlencode({"query": 'artist:"' + name.replace('"', '') + '"', "limit": 5, "fmt": "json"})
    searched = mb.get_json("https://musicbrainz.org/ws/2/artist/?" + query)
    if not isinstance(searched, dict) or not isinstance(searched.get("artists"), list):
        raise ProviderUnavailableError("The artist identity search is incomplete.")
    result["complete"] = int(searched.get("count", len(searched["artists"]))) <= len(searched["artists"])
    candidates = [a for a in searched["artists"][:5] if isinstance(a, dict) and MBID_RE.fullmatch(str(a.get("id", ""))) and uuid.UUID(a["id"]).int != 0
                  and fold(a.get("name", "")) == fold(name)]
    result["mappingStatus"] = "ambiguous" if len(candidates) > 1 else "unresolved"
    result["status"] = result["mappingStatus"]
    if len(candidates) > 2:  # A name alone cannot choose one; no discography walk.
        return result
    confirmed = []
    for candidate in candidates:
        detail = mb.get_json("https://musicbrainz.org/ws/2/artist/" + candidate["id"] + "?inc=url-rels%2Brelease-groups&fmt=json")
        if not isinstance(detail, dict) or detail.get("id", "").lower() != candidate["id"].lower():
            raise ProviderUnavailableError("The canonical artist detail is invalid.")
        relation = channel_relation(detail, seed["providerId"])
        method, evidence = ("external-id", [relation]) if relation else (None, [])
        # Unique name plus two independently dated releases is corroboration,
        # not a name-only guess; never use it to select between same-name artists.
        if not method and len(candidates) == 1:
            own = set()
            for section in ("albums", "singles"):
                for entry in (raw.get(section) or {}).get("results", [])[:20]:
                    if isinstance(entry, dict) and re.fullmatch(r"\d{4}", str(entry.get("year", ""))):
                        own.add((fold(entry.get("title", "")), str(entry["year"])))
            for group in detail.get("release-groups", [])[:25]:
                if isinstance(group, dict) and (fold(group.get("title", "")), str(group.get("first-release-date", ""))[:4]) in own:
                    evidence.append(group.get("id"))
            evidence = list(dict.fromkeys(e for e in evidence if isinstance(e, str) and MBID_RE.fullmatch(e)))
            if len(evidence) >= 2:
                method = "corroborated-metadata"
        if method:
            mapping = {"status": "verified", "provider": "youtube-music", "providerId": seed["providerId"], "kind": "artist",
                       "mbid": candidate["id"].lower(), "method": method, "evidence": evidence,
                       "fetchedAtMs": int(datetime.datetime.fromisoformat(result["fetchedAt"].replace("Z", "+00:00")).timestamp() * 1000)}
            confirmed.append((mapping, detail))
    if len(confirmed) == 1 and result["complete"]:
        mapping, detail = confirmed[0]
        result.update({"status": "verified", "mappingStatus": "verified", "mapping": mapping, "mbid": mapping["mbid"],
                       "artist": {"name": detail.get("name", name), "disambiguation": detail.get("disambiguation", "")}})
    elif len(confirmed) > 1:
        result.update({"status": "ambiguous", "mappingStatus": "ambiguous"})
    return result


def recording_version(raw):
    text = str(raw.get("version") or "") + " " + str(raw.get("title") or "")
    markers = set(re.findall(r"\b(live|cover|remaster(?:ed)?|remix|acoustic|instrumental|karaoke|radio edit|demo|mono|stereo)\b", text.lower()))
    explicit = fold(str(raw.get("version") or ""))
    if explicit and explicit not in {"studio", "original", "album", "album version", "original version"}:
        markers.add(explicit)
    return frozenset(markers)


def recording_match(request, client):
    result = home_envelope(request, "youtube-music", "unresolved", True)
    recording = request.get("recording")
    if not isinstance(recording, dict):
        raise InvalidArgumentError("A recording identity is required.")
    title, refs = recording.get("title"), recording.get("artistRefs")
    seconds = home_duration(recording)
    if not isinstance(title, str) or not 1 <= len(title) <= 300 or not isinstance(refs, list) or not 1 <= len(refs) <= 64 or seconds <= 0:
        raise InvalidArgumentError("Recording title, artists, version and duration are required.")
    expected = set()
    for ref in refs:
        if not isinstance(ref, dict) or ref.get("provider") != "youtube-music" or ref.get("kind") != "artist":
            raise InvalidArgumentError("Recording artists must have provider identities.")
        expected.add(validate_channel_id(ref.get("providerId")))
    query = title + " " + " ".join(str(a.get("name", "")) for a in refs[:3])
    if hasattr(client, "_send_request"):
        page = read_search_page(client, query[:300], "songs", "")
        rows, complete = page["items"], not bool(page["continuation"])
    else:
        rows, complete = _call_provider(client.search, query[:300], filter="songs", limit=20), True
    if not isinstance(rows, list) or len(json.dumps(rows).encode()) > 4 * 1024 * 1024:
        raise ProviderUnavailableError("The recording search is invalid.")
    matches = {}
    for row in rows[:20]:
        if not isinstance(row, dict):
            continue
        ids = {a["providerId"] for a in home_artist_refs(row.get("artists", []))}
        if (fold(row.get("title", "")) != fold(title) or ids != expected or abs(home_duration(row) - seconds) > 2
                or recording_version(row) != recording_version(recording)):
            continue
        card = home_card("song", row, "recording-match")
        if card:
            matches[card["providerId"]] = card
    result["complete"] = complete
    if len(matches) > 1:
        result["status"] = "ambiguous"
    elif len(matches) == 1 and complete:
        card = next(iter(matches.values()))
        card["playbackMatch"] = {"status": "verified", "provider": "youtube-music", "providerId": card["providerId"],
                                 "artistMatched": True, "versionMatched": True, "durationMatched": True,
                                 "method": "exact-metadata", "fetchedAtMs": int(time.time() * 1000)}
        result.update({"status": "verified", "videoId": card["providerId"], "items": [card]})
    elif not complete:
        result["status"] = "partial"
    return result


class ListenBrainzArtistEvidence:
    def __init__(self, urlopen_fn=None):
        self.urlopen = urlopen_fn or urllib.request.urlopen

    def get(self, mbid):
        params = urllib.parse.urlencode({"mode": "easy", "max_similar_artists": 5, "max_recordings_per_artist": 5, "pop_begin": 0, "pop_end": 100})
        req = urllib.request.Request("https://api.listenbrainz.org/1/lb-radio/artist/" + mbid + "?" + params,
                                     headers={"User-Agent": MB_USER_AGENT, "Accept": "application/json"})
        with self.urlopen(req, timeout=4) as response:
            body = response.read(4 * 1024 * 1024 + 1)
            if len(body) > 4 * 1024 * 1024:
                raise ProviderUnavailableError("The population response exceeded its limit.")
            return json.loads(body)


def population_candidates(request, population_client=None):
    result = home_envelope(request, "listenbrainz", "unavailable", True)
    seed = request.get("seed") or {}
    # Only explicit canonical artist identities; provider UUID shapes prove nothing.
    mbid = seed.get("mbid", "") if seed.get("identity") == "canonical" and seed.get("kind") == "artist" else ""
    if not MBID_RE.fullmatch(str(mbid)) or uuid.UUID(mbid).int == 0:
        raise InvalidArgumentError("Population evidence requires a verified canonical artist reference.")
    if not LB_ARTIST_EVIDENCE_QUALIFIED and population_client is None:
        result["error"] = "The optional population source has not passed its live capability gate."
        return result
    data = (population_client or ListenBrainzArtistEvidence()).get(mbid)
    if not isinstance(data, dict):
        raise ProviderUnavailableError("The population evidence response is malformed.")
    for artist, rows in list(data.items())[:10]:
        if not MBID_RE.fullmatch(artist) or uuid.UUID(artist).int == 0 or not isinstance(rows, list):
            raise ProviderUnavailableError("The population artist identity is malformed.")
        for row in rows[:10]:
            recording = row.get("recording_mbid", "") if isinstance(row, dict) else ""
            if (not MBID_RE.fullmatch(str(recording)) or row.get("similar_artist_mbid") != artist
                    or uuid.UUID(recording).int == 0):
                raise ProviderUnavailableError("The population recording identity is malformed.")
            result["items"].append({"ref": {"identity": "canonical", "kind": "recording", "mbid": recording.lower()},
                                    "artistMbid": artist.lower(), "relation": "population-evidence", "destination": {},
                                    "available": False, "title": str(row.get("recording_name") or "")[:300],
                                    "artistName": str(row.get("similar_artist_name") or "")[:300]})
            if len(result["items"]) >= 60:
                break
        if len(result["items"]) >= 60:
            break
    result["status"] = "ready" if result["items"] else "empty"
    return result


def handle_home(request, client=None, mb_client=None, population_client=None):
    operation = request.get("operation")
    source = "musicbrainz" if operation == "recommendationIdentity" else "listenbrainz" if operation == "populationCandidates" else "youtube-music"
    result = home_envelope(request, source)
    try:
        if len(json.dumps(request).encode()) > 256 * 1024:
            raise InvalidArgumentError("The recommendation request is oversized.")
        if operation == "populationCandidates":
            return population_candidates(request, population_client)
        if operation in {"artistCandidates", "recommendationIdentity"}:
            home_seed(request)
        region = requested_region(request)
        if client is None:
            import functools
            import requests
            from ytmusicapi import YTMusic
            session = requests.Session()
            session.request = functools.partial(session.request, timeout=4)
            client = YTMusic(requests_session=session, **ytmusic_location_options(region))
        if operation == "artistCandidates":
            return artist_candidates(request, client)
        if operation == "recommendationIdentity":
            return recommendation_identity(request, client, mb_client)
        if operation == "recordingMatch":
            return recording_match(request, client)
    except Exception as exc:
        result["error"] = "The recommendation source is unavailable. Existing catalogue music remains available."
        result["sourceStatus"] = getattr(exc, "code", "unavailable")
        status = getattr(exc, "code", "")
        http_status = getattr(exc, "code", None) if isinstance(exc, urllib.error.HTTPError) else getattr(getattr(exc, "response", None), "status_code", None)
        if status == "rate_limited" or http_status in (429, 503):
            result["sourceStatus"] = "rate_limited"
            try:
                retry = int(getattr(exc, "retry_after", (getattr(exc, "headers", None) or {}).get("Retry-After", 60)))
            except (TypeError, ValueError):
                retry = 60
            result["retryAfterSeconds"] = max(1, min(3600, retry))
        if operation == "recommendationIdentity":
            result["mappingStatus"] = "unavailable"
        return result


def _lyrics_input_text(request, key, required=True):
    value = request.get(key)
    if value is None and not required:
        return ""
    if not isinstance(value, str):
        raise InvalidArgumentError("Invalid lyrics request metadata.")
    value = value.strip()
    if ((required and not value) or len(value) > LRCLIB_MAX_METADATA_CHARS):
        raise InvalidArgumentError("Invalid lyrics request metadata.")
    return value


def _lrclib_retry_after(headers):
    try:
        value = headers.get("Retry-After", 60) if headers is not None else 60
        if not isinstance(value, (str, int)) or len(str(value)) > 12:
            return 60
        return max(1, min(3600, int(value)))
    except (TypeError, ValueError, OverflowError):
        return 60


def _lrclib_not_found():
    return {"source": "lrclib", "status": "not_found", "fetchedAt": now_iso(), "record": None}


def _validated_lrclib_record(value):
    if not isinstance(value, dict):
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    required = ("id", "trackName", "artistName", "albumName", "duration",
                "instrumental", "plainLyrics", "syncedLyrics")
    if any(key not in value for key in required):
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    record_id = value["id"]
    if not isinstance(record_id, int) or isinstance(record_id, bool) or not 1 <= record_id <= 2**53 - 1:
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    for key in ("trackName", "artistName"):
        text = value[key]
        if not isinstance(text, str) or not text.strip() or len(text) > LRCLIB_MAX_METADATA_CHARS:
            raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    album_name = value["albumName"]
    if (album_name is not None and
            (not isinstance(album_name, str) or len(album_name) > LRCLIB_MAX_METADATA_CHARS)):
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    duration = value["duration"]
    if (not isinstance(duration, (int, float)) or isinstance(duration, bool)
            or not 1 <= duration <= 3600 or not math.isfinite(duration)):
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    if not isinstance(value["instrumental"], bool):
        raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    for key in ("plainLyrics", "syncedLyrics"):
        lyrics = value[key]
        if lyrics is not None and (not isinstance(lyrics, str) or len(lyrics) > LRCLIB_MAX_LYRICS_CHARS):
            raise ProviderUnavailableError("LRCLIB returned an invalid record.")
    return {key: ("" if key == "albumName" and value[key] is None else value[key]) for key in required}


def fetch_lyrics(request, opener=None):
    """Fetch one bounded LRCLIB record for caller-supplied track metadata."""
    if not isinstance(request, dict):
        raise InvalidArgumentError("Invalid lyrics request.")
    opaque_value = request.get("value")
    if (not isinstance(opaque_value, str) or not opaque_value.strip()
            or len(opaque_value) > LRCLIB_MAX_METADATA_CHARS):
        raise InvalidArgumentError("Invalid lyrics request identity.")
    track_name = _lyrics_input_text(request, "trackName")
    artist_name = _lyrics_input_text(request, "artistName")
    album_name = _lyrics_input_text(request, "albumName", required=False)
    duration = request.get("duration")
    if (not isinstance(duration, (int, float)) or isinstance(duration, bool)
            or not 1 <= duration <= 3600 or not math.isfinite(duration)):
        raise InvalidArgumentError("Invalid lyrics request duration.")
    params = {"track_name": track_name, "artist_name": artist_name, "duration": str(duration)}
    if album_name:
        params["album_name"] = album_name
    url = LRCLIB_ENDPOINT + "?" + urllib.parse.urlencode(params)
    request_object = urllib.request.Request(
        url,
        headers={"User-Agent": LRCLIB_USER_AGENT, "Accept": "application/json"},
    )
    open_url = urllib.request.urlopen if opener is None else opener
    try:
        with open_url(request_object, timeout=LRCLIB_TIMEOUT_SECONDS) as response:
            status = getattr(response, "status", None)
            if status is None and hasattr(response, "getcode"):
                status = response.getcode()
            headers = getattr(response, "headers", None)
            if status == 404:
                return _lrclib_not_found()
            if status in (429, 503):
                raise RateLimitError(retry_after=_lrclib_retry_after(headers))
            if status != 200:
                raise ProviderUnavailableError("LRCLIB request failed.")
            content_type = headers.get("Content-Type", "") if headers is not None else ""
            if not isinstance(content_type, str) or content_type.split(";", 1)[0].strip().lower() != "application/json":
                raise ProviderUnavailableError("LRCLIB returned an invalid response.")
            body = response.read(LRCLIB_MAX_RESPONSE_BYTES + 1)
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return _lrclib_not_found()
        if exc.code in (429, 503):
            raise RateLimitError(retry_after=_lrclib_retry_after(exc.headers)) from None
        raise ProviderUnavailableError("LRCLIB request failed.") from None
    except ProviderError:
        raise
    except Exception as exc:
        if _is_timeout_exception(exc):
            raise ProviderTimeoutError("LRCLIB request timed out.") from None
        raise ProviderUnavailableError("LRCLIB is unavailable.") from None
    if not isinstance(body, bytes) or len(body) > LRCLIB_MAX_RESPONSE_BYTES:
        raise ProviderUnavailableError("LRCLIB returned an invalid response.")
    try:
        payload = json.loads(body)
        record = _validated_lrclib_record(payload)
    except ProviderError:
        raise
    except Exception:
        raise ProviderUnavailableError("LRCLIB returned an invalid response.") from None
    return {"source": "lrclib", "status": "found", "fetchedAt": now_iso(), "record": record}

def _video_title_key(text):
    if not isinstance(text, str):
        return ""
    text = unicodedata.normalize("NFKC", text).casefold()
    return " ".join("".join(" " if unicodedata.category(c)[0] in "PZ" else c for c in text).split())


def fetch_related_video(client, video, request):
    """Qualify one official video, never a first-result or uploader-name guess.

    Catalogue artist IDs and the complete version title must agree. A relation
    does not establish a musical time offset for a different video edit.
    """
    if not isinstance(video, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", video):
        raise InvalidArgumentError("Invalid YouTube video ID")
    result = {"version": 1, "sourceVideoId": video, "status": "unavailable"}
    source = _call_provider(client.get_song, video)
    details = source.get("videoDetails", {}) if isinstance(source, dict) else {}
    title = details.get("title")
    if (details.get("videoId") != video or details.get("isLiveContent") is True
            or source.get("playabilityStatus", {}).get("status") != "OK"
            or not isinstance(title, str) or not title.strip()):
        return result
    expected_title = request.get("title") or title
    key = _video_title_key(expected_title)
    if not key or key != _video_title_key(title):
        return result
    # Live/performance discovery is a separate explicit-choice journey.
    if re.search(r"(?:\(|\[)[^)\]]*\b(live|concert|unplugged|session|performance)\b|\blive (?:at|in|from)\b",
                 expected_title, flags=re.IGNORECASE):
        return result
    if details.get("musicVideoType") == "MUSIC_VIDEO_TYPE_OMV":
        result.update({"status": "ready", "videoId": video, "title": title,
                       "relationship": "same-video", "sameMediaClock": True,
                       "mediaTimeOffsetMs": 0})
        return result
    if details.get("musicVideoType") != "MUSIC_VIDEO_TYPE_ATV":
        return result
    raw_ids = request.get("artistIds")
    if not isinstance(raw_ids, list):
        return result
    artist_ids = {x.removeprefix("ytm:artist:") for x in raw_ids if isinstance(x, str)}
    artist_ids = {x for x in artist_ids if re.fullmatch(r"UC[A-Za-z0-9_-]{22}", x)}
    if not artist_ids or details.get("channelId") not in artist_ids:
        return result
    author = details.get("author")
    if not isinstance(author, str) or not author.strip():
        return result
    found = _call_provider(client.search, (author + " " + title)[:300], filter="videos", limit=20)
    if not isinstance(found, list):
        return result
    candidates = {}
    for item in found[:40]:
        if not isinstance(item, dict) or item.get("videoType") != "MUSIC_VIDEO_TYPE_OMV":
            continue
        candidate_id = item.get("videoId")
        artists = item.get("artists")
        if (not isinstance(candidate_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", candidate_id)
                or _video_title_key(item.get("title")) != key or not isinstance(artists, list)):
            continue
        ids = {a.get("id") for a in artists if isinstance(a, dict) and isinstance(a.get("id"), str)}
        if artist_ids.intersection(ids):
            candidates[candidate_id] = item
    if len(candidates) > 1:
        result["status"] = "ambiguous"
        return result
    if not candidates:
        return result
    candidate_id, candidate = next(iter(candidates.items()))
    response = _call_provider(client.get_song, candidate_id)
    actual = response.get("videoDetails", {}) if isinstance(response, dict) else {}
    if (actual.get("videoId") != candidate_id or actual.get("isLiveContent") is True
            or actual.get("musicVideoType") != "MUSIC_VIDEO_TYPE_OMV"
            or _video_title_key(actual.get("title")) != key
            or response.get("playabilityStatus", {}).get("status") != "OK"):
        return result
    result.update({"status": "ready", "videoId": candidate_id, "title": candidate["title"],
                   "relationship": "catalogue-artist-and-version", "sameMediaClock": False})
    return result


def handle(request, client=None, mb_client=None, population_client=None):
    if request.get("operation") in HOME_OPERATIONS:
        return handle_home(request, client, mb_client, population_client)
    operation, value = request["operation"], str(request.get("value", "")).strip()
    region = requested_region(request)
    if operation == "discover" and (value not in DISCOVERY_KEYS
            or (value in {"artists", "charts"} and not region)):
        raise InvalidArgumentError("Invalid discovery shelf or region")
    if operation == "playlist" and not source_key(value):
        raise InvalidArgumentError("Invalid playlist identity")
    if operation in {"cataloguePage", "playlistPage"}:
        kind = request.get("kind", "") if operation == "cataloguePage" else "tracks"
        if operation == "cataloguePage" and (kind not in PAGE_KINDS or not 1 <= len(value) <= 300):
            raise InvalidArgumentError("Enter a search and select a catalogue category")
        if operation == "playlistPage" and not source_key(value):
            raise InvalidArgumentError("Invalid playlist identity")
        validate_page_request(request, operation, value, kind, region)
    if operation == "resolve":
        return resolve(value)
    if operation == "resolveVideo":
        return resolve_video(value)
    if operation == "relatedVideo" and (not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", value)):
        raise InvalidArgumentError("Invalid YouTube video ID")
    if operation == "artistArtwork":
        return fetch_artist_artwork(value)
    if operation == "lyrics":
        return fetch_lyrics(request)
    fetched_at = request.get("fetchedAt")
    if operation == "artistIdentity":
        identifier = request.get("mbid") or request.get("name") or value
        channel_id = request.get("channelId")
        releases = request.get("releases")
        yt_client = client
        if yt_client is None and channel_id and releases is None:
            from ytmusicapi import YTMusic
            yt_client = YTMusic(**ytmusic_location_options(region))
        return fetch_artist_identity(
            identifier,
            client=yt_client,
            channel_id=channel_id,
            releases=releases,
            mb_client=mb_client,
            mb_data=request.get("mbData"),
            fetched_at=fetched_at,
        )
    if client is None:
        from ytmusicapi import YTMusic
        client = YTMusic(**ytmusic_location_options(region))
    if operation == "relatedVideo":
        return fetch_related_video(client, value, request)
    if operation == "discover":
        return fetch_discovery(client, value, region, fetched_at)
    if operation == "playlist":
        return fetch_playlist(client, value, region)
    if operation == "cataloguePage":
        return fetch_catalogue_page(client, request, value, region)
    if operation == "playlistPage":
        return fetch_playlist_page(client, request, value, region)
    if operation == "catalogueSearch":
        if not 1 <= len(value) <= 300:
            raise InvalidArgumentError("Enter an album or artist name")
        found = _call_provider(client.search, value, filter="artists", limit=24)
        artist_rows = [{"id": "ytm:artist:" + a["browseId"], "channelId": a["browseId"],
                        "name": a.get("artist") or a.get("title") or "", "cover": cover(a),
                        "origin": "youtube-music"} for a in found if a.get("browseId")]
        return {"artists": artist_rows,
                "albums": [album_card(a) for a in _call_provider(client.search, value, filter="albums", limit=24)
                           if a.get("browseId")][:24], "complete": False,
                "source": "youtube-music", "fetchedAt": now_iso(fetched_at)}
    if operation == "search":
        if not 1 <= len(value) <= 300:
            raise ValueError("Enter an album or artist name")
        return [album_card(a) for a in client.search(value, filter="albums", limit=24)
                if a.get("browseId")][:24]
    if operation == "album":
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,160}", value):
            raise ValueError("Invalid album identity")
        return normalize_album(client.get_album(value), value)
    if operation == "artist":
        cid = request.get("channelId") or value
        return fetch_artist(client, cid, fetched_at=fetched_at)
    if operation == "artistReleases":
        cid, kind = _parse_releases_request(request, value)
        return fetch_artist_releases(client, cid, kind, fetched_at=fetched_at)
    if operation == "artistVideos":
        cid = request.get("channelId") or value
        return fetch_artist_videos(client, cid, fetched_at=fetched_at)
    raise ValueError("Unknown operation")


def main():
    try:
        raw = sys.stdin.buffer.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise InvalidArgumentError("Catalogue request is too large")
        request = json.loads(raw)
        response = {"ok": True, "data": handle(request)}
    except ImportError:
        response = {"ok": False, "error": "Vinyl's catalogue helper is not installed.", "errorCode": "missing_dependency"}
    except ProviderError as exc:
        response = exc.to_dict()
    except ValueError as exc:
        response = {"ok": False, "error": str(exc)}
    except Exception:
        # Upstream errors may contain signed URLs, cookies or verbose internals.
        response = {"ok": False, "error": "YouTube is unavailable for this request. Try again.", "errorCode": "unavailable"}
    sys.stdout.buffer.write(json.dumps(response, ensure_ascii=True).encode("utf-8"))


if __name__ == "__main__":
    main()
