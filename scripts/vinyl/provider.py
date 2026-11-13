"""Anonymous Vinyl provider. One JSON request on stdin, one JSON response on stdout.

Catalogue identities are durable; resolved URLs and headers are transient. Never
write the resolver response to the library, progress store, logs, or a snapshot.
"""
import contextlib
import datetime
import hashlib
import email.utils
import html
from html.parser import HTMLParser
import json
import math
import os
from pathlib import Path
import re
import socket
import sys
import tempfile
import threading
from concurrent.futures import ThreadPoolExecutor
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

# One long-lived anonymous transport per helper process. The C++ provider owns
# the bounded subprocess lifetime and kills it on cancellation/profile shutdown.
_SERVER_MODE = False
_SERVER_AUDIO_DL = None
_SERVER_VIDEO_DL = None
_SERVER_MUSIC_CLIENT = None
_SERVER_MUSIC_REGION = None


def _youtube_dl(kind):
    from yt_dlp import YoutubeDL

    class QuietLogger:
        def debug(self, message): pass
        def warning(self, message): pass
        def error(self, message): pass

    options = {"quiet": True, "no_warnings": True, "logger": QuietLogger(),
               "format": "bestaudio" if kind == "audio" else VIDEO_FORMAT_SELECTOR,
               "skip_download": True, "noplaylist": True, "socket_timeout": 20,
               "retries": 1, "extractor_retries": 1}
    if "COLOSSEUM_APPDATA_TAG" in os.environ:
        options["cachedir"] = False
    if not _SERVER_MODE:
        return YoutubeDL(options)
    global _SERVER_AUDIO_DL, _SERVER_VIDEO_DL
    if kind == "audio":
        if _SERVER_AUDIO_DL is None:
            _SERVER_AUDIO_DL = YoutubeDL(options)
        return contextlib.nullcontext(_SERVER_AUDIO_DL)
    if _SERVER_VIDEO_DL is None:
        _SERVER_VIDEO_DL = YoutubeDL(options)
    return contextlib.nullcontext(_SERVER_VIDEO_DL)


def _ytmusic_client(region):
    from ytmusicapi import YTMusic
    if not _SERVER_MODE:
        return YTMusic(**ytmusic_location_options(region))
    global _SERVER_MUSIC_CLIENT, _SERVER_MUSIC_REGION
    if _SERVER_MUSIC_CLIENT is None or _SERVER_MUSIC_REGION != region:
        _SERVER_MUSIC_CLIENT = YTMusic(**ytmusic_location_options(region))
        _SERVER_MUSIC_REGION = region
    return _SERVER_MUSIC_CLIENT


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
    return {"location": region} if region and region != "ZZ" else {}


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


def validate_browse_request(request):
    operation, kind = request.get("operation"), request.get("value")
    allowed = {"operation", "value", "refresh"}
    if operation == "browseCatalogue":
        allowed |= {"category", "region", "cursor"}
    if set(request) - allowed or ("refresh" in request and not isinstance(request["refresh"], bool)):
        raise InvalidArgumentError("Unsupported YouTube browsing option")
    if operation == "browseCategories":
        if kind != "":
            raise InvalidArgumentError("Categories do not accept a query")
        return "", "", "", 0, {}
    category, region = request.get("category", ""), request.get("region", "")
    if (not isinstance(kind, str) or kind not in {"songs", "albums", "artists"} or not isinstance(category, str)
            or (category and not re.fullmatch(r"[A-Za-z0-9_+%./=,-]{1,2048}", category))
            or not isinstance(region, str) or (region and not re.fullmatch(r"[A-Z]{2}", region))
            or (kind == "artists" and (category or not region)) or (kind != "artists" and region)
            or not isinstance(request.get("cursor", {}), dict)):
        raise InvalidArgumentError("Select a supported YouTube collection or chart market")
    token, offset, meta = validate_page_request(request, "browseCatalogue", category, kind, region)
    if token:
        ids, tokens, pending = meta.get("seenIds"), meta.get("seenTokens"), meta.get("pending")
        cursor = request["cursor"]
        if (not category or type(cursor.get("version")) is not int
                or set(cursor) != {"version", "operation", "value", "kind", "region", "token", "offset", "meta"}
                or set(meta) != {"seenIds", "seenTokens", "pending"}
                or not isinstance(ids, list) or len(ids) > 2000
                or any(not isinstance(v, str) or len(v) > 160 for v in ids)
                or not isinstance(tokens, list) or len(tokens) > 100
                or not isinstance(pending, list) or len(pending) > 20
                or any(not isinstance(v, str) or not re.fullmatch(r"[A-Za-z0-9_+%./=,-]{1,16384}", v)
                       for v in tokens + pending) or token in tokens):
            raise InvalidArgumentError("Invalid or expired YouTube collection page")
    return category, region, token, offset, meta


def fetch_browse_categories(client):
    raw = _call_provider(client.get_mood_categories)
    if not isinstance(raw, dict):
        raise ProviderUnavailableError("The YouTube categories are unavailable")
    categories, seen = [], set()
    for group, values in raw.items():
        if not isinstance(group, str) or not isinstance(values, list):
            raise ProviderUnavailableError("The YouTube categories changed format")
        for value in values:
            if not isinstance(value, dict):
                continue
            title, params = value.get("title"), value.get("params")
            if (not isinstance(title, str) or not title.strip() or not isinstance(params, str)
                    or not re.fullmatch(r"[A-Za-z0-9_+%./=,-]{1,2048}", params)
                    or params in seen or re.search(r"podcasts?|non[ -]?music|audiobooks?", group + " " + title, re.I)):
                continue
            seen.add(params)
            categories.append({"id": identity("category", params), "title": title, "group": group, "params": params})
    if len(categories) > 300:
        raise ProviderUnavailableError("The YouTube category response is too large")
    return {"version": 1, "source": "youtube-music", "categories": categories, "fetchedAt": now_iso()}


def browse_category_rows(client, category, kind, token):
    # 1.12.3 get_mood_playlists assumes every renderer is a playlist. Category
    # pages actually mix songs, albums and playlists; dispatch by source type.
    from ytmusicapi.parsers.browsing import parse_album, parse_song_flat
    body = {"continuation": token} if token else {"browseId": "FEmusic_moods_and_genres_category", "params": category}
    raw = _call_provider(client._send_request, "browse", body)
    if not isinstance(raw, dict):
        raise ProviderUnavailableError("The YouTube collection is unavailable")
    shelves = []
    names = ("musicCarouselShelfRenderer", "musicImmersiveCarouselShelfRenderer", "musicShelfRenderer", "gridRenderer")
    if token:
        names = ("musicShelfContinuation", "musicCarouselShelfContinuation", "gridContinuation")
    for name in names:
        shelves.extend(renderer_nodes(raw, name))
    if not shelves:
        raise ProviderUnavailableError("The YouTube collection changed format. Retry it.")
    values, continuations = [], []
    for shelf in shelves:
        rows = shelf.get("contents", shelf.get("items", []))
        if not isinstance(rows, list) or len(rows) > 500:
            raise ProviderUnavailableError("The YouTube collection is too large")
        matched = False
        for row in rows:
            if not isinstance(row, dict):
                continue
            try:
                if kind == "songs" and "musicResponsiveListItemRenderer" in row:
                    parsed = parse_song_flat(row["musicResponsiveListItemRenderer"])
                    # Music type is source evidence; podcast/unknown rows never
                    # become songs merely because they carry a video ID.
                    if parsed.get("videoType") in YTM_VIDEO_TYPES:
                        values.append(parsed); matched = True
                elif kind == "albums" and "musicTwoRowItemRenderer" in row:
                    renderer = row["musicTwoRowItemRenderer"]
                    endpoint = renderer.get("navigationEndpoint", {}).get("browseEndpoint", {})
                    page = endpoint.get("browseEndpointContextSupportedConfigs", {}).get("browseEndpointContextMusicConfig", {}).get("pageType")
                    if page == "MUSIC_PAGE_TYPE_ALBUM":
                        values.append(parse_album(renderer)); matched = True
            except (KeyError, IndexError, TypeError):
                raise ProviderUnavailableError("A YouTube music card changed format. Retry the collection.")
        # A continuation is followed only for the matching source shelf, never
        # for the neighboring playlists or an unrelated page control.
        if matched or token:
            for continuation in shelf.get("continuations", []):
                next_token = continuation.get("nextContinuationData", {}).get("continuation", "")
                if not isinstance(next_token, str) or not re.fullmatch(r"[A-Za-z0-9_+%./=,-]{1,16384}", next_token):
                    raise ProviderUnavailableError("The YouTube continuation changed format")
                if next_token not in continuations:
                    continuations.append(next_token)
    return values, continuations


def fetch_browse_catalogue(client, request):
    category, region, token, offset, meta = validate_browse_request(request)
    kind = request["value"]
    continuations, markets = [], []
    if category:
        values, continuations = browse_category_rows(client, category, kind, token)
        title = "YouTube Music collection"
    else:
        raw = _call_provider(client.get_charts, region) if kind == "artists" else _call_provider(client.get_explore)
        if not isinstance(raw, dict):
            raise ProviderUnavailableError("The YouTube selection is unavailable")
        if kind == "artists":
            country_data = raw.get("countries")
            if not isinstance(country_data, dict):
                raise ProviderUnavailableError("The YouTube chart markets changed format")
            countries = country_data.get("options", [])
            if not isinstance(countries, list) or region not in countries:
                raise InvalidArgumentError("YouTube does not support this chart market")
            selected = country_data.get("selected", {})
            selected_title = selected.get("text", "") if isinstance(selected, dict) else ""
            markets = [{"code": code, "title": selected_title if code == region and selected_title else code}
                       for code in dict.fromkeys(v for v in countries if isinstance(v, str) and re.fullmatch(r"[A-Z]{2}", v))]
        key = {"songs": "trending", "albums": "new_releases", "artists": "artists"}[kind]
        values = raw.get(key)
        if isinstance(values, dict):
            values = values.get("items")
        title = {"songs": "Trending songs", "albums": "New releases", "artists": "YouTube artist selection"}[kind]
    if not isinstance(values, list) or len(values) > 500:
        raise ProviderUnavailableError("The YouTube music selection changed format")
    items, records = [], []
    seen = set(meta.get("seenIds", []))
    for value in values:
        if not isinstance(value, dict):
            continue
        # Only YouTube image hosts may become durable art. In particular a
        # signed playback URL can never leak into browse cards or their cache.
        thumbs = []
        for thumb in value.get("thumbnails") or []:
            if not isinstance(thumb, dict) or not isinstance(thumb.get("url"), str):
                continue
            parsed_url = urlparse(thumb["url"])
            host = parsed_url.hostname or ""
            if parsed_url.scheme == "https" and any(host == suffix or host.endswith("." + suffix)
                    for suffix in ("ytimg.com", "ggpht.com", "googleusercontent.com")):
                thumbs.append(thumb)
        value = dict(value, thumbnails=thumbs)
        record = None
        if kind == "songs":
            record = song_record(value)
            if not record:
                continue
            track = record["tracks"][0]
            item = {"id": track["id"], "kind": kind, "albumId": record["id"], "trackId": track["id"], "trackIndex": 0,
                    "title": track["title"], "cover": record["cover"], "subtitle": " · ".join(record["artists"]), "origin": "youtube-music"}
        elif kind == "albums":
            key = value.get("browseId")
            if not source_key(key) or not value.get("title") or any("podcast" in str(value.get(field, "")).lower() for field in ("type", "resultType")):
                continue
            record = album_card(value)
            item = dict(record, kind=kind, albumId=record["id"], subtitle=" · ".join(record["artists"]))
        else:
            channel = value.get("browseId")
            if not isinstance(channel, str) or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", channel) or not value.get("title"):
                continue
            item = {"id": "ytm:artist:" + channel, "artistId": "ytm:artist:" + channel, "kind": kind,
                    "title": value["title"], "artist": value["title"], "cover": cover(value), "round": True, "origin": "youtube-music"}
        if item["id"] in seen:
            continue
        seen.add(item["id"]); items.append(item)
        if record:
            records.append(record)
    prior_tokens = meta.get("seenTokens", [])
    if token and (not items or any(v in prior_tokens + [token] for v in continuations)):
        raise ProviderUnavailableError("The YouTube collection page did not advance. Refresh it.")
    pending = list(dict.fromkeys(meta.get("pending", []) + continuations))
    next_offset = offset + len(items)
    if pending and (not items or next_offset >= 2000 or len(prior_tokens) >= 99 or len(pending) > 20):
        raise ProviderUnavailableError("The YouTube collection paging limit was reached. Refresh it.")
    next_token = pending.pop(0) if pending else ""
    next_cursor = page_cursor("browseCatalogue", category, kind, region, next_token, next_offset,
                              {"seenIds": sorted(seen), "seenTokens": prior_tokens + ([token] if token else []), "pending": pending})
    result = {"version": 1, "kind": kind, "category": category, "region": region, "title": title,
            "source": "youtube-music", "items": items, "records": records, "offset": offset, "nextOffset": next_offset,
            "nextCursor": next_cursor, "complete": not bool(next_cursor), "fetchedAt": now_iso()}
    if kind == "artists":
        result["markets"] = markets
    return result


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
    with _youtube_dl("audio") as client:
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
    with _youtube_dl("video") as client:
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


