import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import unittest
import uuid

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location("provider", ROOT / "scripts/vinyl/provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)

fake_spec = importlib.util.spec_from_file_location("fake_provider", HERE / "fake_provider.py")
fake_provider = importlib.util.module_from_spec(fake_spec)
fake_spec.loader.exec_module(fake_provider)


class ProviderTests(unittest.TestCase):
    def setUp(self):
        self.fixtures = fake_provider.load_fixtures()
        self.yt = fake_provider.FakeYTMusic(fixtures=self.fixtures)
        self.mb_client, self.mb_transport = fake_provider.make_mb_client(
            provider_mod=provider, fixtures=self.fixtures
        )

    def test_identity_stable_and_typed(self):
        a = provider.identity("release", "same")
        self.assertEqual(a, provider.identity("release", "same"))
        self.assertEqual(uuid.UUID(a).version, 5)
        self.assertNotEqual(a, provider.identity("recording", "same"))
        self.assertNotEqual(a, provider.identity("release", "other"))

    def test_album_preserves_unavailable_rows_and_exact_ids(self):
        result = provider.normalize_album({"title": "Album", "trackCount": 2, "tracks": [
            {"title": "First", "videoId": "InEXiAJCOuA", "duration_seconds": 262},
            {"title": "Unavailable", "videoId": None}]}, "release")
        self.assertEqual(len(result["tracks"]), 2)
        self.assertEqual(result["tracks"][0]["videoId"], "InEXiAJCOuA")
        self.assertFalse(result["tracks"][1]["available"])

    def test_truncated_album_rejected(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            provider.normalize_album({"trackCount": 2, "tracks": [{"videoId": "InEXiAJCOuA"}]}, "release")

    def test_resolver_rejects_urls_and_arguments_before_network(self):
        for bad in ("https://example.com", "--exec calc", "", "../foo"):
            with self.assertRaises(ValueError): provider.resolve(bad)

    def test_eminem_artist_rich_with_continuations(self):
        res = provider.handle(
            {"operation": "artist", "value": "UCfM3zsQsOnfWNUppiycmBuw"},
            client=self.yt,
        )
        self.assertEqual(res["name"], "Eminem")
        self.assertEqual(res["channelId"], "UCfM3zsQsOnfWNUppiycmBuw")
        self.assertEqual(res["source"], "get_artist")
        self.assertTrue(res["fetchedAt"])
        self.assertTrue(res["complete"])

        # Artist images: URLs and pixel sizes only (up to 2880x1200)
        self.assertEqual(len(res["images"]), 5)
        largest = max(res["images"], key=lambda img: img["width"])
        self.assertEqual((largest["width"], largest["height"]), (2880, 1200))
        self.assertTrue(all(img["url"].startswith("https://") for img in res["images"]))

        # Songs shelf in provider order: first 10 after title dedupe
        self.assertEqual(len(res["songs"]), 10)
        self.assertEqual(res["songs"][0]["videoId"], "9kznlAwE-8o")
        self.assertEqual(res["songs"][0]["title"], "Mockingbird")
        self.assertEqual(res["songs"][0]["album"], "Encore")
        self.assertEqual(res["songs"][0]["albumId"], "MPREb_xFN2XG22AXC")
        self.assertEqual(res["songs"][0]["duration"], "4:11")
        self.assertEqual(res["songs"][0]["seconds"], 251)
        self.assertTrue(res["songs"][0]["explicit"])
        self.assertEqual(res["songs"][9]["videoId"], "CO4kW70AchE")
        self.assertEqual(res["songs"][9]["title"], "Till I Collapse (feat. Nate Dogg)")
        self.assertEqual(res["songs"][9]["album"], "The Eminem Show (Expanded Edition)")

        # Related artists
        self.assertEqual(len(res["related"]), 10)
        self.assertEqual(res["related"][0]["id"], "UCjiGsk3ePl9fajUfgNVFGBA")
        self.assertEqual(res["related"][0]["name"], "NF")
        self.assertTrue(res["related"][0]["thumbnail"].startswith("https://"))

        # Continuation handles for releases and videos
        cont = res["continuations"]
        self.assertEqual(
            cont["albums"],
            {"browseId": "MPADUCedvOgsKFzcK3hA5taf3KoQ", "params": "ggMIegYIARoCAQI%3D"},
        )
        self.assertEqual(
            cont["singles"],
            {"browseId": "MPADUCedvOgsKFzcK3hA5taf3KoQ", "params": "ggMIegYIAhoCAQI%3D"},
        )
        self.assertEqual(
            cont["videos"],
            {"browseId": "VLOLAK5uy_nWHSgzxuQjrOSNsAmCBRTNjdoYoAUd57w", "params": "ggMCCAI%3D"},
        )

    def test_eminem_releases_and_videos_via_continuation(self):
        albums = provider.handle(
            {"operation": "artistReleases", "value": "UCfM3zsQsOnfWNUppiycmBuw", "kind": "albums"},
            client=self.yt,
        )
        self.assertEqual(albums["source"], "get_artist_albums")
        self.assertTrue(albums["complete"])
        self.assertTrue(albums["fetchedAt"])
        self.assertEqual(len(albums["items"]), 21)
        self.assertEqual(albums["items"][0]["browseId"], "MPREb_DmBhgAVua1o")
        self.assertEqual(albums["items"][0]["title"], "STANS (The Official Soundtrack)")
        self.assertEqual(albums["items"][0]["year"], "2025")
        self.assertEqual(albums["items"][0]["type"], "Album")
        self.assertTrue(albums["items"][0]["thumbnail"].startswith("https://"))

        singles = provider.handle(
            {"operation": "artistReleases", "value": "UCfM3zsQsOnfWNUppiycmBuw:singles"},
            client=self.yt,
        )
        self.assertEqual(singles["source"], "get_artist_albums")
        self.assertTrue(singles["complete"])
        self.assertEqual(len(singles["items"]), 26)
        self.assertTrue(all(s["type"] == "Single" for s in singles["items"]))

        # Verify continuation calls passed limit=None
        album_calls = [c for c in self.yt.calls if c["method"] == "get_artist_albums"]
        self.assertEqual(len(album_calls), 2)
        self.assertTrue(all(c["limit"] is None for c in album_calls))

        videos = provider.handle(
            {"operation": "artistVideos", "value": "UCfM3zsQsOnfWNUppiycmBuw"},
            client=self.yt,
        )
        self.assertEqual(videos["source"], "artist videos playlist")
        self.assertFalse(videos["fallback"])
        self.assertTrue(videos["complete"])
        self.assertEqual(len(videos["items"]), 109)
        self.assertEqual(len(videos["videos"]), 105)
        self.assertEqual(len(videos["live"]), 4)
        self.assertTrue(all("live" in v["title"].lower() for v in videos["live"]))
        self.assertTrue(all("live" not in v["title"].lower() for v in videos["videos"]))

    def test_eminem_identity_edition_grouping_and_rate_limits(self):
        identity_res = provider.handle(
            {
                "operation": "artistIdentity",
                "value": "b95ce3ff-3d05-4e87-9e01-c97b66af13d4",
                "channelId": "UCfM3zsQsOnfWNUppiycmBuw",
            },
            client=self.yt,
            mb_client=self.mb_client,
        )
        self.assertEqual(identity_res["mbid"], "b95ce3ff-3d05-4e87-9e01-c97b66af13d4")
        self.assertEqual(identity_res["source"], "musicbrainz")
        self.assertTrue(identity_res["complete"])
        self.assertEqual(identity_res["releaseGroupsBrowsed"], 317)
        self.assertEqual(
            identity_res["afterEditionGrouping"],
            {"albums": 11, "singles": 25, "compilations": 2, "soundtracks": 1, "other": 1},
        )
        self.assertEqual(len(identity_res["unmatched"]), 5)
        unmatched_titles = [u["title"] for u in identity_res["unmatched"]]
        self.assertIn("Eminem Presents The Re-Up", unmatched_titles)
        self.assertIn("Realest", unmatched_titles)

        by_title = {g["title"]: g for g in identity_res["groups"]}
        self.assertEqual(by_title["The Marshall Mathers LP"]["editionCount"], 3)
        self.assertEqual(by_title["The Marshall Mathers LP"]["year"], "2000")
        self.assertEqual(by_title["The Marshall Mathers LP"]["matchedBy"], "key1")
        self.assertEqual(by_title["The Eminem Show"]["year"], "2002")
        self.assertEqual(by_title["Music To Be Murdered By"]["matchedBy"], "key2")
        self.assertEqual(by_title["Music To Be Murdered By"]["editionCount"], 2)
        self.assertEqual(by_title["Eminem Presents The Re-Up"]["matchedBy"], "unmatched")
        self.assertEqual(by_title["Eminem Presents The Re-Up"]["category"], "other")

        # 317 release groups = 4 paginated calls, with >= 1.1s sleep between consecutive calls
        self.assertEqual(len(self.mb_transport.calls), 4)
        self.assertTrue(all(c["userAgent"] == provider.MB_USER_AGENT for c in self.mb_transport.calls))
        self.assertEqual(len(self.mb_transport.sleeps), 3)
        self.assertTrue(all(delay >= 1.1 for delay in self.mb_transport.sleeps))

    def test_lifafa_inline_shelves_bilingual_mb_and_missing_video_durations(self):
        artist = provider.handle(
            {"operation": "artist", "value": "UC3waM1mDEbfrMGxJ0o9Y11Q"},
            client=self.yt,
        )
        self.assertEqual(artist["name"], "Lifafa")
        self.assertEqual(artist["channelId"], "UCYBh5kDH7w7pFeLiLMX6lkA")
        self.assertIsNone(artist["continuations"]["albums"])
        self.assertIsNone(artist["continuations"]["singles"])
        self.assertIsNone(artist["continuations"]["videos"])
        self.assertIsNotNone(artist["continuations"]["songs"])
        self.assertEqual(len(artist["songs"]), 10)

        albums = provider.handle(
            {"operation": "artistReleases", "value": "UCYBh5kDH7w7pFeLiLMX6lkA", "kind": "albums"},
            client=self.yt,
        )
        self.assertEqual(albums["source"], "get_artist inline shelf")
        self.assertTrue(albums["complete"])
        self.assertEqual(len(albums["items"]), 2)
        self.assertEqual([a["title"] for a in albums["items"]], ["SUPERPOWER 2020", "Jaago"])

        singles = provider.handle(
            {"operation": "artistReleases", "value": "UCYBh5kDH7w7pFeLiLMX6lkA", "kind": "singles"},
            client=self.yt,
        )
        self.assertEqual(singles["source"], "get_artist inline shelf")
        self.assertTrue(singles["complete"])
        self.assertEqual(len(singles["items"]), 1)
        self.assertEqual(singles["items"][0]["title"], "Din Raat")

        # Videos fall back to inline shelf and have no durations (never invented)
        videos = provider.handle(
            {"operation": "artistVideos", "value": "UCYBh5kDH7w7pFeLiLMX6lkA"},
            client=self.yt,
        )
        self.assertEqual(videos["source"], "artist page videos shelf (no playlist handle returned)")
        self.assertTrue(videos["fallback"])
        self.assertEqual(videos["fallbackReason"], "no playlist handle returned")
        self.assertFalse(videos["complete"])
        self.assertEqual(len(videos["items"]), 4)
        self.assertTrue(all(v["duration"] is None and v["seconds"] is None for v in videos["items"]))
        self.assertTrue(all(not v["live"] for v in videos["items"]))

        # Bilingual MusicBrainz titles ("Jaago जागो", "SUPERPOWER 2020 सुपरपॉवर २०२०") match via key3
        mb_res = provider.handle(
            {
                "operation": "artistIdentity",
                "value": "name:Lifafa",
                "channelId": "UCYBh5kDH7w7pFeLiLMX6lkA",
            },
            client=self.yt,
            mb_client=self.mb_client,
        )
        self.assertEqual(mb_res["mbid"], "217f190a-a809-42c7-97a4-f51ad7f268a1")
        by_title = {g["title"]: g for g in mb_res["groups"]}
        self.assertEqual(by_title["SUPERPOWER 2020"]["matchedBy"], "key3")
        self.assertEqual(by_title["Jaago"]["matchedBy"], "key3")
        self.assertEqual(by_title["Din Raat"]["matchedBy"], "unmatched")
        self.assertEqual(len(mb_res["unmatched"]), 1)

    def test_easy_wanderlings_small_image_and_eps(self):
        artist = provider.handle(
            {"operation": "artist", "value": "UCLo0ZqEI1xTikp4sU-bAIMA"},
            client=self.yt,
        )
        self.assertEqual(artist["name"], "Easy Wanderlings")
        self.assertEqual(artist["channelId"], "UCPv2LOFD6FTk6bzEK2lHbWQ")
        largest_img = max(artist["images"], key=lambda img: img["width"])
        self.assertEqual((largest_img["width"], largest_img["height"]), (1080, 450))
        self.assertLess(largest_img["width"], 1440)

        singles = provider.handle(
            {"operation": "artistReleases", "value": "UCPv2LOFD6FTk6bzEK2lHbWQ", "kind": "singles"},
            client=self.yt,
        )
        self.assertEqual(singles["source"], "get_artist inline shelf")
        self.assertTrue(singles["complete"])
        self.assertEqual(len(singles["items"]), 5)
        eps = [s for s in singles["items"] if s["type"] == "EP"]
        self.assertEqual(len(eps), 2)
        self.assertEqual(
            [e["title"] for e in eps],
            ["Caught in a Parade", "The Akkare Tapes (Live from Akkarai)"],
        )

        videos = provider.handle(
            {"operation": "artistVideos", "value": "UCPv2LOFD6FTk6bzEK2lHbWQ"},
            client=self.yt,
        )
        self.assertEqual(videos["source"], "artist videos playlist")
        self.assertFalse(videos["fallback"])
        self.assertEqual(len(videos["items"]), 26)
        self.assertEqual(len(videos["videos"]), 23)
        self.assertEqual(len(videos["live"]), 3)

        mb_res = provider.handle(
            {
                "operation": "artistIdentity",
                "value": "db7571d3-fde7-4fb5-b6ea-157411241a24",
                "channelId": "UCPv2LOFD6FTk6bzEK2lHbWQ",
            },
            client=self.yt,
            mb_client=self.mb_client,
        )
        by_title = {g["title"]: g for g in mb_res["groups"]}
        self.assertEqual(by_title["As Written in the Stars"]["matchedBy"], "key1")
        self.assertEqual(by_title["Caught in a Parade"]["matchedBy"], "key1")
        self.assertEqual(by_title["Caught in a Parade"]["kind"], "EP")
        self.assertEqual(len(mb_res["unmatched"]), 4)

    def test_unknown_channel_returns_typed_error(self):
        for op in ("artist", "artistVideos"):
            with self.assertRaises(provider.ArtistNotFoundError) as ctx:
                provider.handle({"operation": op, "value": "UCunknown00000000000000"}, client=self.yt)
            self.assertEqual(ctx.exception.code, "not_found")
            self.assertEqual(ctx.exception.to_dict()["errorCode"], "not_found")

        with self.assertRaises(provider.ArtistNotFoundError) as ctx:
            provider.handle(
                {"operation": "artistReleases", "value": "UCunknown00000000000000", "kind": "albums"},
                client=self.yt,
            )
        self.assertEqual(ctx.exception.code, "not_found")

    def test_provider_timeout_returns_typed_error(self):
        for op in ("artist", "artistVideos"):
            with self.assertRaises(provider.ProviderTimeoutError) as ctx:
                provider.handle({"operation": op, "value": "UCtimeout00000000000000"}, client=self.yt)
            self.assertEqual(ctx.exception.code, "timeout")
            self.assertEqual(ctx.exception.to_dict()["errorCode"], "timeout")

        with self.assertRaises(provider.ProviderTimeoutError) as ctx:
            provider.handle(
                {"operation": "artistReleases", "value": "UCtimeout00000000000000", "kind": "singles"},
                client=self.yt,
            )
        self.assertEqual(ctx.exception.code, "timeout")

    def test_musicbrainz_503_backoff_and_exhaustion(self):
        mb_retry, transport_retry = fake_provider.make_mb_client(
            provider_mod=provider, fixtures=self.fixtures, fail_503_count=2
        )
        res = provider.handle(
            {"operation": "artistIdentity", "value": "217f190a-a809-42c7-97a4-f51ad7f268a1"},
            client=self.yt,
            mb_client=mb_retry,
        )
        self.assertEqual(res["mbid"], "217f190a-a809-42c7-97a4-f51ad7f268a1")
        self.assertEqual(len(transport_retry.calls), 3)
        self.assertGreaterEqual(len(transport_retry.sleeps), 2)

        mb_exhaust, _ = fake_provider.make_mb_client(
            provider_mod=provider, fixtures=self.fixtures, fail_503_count=10
        )
        with self.assertRaises(provider.RateLimitError) as ctx:
            provider.handle(
                {"operation": "artistIdentity", "value": "217f190a-a809-42c7-97a4-f51ad7f268a1"},
                client=self.yt,
                mb_client=mb_exhaust,
            )
        self.assertEqual(ctx.exception.code, "rate_limited")

    def test_fake_provider_subprocess_contract(self):
        proc = subprocess.run(
            [sys.executable, str(HERE / "fake_provider.py")],
            input=json.dumps({"operation": "artist", "value": "UCfM3zsQsOnfWNUppiycmBuw"}).encode("utf-8"),
            capture_output=True,
            check=True,
        )
        payload = json.loads(proc.stdout.decode("utf-8"))
        self.assertTrue(payload["ok"])
        self.assertEqual(payload["data"]["name"], "Eminem")

        err_proc = subprocess.run(
            [sys.executable, str(HERE / "fake_provider.py")],
            input=json.dumps({"operation": "artist", "value": "UCunknown00000000000000"}).encode("utf-8"),
            capture_output=True,
            check=True,
        )
        err_payload = json.loads(err_proc.stdout.decode("utf-8"))
        self.assertFalse(err_payload["ok"])
        self.assertEqual(err_payload["errorCode"], "not_found")

    @unittest.skipUnless(
        os.environ.get("COLOSSEUM_VINYL_LIVE_SMOKE") == "1",
        "Optional live smoke test is skipped by default",
    )
    def test_live_smoke_eminem_artist(self):
        res = provider.handle({"operation": "artist", "value": "UCfM3zsQsOnfWNUppiycmBuw"})
        self.assertEqual(res["name"], "Eminem")
        self.assertTrue(res["songs"])


if __name__ == "__main__":
    unittest.main()
