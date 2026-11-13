"""Offline coverage for the public Vinyl Discover metadata and tap operations."""
import copy
import importlib.util
import json
import os
import pathlib
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "vinyl-catalogue" / "fixtures"
spec = importlib.util.spec_from_file_location("music_discover_provider", ROOT / "scripts/vinyl/provider.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
VIDEO = "KCMCPsUpD3w"
CHANNEL = "UCbRSywya_rl8YS15Lo9ttsA"


def fixture(name):
    return json.loads((FIXTURES / (name + ".json")).read_text(encoding="utf-8"))


def response(data, cache_control=""):
    return data, cache_control, "0", None, None


def browse(source, kind, key, **options):
    return p.handle(dict(operation="browseCatalogue", catalogueSource=source,
                         value=kind, list=key, **options))


def hit(**overrides):
    return dict({"title": "Song", "artists": [{"name": "Artist, Jr.", "id": CHANNEL}],
                 "videoId": VIDEO, "videoType": "MUSIC_VIDEO_TYPE_ATV",
                 "duration_seconds": 190}, **overrides)


class MusicDiscoverTest(unittest.TestCase):
    def setUp(self):
        self.cache = tempfile.TemporaryDirectory()
        self.addCleanup(self.cache.cleanup)
        env = patch.dict(os.environ, {"COLOSSEUM_VINYL_DISCOVER_CACHE_DIR": self.cache.name})
        env.start()
        self.addCleanup(env.stop)
        youtube = patch.object(p, "_ytmusic_client", side_effect=AssertionError("Unexpected YouTube client"))
        self.youtube = youtube.start()
        self.addCleanup(youtube.stop)
        transport = patch.object(p, "_discover_http")
        self.http = transport.start()
        self.addCleanup(transport.stop)
        self.jio = fixture("jiosaavn-sample")

    def tearDown(self):
        self.youtube.assert_not_called()

    def use_jio(self):
        first = self.jio["charts"][0]["perma_url"].split("/")[-1]
        row = copy.deepcopy(self.jio["playlist"][0])
        row.update(id="otherlang", language="hindi")
        later = copy.deepcopy(self.jio["playlist"][0])
        later["id"] = "editorialSong"
        self.jio["playlist"].append(row)

        def dispatch(source, method, params, language):
            self.assertEqual((source, language), ("jiosaavn", "tamil"))
            if method == "content.getCharts":
                data = self.jio["charts"]
            elif method == "content.getFeaturedPlaylists":
                data = {"data": self.jio["editorial"]}
            elif method == "content.getAlbums":
                data = {"data": self.jio["new"]}
            elif method == "webapi.get":
                data = {"list": self.jio["playlist"] if params["token"] == first
                        else [self.jio["playlist"][0], later]}
            else:
                raise AssertionError(method)
            return response(data, "max-age=0, no-cache, no-store")

        self.http.side_effect = dispatch

    def test_lastfm_catalogue_and_art_are_independent_of_youtube(self):
        image = fixture("lastfm-album")["albums"]["album"][0]["image"]
        mapping = {
            "tag.gettoptracks": fixture("lastfm-track"),
            "chart.gettopartists": fixture("lastfm-artist"),
            "tag.gettopalbums": fixture("lastfm-album"),
            "track.getInfo": {"track": {"album": {"image": image}}},
            "artist.getTopAlbums": {"topalbums": {"album": [{"image": image}]}},
        }
        self.http.side_effect = lambda source, method, params: response(mapping[method])
        song = browse("lastfm", "songs", "pop")["items"][0]
        artist = browse("lastfm", "artists", "chart")["items"][0]
        album = browse("lastfm", "albums", "pop")["items"][0]
        self.assertEqual(song["cover"], "")
        self.assertEqual(artist["cover"], "")
        self.assertTrue(album["cover"].startswith("https://lastfm-img.freetls.fastly.net/"))
        art = p.handle(dict(operation="resolveCatalogueArt", value="lastfm", items=[song, artist, album]))
        self.assertEqual(set(art["covers"]), {song["id"], artist["id"]})
        with self.assertRaises(p.InvalidArgumentError):
            browse("lastfm", "albums", "chart")

    def test_lastfm_fresh_and_original_stale_copy(self):
        feed = fixture("lastfm-track")
        feed["unneeded"] = "upstream-only field"
        self.http.return_value = response(feed)
        first = browse("lastfm", "songs", "pop", pageSize=2)
        self.assertEqual(first["nextCursor"]["page"], 2)
        self.assertEqual(browse("lastfm", "songs", "pop", pageSize=2), first)
        self.http.assert_called_once()
        snapshot, = pathlib.Path(self.cache.name).glob("*.json")
        saved = json.loads(snapshot.read_text())
        self.assertEqual(saved["value"], {"count": 2, "items": first["items"]})
        self.http.side_effect = p.ProviderTimeoutError()
        stale = browse("lastfm", "songs", "pop", pageSize=2, refresh=True)
        self.assertTrue(stale["stale"])
        self.assertEqual(stale["items"], first["items"])
        self.assertEqual(stale["fetchedAt"], first["fetchedAt"])
        with self.assertRaises(p.InvalidArgumentError):
            browse("lastfm", "songs", "rock", pageSize=2, cursor=first["nextCursor"])

    def test_unexpected_list_and_art_errors_name_catalogue_and_hide_details(self):
        operations = [
            ("fetch_music_catalogue", {"operation": "browseCatalogue", "catalogueSource": source})
            for source in ("lastfm", "jiosaavn")
        ]
        operations.append(("resolve_catalogue_art", {"operation": "resolveCatalogueArt"}))
        for function, request in operations:
            with self.subTest(request=request), patch.object(
                p, function, side_effect=ValueError("internal URL and details")
            ):
                result = p._answer(json.dumps(request).encode())
                self.assertEqual(result["errorCode"], "unavailable")
                self.assertIn("Music catalogue metadata", result["error"])
                self.assertNotIn("YouTube", result["error"])
                self.assertNotIn("internal", result["error"])

    def test_lastfm_no_store_and_bad_shape(self):
        self.http.return_value = response(fixture("lastfm-track"), "no-store")
        browse("lastfm", "songs", "pop")
        self.assertEqual(list(pathlib.Path(self.cache.name).iterdir()), [])
        self.http.return_value = response({"tracks": {"track": "changed"}})
        with self.assertRaises(p.ProviderUnavailableError):
            browse("lastfm", "songs", "pop")

    def test_jio_lists_paging_dedupe_and_language(self):
        self.use_jio()
        seen = {}
        cursor = {}
        for _ in range(5):
            page = browse("jiosaavn", "songs", "tamil:default", pageSize=2, cursor=cursor)
            for card in page["items"]:
                key = card["sourceIds"]["id"]
                self.assertNotIn(key, seen)
                seen[key] = card
            cursor = page["nextCursor"]
            if page["complete"]:
                break
        self.assertTrue(page["complete"])
        self.assertEqual(set(seen), {"jlBMldVu", "bWAryKJA", "ofkIhrUs", "editorialSong"})
        first = seen["jlBMldVu"]
        self.assertIn('From "OM Chapter', first["title"])
        self.assertIn("500x500", first["cover"])
        self.assertEqual(first["hints"]["artists"], ["Sai Abhyankkar", "Tippu", "Arun Srinivasan"])
        self.assertIn({"key": "1134651042", "label": "Tamil: India Superhits Top 50"}, page["listOptions"])
        newest = browse("jiosaavn", "albums", "tamil:new")
        self.assertEqual([a["sourceIds"]["id"] for a in newest["items"]], ["81308476"])
        self.assertEqual(newest["listOptions"][0]["key"], "new")
        artists = browse("jiosaavn", "artists", "tamil:1134651042")
        self.assertIn("Sai Abhyankkar", [a["title"] for a in artists["items"]])

    def test_jio_persists_only_projections_and_falls_back(self):
        self.use_jio()
        first = browse("jiosaavn", "songs", "tamil:default")
        self.http.reset_mock()
        self.assertEqual(browse("jiosaavn", "songs", "tamil:default"), first)
        self.http.assert_not_called()
        snapshot = "".join(path.read_text() for path in pathlib.Path(self.cache.name).glob("*.json"))
        self.assertIn("catalogueCard", snapshot)
        self.assertNotIn("perma_url", snapshot)
        self.assertNotIn("more_info", snapshot)
        self.assertNotIn("no-store", snapshot)
        self.http.side_effect = p.ProviderTimeoutError()
        stale = browse("jiosaavn", "songs", "tamil:default", refresh=True)
        self.assertTrue(stale["stale"])
        self.assertEqual(stale["items"], first["items"])
        self.assertEqual(stale["fetchedAt"], first["fetchedAt"])

    def test_jio_cursor_rejects_changed_partial_playlist(self):
        self.use_jio()
        with patch.dict(os.environ, {"COLOSSEUM_VINYL_DISCOVER_CACHE_DIR": ""}):
            first = browse("jiosaavn", "songs", "tamil:default", pageSize=2)
            self.jio["playlist"].reverse()
            with self.assertRaises(p.ProviderUnavailableError):
                browse("jiosaavn", "songs", "tamil:default", pageSize=2, cursor=first["nextCursor"])

    def tap(self, key, client, title='Song (From "Movie") [Tamil]', **hints):
        card = p._discover_card("jiosaavn", "songs", key, title, ["Artist, Jr."], "", **hints)
        return p.handle(dict(operation="resolveCatalogueItem", value=card["id"], item=card), client=client)

    def test_tap_positive_cache_and_preservation_of_full_credit(self):
        client = Mock()
        client.search.return_value = [hit()]
        result = self.tap("song", client, duration=191)
        self.assertTrue(result["matched"])
        self.assertEqual(result["record"]["providerIds"]["youtube-music"], VIDEO)
        self.assertEqual(self.tap("song", client, duration=191), result)
        client.search.assert_called_once_with("Artist, Jr. Song", filter="songs", limit=6)

    def test_tap_rejects_ambiguity_editions_videos_and_changed_credits(self):
        client = Mock()
        client.search.return_value = [hit(), hit(videoId="9cMCPsUpD3w")]
        ambiguous = self.tap("ambiguous", client)
        self.assertFalse(ambiguous["matched"])
        self.assertIn("Ambiguous", ambiguous["reason"])
        self.assertEqual(self.tap("ambiguous", client), ambiguous)
        client.search.assert_called_once()
        client.search.return_value = [
            hit(title="Song (Live)"), hit(title="Song (Remastered)"),
            hit(videoType="MUSIC_VIDEO_TYPE_OMV"),
            hit(artists=[{"name": "Artist"}]), hit(duration_seconds=220)]
        self.assertFalse(self.tap("versions", client, duration=190)["matched"])
        self.assertEqual(p._catalogue_match_title("Song (Live) [Tamil]"), "Song (Live)")

    def test_tap_uses_album_hint_to_disambiguate_and_does_not_cache_transient_error(self):
        client = Mock()
        client.search.side_effect = [
            p.ProviderUnavailableError(),
            [hit(album={"name": "Film"}), hit(videoId="9cMCPsUpD3w", album={"name": "Compilation"})],
        ]
        with self.assertRaises(p.ProviderUnavailableError):
            self.tap("albumhint", client, album="Film")
        self.assertTrue(self.tap("albumhint", client, album="Film")["matched"])
        self.assertEqual(client.search.call_count, 2)


if __name__ == "__main__":
    unittest.main()