def _artist_browse_channel(channel_id):
    if not isinstance(channel_id, str) or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", channel_id):
        raise InvalidArgumentError("Invalid artist browse channel ID.")
    return channel_id


def _artist_continuation(value, require_params=False):
    if not isinstance(value, dict) or set(value) - {"browseId", "params"}:
        raise InvalidArgumentError("Invalid artist continuation.")
    browse_id, params = value.get("browseId"), value.get("params")
    if (not source_key(browse_id) or (require_params and not params)
            or (params is not None and (not isinstance(params, str) or not 1 <= len(params) <= 4096
                                       or not re.fullmatch(r"(?:[A-Za-z0-9_+/=-]|%[0-9A-Fa-f]{2})+", params)))):
        raise InvalidArgumentError("Invalid artist continuation.")
    return browse_id, params


def _artist_overview_handle(handle, require_params=False):
    try:
        _artist_continuation(handle, require_params)
    except InvalidArgumentError:
        return None
    return handle


def normalize_artist(raw_artist, songs_playlist=None, requested_channel_id=None, fetched_at=None, overview=False):
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    # get_artist returns a subscription channel. Overview is bound to the requested browse channel.
    resolved_cid = requested_channel_id if overview else raw_artist.get("channelId") or requested_channel_id
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
        song = {
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
        }
        if overview:
            video_type = track.get("videoType")
            if video_type == "MUSIC_VIDEO_TYPE_ATV" or (video_type is None and source_key(album_obj.get("id"))):
                source_track = dict(track, duration_seconds=dur_seconds)
                if video_type is None:
                    source_track.pop("videoType", None)
                record = song_record(source_track)
                if record:
                    song["record"] = record
        songs.append(song)
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
    if overview:
        albums_handle = _artist_overview_handle(albums_handle, require_params=True)
        singles_handle = _artist_overview_handle(singles_handle, require_params=True)
        videos_handle = _artist_overview_handle(videos_handle)
        songs_handle = _artist_overview_handle(songs_handle)
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

    songs_complete = not songs_handle if overview else bool(pl_list) or not bool(songs_sec.get("browseId"))
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


def fetch_artist(client, channel_id, fetched_at=None, overview=False):
    cid = _artist_browse_channel(channel_id) if overview else validate_channel_id(channel_id)
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    if overview:
        timestamp = now_iso(fetched_at)
        result = normalize_artist(raw_artist, requested_channel_id=cid, fetched_at=timestamp, overview=True)
        handles = result["continuations"]
        for kind, shelf in (("albums", "albumShelf"), ("singles", "singleShelf")):
            result[shelf] = _artist_release_shelf(
                _artist_inline_results(raw_artist, kind), cid, cid, kind,
                "get_artist inline shelf", not handles[kind], timestamp)
        videos = handles["videos"]
        result["videoShelf"] = _artist_video_shelf(
            _artist_inline_results(raw_artist, "videos"), cid, cid,
            videos["browseId"] if videos else None, "artist page videos shelf",
            False, None, not videos, timestamp)
        return result
    songs_sec = raw_artist.get("songs") if isinstance(raw_artist.get("songs"), dict) else {}
    songs_pl = None
    if songs_sec.get("browseId") and hasattr(client, "get_playlist"):
        songs_pl = _call_provider(client.get_playlist, songs_sec["browseId"], limit=25)
        if songs_pl is None or not isinstance(songs_pl, dict) or not isinstance(songs_pl.get("tracks"), list):
            raise ProviderUnavailableError("Failed to fetch artist songs playlist.")
    return normalize_artist(raw_artist, songs_playlist=songs_pl, requested_channel_id=cid, fetched_at=fetched_at)


def _artist_inline_results(raw_artist, kind):
    section = raw_artist.get(kind)
    if section is None:
        return []
    if not isinstance(section, dict):
        raise ProviderUnavailableError(f"Provider returned an invalid '{kind}' inline shelf.")
    items = section.get("results")
    if items is None:
        return []
    if not isinstance(items, list):
        raise ProviderUnavailableError(f"Provider returned an invalid '{kind}' inline shelf.")
    return items


def fetch_artist_releases(client, channel_id, kind, fetched_at=None, continuation=None):
    cid = _artist_browse_channel(channel_id) if continuation is not None else validate_channel_id(channel_id)
    norm_kind = str(kind or "").strip().lower()
    if norm_kind not in ("albums", "singles"):
        raise InvalidArgumentError("Invalid release kind; expected 'albums' or 'singles'.")
    if continuation is not None:
        browse_id, params = _artist_continuation(continuation, require_params=True)
        return _fetch_artist_release_continuation(client, cid, cid, norm_kind, browse_id, params, fetched_at)
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    resolved_cid = raw_artist.get("channelId") or cid
    sec = raw_artist.get(norm_kind)
    if not isinstance(sec, dict):
        raise ProviderUnavailableError(f"Artist response missing '{norm_kind}' shelf.")

    if sec.get("browseId") and sec.get("params"):
        return _fetch_artist_release_continuation(
            client, cid, resolved_cid, norm_kind, sec["browseId"], sec["params"], fetched_at)
    raw_items = sec.get("results")
    if not isinstance(raw_items, list):
        raise ProviderUnavailableError(f"Provider returned an invalid '{norm_kind}' inline shelf.")
    return _artist_release_shelf(raw_items, cid, resolved_cid, norm_kind,
                                 "get_artist inline shelf", not sec.get("browseId"), fetched_at)


def _fetch_artist_release_continuation(client, cid, resolved_cid, kind, browse_id, params, fetched_at):
    raw_items = _call_provider(client.get_artist_albums, browse_id, params, limit=None)
    if not isinstance(raw_items, list):
        raise ProviderUnavailableError(f"Provider returned no '{kind}' continuation list.")
    return _artist_release_shelf(raw_items, cid, resolved_cid, kind, "get_artist_albums", True, fetched_at)


def _artist_release_shelf(raw_items, cid, resolved_cid, norm_kind, source, complete, fetched_at):
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


def fetch_artist_videos(client, channel_id, fetched_at=None, continuation=None):
    cid = _artist_browse_channel(channel_id) if continuation is not None else validate_channel_id(channel_id)
    if continuation is not None:
        playlist_id, _ = _artist_continuation(continuation)
        return _fetch_artist_video_continuation(client, cid, cid, playlist_id, fetched_at)
    raw_artist = _call_provider(client.get_artist, cid, not_found_on_error=True)
    if not isinstance(raw_artist, dict) or not raw_artist.get("name"):
        raise ArtistNotFoundError("Artist channel not found.")
    resolved_cid = raw_artist.get("channelId") or cid
    vsec = raw_artist.get("videos")
    if not isinstance(vsec, dict):
        raise ProviderUnavailableError("Artist response missing 'videos' section.")

    playlist_id = vsec.get("browseId")
    if playlist_id:
        return _fetch_artist_video_continuation(client, cid, resolved_cid, playlist_id, fetched_at)
    raw_videos = vsec.get("results")
    if not isinstance(raw_videos, list):
        raise ProviderUnavailableError("Provider returned an invalid 'videos' inline shelf.")
    return _artist_video_shelf(
        raw_videos, cid, resolved_cid, playlist_id, "artist page videos shelf (no playlist handle returned)",
        True, "no playlist handle returned", False, fetched_at)


def _fetch_artist_video_continuation(client, cid, resolved_cid, playlist_id, fetched_at):
    playlist = _call_provider(client.get_playlist, playlist_id, limit=None)
    if not isinstance(playlist, dict) or not isinstance(playlist.get("tracks"), list):
        raise ProviderUnavailableError("Failed to fetch artist videos playlist.")
    return _artist_video_shelf(playlist["tracks"], cid, resolved_cid, playlist_id,
                               "artist videos playlist", False, None, True, fetched_at)


def _artist_video_shelf(raw_videos, cid, resolved_cid, playlist_id, source, fallback, fallback_reason,
                        complete, fetched_at):
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
HOME_OPERATIONS = {"artistCandidates", "songCandidates", "recommendationIdentity", "recommendationDiscography", "chartCandidates", "similarArtists", "recordingMatch", "populationCandidates"}
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
        album = raw.get("album")
        album_id = album.get("id") if isinstance(album, dict) else None
        if source_key(album_id):
            out["albumRef"] = {"kind": "album", "provider": "youtube-music",
                                "providerId": album_id, "id": "ytm:album:" + album_id}
        video_type = raw.get("videoType")
        if isinstance(video_type, str) and video_type in YTM_VIDEO_TYPES:
            out["videoType"] = video_type
        if raw.get("version"):
            out["version"] = str(raw["version"])[:300]
    return out


def home_duration(raw):
    duration = raw.get("duration_seconds") or raw.get("seconds")
    if isinstance(duration, (int, float)) and not isinstance(duration, bool) and 0 < duration <= 86400:
        return int(duration)
    return secs(raw.get("duration")) or 0


def song_candidate_duration_seconds(raw):
    field = next((key for key in ("duration_seconds", "seconds", "duration", "length") if key in raw), None)
    if field is None:
        return 0
    value = raw[field]
    if isinstance(value, bool) or not isinstance(value, (str, int, float)):
        return 0
    try:
        if isinstance(value, str):
            if not value.strip():
                return 0
            seconds = secs(value.strip())
        else:
            if not math.isfinite(value):
                return 0
            seconds = int(value)
    except (TypeError, ValueError, OverflowError):
        return 0
    if seconds is None or seconds <= 0 or seconds > 86400:
        return 0
    return int(seconds)


def song_candidate_artwork(raw):
    if not isinstance(raw, dict):
        return raw
    normalized = dict(raw)

    def valid_thumbnails(values):
        if not isinstance(values, list):
            return []
        out = []
        for value in values:
            if not isinstance(value, dict):
                continue
            url = value.get("url")
            if not isinstance(url, str) or not url.strip():
                continue
            thumbnail = dict(value)
            width = thumbnail.get("width")
            try:
                valid_width = (not isinstance(width, bool) and isinstance(width, (int, float))
                               and math.isfinite(width) and width >= 0)
            except (OverflowError, TypeError, ValueError):
                valid_width = False
            if not valid_width:
                thumbnail["width"] = 0
            else:
                thumbnail["width"] = int(width)
            out.append(thumbnail)
        return out

    thumbnails = valid_thumbnails(raw.get("thumbnails"))
    if not thumbnails:
        thumbnails = valid_thumbnails(raw.get("thumbnail"))
    normalized["thumbnails"] = thumbnails
    return normalized


def song_candidate_request(request):
    value = request.get("value")
    seed = request.get("seed")
    recording = request.get("recording")
    fetched_at = request.get("fetchedAt")
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", value):
        raise InvalidArgumentError("A YouTube recording identity is required.")
    if (not isinstance(seed, dict) or seed.get("provider") != "youtube-music"
            or seed.get("kind") != "artist"
            or not isinstance(seed.get("providerId"), str)
            or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", seed["providerId"])
            or ("name" in seed and (not isinstance(seed["name"], str) or len(seed["name"]) > 300))
            or len(json.dumps(seed, ensure_ascii=True).encode("utf-8")) > 4096):
        raise InvalidArgumentError("A selected YouTube Music artist reference is required.")
    if (not isinstance(recording, dict) or recording.get("kind") != "song"
            or recording.get("provider") != "youtube-music"
            or recording.get("providerId") != value
            or recording.get("seedArtistRef") != seed):
        raise InvalidArgumentError("The selected top recording must match its artist and video identity.")
    if fetched_at is not None and (not isinstance(fetched_at, str) or len(fetched_at) > 128):
        raise InvalidArgumentError("The recommendation timestamp is invalid.")
    return value, seed


def song_candidate_metadata_valid(raw):
    if not isinstance(raw, dict):
        return False
    raw = song_candidate_artwork(raw)
    title = raw.get("title")
    if not isinstance(title, str) or not title.strip() or len(title) > 4096:
        return False
    for field in ("resultType", "type", "category"):
        classification = raw.get(field)
        if classification is None or classification == "":
            continue
        if not isinstance(classification, str):
            return False
        key = classification.strip().casefold().replace("_", " ")
        if key in {"song", "track", "music", "audio"}:
            continue
        if (key in {"video", "music video"}
                and raw.get("videoType") in {"MUSIC_VIDEO_TYPE_OMV", "MUSIC_VIDEO_TYPE_UGC"}):
            continue
        return False
    try:
        record = song_record(raw)
    except (TypeError, ValueError, OverflowError):
        return False
    return bool(record and home_artist_refs(raw.get("artists", [])))


