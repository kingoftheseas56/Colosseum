"""Anonymous Vinyl provider. One JSON request on stdin, one JSON response on stdout.

Catalogue identities are durable; resolved URLs and headers are transient. Never
write the resolver response to the library, progress store, logs, or a snapshot.
"""
import datetime
import json
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
    def __init__(self, message="Provider rate limit exceeded"):
        super().__init__(message, code="rate_limited")


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
                    return json.load(resp)
            except urllib.error.HTTPError as exc:
                last_error = exc
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


def handle(request, client=None, mb_client=None):
    operation, value = request["operation"], str(request.get("value", "")).strip()
    if operation == "resolve":
        return resolve(value)
    fetched_at = request.get("fetchedAt")
    if operation == "artistIdentity":
        identifier = request.get("mbid") or request.get("name") or value
        channel_id = request.get("channelId")
        releases = request.get("releases")
        yt_client = client
        if yt_client is None and channel_id and releases is None:
            from ytmusicapi import YTMusic
            yt_client = YTMusic(location=request.get("region", "IN"))
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
        client = YTMusic(location=request.get("region", "IN"))
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
