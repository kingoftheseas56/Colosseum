import importlib.util
import pathlib
import unittest
import uuid

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


if __name__ == "__main__":
    unittest.main()