def song_candidate_row(raw, seed, relation):
    raw = song_candidate_artwork(raw)
    if not song_candidate_metadata_valid(raw):
        return None
    seconds = song_candidate_duration_seconds(raw)
    if seconds <= 0:
        return None
    normalized = dict(raw)
    normalized["duration_seconds"] = seconds
    try:
        record = song_record(normalized)
    except (TypeError, ValueError, OverflowError):
        return None
    if (not record or seconds <= 0 or not record.get("tracks")
            or record["tracks"][0].get("seconds", 0) <= 0
            or not home_artist_refs(normalized.get("artists", []))):
        return None
    card = home_card("song", normalized, relation)
    if not card:
        return None
    card["seedArtistRef"] = seed
    card["seconds"] = seconds
    card["record"] = record
    return card


def related_row_matches_existing(raw, existing):
    if not song_candidate_metadata_valid(raw):
        return False
    if any(key in raw for key in ("duration_seconds", "seconds", "duration", "length")):
        if song_candidate_duration_seconds(raw) <= 0:
            return False
    if fold(str(raw.get("title") or "").strip()) != fold(str(existing.get("title") or "").strip()):
        return False
    related_refs = home_artist_refs(raw.get("artists", []))
    existing_refs = existing.get("artistRefs", [])
    related_ids = {ref["providerId"] for ref in related_refs}
    existing_ids = {ref.get("providerId") for ref in existing_refs if isinstance(ref, dict)}
    if not related_ids or related_ids != existing_ids:
        return False
    related_type = raw.get("videoType")
    if related_type and existing.get("videoType") and related_type != existing["videoType"]:
        return False
    return True


def song_hydration_result(raw, video_id, response):
    if not isinstance(response, dict) or not isinstance(response.get("videoDetails"), dict):
        return None, "metadata_unavailable"
    details = response["videoDetails"]
    if details.get("videoId") != video_id:
        return None, "video_id_mismatch"
    title = details.get("title")
    if (not isinstance(title, str)
            or fold(title.strip()) != fold(str(raw.get("title") or "").strip())
            or recording_version(raw) != recording_version({"title": title,
                                                           "version": details.get("version")})):
        return None, "title_mismatch"
    if details.get("isLiveContent") is True:
        return None, "nonmusic"
    if (details.get("musicVideoType") and raw.get("videoType")
            and details["musicVideoType"] != raw["videoType"]):
        return None, "video_type_mismatch"
    if (not isinstance(response.get("playabilityStatus"), dict)
            or response["playabilityStatus"].get("status") != "OK"):
        return None, "unavailable"
    length = details.get("lengthSeconds")
    if isinstance(length, str) and re.fullmatch(r"[0-9]{1,6}", length.strip()):
        seconds = int(length.strip())
    elif isinstance(length, int) and not isinstance(length, bool):
        seconds = length
    else:
        return None, "invalid_duration"
    if not 0 < seconds <= 86400:
        return None, "invalid_duration"
    return seconds, None


def song_candidate_error(exc):
    code = getattr(exc, "code", None)
    status = getattr(exc, "code", None) if isinstance(exc, urllib.error.HTTPError) else getattr(
        getattr(exc, "response", None), "status_code", None)
    if code == "rate_limited" or status in (429, 503):
        code = "rate_limited"
    elif not isinstance(code, str):
        code = "timeout" if _is_timeout_exception(exc) else "unavailable"
    result = {"status": "unavailable", "candidateCount": 0, "errorCode": code}
    if code == "rate_limited":
        headers = getattr(exc, "headers", None) or getattr(getattr(exc, "response", None), "headers", {})
        try:
            retry = int(getattr(exc, "retry_after", headers.get("Retry-After", 60)))
        except (AttributeError, TypeError, ValueError, OverflowError):
            retry = 60
        result["retryAfterSeconds"] = max(1, min(3600, retry))
    return result


def song_candidate_counts(result):
    counts = {"radio": 0, "related": 0}
    for item in result["items"]:
        for source in item.get("candidateSources", []):
            if source in counts:
                counts[source] += 1
    for source, count in counts.items():
        result["provenance"]["sources"][source]["candidateCount"] = count


