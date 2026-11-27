"""Offline coverage for the public Vinyl Discover metadata and tap operations."""
import copy
import importlib.util
import json
import os
import pathlib
import tempfile
import threading
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


class WeeklyArtworkTest(unittest.TestCase):
    def setUp(self):
        cache = self.enterContext(tempfile.TemporaryDirectory())
        self.enterContext(patch.dict(os.environ, {"COLOSSEUM_VINYL_DISCOVER_CACHE_DIR": cache}))
        self.key = self.enterContext(patch.object(p, "_lastfm_key", return_value="0" * 32))
        self.http = self.enterContext(patch.object(p, "_discover_http"))
        self.youtube = Mock()
        self.make_youtube = self.enterContext(patch.object(p, "_ytmusic_client", return_value=self.youtube))
        self.enterContext(patch.object(p, "_ENRICHMENT_RATE_LIMITS", {}))
        self.enterContext(patch.object(p, "_GENRE_RETRY_AT", 0))
        self.addCleanup(self.drain)

    def drain(self):
        with p._GENRE_LOCK:
            worker = p._GENRE_WORKER
        if worker is not None:
            worker.join(5)
            self.assertFalse(worker.is_alive())

    def card(self, kind="songs", **hints):
        title = {"songs": "Song", "albums": "Release", "artists": "Artist"}[kind]
        card = p._chart_card("kworb", kind, title, ["Artist"], 1)
        card["hints"].update(hints)
        return card

    def art(self, card):
        request = dict(operation="resolveCatalogueArt", value="kworb", items=[card])
        answer = p._wire_answer(json.dumps(request).encode())
        self.assertTrue(answer["ok"], answer)
        return answer["data"]

    def test_art_poll_starts_worker_and_next_poll_returns_cover(self):
        card = self.card()
        image = "https://is1-ssl.mzstatic.com/song/100x100bb.jpg"
        started = threading.Event()
        release = threading.Event()

        def lookup(*args, **kwargs):
            started.set()
            if not release.wait(5):
                raise p.ProviderTimeoutError()
            return response({"results": [{"artistName": "Artist", "trackName": "Song",
                                          "collectionName": "Release", "artworkUrl100": image}]})

        self.http.side_effect = lookup
        try:
            cold = self.art(card)
            self.assertEqual(cold["covers"], {})
            self.assertEqual(cold["settled"], [])
            self.assertTrue(started.wait(2), "A cold art poll never enqueued work")
            self.assertEqual(self.art(card)["settled"], [])  # In-flight polls must not duplicate work.
        finally:
            release.set()
        self.drain()
        warm = self.art(card)
        self.assertEqual(warm["covers"], {card["id"]: image.replace("100x100bb", "600x600bb")})
        self.assertEqual(warm["albums"], {card["id"]: "Release"})
        self.assertEqual(warm["settled"], [card["id"]])
        self.http.assert_called_once()
        self.make_youtube.assert_not_called()


    def test_youtube_song_and_album_fallback_choose_largest_exact_thumbnail(self):
        best = "https://lh3.googleusercontent.com/large"
        for kind, title, entity in (("songs", "Song", "song"), ("albums", "Release", "album")):
            with self.subTest(kind=kind):
                self.http.reset_mock()
                self.http.side_effect = p.ProviderTimeoutError()
                image_options = [{"url": best, "width": 600},
                                 {"url": "https://lh3.googleusercontent.com/small", "width": 120},
                                 {"url": "https://unrelated.invalid/image", "width": 800}]
                self.youtube.search.return_value = [
                    {"title": title, "artists": [{"name": "Different Artist"}], "thumbnails": image_options[1:]},
                    {"title": title, "artists": [{"name": "Artist"}], "thumbnails": image_options,
                     "album": {"name": "Release"}}]
                card = self.card(kind)
                self.art(card)
                self.drain()
                self.assertEqual(self.art(card)["covers"], {card["id"]: best})
                self.http.assert_called_once_with("itunes", "search", {"term": "Artist " + title,
                                                   "media": "music", "entity": entity, "limit": 5}, timeout=4)
                self.youtube.search.assert_called_with("Artist " + title, filter=kind, limit=6)
                self.make_youtube.assert_called_with("", bounded=True, user_agent=p._CHART_USER_AGENT, reuse=False)

    def test_itunes_chart_hits_match_title_and_artist_and_rewrite_size(self):
        samples = {
            "songs": [("Tate McRae", "Housewife", "housewife"), ("KAROL G", "BbY WOW", "bby-wow"),
                      ("sombr", "back to friends", "back-to-friends"), ("Olivia Rodrigo", "the cure", "the-cure"),
                      ("ADÉLA", "Nicole Kidman", "nicole-kidman")],
            "albums": [("Stray Kids", "SUIATSU", "suiatsu"), ("Duran Duran", "Greatest", "greatest")],
        }
        for kind, cases in samples.items():
            responses = fixture("itunes-" + kind)
            for artist, title, slug in cases:
                with self.subTest(title=title):
                    self.http.reset_mock()
                    self.http.return_value = response(responses[artist + " " + title])
                    params = p._vinyl_genre_lookup(kind, title, [artist])
                    art = p._kworb_art(kind, params, self.make_youtube)
                    self.assertEqual(art["source"], "itunes")
                    self.assertEqual(art["cover"], f"https://is1-ssl.mzstatic.com/{slug}/600x600bb.jpg")
                    self.http.assert_called_once_with("itunes", "search", {"term": artist + " " + title,
                                                      "media": "music", "entity": kind[:-1], "limit": 5}, timeout=4)
        self.make_youtube.assert_not_called()
        self.key.assert_not_called()

    def test_itunes_rejects_carnaval_de_luz_wrong_album(self):
        self.http.return_value = response(fixture("itunes-wrong-album"))
        params = p._vinyl_genre_lookup("albums", "Carnaval De Luz", ["U2"])
        self.assertEqual(p._kworb_itunes_art("albums", params), ("", ""))
        image = "https://lh3.googleusercontent.com/carnaval.jpg"
        self.youtube.search.return_value = [{"title": "Carnaval De Luz", "artists": [{"name": "U2"}],
                                              "thumbnails": [{"url": image, "width": 600}]}]
        art = p._kworb_art("albums", params, self.make_youtube)
        self.assertEqual(art, {"source": "youtube-music", "cover": image, "album": ""})

    def test_deezer_exact_artist_prefers_xl_then_big(self):
        responses = fixture("deezer-artists")
        for name, slug in [("Bad Bunny", "bad-bunny"), ("Olivia Rodrigo", "olivia-rodrigo"), ("Drake", "drake"),
                           ("The Weeknd", "the-weeknd"), ("KAROL G", "karol-g"), ("Taylor Swift", "taylor-swift")]:
            with self.subTest(name=name):
                self.http.reset_mock()
                self.http.return_value = response(responses[name])
                art = p._kworb_art("artists", {"artist": name.upper()}, self.make_youtube)
                size = 500 if name == "Drake" else 1000
                self.assertEqual(art, {"source": "deezer", "album": "",
                                       "cover": f"https://cdn-images.dzcdn.net/{slug}/{size}x{size}.jpg"})
                self.http.assert_called_once_with("deezer", "search/artist", {"q": name.upper(), "limit": 5}, timeout=4)
        self.make_youtube.assert_not_called()
        self.key.assert_not_called()

    def test_deezer_miss_uses_youtube_artist_thumbnail(self):
        calls = Mock()
        calls.attach_mock(self.http, "http")
        calls.attach_mock(self.youtube.search, "search")
        tribute = fixture("deezer-artists")["Bad Bunny"]["data"][0]
        self.http.return_value = response({"data": [tribute]})
        image = "https://lh3.googleusercontent.com/artist.jpg"
        self.youtube.search.return_value = [{"artist": "Bad Bunny", "browseId": CHANNEL,
                                              "thumbnails": [{"url": image, "width": 600}]}]
        art = p._kworb_art("artists", {"artist": "Bad Bunny"}, self.make_youtube)
        self.assertEqual(art, {"source": "youtube-music", "cover": image, "album": ""})
        self.assertEqual([call[0] for call in calls.mock_calls], ["http", "search"])

    def test_timeout_in_genre_does_not_suppress_art_without_a_lastfm_key(self):
        entered, release = threading.Event(), threading.Event()
        image = "https://lh3.googleusercontent.com/new-cover"
        self.youtube.search.return_value = [{"title": "Release", "artists": [{"name": "Artist"}],
                                              "thumbnails": [{"url": image, "width": 600}]}]

        def timeout(*args, **kwargs):
            entered.set()
            release.wait(5)
            raise p.ProviderTimeoutError()

        self.http.side_effect = timeout
        card = self.card("albums")
        params = {"artist": "Artist", "album": "Release", "autocorrect": 1}
        p._vinyl_queue_enrichment({p._vinyl_genre_cache_key("albums", params): ("albums", params, None)})
        try:
            self.assertTrue(entered.wait(2))
            self.key.return_value = ""
            self.assertEqual(self.art(card)["settled"], [])
        finally:
            release.set()
        self.drain()
        self.assertEqual(self.art(card)["covers"], {card["id"]: image})
        self.assertEqual([call.args[:2] for call in self.http.call_args_list],
                         [("lastfm", "album.getTopTags"), ("itunes", "search")])

    def test_miss_is_settled_for_one_day_then_retried(self):
        card = p._chart_card("kworb", "albums", "Live in Mexico City", ["Rammstein"], 1)
        self.http.return_value = response(fixture("itunes-albums")["Rammstein Live in Mexico City"])
        self.youtube.search.return_value = []
        clock = Mock(return_value=2000000000)
        with patch.object(p.time, "time", clock):
            self.art(card)
            self.drain()
            self.assertEqual(self.art(card)["settled"], [card["id"]])
            self.assertEqual(self.art(card)["covers"], {})
            clock.return_value += 86399
            self.assertEqual(self.art(card)["settled"], [card["id"]])
            self.assertEqual(self.http.call_count, 1)
            clock.return_value += 1
            self.assertEqual(self.art(card)["settled"], [])
            self.drain()
            self.assertEqual(self.http.call_count, 2)
        self.assertEqual(self.youtube.search.call_count, 2)


if __name__ == "__main__":
    unittest.main()
