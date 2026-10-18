"""Deterministic transport and offline fixture provider. Never used by production or live probes."""
import copy
import importlib.util
import io
import json
import pathlib
import sys
import time
import urllib.error
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES_DIR = HERE / "fixtures"
PROVIDER_PATH = HERE.parents[1] / "scripts/vinyl/provider.py"


def _load_provider_module():
    spec = importlib.util.spec_from_file_location("vinyl_provider", PROVIDER_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_fixtures():
    fixtures = {}
    for slug in ("eminem", "lifafa", "easywanderlings"):
        path = FIXTURES_DIR / f"{slug}.json"
        if path.exists():
            fixtures[slug] = json.loads(path.read_text(encoding="utf-8"))
    return fixtures


class FakeYTMusic:
    """Offline recorded YTMusic double backed by tests/vinyl-live/fixtures/*.json."""

    def __init__(self, location="IN", fixtures=None):
        self.location = location
        self.fixtures = fixtures if fixtures is not None else load_fixtures()
        self.calls = []
        self._by_channel = {}
        self._albums_continuations = {}
        self._playlists = {}
        self._by_mbid = {}
        self._by_name = {}

        for slug, doc in self.fixtures.items():
            artist = doc.get("artist") or {}
            cid = doc.get("channelId") or artist.get("channelId")
            req_cid = doc.get("requestedChannelId")
            for key in (slug, cid, req_cid):
                if key:
                    self._by_channel[key] = doc
            for song in (artist.get("songs") or {}).get("results") or []:
                for a in song.get("artists") or []:
                    if a.get("name") == artist.get("name") and a.get("id"):
                        self._by_channel[a["id"]] = doc

            for kind in ("albums", "singles"):
                sec = artist.get(kind) or {}
                if sec.get("browseId") and sec.get("params"):
                    items = ((doc.get(kind) or {}).get("items")) or []
                    self._albums_continuations[(sec["browseId"], sec["params"])] = items

            songs_sec = artist.get("songs") or {}
            if songs_sec.get("browseId") and doc.get("songsPlaylist"):
                self._playlists[songs_sec["browseId"]] = doc["songsPlaylist"]

            videos_sec = artist.get("videos") or {}
            if videos_sec.get("browseId") and doc.get("videosPlaylist"):
                self._playlists[videos_sec["browseId"]] = doc["videosPlaylist"]

            mb = doc.get("mb") or {}
            if mb.get("mbid"):
                self._by_mbid[mb["mbid"].lower()] = cid
            if artist.get("name"):
                self._by_name[artist["name"].lower()] = cid

    def resolve_channel_id(self, identifier, mbid=None):
        if mbid and str(mbid).lower() in self._by_mbid:
            return self._by_mbid[str(mbid).lower()]
        clean = str(identifier or "").strip()
        if clean.startswith("name:"):
            clean = clean[5:].strip()
        if clean.lower() in self._by_mbid:
            return self._by_mbid[clean.lower()]
        if clean.lower() in self._by_name:
            return self._by_name[clean.lower()]
        if clean in self._by_channel:
            return self._by_channel[clean].get("channelId")
        return None

    def _check_special(self, token):
        val = str(token or "")
        if val in ("timeout", "UCtimeout00000000000000") or "timeout" in val.lower():
            raise TimeoutError("Simulated YouTube Music provider timeout")

    def get_artist(self, channel_id):
        self.calls.append({"method": "get_artist", "channelId": channel_id})
        self._check_special(channel_id)
        doc = self._by_channel.get(channel_id)
        if not doc:
            raise LookupError(f"404 Not Found: unknown artist channel '{channel_id}'")
        return copy.deepcopy(doc["artist"])

    def get_artist_albums(self, browse_id, params, limit=None):
        self.calls.append({
            "method": "get_artist_albums",
            "browseId": browse_id,
            "params": params,
            "limit": limit,
        })
        self._check_special(browse_id)
        items = self._albums_continuations.get((browse_id, params))
        if items is None:
            raise LookupError(f"404 Not Found: unknown continuation ({browse_id}, {params})")
        copied = copy.deepcopy(items)
        return copied if limit is None else copied[: int(limit)]

    def get_playlist(self, playlist_id, limit=25):
        self.calls.append({
            "method": "get_playlist",
            "playlistId": playlist_id,
            "limit": limit,
        })
        self._check_special(playlist_id)
        pl = self._playlists.get(playlist_id)
        if pl is None:
            raise LookupError(f"404 Not Found: unknown playlist '{playlist_id}'")
        return copy.deepcopy(pl)


class FakeMusicBrainzTransport:
    """Offline simulated MusicBrainz web service v2 transport for urllib.request.urlopen."""

    def __init__(self, fixtures=None, fail_503_count=0):
        self.fixtures = fixtures if fixtures is not None else load_fixtures()
        self.fail_503_count = int(fail_503_count)
        self.calls = []
        self.sleeps = []
        self.now = 1000.0
        self._by_mbid = {}
        self._by_name = {}

        for slug, doc in self.fixtures.items():
            mb = doc.get("mb") or {}
            artist_name = (doc.get("artist") or {}).get("name") or slug
            mbid = (mb.get("mbid") or "").lower()
            if mbid:
                self._by_mbid[mbid] = mb
            search_list = mb.get("search") or [
                {"id": mbid, "name": artist_name, "score": 100, "country": None, "disambiguation": None}
            ]
            self._by_name[artist_name.lower()] = search_list
            self._by_name[slug.lower()] = search_list

    def clock(self):
        return self.now

    def sleep(self, seconds):
        delay = float(seconds)
        self.sleeps.append(delay)
        self.now += delay

    def urlopen(self, request, timeout=30):
        url = request.full_url if hasattr(request, "full_url") else str(request)
        ua = request.get_header("User-agent") if hasattr(request, "get_header") else None
        self.calls.append({"url": url, "userAgent": ua, "timeout": timeout, "timestamp": self.now})
        if not ua:
            raise urllib.error.HTTPError(url, 403, "Missing User-Agent header", {}, None)
        if "timeout" in url.lower():
            raise TimeoutError("Simulated MusicBrainz timeout")
        if self.fail_503_count > 0:
            self.fail_503_count -= 1
            raise urllib.error.HTTPError(url, 503, "Service Unavailable", {}, None)

        parsed = urllib.parse.urlparse(url)
        qs = urllib.parse.parse_qs(parsed.query)
        if parsed.path.endswith("/ws/2/artist/"):
            raw_q = (qs.get("query") or [""])[0]
            name = raw_q
            if 'artist:"' in raw_q:
                name = raw_q.split('artist:"', 1)[1].rsplit('"', 1)[0]
            matches = self._by_name.get(name.strip().lower(), [])
            payload = {"artists": copy.deepcopy(matches)}
        elif parsed.path.endswith("/ws/2/release-group"):
            mbid = ((qs.get("artist") or [""])[0]).lower()
            limit = int((qs.get("limit") or ["100"])[0])
            offset = int((qs.get("offset") or ["0"])[0])
            mb = self._by_mbid.get(mbid)
            if not mb:
                raise urllib.error.HTTPError(url, 404, "Not Found", {}, None)
            all_rgs = mb.get("releaseGroups") or []
            page = all_rgs[offset : offset + limit]
            wire_rgs = [
                {
                    "id": g["id"],
                    "title": g["title"],
                    "primary-type": g.get("primary"),
                    "secondary-types": g.get("secondary", []),
                    "first-release-date": g.get("first", ""),
                }
                for g in page
            ]
            payload = {
                "release-groups": wire_rgs,
                "release-group-count": len(all_rgs),
                "release-group-offset": offset,
            }
        else:
            raise urllib.error.HTTPError(url, 404, "Not Found", {}, None)

        raw_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        stream = io.BytesIO(raw_bytes)
        stream.status = 200
        return stream


def make_mb_client(provider_mod=None, fixtures=None, fail_503_count=0):
    mod = provider_mod or _load_provider_module()
    transport = FakeMusicBrainzTransport(fixtures=fixtures, fail_503_count=fail_503_count)
    client = mod.MusicBrainzClient(
        urlopen_fn=transport.urlopen,
        sleep_fn=transport.sleep,
        clock_fn=transport.clock,
    )
    return client, transport


def handle_fake_request(request):
    operation = request.get("operation")
    value = str(request.get("value", "")).strip()
    if value == "slow":
        time.sleep(2)
    if operation == "resolve":
        return {"ok": False, "error": "Fixture unavailable"}
    if operation in ("artist", "artistReleases", "artistVideos", "artistIdentity"):
        mod = _load_provider_module()
        yt = FakeYTMusic()
        mb_client, _ = make_mb_client(provider_mod=mod, fixtures=yt.fixtures)
        try:
            data = mod.handle(request, client=yt, mb_client=mb_client)
            return {"ok": True, "data": data}
        except mod.ProviderError as exc:
            return exc.to_dict()
        except ValueError as exc:
            return {"ok": False, "error": str(exc)}
    return {"ok": True, "data": [{"id": value, "title": value, "browseId": value}]}


def main():
    request = json.load(sys.stdin)
    result = handle_fake_request(request)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