def song_candidates(request, client):
    value, seed = song_candidate_request(request)
    result = home_envelope(request, "youtube-music", "unavailable", False)
    result["provenance"] = {"source": "youtube-music", "generator": "song-radio-related",
                            "sources": {}}
    result["capabilities"] = {"songRadio": False, "songRelated": False}
    try:
        playlist = _call_provider(client.get_watch_playlist, videoId=value, radio=True, limit=25)
        if not isinstance(playlist, dict):
            raise ProviderUnavailableError("The song radio response is invalid.")
    except Exception as exc:
        result["provenance"]["sources"]["radio"] = song_candidate_error(exc)
        result["provenance"]["sources"]["related"] = {
            "status": "not_attempted", "candidateCount": 0, "errorCode": "not_attempted"}
        result["sourceStatus"] = result["provenance"]["sources"]["radio"]["errorCode"]
        if "retryAfterSeconds" in result["provenance"]["sources"]["radio"]:
            result["sourceStatus"] = "rate_limited"
            result["retryAfterSeconds"] = result["provenance"]["sources"]["radio"]["retryAfterSeconds"]
        result["error"] = "The song radio source is unavailable."
        return result

    radio_rows = playlist.get("tracks")
    radio_error = None
    if not isinstance(radio_rows, list):
        radio_error = {"status": "unavailable", "candidateCount": 0,
                       "errorCode": "invalid_response"}
        radio_rows = []
    else:
        result["capabilities"]["songRadio"] = True
        radio_error = {"status": "ready", "candidateCount": 0}
    result["provenance"]["sources"]["radio"] = radio_error

    seen = {}
    source_counts = {"radio": 0, "related": 0}

    def collect(rows, source, relation, row_limit):
        for raw in rows[:row_limit]:
            card = song_candidate_row(raw, seed, relation)
            if not card:
                continue
            video_id = card["providerId"]
            existing = seen.get(video_id)
            if existing is not None:
                if source not in existing["candidateSources"] and source_counts[source] < 20:
                    existing["candidateSources"].append(source)
                    source_counts[source] += 1
                continue
            if source_counts[source] >= 20:
                break
            card["candidateSources"] = [source]
            seen[video_id] = card
            result["items"].append(card)
            source_counts[source] += 1

    def collect_related(rows):
        hydration = {"attempted": 0, "accepted": 0, "failed": 0, "deferred": 0, "budget": 2}
        attempted_ids = set()
        hydration_error = None
        retry_after = None
        for raw in rows[:2000]:
            if source_counts["related"] >= 20:
                break
            if not song_candidate_metadata_valid(raw):
                continue
            video_id = raw.get("videoId")
            if not isinstance(video_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", video_id):
                continue
            has_duration = any(key in raw for key in ("duration_seconds", "seconds", "duration", "length"))
            seconds = song_candidate_duration_seconds(raw)
            if has_duration and seconds <= 0:
                continue
            existing = seen.get(video_id)
            if existing is not None:
                if ("related" not in existing["candidateSources"]
                        and source_counts["related"] < 20
                        and related_row_matches_existing(raw, existing)):
                    existing["candidateSources"].append("related")
                    source_counts["related"] += 1
                continue
            candidate_raw = raw
            if not has_duration:
                if video_id in attempted_ids:
                    continue
                if hydration["attempted"] >= hydration["budget"]:
                    hydration["deferred"] += 1
                    continue
                attempted_ids.add(video_id)
                hydration["attempted"] += 1
                try:
                    metadata = _call_provider(client.get_song, video_id)
                except Exception as exc:
                    hydration["failed"] += 1
                    error = song_candidate_error(exc)
                    hydration_error = error["errorCode"]
                    retry_after = error.get("retryAfterSeconds", retry_after)
                    continue
                seconds, error_code = song_hydration_result(raw, video_id, metadata)
                if error_code:
                    hydration["failed"] += 1
                    hydration_error = error_code
                    continue
                candidate_raw = dict(raw)
                candidate_raw["duration_seconds"] = seconds
            card = song_candidate_row(candidate_raw, seed, "song-related")
            if not card:
                if not has_duration:
                    hydration["failed"] += 1
                    hydration_error = hydration_error or "metadata_unavailable"
                continue
            if source_counts["related"] >= 20:
                break
            card["candidateSources"] = ["related"]
            seen[video_id] = card
            result["items"].append(card)
            source_counts["related"] += 1
            if not has_duration:
                hydration["accepted"] += 1
        source = {"status": "ready", "candidateCount": 0}
        if hydration["attempted"] or hydration["deferred"]:
            source["hydration"] = hydration
        if hydration_error or hydration["deferred"]:
            source["status"] = "partial"
            source["errorCode"] = hydration_error or "hydration_budget"
            if retry_after:
                source["retryAfterSeconds"] = retry_after
        return source

    if radio_error["status"] == "ready":
        collect(radio_rows, "radio", "song-radio", 25)

    related_token = playlist.get("related")
    if not isinstance(related_token, str) or not source_key(related_token):
        related_source = {"status": "unavailable", "candidateCount": 0,
                          "errorCode": "missing_related_token"}
    else:
        try:
            related_rows = _call_provider(client.get_song_related, related_token)
            if not isinstance(related_rows, list):
                raise ProviderUnavailableError("The Related response is invalid.")
            result["capabilities"]["songRelated"] = True
            rows = []
            for shelf in related_rows[:20]:
                if isinstance(shelf, dict) and isinstance(shelf.get("contents"), list):
                    rows.extend(shelf["contents"][:100])
            related_source = collect_related(rows)
        except Exception as exc:
            related_source = song_candidate_error(exc)
    result["provenance"]["sources"]["related"] = related_source
    song_candidate_counts(result)

    source_ok = [radio_error["status"] == "ready", related_source["status"] == "ready"]
    if all(source_ok):
        result["status"] = "ready" if result["items"] else "empty"
    elif any(source_ok):
        result["status"] = "partial"
    else:
        result["status"] = "unavailable"

    errors = [radio_error, related_source]
    rate_limit = next((entry for entry in errors if entry.get("errorCode") == "rate_limited"), None)
    source_error = rate_limit or next((entry for entry in errors
                                       if entry.get("status") in {"unavailable", "partial"}
                                       and entry.get("errorCode")), None)
    if source_error:
        result["sourceStatus"] = source_error["errorCode"]
        if source_error.get("retryAfterSeconds"):
            result["sourceStatus"] = "rate_limited"
            result["retryAfterSeconds"] = source_error["retryAfterSeconds"]
        result["error"] = "One or more song recommendation sources are unavailable."

    while len(json.dumps({"ok": True, "data": result}, ensure_ascii=True).encode("utf-8")) > 256 * 1024 and result["items"]:
        result["items"].pop()
        result["status"] = "partial"
        result["provenance"]["truncatedForResponseLimit"] = True
        song_candidate_counts(result)
    return result


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


def _musicbrainz_artist_names(detail):
    """Keep every distinct official spelling; folding is only for query matching."""
    names, seen = [], set()
    complete = True

    def append(value):
        nonlocal complete
        if not isinstance(value, str) or not value.strip():
            complete = False
            return
        value = value.strip()
        if value not in seen:
            seen.add(value)
            names.append(value)

    append(detail.get("name"))
    if detail.get("sort-name") is not None:
        append(detail["sort-name"])
    aliases = detail.get("aliases", [])
    if not isinstance(aliases, list):
        return names, False
    for alias in aliases:
        if not isinstance(alias, dict):
            complete = False
            continue
        append(alias.get("name"))
        if alias.get("sort-name") is not None:
            append(alias["sort-name"])
    return names, complete


def _identity_metadata_fits(result):
    # Native normalizeHome accepts 256 KiB. Leave headroom for Qt's serialization
    # and the receiving ref/key/destination projection; count UTF-8, not characters.
    return len(json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) <= 239 * 1024


def _enrich_recommendation_aliases(result, detail):
    artist = result["artist"]
    artist.update({"aliases": [], "aliasesComplete": False})
    names, names_complete = _musicbrainz_artist_names(detail)
    for name in names:
        artist["aliases"].append(name)
        if not _identity_metadata_fits(result):
            artist["aliases"].pop()
            break
    else:
        artist["aliasesComplete"] = names_complete



def _load_recommendation_release_groups(result, mb, window=None):
    # Metadata only: the caller's canonical artist binding is validated separately.
    # This phase never creates or revises an identity attestation.
    artist = result["artist"]
    fits = _pool_wire_fits if window is not None else _identity_metadata_fits
    offset, total, seen = 0, None, set()
    for _ in range(5):
        parameters = {"limit":100, "offset":offset, "fmt":"json"}
        if window is None:
            parameters["artist"] = artist["id"]
        else:
            parameters["query"] = (f'arid:"{artist["id"]}" AND firstreleasedate:'
                                   f'[{window["since"]} TO {window["until"]}]')
        query = urllib.parse.urlencode(parameters)
        try:
            page = mb.get_json("https://musicbrainz.org/ws/2/release-group?" + query)
        except Exception as exc:
            result["error"] = "The optional release-group source is unavailable."
            code = getattr(exc, "code", "unavailable")
            result["sourceStatus"] = code if isinstance(code, str) else "unavailable"
            if code == "rate_limited" or (isinstance(exc, urllib.error.HTTPError) and code in (429, 503)):
                result["sourceStatus"] = "rate_limited"
                try:
                    retry = int(getattr(exc, "retry_after", (getattr(exc, "headers", None) or {}).get("Retry-After", 60)))
                except (TypeError, ValueError):
                    retry = 60
                result["retryAfterSeconds"] = max(1, min(3600, retry))
            return
        if not isinstance(page, dict):
            return
        rows, count, returned_offset = (page.get("release-groups"),
            page.get("count" if window is not None else "release-group-count"),
            page.get("offset" if window is not None else "release-group-offset"))
        if (not isinstance(rows, list) or len(rows) > 100 or type(count) is not int or count < 0
                or type(returned_offset) is not int or returned_offset != offset
                or (total is not None and count != total) or offset + len(rows) > count):
            return
        total = count
        artist["releaseGroupCount"] = total
        if not fits(result):
            artist["releaseGroupCount"] = None
            return
        normalized, page_ids = [], set()
        for row in rows:
            if (not isinstance(row, dict) or not isinstance(row.get("id"), str)
                    or not MBID_RE.fullmatch(row["id"]) or uuid.UUID(row["id"]).int == 0
                    or not isinstance(row.get("title"), str) or not row["title"].strip()):
                return
            if window is not None:
                credits = row.get("artist-credit")
                if not isinstance(credits, list) or not any(
                        isinstance(credit, dict) and isinstance(credit.get("artist"), dict)
                        and isinstance(credit["artist"].get("id"), str)
                        and credit["artist"]["id"].lower() == artist["id"].lower()
                        for credit in credits):
                    return
            key = row["id"].lower()
            if key in seen or key in page_ids:
                return
            if ("first-release-date" in row and not isinstance(row["first-release-date"], str)
                    or "primary-type" in row and row["primary-type"] is not None and not isinstance(row["primary-type"], str)
                    or "secondary-types" in row and (not isinstance(row["secondary-types"], list)
                        or any(not isinstance(value, str) for value in row["secondary-types"]))):
                return
            group = {field: row[field] for field in ("id", "title", "first-release-date", "primary-type", "secondary-types") if field in row}
            group["id"] = key
            normalized.append(group)
            page_ids.add(key)
        for group in normalized:
            artist["releaseGroups"].append(group)
            if not fits(result):
                artist["releaseGroups"].pop()
                return
        seen.update(page_ids)
        offset += len(rows)
        if offset == total:
            artist["releaseGroupsComplete"] = True
            return
        if len(rows) < 100:  # Missing rows or a premature empty/short page.
            return



def recommendation_discography(request, mb_client=None):
    if set(request) - {"operation", "value", "seed", "fetchedAt", "refresh", "since", "until"}:
        raise InvalidArgumentError("Unknown discography request option.")
    if ("fetchedAt" in request and (type(request["fetchedAt"]) is not str or len(request["fetchedAt"]) > 128)
            or "refresh" in request and type(request["refresh"]) is not bool):
        raise InvalidArgumentError("Invalid discography request metadata.")
    window = None
    if "since" in request or "until" in request:
        dates = {}
        for key in ("since", "until"):
            value = request.get(key)
            if type(value) is not str or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", value):
                raise InvalidArgumentError("Discography dates require paired full ISO dates.")
            try:
                dates[key] = datetime.date.fromisoformat(value)
            except ValueError:
                raise InvalidArgumentError("Discography dates are invalid.") from None
        if dates["since"] > dates["until"]:
            raise InvalidArgumentError("Discography date window is inverted.")
        window = {key:request[key] for key in ("since", "until")}
    value, seed = request.get("value"), request.get("seed")
    if (type(value) is not str or not MBID_RE.fullmatch(value) or uuid.UUID(value).int == 0
            or type(seed) is not dict
            or seed != {"identity":"canonical", "kind":"artist", "mbid":value}):
        raise InvalidArgumentError("Discography requires an exactly bound canonical artist reference.")
    result = home_envelope(request, "musicbrainz")
    result["artist"] = {"id":value, "releaseGroups":[], "releaseGroupsComplete":False,
                        "releaseGroupCount":None}
    if window is not None:
        result["artist"].update({"releaseGroupScope":"recent-releases", "releaseGroupWindow":window})
    mb = mb_client or MusicBrainzClient(max_retries=1, timeout=2.5)
    _load_recommendation_release_groups(result, mb, window)
    groups = result["artist"]["releaseGroups"]
    # Include any late transport/error metadata in the actual ASCII stdio budget.
    # Keep the legacy browse response/schema unchanged when no window is supplied.
    if window is not None:
        while groups and not _pool_wire_fits(result):
            groups.pop()
            result["artist"]["releaseGroupsComplete"] = False
    result["complete"] = result["artist"]["releaseGroupsComplete"]
    result["status"] = ("ready" if groups else "empty") if result["complete"] else ("partial" if groups else "unavailable")
    if result["status"] == "unavailable":
        result["error"] = "The optional release-group evidence is unavailable or incomplete."
    return result


def recommendation_identity(request, client, mb_client=None):
    result = home_envelope(request, "musicbrainz", "unresolved", True)
    seed = home_seed(request)
    raw = home_raw_artist(client, seed)
    mb = mb_client or MusicBrainzClient(max_retries=1, timeout=2.5)
    name = raw["name"].strip()[:300]
    phrase = name.replace("\\", "\\\\").replace('"', '\\"')
    query = urllib.parse.urlencode({"query": '(artist:"' + phrase + '" OR alias:"' + phrase + '")',
                                   "limit": 5, "fmt": "json"})
    searched = mb.get_json("https://musicbrainz.org/ws/2/artist/?" + query)
    if not isinstance(searched, dict) or not isinstance(searched.get("artists"), list):
        raise ProviderUnavailableError("The artist identity search is incomplete.")
    rows = searched["artists"]
    count = searched.get("count", len(rows))
    if type(count) is not int or count < 0:
        raise ProviderUnavailableError("The artist identity search count is invalid.")
    offset = searched.get("offset", 0)
    result["complete"] = count == len(rows) and len(rows) <= 5 and type(offset) is int and offset == 0
    candidates, seen = [], set()
    for candidate in rows[:5]:
        if (not isinstance(candidate, dict) or not isinstance(candidate.get("id"), str)
                or not MBID_RE.fullmatch(candidate["id"]) or uuid.UUID(candidate["id"]).int == 0
                or candidate["id"].lower() in seen):
            result["complete"] = False
            continue
        seen.add(candidate["id"].lower())
        candidates.append(candidate)
    if sum(fold(candidate.get("name", "")) == fold(name) for candidate in candidates) > 2:
        result.update({"mappingStatus": "ambiguous", "status": "ambiguous"})
        return result
    matching = []
    for candidate in candidates:
        detail = mb.get_json("https://musicbrainz.org/ws/2/artist/" + candidate["id"] + "?inc=aliases%2Burl-rels%2Brelease-groups&fmt=json")
        if (not isinstance(detail, dict) or not isinstance(detail.get("id"), str)
                or detail["id"].lower() != candidate["id"].lower()
                or not isinstance(detail.get("name"), str) or not detail["name"].strip()):
            raise ProviderUnavailableError("The canonical artist detail is invalid.")
        names, names_complete = _musicbrainz_artist_names(detail)
        if not names_complete:
            result["complete"] = False
        if fold(name) in {fold(value) for value in names}:
            matching.append((candidate, detail))
    result["mappingStatus"] = "ambiguous" if len(matching) > 1 else "unresolved"
    result["status"] = result["mappingStatus"]
    if len(matching) > 2:
        return result
    confirmed = []
    for candidate, detail in matching:
        relation = channel_relation(detail, seed["providerId"])
        method, evidence = ("external-id", [relation]) if relation else (None, [])
        # Alias equality only admits a candidate. Unique name plus two dated
        # releases or the bound channel relation still supplies the attestation.
        if not method and len(matching) == 1:
            own = set()
            for section in ("albums", "singles"):
                for entry in (raw.get(section) or {}).get("results", [])[:20]:
                    if isinstance(entry, dict) and re.fullmatch(r"\d{4}", str(entry.get("year", ""))):
                        own.add((fold(entry.get("title", "")), str(entry["year"])))
            for group in detail.get("release-groups", [])[:25]:
                if isinstance(group, dict) and (fold(group.get("title", "")), str(group.get("first-release-date", ""))[:4]) in own:
                    evidence.append(group.get("id"))
            evidence = list(dict.fromkeys(e for e in evidence if isinstance(e, str) and MBID_RE.fullmatch(e) and uuid.UUID(e).int != 0))
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
                       "artist": {"id": mapping["mbid"], "name": detail["name"], "disambiguation": detail.get("disambiguation", "")}})
        if not _identity_metadata_fits(result):
            raise ProviderUnavailableError("The canonical artist identity exceeds its response budget.")
        _enrich_recommendation_aliases(result, detail)
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


def handle_home(request, client=None, mb_client=None, population_client=None, source_pool_transport=None):
    operation = request.get("operation")
    if operation in {"chartCandidates", "similarArtists"}:
        return source_pool_metadata(request, source_pool_transport)
    source = "musicbrainz" if operation in {"recommendationIdentity", "recommendationDiscography"} else "listenbrainz" if operation == "populationCandidates" else "youtube-music"
    envelope_request = request
    if operation == "songCandidates":
        seed = request.get("seed")
        try:
            seed_size = len(json.dumps(seed, ensure_ascii=True).encode("utf-8"))
        except (TypeError, ValueError):
            seed_size = 4097
        if not isinstance(seed, dict) or seed_size > 4096:
            envelope_request = dict(request, seed={})
        fetched_at = request.get("fetchedAt")
        if fetched_at is not None and (not isinstance(fetched_at, str) or len(fetched_at) > 128):
            envelope_request = dict(envelope_request, fetchedAt=None)
    result = home_envelope(envelope_request, source)
    if operation == "songCandidates":
        result["provenance"]["generator"] = "song-radio-related"
    try:
        if len(json.dumps(request).encode()) > 256 * 1024:
            raise InvalidArgumentError("The recommendation request is oversized.")
        if operation == "recommendationDiscography":
            return recommendation_discography(request, mb_client)
        if operation == "populationCandidates":
            return population_candidates(request, population_client)
        if operation in {"artistCandidates", "recommendationIdentity"}:
            home_seed(request)
        if operation == "songCandidates":
            song_candidate_request(request)
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
        if operation == "songCandidates":
            return song_candidates(request, client)
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


DISCOVER_KINDS = ("songs", "albums", "artists")
DISCOVER_TAGS = tuple(("pop rock hip-hop rnb indie electronic dance metal jazz classical soul folk "
                       "punk alternative k-pop j-pop latin reggae blues country soundtrack lo-fi ambient").split())
DISCOVER_LANGUAGES = tuple("tamil telugu malayalam kannada hindi punjabi bengali marathi".split())
_DISCOVER_RESPONSE_LIMIT = 2 * 1024 * 1024
_DISCOVER_CACHE_LIMIT = 64 * 1024 * 1024
_DISCOVER_CACHE_FILES = 512
_DISCOVER_CACHE_LOCK = threading.Lock()
_LASTFM_LOCK = threading.Lock()
_LASTFM_NEXT_REQUEST = 0.0
_LASTFM_TTL = 24 * 3600
_DISCOVER_ART_TTL = 30 * 24 * 3600


class _DiscoverNoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ProviderUnavailableError("The catalogue redirected its metadata request.")


def _discover_hash(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=True, sort_keys=True,
                                     separators=(",", ":")).encode()).hexdigest()


