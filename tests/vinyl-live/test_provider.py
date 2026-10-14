import importlib.util
import io
import json
import pathlib
import sys
import types
import unittest
import uuid
from unittest import mock

spec = importlib.util.spec_from_file_location("provider", pathlib.Path(__file__).parents[2] / "scripts/vinyl/provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)


class ProviderTests(unittest.TestCase):
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


    def invoke(self, raw):
        inbound = types.SimpleNamespace(buffer=io.BytesIO(raw))
        outbound = types.SimpleNamespace(buffer=io.BytesIO())
        with mock.patch.object(provider.sys, "stdin", inbound), mock.patch.object(provider.sys, "stdout", outbound):
            provider.main()
        return json.loads(outbound.buffer.getvalue())

    def test_invalid_and_oversized_stdin_are_bounded(self):
        for raw in (b"{", b"[]", b'{"operation":"nonsense","value":"x"}',
                    b'{"operation":"search","value":null}', b" " * 8193):
            with self.subTest(raw=raw[:30]):
                response = self.invoke(raw)
                self.assertFalse(response["ok"])
                self.assertEqual(response["errorCode"], "invalid_request")

    def test_untrusted_upstream_errors_do_not_leak(self):
        secret = "https://media.example/signed?secret=TOKEN"
        for exc, category in ((ValueError(secret), "provider_unavailable"),
                              (RuntimeError(secret), "provider_unavailable"),
                              (ImportError(secret), "dependency_missing"),
                              (TimeoutError(secret), "timeout")):
            with self.subTest(category=category, error=type(exc).__name__):
                with mock.patch.object(provider, "handle", side_effect=exc):
                    response = self.invoke(b'{"operation":"search","value":"test"}')
                self.assertEqual(response["errorCode"], category)
                self.assertNotIn("TOKEN", json.dumps(response))
                self.assertNotIn("https://", json.dumps(response))

    def test_search_album_success_identity_and_locale(self):
        class Music:
            def __init__(self, **options):
                self.options = options
            def search(self, query, **kwargs):
                self.asserted = (query, kwargs)
                return [{"browseId": "MPREb_album", "title": "Album", "artists": [{"name": "Singer"}]}]
            def get_album(self, key):
                return {"title": "Album", "tracks": [
                    {"title": "Track", "videoId": "InEXiAJCOuA", "duration_seconds": 75}]}
        clients = []
        module = types.ModuleType("ytmusicapi")
        module.YTMusic = lambda **options: clients.append(Music(**options)) or clients[-1]
        with mock.patch.dict(sys.modules, {"ytmusicapi": module}):
            search = self.invoke(b'{"operation":"search","value":"  Album  ","region":"IN"}')
            album = self.invoke(b'{"operation":"album","value":"MPREb_album"}')
        self.assertTrue(search["ok"])
        self.assertEqual(search["data"][0]["browseId"], "MPREb_album")
        self.assertEqual(clients[0].asserted, ("Album", {"filter": "albums", "limit": 24}))
        self.assertEqual(clients[0].options["location"], "IN")
        self.assertTrue(album["ok"])
        self.assertEqual(album["data"]["tracks"][0]["videoId"], "InEXiAJCOuA")
        self.assertEqual(album["data"]["tracks"][0]["id"], provider.identity("recording", "InEXiAJCOuA"))

    def test_resolve_exact_video_and_no_title_replacement(self):
        class Downloader:
            selected = None
            def __init__(self, options):
                self.options = options
            def __enter__(self):
                return self
            def __exit__(self, *_):
                return False
            def extract_info(self, url, download):
                Downloader.selected = url
                return {"id": "InEXiAJCOuA", "url": "https://r.googlevideo.com/audio?signature=secret",
                        "http_headers": {"Referer": "music"}, "acodec": "opus", "abr": 128}
        module = types.ModuleType("yt_dlp")
        module.YoutubeDL = Downloader
        with mock.patch.dict(sys.modules, {"yt_dlp": module}):
            valid = self.invoke(b'{"operation":"resolve","value":"InEXiAJCOuA"}')
            mismatch = self.invoke(b'{"operation":"resolve","value":"AAAAAAAAAAA"}')
        self.assertEqual(Downloader.selected, "https://music.youtube.com/watch?v=AAAAAAAAAAA")
        self.assertTrue(valid["ok"])
        self.assertEqual(valid["data"]["videoId"], "InEXiAJCOuA")
        self.assertIn("signature=secret", valid["data"]["url"])
        self.assertFalse(mismatch["ok"])
        self.assertEqual(mismatch["errorCode"], "identity_mismatch")
        self.assertNotIn("signature=secret", json.dumps(mismatch))


if __name__ == "__main__":
    unittest.main()
