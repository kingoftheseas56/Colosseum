import importlib.util
import pathlib
import unittest
from unittest.mock import Mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("vinyl_provider", ROOT / "scripts/vinyl/provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
CHANNEL = "UCfM3zsQsOnfWNUppiycmBuw"


class CatalogueContract(unittest.TestCase):
    def test_album_artist_credit_ids_keep_positional_alignment(self):
        result = provider.album_card({"browseId": "MPRE_test", "title": "Album", "artists": [
            {"name": "Unmapped"}, {"name": "Mapped", "id": CHANNEL}, {"id": "not-a-named-credit"}]})
        self.assertEqual(result["artists"], ["Unmapped", "Mapped"])
        self.assertEqual(result["artistIds"], ["", "ytm:artist:" + CHANNEL])
    def test_search_keeps_provider_artist_ids_distinct_and_bounded(self):
        client = Mock()
        client.search.side_effect = [[{"browseId": CHANNEL, "artist": "Eminem"}],
                                     [{"browseId": "MPRE_test", "title": "Album"}]]
        result = provider.handle({"operation": "catalogueSearch", "value": "Eminem"}, client=client)
        self.assertEqual(result["artists"][0]["id"], "ytm:artist:" + CHANNEL)
        self.assertFalse(result["complete"])
        self.assertEqual(result["albums"][0]["providerIds"]["youtube-music"], "MPRE_test")

    def test_video_playlist_requests_all_continuations(self):
        client = Mock()
        client.get_artist.return_value = {"name": "Artist", "videos": {"browseId": "VLvideos"}}
        client.get_playlist.return_value = {"tracks": [{"videoId": f"video{i:06}", "title": "Song"} for i in range(60)]}
        result = provider.fetch_artist_videos(client, CHANNEL)
        client.get_playlist.assert_called_once_with("VLvideos", limit=None)
        self.assertTrue(result["complete"])

    def test_one_live_performance_does_not_disappear(self):
        client = Mock()
        client.get_artist.return_value = {"name": "Artist", "videos": {"results": [{"videoId": "abcdefghijk", "title": "Song live"}]}}
        result = provider.fetch_artist_videos(client, CHANNEL)
        self.assertEqual(len(result["livePerformances"]), 1)

    def test_ambiguous_names_do_not_select_first_musicbrainz_id(self):
        client = provider.MusicBrainzClient()
        client.get_json = Mock(return_value={"artists": [
            {"id": "11111111-1111-4111-8111-111111111111", "name": "Artist", "score": "100"},
            {"id": "22222222-2222-4222-8222-222222222222", "name": "Artist", "score": "100"}]})
        with self.assertRaisesRegex(provider.ProviderUnavailableError, "ambiguous"):
            client.fetch_release_groups("Artist")
        self.assertEqual(client.get_json.call_count, 1)

    def test_canonical_artist_lookup_does_not_fold_distinct_diacritics(self):
        client = provider.MusicBrainzClient()
        client.get_json = Mock(return_value={"artists": [
            {"id": "11111111-1111-4111-8111-111111111111", "name": "Rós", "score": "100"}]})
        with self.assertRaisesRegex(provider.ProviderUnavailableError, "ambiguous"):
            client.fetch_release_groups("Ros")
        self.assertEqual(client.get_json.call_count, 1)


if __name__ == "__main__":
    unittest.main()