def _discover_read(key, valid):
    root = os.environ.get("COLOSSEUM_VINYL_DISCOVER_CACHE_DIR")
    if not root:
        return None
    path = Path(root) / ("discover-v1-" + _discover_hash(key) + ".json")
    try:
        with path.open("rb") as f:
            data = json.loads(f.read(_DISCOVER_RESPONSE_LIMIT + 1))
        if (isinstance(data, dict) and type(data.get("time")) in (float, int)
                and 0 < data["time"] <= time.time()
                and type(data.get("ttl")) is int and 0 <= data["ttl"] <= _DISCOVER_ART_TTL
                and isinstance(data.get("staleAllowed"), bool) and valid(data.get("value"))):
            return data
    except (OSError, ValueError, TypeError, KeyError, ProviderError):
        pass
    return None


def _discover_write(key, entry):
    root = os.environ.get("COLOSSEUM_VINYL_DISCOVER_CACHE_DIR")
    if not root:
        return
    root = Path(root)
    path = root / ("discover-v1-" + _discover_hash(key) + ".json")
    if entry is None:
        with contextlib.suppress(OSError):
            path.unlink(missing_ok=True)
        return
    encoded = json.dumps(entry, ensure_ascii=True, separators=(",", ":")).encode()
    if len(encoded) > _DISCOVER_RESPONSE_LIMIT:
        return
    temporary = None
    with _DISCOVER_CACHE_LOCK:
        try:
            root.mkdir(parents=True, exist_ok=True)
            entries = []
            for candidate in root.glob("discover-v1-*.json"):
                if candidate != path:
                    with contextlib.suppress(OSError):
                        stats = candidate.stat()
                        entries.append((stats.st_mtime, stats.st_size, candidate))
            total = len(encoded) + sum(size for _, size, _ in entries)
            count = len(entries) + 1
            for _, size, candidate in sorted(entries):
                if total <= _DISCOVER_CACHE_LIMIT and count <= _DISCOVER_CACHE_FILES:
                    break
                candidate.unlink(missing_ok=True)
                total -= size
                count -= 1
            with tempfile.NamedTemporaryFile(dir=root, prefix="discover-tmp-", delete=False) as f:
                temporary = Path(f.name)
                f.write(encoded)
            temporary.replace(path)
        except OSError:
            pass
        finally:
            if temporary:
                with contextlib.suppress(OSError):
                    temporary.unlink(missing_ok=True)


def _discover_http(source, method, params, language="", *, transport=None, timeout=8):
    global _LASTFM_NEXT_REQUEST
    if source == "lastfm":
        key = os.environ.get("COLOSSEUM_VINYL_LASTFM_API_KEY", "").strip()
        if not key:
            key_file = os.environ.get("COLOSSEUM_VINYL_LASTFM_KEY_FILE")
            if key_file:
                try:
                    with Path(key_file).open(encoding="utf-8-sig") as f:
                        key = f.read(128).strip()
                except (OSError, UnicodeError):
                    pass
        if not re.fullmatch(r"[a-fA-F0-9]{32}", key):
            raise ProviderUnavailableError("Last.fm is unavailable because its API key is missing or invalid.")
        url = "https://ws.audioscrobbler.com/2.0/?" + urllib.parse.urlencode(
            dict(params, method=method, api_key=key, format="json"))
        headers = {"User-Agent": "Colosseum Vinyl"}
        with _LASTFM_LOCK:
            time.sleep(max(0, _LASTFM_NEXT_REQUEST - time.monotonic()))
            _LASTFM_NEXT_REQUEST = time.monotonic() + 0.205
    else:
        url = "https://www.jiosaavn.com/api.php?" + urllib.parse.urlencode(
            dict(params, _format="json", _marker=0, api_version=4, ctx="web6dot0", __call=method))
        headers = {"User-Agent": "Mozilla/5.0", "Cookie": "L=" + language}
    label = "Last.fm" if source == "lastfm" else "JioSaavn"
    try:
        opener = transport or urllib.request.build_opener(_DiscoverNoRedirect())
        with opener.open(urllib.request.Request(url, headers=headers), timeout=timeout) as response:
            if not 200 <= getattr(response, "status", 200) < 300:
                raise urllib.error.HTTPError(url, response.status, "Metadata request failed", response.headers, None)
            raw = response.read(_DISCOVER_RESPONSE_LIMIT + 1)
            if len(raw) > _DISCOVER_RESPONSE_LIMIT:
                raise ProviderUnavailableError(label + " returned too much catalogue data.")
            data = json.loads(raw)
            directives = response.headers.get("Cache-Control", "")
            age = response.headers.get("Age", "0")
            expires = response.headers.get("Expires")
            date = response.headers.get("Date")
    except urllib.error.HTTPError as exc:
        if exc.code in (429, 503):
            raise RateLimitError(label + " is limiting catalogue requests.") from None
        raise ProviderUnavailableError(label + " is unavailable for this catalogue request.") from None
    except (OSError, ValueError) as exc:
        if _is_timeout_exception(exc):
            raise ProviderTimeoutError(label + " catalogue request timed out.") from None
        raise ProviderUnavailableError(label + " is unavailable for this catalogue request.") from None
    if isinstance(data, dict) and "error" in data:
        if source == "lastfm" and data["error"] == 29:
            raise RateLimitError("Last.fm is limiting catalogue requests.")
        raise ProviderUnavailableError(label + " could not provide this catalogue data.")
    return data, directives, age, expires, date


def _discover_cached(key, ttl, refresh, load, valid, honor_headers=False):
    previous = _discover_read(key, valid)
    if previous and not refresh and time.time() < previous["time"] + previous["ttl"]:
        return previous["value"], previous["time"], False
    try:
        value, directives, age, expires, date = load()
        if not valid(value):
            raise ProviderUnavailableError("The catalogue source changed its metadata format.")
    except ProviderError:
        if previous and previous["staleAllowed"]:
            return previous["value"], previous["time"], True
        raise
    timestamp = time.time()
    rules = {name.strip().lower(): val.strip().strip('"')
             for name, _, val in (part.partition("=") for part in directives.split(","))}
    if honor_headers:
        if "max-age" in rules and rules["max-age"].isdigit():
            ttl = min(ttl, int(rules["max-age"][:9]))
        elif expires:
            try:
                end = email.utils.parsedate_to_datetime(expires).timestamp()
                start = email.utils.parsedate_to_datetime(date).timestamp() if date else timestamp
                ttl = min(ttl, max(0, int(end - start)))
            except (TypeError, ValueError, OverflowError):
                pass
        if age.isdigit():
            ttl = max(0, ttl - int(age[:9]))
        if "no-cache" in rules:
            ttl = 0
    stale_allowed = not honor_headers or not ({"no-cache", "must-revalidate"} & rules.keys())
    _discover_write(key, None if honor_headers and "no-store" in rules else
                    {"time": timestamp, "ttl": ttl, "staleAllowed": stale_allowed, "value": value})
    return value, timestamp, False


def _discover_text(value):
    return html.unescape(value).strip() if isinstance(value, str) and len(value) <= 600 else ""


def _discover_rows(value, limit=250):
    if not isinstance(value, list) or len(value) > limit or not all(isinstance(row, dict) for row in value):
        raise ProviderUnavailableError("The catalogue source changed its metadata format.")
    return value


def _discover_number(value):
    if type(value) is int and 0 < value <= 86400:
        return value
    if isinstance(value, str) and re.fullmatch(r"[0-9]{1,5}", value):
        return min(int(value), 86400)
    return 0


def _discover_cover(value, hosts):
    if not isinstance(value, str) or len(value) > 1500 or re.search(r"[\s\\]", value):
        return ""
    try:
        url = urlparse(value)
        host = url.hostname or ""
        if (url.scheme == "https" and url.username is None and url.password is None and url.port in (None, 443)
                and any(host == suffix or host.endswith("." + suffix) for suffix in hosts)):
            return value
    except ValueError:
        pass
    return ""


def _lastfm_cover(url):
    return _discover_cover(url, ("last.fm", "lastfm-img.freetls.fastly.net", "lst.fm"))


def _lastfm_image(row):
    images = row.get("image")
    if isinstance(images, list):
        for image in reversed(images):
            url = _lastfm_cover(image.get("#text")) if isinstance(image, dict) else ""
            if url and "2a96cbd8b46e442fc41c2b86b821562f" not in url:
                return url
    return ""


def _discover_card(source, kind, key, title, credits, image, year="", **hints):
    label = ", ".join(credits)
    return {"id": f"{source}:{kind}:" + hashlib.sha256(key.encode()).hexdigest()[:24], "kind": kind, "title": title,
            "artist": label, "subtitle": label, "cover": image, "year": year, "catalogueCard": True,
            "source": source, "sourceIds": {"id": key}, "hints": dict(hints, artists=credits)}


def _lastfm_page(kind, list_key, cursor, page_size, refresh):
    # Last.fm's documented global chart only has track and artist methods.
    if list_key not in DISCOVER_TAGS and not (list_key == "chart" and kind != "albums"):
        raise InvalidArgumentError("Unsupported Last.fm list.")
    binding = _discover_hash(["lastfm", kind, list_key, page_size])[:24]
    page = cursor.get("page", 1)
    if cursor and (set(cursor) != {"page", "binding"} or cursor["binding"] != binding
                   or type(page) is not int or not 2 <= page <= 20):
        raise InvalidArgumentError("Invalid Last.fm page cursor.")
    leaf = "track" if kind == "songs" else kind[:-1]
    chart = list_key == "chart"
    method = ("chart" if chart else "tag") + ".gettop" + leaf + "s"
    root = "topartists" if kind == "artists" and not chart else leaf + "s"
    params = {"limit": page_size, "page": page}
    if not chart:
        params["tag"] = list_key

    def parse(data):
        envelope = data.get(root) if isinstance(data, dict) else None
        if not isinstance(envelope, dict):
            raise ProviderUnavailableError("Last.fm changed its catalogue list format.")
        rows = _discover_rows(envelope.get(leaf), page_size)
        items = {}
        for row in rows:
            title = _discover_text(row.get("name"))
            artist = row.get("artist")
            name = _discover_text(artist.get("name")) if isinstance(artist, dict) else _discover_text(artist)
            if not title or (kind != "artists" and not name):
                raise ProviderUnavailableError("Last.fm returned an invalid catalogue row.")
            # The canonical URL identifies the Last.fm item even when its optional MBID is absent.
            key = _discover_cover(row.get("url"), ("last.fm",)) or _discover_text(row.get("mbid"))
            key = key or json.dumps([title, name], ensure_ascii=True, separators=(",", ":"))
            credits = [title] if kind == "artists" else [name]
            hints = {}
            if kind == "songs" and _discover_number(row.get("duration")):
                hints["duration"] = _discover_number(row["duration"])
            card = _discover_card("lastfm", kind, key, title, credits, _lastfm_image(row), **hints)
            if kind == "artists":
                card["subtitle"] = ""
            items.setdefault(card["id"], card)
        return {"items": list(items.values()), "count": len(rows)}

    def load():
        data, *headers = _discover_http("lastfm", method, params)
        return parse(data), *headers

    def valid(value):
        if not isinstance(value, dict) or type(value.get("count")) is not int:
            return False
        items = value.get("items")
        return (isinstance(items, list) and 0 <= len(items) <= value["count"] <= page_size
                and all(_catalogue_input(item)["source"] == "lastfm" and item["kind"] == kind
                        and _lastfm_cover(item["cover"]) == item["cover"]
                        and isinstance(item.get("year"), str) and isinstance(item.get("subtitle"), str)
                        for item in items))

    data, stamp, stale = _discover_cached(
        ["lastfm-projection", method, params], _LASTFM_TTL, refresh, load, valid, honor_headers=True)
    items = data["items"]
    complete = data["count"] < page_size or page == 20
    return items, {} if complete else {"page": page + 1, "binding": binding}, stamp, stale, None


def _jio_playlist_refs(rows):
    result = {}
    for row in _discover_rows(rows, 60):
        key, title = row.get("id"), _discover_text(row.get("title"))
        url = _discover_cover(row.get("perma_url"), ("jiosaavn.com",))
        token = urlparse(url).path.rstrip("/").split("/")[-1]
        if not source_key(key) or not title or not re.fullmatch(r"[A-Za-z0-9_,.-]{1,160}", token):
            raise ProviderUnavailableError("JioSaavn changed its playlist directory format.")
        result.setdefault(key, [key, title, token])
    return list(result.values())


def _jio_items(rows, kind, language, new=False):
    for row in _discover_rows(rows, 50):
        if row.get("type") != ("album" if new else "song"):
            continue
        row_language = row.get("language")
        if not isinstance(row_language, str):
            raise ProviderUnavailableError("JioSaavn returned a row without a language.")
        if row_language.casefold() != language:
            continue
        title, key = _discover_text(row.get("title")), row.get("id")
        info = row.get("more_info")
        if not title or not source_key(key) or not isinstance(info, dict):
            raise ProviderUnavailableError("JioSaavn changed its catalogue item format.")
        mapping = info.get("artistMap")
        if not isinstance(mapping, dict):
            raise ProviderUnavailableError("JioSaavn changed its artist credits format.")
        primary = _discover_rows(mapping.get("primary_artists"), 60)
        all_artists = _discover_rows(mapping.get("artists", primary), 100)
        credits = [_discover_text(a.get("name")) for a in primary]
        credits = list(dict.fromkeys(name for name in credits if name))
        if not credits:
            credits = list(dict.fromkeys(name for a in all_artists if (name := _discover_text(a.get("name")))))
        image = _jio_image(row)
        year = _discover_text(row.get("year")) or _discover_text(info.get("release_date"))[:4]
        year = year if re.fullmatch(r"[0-9]{4}", year) else ""
        album = _discover_text(info.get("album"))
        if kind == "songs":
            duration = _discover_number(info.get("duration"))
            yield _discover_card("jiosaavn", kind, key, title, credits, image, year,
                                 language=language, album=album, duration=duration)
        elif kind == "albums":
            if not new:
                key, title = info.get("album_id"), album
            if source_key(key) and title:
                yield _discover_card("jiosaavn", kind, key, title, credits, image, year, language=language)
        else:
            for artist in primary:
                name, key = _discover_text(artist.get("name")), artist.get("id")
                if source_key(key) and name:
                    card = _discover_card("jiosaavn", kind, key, name, [name], _jio_image(artist),
                                          language=language)
                    card["subtitle"] = ""
                    yield card


def _jio_image(row):
    return _discover_cover(row.get("image"), ("saavncdn.com",)).replace("150x150", "500x500")


def _jio_page(kind, list_key, cursor, page_size, refresh):
    language, _, selected = list_key.partition(":")
    if (language not in DISCOVER_LANGUAGES or not source_key(selected)
            or (selected == "new" and kind != "albums")):
        raise InvalidArgumentError("Unsupported JioSaavn language or list.")
    binding = _discover_hash(["jiosaavn", kind, list_key, page_size])[:24]
    index, offset, seen = cursor.get("listIndex", 0), cursor.get("offset", 0), cursor.get("seen", [])
    if cursor and (set(cursor) != {"binding", "listIndex", "offset", "seen", "directory", "playlist"}
                   or cursor["binding"] != binding
                   or type(index) is not int or not 0 <= index <= 61
                   or type(offset) is not int or not 0 <= offset <= 3000
                   or not isinstance(seen, list) or len(seen) > 10000
                   or not all(isinstance(x, str) and re.fullmatch(r"[a-f0-9]{24}", x) for x in seen)
                   or not all(isinstance(cursor[k], str) and re.fullmatch(r"[a-f0-9]{64}|", cursor[k])
                              for k in ("directory", "playlist"))):
        raise InvalidArgumentError("Invalid JioSaavn page cursor.")
    stamps = []
    stale_stamps = []

    def read(method, params, ttl, parse, directory=False):
        def load():
            raw, *headers = _discover_http("jiosaavn", method, params, language)
            return parse(raw), *headers

        def valid(rows):
            if not isinstance(rows, list):
                return False
            if directory:
                return len(rows) <= 60 and all(
                    isinstance(row, list) and len(row) == 3 and source_key(row[0])
                    and bool(_discover_text(row[1])) and isinstance(row[2], str)
                    and bool(re.fullmatch(r"[A-Za-z0-9_,.-]{1,160}", row[2])) for row in rows)
            return len(rows) <= 3000 and all(
                _catalogue_input(row)["source"] == "jiosaavn" and row["kind"] == kind
                and row["hints"].get("language") == language and _jio_image({"image": row["cover"]}) == row["cover"]
                and isinstance(row.get("year"), str) and isinstance(row.get("subtitle"), str) for row in rows)

        # JioSaavn sends no-store. Persist only the validated application projection,
        # never the HTTP response, headers, or session data.
        return _discover_cached(
            ["jiosaavn-projection", method, language, params, "" if directory else kind],
            ttl, refresh, load, valid)

    def accept(response):
        result, stamp, stale = response
        stamps.append(stamp)
        if stale:
            stale_stamps.append(stamp)
        return result

    def wrapped(data, key):
        return data.get(key) if isinstance(data, dict) else None

    chart_ttl, editorial_ttl = 6 * 3600, 12 * 3600
    with ThreadPoolExecutor(max_workers=3) as executor:
        charts_future = executor.submit(read, "content.getCharts", {"languages": language},
                                        chart_ttl, _jio_playlist_refs, True)
        editorial_future = None
        new_future = None
        if selected in {"default", "editorial"}:
            editorial_future = executor.submit(
                read, "content.getFeaturedPlaylists",
                {"languages": language, "fetch_from_serialized_files": "true", "p": 1, "n": 50},
                editorial_ttl, lambda data: _jio_playlist_refs(wrapped(data, "data")), True)
        if kind == "albums" and selected in {"default", "new"} and index == 0:
            new_future = executor.submit(
                read, "content.getAlbums", {"languages": language, "p": 1, "n": 50},
                editorial_ttl, lambda data: list(_jio_items(wrapped(data, "data"), kind, language, new=True)))
        charts = accept(charts_future.result())
        editorial = accept(editorial_future.result()) if editorial_future else []
        new_items = accept(new_future.result()) if new_future else []

    options = [{"key": "default", "label": "Top 50 & Editorial"}, {"key": "editorial", "label": "Editorial"}]
    if kind == "albums":
        options.insert(0, {"key": "new", "label": "New releases"})
    options.extend({"key": key, "label": title} for key, title, _ in charts)
    chart_lists = [(token, chart_ttl) for key, _, token in charts if key == selected]
    if selected == "default":
        top = next((token for _, title, token in charts
                    if language in title.casefold() and "superhits top 50" in title.casefold()), "")
        if not top:
            top = next((token for _, title, token in charts if "superhits top 50" in title.casefold()), "")
        chart_lists = [(top, chart_ttl)] if top else []
    elif selected not in {"editorial", "new"} and not chart_lists:
        raise InvalidArgumentError("This JioSaavn chart is no longer available.")
    playlists = dict(chart_lists)
    for _, _, token in editorial:
        playlists.setdefault(token, editorial_ttl)
    if kind == "albums" and selected in {"default", "new"}:
        playlists = {"": editorial_ttl, **playlists}
    entries = list(playlists.items())
    directory = _discover_hash(entries)
    if cursor and (cursor["directory"] != directory or index >= len(entries)):
        raise ProviderUnavailableError("JioSaavn changed this list. Refresh to start from the current ranking.")

    seen = set(seen)
    items = []
    revision = ""
    while index < len(entries) and len(items) < page_size and len(seen) < 10000:
        token, ttl = entries[index]
        if token:
            cards = accept(read(
                "webapi.get", {"type": "playlist", "token": token, "p": 1, "n": 50}, ttl,
                lambda data: list(_jio_items(wrapped(data, "list"), kind, language))))
        else:
            cards = new_items
        revision = _discover_hash([card["id"] for card in cards])
        if offset and (not cursor or revision != cursor["playlist"] or offset >= len(cards)):
            raise ProviderUnavailableError("JioSaavn changed this playlist. Refresh to start from the current ranking.")
        while offset < len(cards) and len(items) < page_size and len(seen) < 10000:
            card = cards[offset]
            offset += 1
            key = card["id"].rsplit(":", 1)[-1]
            if key not in seen:
                seen.add(key)
                items.append(card)
        if offset == len(cards):
            index += 1
            offset = 0
            revision = ""
    next_cursor = {}
    if index < len(entries) and len(seen) < 10000:
        next_cursor = {"binding": binding, "listIndex": index, "offset": offset, "seen": sorted(seen),
                       "directory": directory, "playlist": revision}
    return items, next_cursor, min(stale_stamps or stamps), bool(stale_stamps), options


def fetch_music_catalogue(request):
    kind, source, list_key = request.get("value"), request.get("catalogueSource"), request.get("list")
    page_size, cursor, refresh = request.get("pageSize", 50), request.get("cursor", {}), request.get("refresh", False)
    if (kind not in DISCOVER_KINDS or not isinstance(list_key, str) or len(list_key) > 180
            or not isinstance(cursor, dict) or type(page_size) is not int or not 1 <= page_size <= 50
            or type(refresh) is not bool or (cursor and refresh)):
        raise InvalidArgumentError("Invalid music catalogue page request.")
    fetch = _lastfm_page if source == "lastfm" else _jio_page
    items, next_cursor, stamp, stale, options = fetch(kind, list_key, cursor, page_size, refresh)
    result = {"version": 1, "kind": kind, "source": source, "list": list_key, "items": items,
              "nextCursor": next_cursor, "complete": not next_cursor,
              "fetchedAt": datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc).isoformat(),
              "stale": stale, "warning": ""}
    if options is not None:
        result["listOptions"] = options
    return result


def _catalogue_input(item, expected_id=None):
    if not isinstance(item, dict) or item.get("catalogueCard") is not True:
        raise InvalidArgumentError("Invalid music catalogue card.")
    source, kind, identifier = item.get("source"), item.get("kind"), item.get("id")
    title, artist, hints = item.get("title"), item.get("artist"), item.get("hints")
    ids = item.get("sourceIds")
    key = ids.get("id") if isinstance(ids, dict) else None
    if (source not in ("lastfm", "jiosaavn", "apple-music", "listenbrainz") or kind not in DISCOVER_KINDS
            or not isinstance(key, str) or not 1 <= len(key) <= 1600
            or identifier != f"{source}:{kind}:" + hashlib.sha256(key.encode()).hexdigest()[:24]
            or (expected_id is not None and identifier != expected_id)
            or not isinstance(title, str) or not 1 <= len(title) <= 600
            or not isinstance(artist, str) or len(artist) > 10000
            or not isinstance(hints, dict)):
        raise InvalidArgumentError("Invalid music catalogue card.")
    if source == "apple-music" and not re.fullmatch(r"[1-9][0-9]{0,19}", key):
        raise InvalidArgumentError("Invalid Apple catalogue identity.")
    if source == "listenbrainz" and (not MBID_RE.fullmatch(key) or uuid.UUID(key).int == 0):
        raise InvalidArgumentError("Invalid ListenBrainz catalogue identity.")
    credits = hints.get("artists", [artist] if artist else [])
    if (not isinstance(credits, list) or len(credits) > 100
            or not all(isinstance(name, str) and 1 <= len(name) <= 600 for name in credits)
            or ", ".join(credits) != artist):
        raise InvalidArgumentError("Invalid music catalogue credits.")
    return {"id": identifier, "source": source, "kind": kind, "title": title, "artists": credits,
            "album": str(hints.get("album") or "")[:600], "duration": _discover_number(hints.get("duration"))}


_CATALOGUE_METADATA_SUFFIX = re.compile(
    r"""\s*(?:\(From\s+(?:"[^"]{1,300}"|“[^”]{1,300}”)\)|\[(?:Tamil|Telugu|Malayalam|Kannada|Hindi|Punjabi|Bengali|Marathi)\]|\(Language:\s*(?:Tamil|Telugu|Malayalam|Kannada|Hindi|Punjabi|Bengali|Marathi)(?:;\s*Film:\s*[^()]{1,300})?\)|\(Original Motion Picture Soundtrack\))\s*$""",
    re.IGNORECASE)


def _catalogue_match_title(title):
    title = str(title or "")
    while match := _CATALOGUE_METADATA_SUFFIX.search(title):
        title = title[:match.start()].rstrip()
    return title


def resolve_catalogue_item(request, client):
    item_id = request.get("value")
    if not isinstance(item_id, str):
        raise InvalidArgumentError("Invalid music catalogue identity.")
    row = _catalogue_input(request.get("item"), item_id)
    kind = row["kind"]
    cache_key = ["tap", _discover_hash(row)]
    envelope = {"version": 1, "id": item_id, "kind": kind}
    cached = _discover_read(cache_key, lambda value: (
        isinstance(value, dict) and type(value.get("matched")) is bool
        and all(value.get(k) == v for k, v in envelope.items())
        and ((isinstance(value.get("reason"), str) and not value.get("record")) if not value["matched"]
             else isinstance(value.get("artistId"), str) if kind == "artists"
             else isinstance(value.get("record"), dict) and value["record"].get("id") == value.get("albumId")
             and isinstance(value.get("albumId"), str))))
    if cached and time.time() < cached["time"] + cached["ttl"]:
        return cached["value"]
    row["title"] = _catalogue_match_title(row["title"]) if kind != "artists" else row["title"]
    if kind != "artists" and (not row["title"] or not row["artists"]):
        return dict(envelope, matched=False, reason="This catalogue entry has insufficient matching metadata.")
    match, reason = _catalogue_match(client if client is not None else _ytmusic_client(""), row, kind)
    result = dict(envelope, matched=match is not None)
    if match:
        result.update({key: value for key, value in match.items() if key in {"record", "albumId", "artistId", "cover"}})
    else:
        result["reason"] = reason
    _discover_write(cache_key, {"time": time.time(), "ttl": _DISCOVER_ART_TTL if match else _LASTFM_TTL,
                                "staleAllowed": False, "value": result})
    return result


def resolve_catalogue_art(request):
    items = request.get("items")
    if request.get("value") != "lastfm" or not isinstance(items, list) or len(items) > 12:
        raise InvalidArgumentError("Invalid music catalogue artwork request.")
    rows = {}
    for item in items:
        row = _catalogue_input(item)
        if row["source"] != "lastfm":
            raise InvalidArgumentError("Artwork enrichment is available for Last.fm cards.")
        if row["kind"] in ("songs", "artists"):
            rows[row["id"]] = row

    def resolve(row):
        if row["kind"] == "songs" and row["artists"]:
            method = "track.getInfo"
            params = {"artist": row["artists"][0], "track": row["title"]}
        elif row["kind"] == "artists":
            method = "artist.getTopAlbums"
            params = {"artist": row["title"], "limit": 5}
        else:
            return ""

        def load():
            data, *headers = _discover_http("lastfm", method, params)
            if not isinstance(data, dict):
                raise ProviderUnavailableError("Last.fm changed its artwork format.")
            if method == "track.getInfo":
                track = data.get("track")
                if not isinstance(track, dict):
                    raise ProviderUnavailableError("Last.fm changed its track artwork format.")
                album = track.get("album")
                return (_lastfm_image(album) if isinstance(album, dict) else "", *headers)
            top = data.get("topalbums")
            albums = _discover_rows(top.get("album") if isinstance(top, dict) else None, 5)
            return (next((image for album in albums if (image := _lastfm_image(album))), ""), *headers)

        try:
            image, _, _ = _discover_cached(
                ["art", method, params], _DISCOVER_ART_TTL, False, load,
                lambda value: value == "" or bool(_lastfm_cover(value)), honor_headers=True)
            return image
        except ProviderError:
            return ""

    with ThreadPoolExecutor(max_workers=4) as executor:
        covers = {key: image for key, image in zip(rows, executor.map(resolve, rows.values())) if image}
    return {"version": 1, "covers": covers, "fetchedAt": now_iso()}


def _catalogue_text_key(text, release=False):
    text = unicodedata.normalize("NFKC", str(text or "")).casefold()
    if release:
        text = re.sub(r"\s+-\s+(?:ep|single)\s*$", "", text)
    # Punctuation is superficial; all words (including edition tokens) survive.
    return " ".join("".join(c if c.isalnum() or unicodedata.category(c).startswith("M") else " " for c in text).split())


def _catalogue_safe_thumbnails(raw):
    thumbs = []
    for thumb in raw.get("thumbnails") or []:
        if not isinstance(thumb, dict) or not isinstance(thumb.get("url"), str):
            continue
        if _discover_cover(thumb["url"], ("ytimg.com", "ggpht.com", "googleusercontent.com")):
            width = thumb.get("width")
            thumbs.append({"url": thumb["url"], "width": width if type(width) is int else 0})
    return thumbs


def _catalogue_match(client, row, kind):
    query_title = re.sub(r"\s+-\s+(?:EP|Single)\s*$", "", row["title"], flags=re.I) if kind == "albums" else row["title"]
    query = " ".join(row["artists"] + ([] if kind == "artists" else [query_title]))[:600]
    found = _call_provider(client.search, query, filter=kind, limit=6)
    if not isinstance(found, list):
        raise ProviderUnavailableError("The metadata search changed format")
    candidates = {}
    for raw in found[:6]:
        if not isinstance(raw, dict):
            continue
        raw = dict(raw, thumbnails=_catalogue_safe_thumbnails(raw))
        if kind == "artists":
            name = raw.get("artist") or raw.get("title") or ""
            key = raw.get("browseId")
            if (not isinstance(key, str) or not re.fullmatch(r"UC[A-Za-z0-9_-]{22}", key)
                    or _catalogue_text_key(name) != _catalogue_text_key(row["title"])):
                continue
        else:
            if (_catalogue_text_key(_catalogue_match_title(raw.get("title")), kind == "albums") != _catalogue_text_key(row["title"], kind == "albums")
                    or recording_version(raw) != recording_version(row)):
                continue
            wanted = {_catalogue_text_key(a) for a in row["artists"]}
            raw_credits = raw.get("artists")
            if not isinstance(raw_credits, list) or not all(isinstance(a, dict) for a in raw_credits):
                continue
            credits = {_catalogue_text_key(a) for a in artists(raw)}
            if not wanted or credits != wanted:
                continue
            if kind == "songs":
                # An official video is a different presentation. Only audio ATV
                # entries qualify as a playable Discover song recording.
                if raw.get("videoType") != "MUSIC_VIDEO_TYPE_ATV":
                    continue
                if raw.get("duration_seconds") is None and raw.get("duration"):
                    raw["duration_seconds"] = secs(raw["duration"])
                duration = _discover_number(raw.get("duration_seconds"))
                if row["duration"] and duration and abs(row["duration"] - duration) > 5:
                    continue
                record = song_record(raw)
                if not record:
                    continue
                key = raw["videoId"]
            else:
                key = raw.get("browseId")
                if not source_key(key) or any("podcast" in str(raw.get(f, "")).lower() for f in ("type", "resultType")):
                    continue
        candidates[key] = raw
    if kind == "songs" and len(candidates) > 1 and row["album"]:
        album_key = _catalogue_text_key(_catalogue_match_title(row["album"]), True)
        if album_key:
            album_candidates = {key: raw for key, raw in candidates.items()
                                if isinstance(raw.get("album"), dict)
                                and _catalogue_text_key(_catalogue_match_title(raw["album"].get("name")), True) == album_key}
            if album_candidates:
                candidates = album_candidates
    if len(candidates) != 1:
        return None, "Ambiguous metadata match" if candidates else "No exact metadata match"
    key, raw = next(iter(candidates.items()))
    if kind == "artists":
        detail = _call_provider(client.get_artist, key)
        if not isinstance(detail, dict) or not isinstance(detail.get("name"), str):
            raise ProviderUnavailableError("YouTube Music changed its artist metadata format.")
        if _catalogue_text_key(detail["name"]) != _catalogue_text_key(row["title"]):
            return None, "Artist profile could not be corroborated"
        songs = detail.get("songs")
        songs = songs.get("results", []) if isinstance(songs, dict) else []
        corroborated = any(isinstance(song, dict) and any(isinstance(a, dict) and a.get("id") == key
                          and _catalogue_text_key(a.get("name")) == _catalogue_text_key(row["title"])
                          for a in song.get("artists", [])) for song in songs[:10]) if isinstance(songs, list) else False
        if not corroborated:
            return None, "Artist profile has insufficient recording evidence"
        return {"id": "ytm:artist:" + key, "artistId": "ytm:artist:" + key,
                "cover": cover(raw) or cover(dict(detail, thumbnails=_catalogue_safe_thumbnails(detail))),
                "matchBasis": "unique-name-and-profile-recording"}, ""
    record = song_record(raw) if kind == "songs" else album_card(raw)
    return {"albumId": record["id"], "record": record, "cover": record["cover"]}, ""



_POOL_STOREFRONTS = ("us", "gb", "jp", "kr", "in", "br", "ng", "de", "fr", "mx")
_LABS_ALGORITHM_CACHE = None
_LABS_ALGORITHM_LOCK = threading.Lock()


def _pool_text(value, maximum=600):
    if (type(value) is not str or not value.strip() or len(value) > maximum
            or any(ord(c) < 32 for c in value)):
        raise ProviderUnavailableError("The source returned invalid metadata text.")
    return value.strip()


def _pool_mbid(value):
    if value is None or value == "":
        return ""
    if type(value) is not str or not MBID_RE.fullmatch(value) or uuid.UUID(value).int == 0:
        raise ProviderUnavailableError("The source returned an invalid metadata identifier.")
    return value.lower()


def _pool_http(url, transport=None, *, text=False, timeout=4):
    opener = transport or urllib.request.build_opener(_DiscoverNoRedirect())
    maximum = 256 * 1024 if text else _DISCOVER_RESPONSE_LIMIT
    try:
        request = urllib.request.Request(url, headers={"User-Agent":MB_USER_AGENT,
            "Accept":"text/html" if text else "application/json"})
        with opener.open(request, timeout=timeout) as response:
            status = getattr(response, "status", 200)
            if type(status) is not int or not 200 <= status < 300:
                raise urllib.error.HTTPError(url, status, "Metadata request failed", response.headers, None)
            raw = response.read(maximum + 1)
            if len(raw) > maximum:
                raise ProviderUnavailableError("The metadata response exceeded its limit.")
            return raw.decode("utf-8") if text else json.loads(raw)
    except urllib.error.HTTPError as exc:
        if exc.code in (429, 503):
            raise RateLimitError("The metadata source is limiting requests.") from None
        raise ProviderUnavailableError("The metadata source is unavailable.") from None
    except (OSError, ValueError, UnicodeError) as exc:
        if _is_timeout_exception(exc):
            raise ProviderTimeoutError("The metadata request timed out.") from None
        raise ProviderUnavailableError("The metadata source returned invalid data.") from None


class _LabsAlgorithmForm(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.active = False
        self.closed = False
        self.groups = []
        self.choices = []
        self.selected = []

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == "select":
            self.active = attrs.get("name") == "algorithm" and "disabled" not in attrs
            self.groups = []
        elif self.active and tag == "optgroup":
            self.groups.append("disabled" in attrs)
        elif self.active and tag == "option" and "disabled" not in attrs and not any(self.groups):
            value = attrs.get("value", "")
            if re.fullmatch(r"[A-Za-z0-9_.:-]{1,300}", value) and value not in self.choices:
                self.choices.append(value)
                if "selected" in attrs:
                    self.selected.append(value)

    def handle_endtag(self, tag):
        if tag == "select":
            self.closed |= self.active
            self.active = False
        elif self.active and tag == "optgroup" and self.groups:
            self.groups.pop()


def _labs_algorithm(transport):
    global _LABS_ALGORITHM_CACHE
    with _LABS_ALGORITHM_LOCK:
        cached = _LABS_ALGORITHM_CACHE
        if cached and 0 <= time.monotonic() - cached["at"] < 300:
            return cached["choice"]
        parser = _LabsAlgorithmForm()
        parser.feed(_pool_http("https://labs.api.listenbrainz.org/similar-artists", transport, text=True, timeout=2.5))
        parser.close()
        if not parser.closed or not 1 <= len(parser.choices) <= 64:
            raise ProviderUnavailableError("ListenBrainz did not advertise a usable algorithm.")
        choice = (parser.selected or parser.choices)[0]
        _LABS_ALGORITHM_CACHE = {"choice":choice, "at":time.monotonic()}
        return choice


def _pool_lastfm_card(row, kind):
    if not isinstance(row, dict):
        raise ProviderUnavailableError("Last.fm returned an invalid metadata row.")
    title = _pool_text(row.get("name"))
    mbid = _pool_mbid(row.get("mbid"))
    raw_url = row.get("url")
    if raw_url is not None and type(raw_url) is not str:
        raise ProviderUnavailableError("Last.fm returned an invalid item URL.")
    url = _discover_cover(raw_url, ("last.fm",)) if raw_url else ""
    path = urllib.parse.urlparse(url).path.strip("/").split("/") if url else []
    valid_path = (len(path) == 2 and path[0] == "music" and bool(path[1])) if kind == "artists" else (
        len(path) == 4 and path[0] == "music" and bool(path[1]) and path[2] == "_" and bool(path[3]))
    if raw_url and (not url or not valid_path):
        raise ProviderUnavailableError("Last.fm returned an invalid item URL.")
    key = url or ("mbid:" + mbid if mbid else "")
    if not key:
        raise ProviderUnavailableError("Last.fm returned no source identifier.")
    evidence = {}
    if mbid:
        evidence["artist" if kind == "artists" else "recording"] = mbid
    if kind == "artists":
        credits = [title]
    else:
        artist = row.get("artist")
        if not isinstance(artist, dict):
            raise ProviderUnavailableError("Last.fm returned invalid artist credits.")
        credits = [_pool_text(artist.get("name"))]
        artist_mbid = _pool_mbid(artist.get("mbid"))
        if artist_mbid:
            evidence["artist"] = artist_mbid
    hints = {"mbidEvidence":evidence} if evidence else {}
    if kind == "songs" and _discover_number(row.get("duration")):
        hints["duration"] = _discover_number(row["duration"])
    return _discover_card("lastfm", kind, key, title, credits, _lastfm_image(row), **hints)


def _pool_apple_rows(request, transport, cc, kind):
    data = _pool_http("https://rss.marketingtools.apple.com/api/v2/" + cc + "/music/most-played/50/" + kind + ".json", transport)
    feed = data.get("feed") if isinstance(data, dict) else None
    if not isinstance(feed, dict) or ("country" in feed and feed["country"] != cc):
        raise ProviderUnavailableError("Apple returned an invalid chart feed.")
    rows = _discover_rows(feed.get("results"), 1000)
    items = []
    for rank, row in enumerate(rows, 1):
        key = row.get("id")
        if type(key) is not str or not re.fullmatch(r"[1-9][0-9]{0,19}", key) or row.get("kind") != kind:
            raise ProviderUnavailableError("Apple returned an invalid item identity.")
        artist_id = row.get("artistId")
        if artist_id not in (None, "") and (type(artist_id) is not str or not re.fullmatch(r"[1-9][0-9]{0,19}", artist_id)):
            raise ProviderUnavailableError("Apple returned an invalid artist identifier.")
        date = row.get("releaseDate", "")
        if type(date) is not str or len(date) > 32:
            raise ProviderUnavailableError("Apple returned an invalid release date.")
        item = _discover_card("apple-music", kind, key, _pool_text(row.get("name")),
            [_pool_text(row.get("artistName"))], _discover_cover(row.get("artworkUrl100"), ("mzstatic.com",)), releaseDate=date)
        if artist_id:
            item["sourceIds"]["artistId"] = artist_id
        url = _discover_cover(row.get("url"), ("music.apple.com",))
        if url:
            item["sourceIds"]["url"] = url
        item.update(rank=rank, chartKind=kind, storefront=cc)
        items.append(item)
    return items, {"feed":request["value"], "updated":str(feed.get("updated", ""))[:128]}, True


def _pool_lastfm_rows(request, transport, chart=False):
    if chart:
        leaf = request["value"].split(":")[1]
        kind = "songs" if leaf == "tracks" else "artists"
        method = "chart.getTopTracks" if leaf == "tracks" else "chart.getTopArtists"
        params = {"limit":50, "page":1}
    else:
        leaf, kind, method = "similarartists", "artists", "artist.getSimilar"
        params = {"artist":request["artistName"], "autocorrect":0, "limit":60}
    data, *_ = _discover_http("lastfm", method, params, transport=transport, timeout=4)
    envelope = data.get(leaf) if isinstance(data, dict) else None
    if not isinstance(envelope, dict):
        raise ProviderUnavailableError("Last.fm returned an invalid source list.")
    rows = _discover_rows(envelope.get("track" if kind == "songs" else "artist"), 1000)
    items = []
    for rank, row in enumerate(rows, 1):
        item = _pool_lastfm_card(row, kind)
        item["rank"] = rank
        if chart:
            item["chartKind"] = kind
        else:
            raw_match = row.get("match")
            if type(raw_match) not in (str, float, int):
                raise ProviderUnavailableError("Last.fm returned invalid similarity evidence.")
            try:
                match = float(raw_match)
            except ValueError:
                raise ProviderUnavailableError("Last.fm returned invalid similarity evidence.") from None
            if not math.isfinite(match) or not 0 <= match <= 1:
                raise ProviderUnavailableError("Last.fm returned invalid similarity evidence.")
            item["similarity"] = match
        items.append(item)
    return items, {"method":method, **({"artistName":request["artistName"]} if not chart else {})}, True


def _pool_labs_rows(request, transport):
    global _LABS_ALGORITHM_CACHE
    algorithm = _labs_algorithm(transport)
    url = "https://labs.api.listenbrainz.org/similar-artists/json?" + urllib.parse.urlencode({"artist_mbids":request["value"], "algorithm":algorithm})
    try:
        data = _pool_http(url, transport, timeout=2.5)
    except ProviderError:
        _LABS_ALGORITHM_CACHE = None
        raise
    rows = _discover_rows(data, 1000)
    items, complete = [], True
    for rank, row in enumerate(rows, 1):
        identifier = _pool_mbid(row.get("artist_mbid"))
        reference = _pool_mbid(row.get("reference_mbid"))
        if reference and reference != request["value"].lower():
            raise ProviderUnavailableError("ListenBrainz returned evidence for a different seed.")
        if not identifier or row.get("name") is None:
            complete = False
            continue
        name = _pool_text(row["name"])
        score = row.get("score")
        if score is not None and (type(score) is not int or score < 0):
            raise ProviderUnavailableError("ListenBrainz returned invalid similarity evidence.")
        evidence = {"artist":identifier}
        if reference:
            evidence["referenceArtist"] = reference
        item = _discover_card("listenbrainz", "artists", identifier, name, [name], "", mbidEvidence=evidence)
        item["rank"] = rank
        if score is not None:
            item["similarity"] = score
        items.append(item)
    return items, {"algorithm":algorithm}, complete


def _pool_wire_fits(result):
    return len(json.dumps({"ok":True, "data":result}, ensure_ascii=True).encode("utf-8")) + 1 <= 239 * 1024


def source_pool_metadata(request, transport=None):
    operation, value, source = request.get("operation"), request.get("value"), request.get("source")
    origin = "apple-music" if type(value) is str and value.startswith("apple:") else ("listenbrainz" if source == "listenbrainz" else "lastfm")
    seed = request.get("seed")
    safe_seed = seed if type(seed) is dict and set(seed) == {"identity", "kind", "mbid"} and all(type(v) is str and len(v) < 128 for v in seed.values()) else {}
    result = home_envelope({"seed":safe_seed}, origin)
    result["provenance"]["generator"] = "chart-feed" if operation == "chartCandidates" else "artist-similarity"
    try:
        if len(json.dumps(request, ensure_ascii=True).encode("utf-8")) > 239 * 1024:
            raise InvalidArgumentError("Source-pool request is oversized.")
        allowed = {"operation", "value", "seed", "fetchedAt", "refresh"}
        if operation == "similarArtists":
            allowed.add("source")
            if source == "lastfm":
                allowed.add("artistName")
        if set(request) - allowed or ("refresh" in request and type(request["refresh"]) is not bool):
            raise InvalidArgumentError("Invalid source-pool request options.")
        if "fetchedAt" in request:
            if type(request["fetchedAt"]) is not str or len(request["fetchedAt"]) > 128:
                raise InvalidArgumentError("Invalid source-pool timestamp.")
            result["fetchedAt"] = now_iso(request["fetchedAt"])
        if operation == "chartCandidates":
            if type(seed) is not dict or seed or type(value) is not str:
                raise InvalidArgumentError("Charts require an empty seed and a bound source feed.")
            parts = value.split(":")
            if len(parts) == 3 and parts[0] == "apple" and parts[1] in _POOL_STOREFRONTS and parts[2] in ("albums", "songs"):
                items, provenance, complete = _pool_apple_rows(request, transport, parts[1], parts[2])
            elif value in ("lastfm:tracks", "lastfm:artists"):
                items, provenance, complete = _pool_lastfm_rows(request, transport, True)
            else:
                raise InvalidArgumentError("Unsupported global chart feed.")
            limit = 50
        else:
            if (type(value) is not str or not MBID_RE.fullmatch(value) or uuid.UUID(value).int == 0
                    or type(seed) is not dict or seed != {"identity":"canonical", "kind":"artist", "mbid":value}
                    or source not in ("lastfm", "listenbrainz")):
                raise InvalidArgumentError("Similarity requires an exactly bound canonical artist and source.")
            if source == "lastfm":
                name = request.get("artistName")
                if type(name) is not str or not 1 <= len(name) <= 300 or not name.strip() or any(ord(c) < 32 for c in name):
                    raise InvalidArgumentError("Similarity requires one artist spelling.")
                items, provenance, complete = _pool_lastfm_rows(request, transport)
            else:
                items, provenance, complete = _pool_labs_rows(request, transport)
            limit = 60
        result["provenance"].update(provenance)
        for item in items[:limit]:
            _catalogue_input(item)
            result["items"].append(item)
            if not _pool_wire_fits(result):
                result["items"].pop()
                complete = False
                break
        complete &= len(items) <= limit
        if not result["items"] and items:
            complete = False
        result["complete"] = complete
        result["status"] = ("ready" if result["items"] else "empty") if complete else ("partial" if result["items"] else "unavailable")
        if result["status"] == "unavailable":
            result["error"] = "The source returned no usable metadata evidence."
    except Exception as exc:
        result.update(status="unavailable", complete=False, items=[], error="The optional metadata source is unavailable.")
        result["sourceStatus"] = getattr(exc, "code", "unavailable")
        if isinstance(exc, RateLimitError):
            result["retryAfterSeconds"] = max(1, min(3600, exc.retry_after))
    return result

def handle(request, client=None, mb_client=None, population_client=None, source_pool_transport=None):
    discover_page = (request.get("operation") == "browseCatalogue"
                     and request.get("catalogueSource") in ("lastfm", "jiosaavn"))
    discover_art = request.get("operation") == "resolveCatalogueArt"
    if discover_page or discover_art:
        try:
            return fetch_music_catalogue(request) if discover_page else resolve_catalogue_art(request)
        except ProviderError:
            raise
        except Exception:
            # Keep unexpected transport and parser details out of the error envelope.
            raise ProviderUnavailableError("Music catalogue metadata is unavailable for this request.") from None
    if request.get("operation") == "resolveCatalogueItem":
        return resolve_catalogue_item(request, client)
    if request.get("operation") in {"browseCategories", "browseCatalogue"}:
        _, region, _, _, _ = validate_browse_request(request)
        client = client if client is not None else _ytmusic_client(region)
        return fetch_browse_categories(client) if request["operation"] == "browseCategories" else fetch_browse_catalogue(client, request)
    if request.get("operation") in HOME_OPERATIONS:
        return handle_home(request, client, mb_client, population_client, source_pool_transport)
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
    if operation == "mediaWarmup":
        if request.get("value") != "" or set(request) != {"operation", "value"}:
            raise InvalidArgumentError("Media warmup does not accept content or options")
        # Construction loads local extractor machinery only. No song, network,
        # catalogue or user state is touched, and the response has no resource.
        with _youtube_dl("audio"), _youtube_dl("video"):
            return {"ready": True, "audioReady": True, "videoReady": True}
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
            yt_client = _ytmusic_client(region)
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
        client = _ytmusic_client(region)
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
        overview = request.get("overview", False)
        if not isinstance(overview, bool):
            raise InvalidArgumentError("Invalid artist overview option.")
        return fetch_artist(client, cid, fetched_at=fetched_at, overview=overview)
    if operation in {"artistReleases", "artistVideos"} and "continuation" in request:
        _artist_continuation(request["continuation"], require_params=operation == "artistReleases")
    if operation == "artistReleases":
        cid, kind = _parse_releases_request(request, value)
        return fetch_artist_releases(client, cid, kind, fetched_at=fetched_at,
                                     continuation=request.get("continuation"))
    if operation == "artistVideos":
        cid = request.get("channelId") or value
        return fetch_artist_videos(client, cid, fetched_at=fetched_at,
                                   continuation=request.get("continuation"))
    raise ValueError("Unknown operation")


def _answer(raw):
    try:
        if len(raw) > 1024 * 1024:
            raise InvalidArgumentError("Catalogue request is too large")
        request = json.loads(raw)
        if isinstance(request, dict) and request.get("operation") in ("chartCandidates", "similarArtists") and len(raw) > 239 * 1024:
            raise InvalidArgumentError("Source-pool request is oversized.")
        return {"ok": True, "data": handle(request)}
    except ImportError:
        return {"ok": False, "error": "Vinyl's catalogue helper is not installed.", "errorCode": "missing_dependency"}
    except ProviderError as exc:
        return exc.to_dict()
    except ValueError as exc:
        return {"ok": False, "error": str(exc)}
    except Exception:
        # Upstream errors may contain signed URLs, cookies or verbose internals.
        return {"ok": False, "error": "YouTube is unavailable for this request. Try again.", "errorCode": "unavailable"}


def main():
    raw = sys.stdin.buffer.read(1024 * 1024 + 1)
    sys.stdout.buffer.write(json.dumps(_answer(raw), ensure_ascii=True).encode("utf-8"))


def serve():
    """Bounded newline-delimited requests; a response is never written to disk."""
    global _SERVER_MODE
    _SERVER_MODE = True
    while True:
        raw = sys.stdin.buffer.readline(1024 * 1024 + 3)
        if not raw:
            break
        too_large = len(raw) > 1024 * 1024 + 1 or not raw.endswith(b"\n")
        response = (_answer(b" " * (1024 * 1024 + 1)) if too_large
                    else _answer(raw[:-1]))
        sys.stdout.buffer.write(json.dumps(response, ensure_ascii=True).encode("utf-8") + b"\n")
        sys.stdout.buffer.flush()
        if too_large:
            break


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--serve":
        serve()
    else:
        main()
