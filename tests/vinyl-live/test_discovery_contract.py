import importlib.util
import pathlib
import unittest
from unittest.mock import Mock, patch


ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "vinyl_provider_discovery", ROOT / "scripts/vinyl/provider.py"
)
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)

CHANNEL = "UCfM3zsQsOnfWNUppiycmBuw"
PLAYLIST = "PL_music_123"
VIDEO = "abcdefghijk"
FETCHED_AT = "2026-10-21T00:00:00Z"


def artist(name="Native Artist", channel=CHANNEL):
    return {"name": name, "id": channel}


def video(video_id=VIDEO, title="Native Song", **extra):
    item = {
        "videoId": video_id,
        "title": title,
        "artists": [artist()],
        "album": {"name": "Native Album", "id": "MPRE_native"},
        "duration_seconds": 217,
        "videoType": "MUSIC_VIDEO_TYPE_ATV",
        "isAvailable": True,
    }
    item.update(extra)
    return item


class DiscoveryContract(unittest.TestCase):
    def setUp(self):
        self.client = Mock()
        self.client.get_explore.return_value = {
            "new_releases": {"items": []},
            "trending": {"items": []},
        }
        self.client.get_charts.return_value = {"artists": [], "videos": []}
        self.client.get_playlist.return_value = {"title": "Native Playlist", "tracks": []}
        def page_reader(client, key, token):
            raw = client.get_playlist(key, limit=0)
            return {"items": raw.get("tracks"), "meta": raw, "continuation": ""}
        reader_patch = patch.object(provider, "read_playlist_page", side_effect=page_reader)
        reader_patch.start()
        self.addCleanup(reader_patch.stop)

    def assert_discovery_envelope(self, result, key, title, kind):
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["key"], key)
        self.assertEqual(result["title"], title)
        self.assertEqual(result["kind"], kind)
        self.assertEqual(result["source"], "youtube-music")
        self.assertEqual(result["region"], "IN")
        self.assertEqual(result["fetchedAt"], FETCHED_AT)
        self.assertFalse(result["complete"])
        self.assertIsInstance(result["items"], list)
        self.assertIsInstance(result["records"], list)

    def discover(self, key, region="IN"):
        return provider.handle(
            {"operation": "discover", "value": key, "region": region, "fetchedAt": FETCHED_AT},
            client=self.client,
        )

    def test_trending_normalizes_songs_and_source_records(self):
        self.client.get_explore.return_value["trending"] = {
            "items": [video(), video("lmnopqrstuv", "Second Song")]
        }

        result = self.discover("trending")

        self.assert_discovery_envelope(result, "trending", "Trending", "songs")
        self.client.get_explore.assert_called_once_with()
        self.assertEqual([item["title"] for item in result["items"]], ["Native Song", "Second Song"])
        first = result["items"][0]
        recording_id = provider.identity("recording", VIDEO)
        self.assertEqual(first["id"], recording_id)
        self.assertEqual(first["albumId"], result["records"][0]["id"])
        self.assertEqual(first["trackId"], recording_id)
        self.assertEqual(first["trackIndex"], 0)

        record = result["records"][0]
        self.assertEqual(record["id"], provider.identity("release", "track:" + VIDEO))
        self.assertEqual(record["browseId"], "ytm:track:" + VIDEO)
        self.assertEqual(record["id"], first["albumId"])
        self.assertEqual(record["type"], "Song")
        self.assertEqual(record["origin"], "youtube-music")
        self.assertEqual(len(record["tracks"]), 1)
        track = record["tracks"][0]
        self.assertEqual(track["id"], recording_id)
        self.assertEqual(track["videoId"], VIDEO)
        self.assertEqual(track["seconds"], 217)
        self.assertEqual(track["artistIds"], ["ytm:artist:" + CHANNEL])

    def test_trending_skips_bad_ids_missing_titles_unavailable_podcasts_and_unknown_video_types(self):
        no_type = video("56789012345", "No Type Field")
        no_type.pop("videoType")
        self.client.get_explore.return_value["trending"] = {"items": [
            video("bad-id", "Malformed ID"),
            video("mnopqrstuvw", ""),
            video("12345678901", "Unavailable", isAvailable=False, availability="unavailable"),
            video("23456789012", "Podcast", resultType="podcast", type="Podcast", category="podcast"),
            video("34567890123", "Unknown Type", videoType="MUSIC_VIDEO_TYPE_UNSUPPORTED"),
            video("45678901234", "Known Music Video", videoType="MUSIC_VIDEO_TYPE_OMV"),
            no_type,
        ]}

        result = self.discover("trending")

        self.assertEqual([item["title"] for item in result["items"]], ["Known Music Video", "No Type Field"])
        self.assertTrue(all(len(item["trackId"]) == 36 for item in result["items"]))

    def test_discovery_deduplicates_by_source_identity_preserves_order_and_caps_at_40(self):
        items = [video(f"v{index:010d}", f"Song {index}") for index in range(45)]
        items.insert(4, video("v0000000002", "Duplicate source identity"))
        self.client.get_explore.return_value["trending"] = {"items": items}

        result = self.discover("trending")

        self.assertEqual(len(result["items"]), 40)
        self.assertEqual(len(result["records"]), 40)
        self.assertEqual([item["title"] for item in result["items"][:5]],
                         ["Song 0", "Song 1", "Song 2", "Song 3", "Song 4"])
        self.assertEqual(result["items"][2]["id"], provider.identity("recording", "v0000000002"))

    def test_new_releases_are_stable_album_cards(self):
        release = {
            "browseId": "MPRE_new_release",
            "title": "Fresh Release",
            "artists": [artist()],
            "year": 2026,
            "thumbnails": [{"url": "https://img.example/cover.jpg", "width": 500}],
        }
        self.client.get_explore.return_value["new_releases"] = [release, release]

        result = self.discover("new-releases")

        self.assert_discovery_envelope(result, "new-releases", "New releases", "albums")
        self.assertEqual(len(result["items"]), 1)
        item = result["items"][0]
        self.assertEqual(item["id"], provider.identity("release", "MPRE_new_release"))
        self.assertEqual(item["browseId"], "MPRE_new_release")
        self.assertEqual(item["origin"], "youtube-music")
        self.assertEqual(item["title"], "Fresh Release")

    def test_artist_and_chart_discovery_use_valid_source_identities(self):
        self.client.get_charts.return_value = {
            "artists": [
                {"browseId": CHANNEL, "title": "Native Artist", "thumbnails": []},
                {"browseId": "invalid channel", "title": "Bad Artist"},
            ],
            "videos": [
                {"playlistId": PLAYLIST, "title": "Top Songs", "artists": [artist()]},
                {"playlistId": "bad id!", "title": "Bad Playlist"},
            ],
        }

        artists = self.discover("artists")
        self.assert_discovery_envelope(artists, "artists", "Chart artists", "artists")
        self.client.get_charts.assert_called_once_with("IN")
        self.client.get_explore.assert_not_called()
        self.assertEqual(len(artists["items"]), 1)
        self.assertEqual(artists["items"][0]["id"], "ytm:artist:" + CHANNEL)

        self.client.get_charts.reset_mock()
        charts = self.discover("charts")
        self.assert_discovery_envelope(charts, "charts", "Charts", "catalogue-playlists")
        self.client.get_charts.assert_called_once_with("IN")
        self.client.get_explore.assert_not_called()
        self.assertEqual(len(charts["items"]), 1)
        playlist = charts["items"][0]
        self.assertEqual(playlist["id"], provider.identity("playlist", PLAYLIST))
        self.assertEqual(playlist["playlistId"], PLAYLIST)
        self.assertEqual(playlist["browseId"], "ytm:playlist:" + PLAYLIST)

    def test_invalid_discovery_key_and_region_fail_before_client_calls(self):
        for key in ("", "unknown", "trending/other"):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    self.discover(key)
        with self.assertRaises(ValueError):
            self.discover("trending", region="!")
        self.client.get_explore.assert_not_called()
        self.client.get_charts.assert_not_called()

    def test_malformed_discovery_response_is_rejected_without_partial_rows(self):
        self.client.get_explore.return_value = None

        with self.assertRaises(ValueError):
            self.discover("trending")

        self.client.get_explore.reset_mock()
        self.client.get_explore.return_value = {"trending": {"items": "not a list"}}
        with self.assertRaises(ValueError):
            self.discover("trending")

    def test_empty_discovery_shelves_are_valid_incomplete_results(self):
        self.client.get_explore.return_value = {"new_releases": [], "trending": {"items": []}}
        self.client.get_charts.return_value = {"artists": [], "videos": []}

        for key in ("new-releases", "trending", "artists", "charts"):
            with self.subTest(key=key):
                result = self.discover(key)
                self.assertEqual(result["items"], [])
                self.assertEqual(result["records"], [])
                self.assertFalse(result["complete"])

    def test_failed_discovery_can_be_retried_without_stale_partial_results(self):
        self.client.get_explore.side_effect = [
            TimeoutError("temporary provider timeout"),
            {"new_releases": {"items": []}, "trending": {"items": [video()]}},
        ]

        with self.assertRaises(ValueError):
            self.discover("trending")
        result = self.discover("trending")

        self.assertFalse(result["complete"])
        self.assertEqual([item["trackId"] for item in result["items"]],
                         [provider.identity("recording", VIDEO)])
        self.assertEqual(self.client.get_explore.call_count, 2)

    def test_playlist_fetch_is_bounded_and_keeps_stable_source_identity(self):
        self.client.get_playlist.return_value = {
            "title": "Native Playlist",
            "tracks": [video(), video("lmnopqrstuv", "Second Song")],
            "trackCount": 2,
        }

        result = provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)

        self.client.get_playlist.assert_called_once_with(PLAYLIST, limit=0)
        self.assertEqual(result["type"], "Playlist")
        self.assertEqual(result["id"], provider.identity("playlist", PLAYLIST))
        self.assertEqual(result["playlistId"], PLAYLIST)
        self.assertEqual(result["browseId"], "ytm:playlist:" + PLAYLIST)
        self.assertEqual(result["title"], "Native Playlist")
        self.assertTrue(result["complete"])
        self.assertEqual(len(result["tracks"]), 2)
        self.assertEqual(result["tracks"][0]["id"], provider.identity("recording", VIDEO))
        self.assertEqual(result["tracks"][1]["videoId"], "lmnopqrstuv")

    def test_playlist_keeps_missing_tracks_unavailable_and_aligns_artist_ids(self):
        track = video(
            None,
            "Unavailable song",
            artists=[{"name": "Unmapped Artist"}, {"name": "Mapped Artist", "id": CHANNEL}],
        )
        self.client.get_playlist.return_value = {"title": "Partial Source", "tracks": [track], "trackCount": 1}

        result = provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)

        self.assertEqual(len(result["tracks"]), 1)
        normalized = result["tracks"][0]
        self.assertFalse(normalized["available"])
        self.assertEqual(normalized["artists"], ["Unmapped Artist", "Mapped Artist"])
        self.assertEqual(normalized["artistIds"], ["", "ytm:artist:" + CHANNEL])
        self.assertTrue(normalized["id"])

    def test_playlist_reports_omitted_entries_and_rejects_over_limit_results(self):
        self.client.get_playlist.return_value = {
            "title": "Truncated",
            "tracks": [video(), video("lmnopqrstuv", "Second Song")],
            "trackCount": 3,
        }
        result = provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)
        self.assertTrue(result["complete"])
        self.assertEqual(result["omittedTracks"], 1)

        self.client.get_playlist.reset_mock()
        self.client.get_playlist.return_value = {
            "title": "Too Long",
            "tracks": [video(f"v{index:010d}", f"Song {index}") for index in range(10001)],
            "trackCount": 10001,
        }
        with self.assertRaises(ValueError):
            provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)

    def test_playlist_rejects_invalid_keys_and_malformed_responses(self):
        for key in ("", "bad id", "x" * 161):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    provider.handle({"operation": "playlist", "value": key}, client=self.client)
        self.client.get_playlist.assert_not_called()

        self.client.get_playlist.return_value = {"title": "No track list"}
        with self.assertRaises(ValueError):
            provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)
        for count in ("3", -1, True):
            self.client.get_playlist.return_value = {"title": "Unknown listing", "tracks": [video()], "trackCount": count}
            with self.subTest(count=count), self.assertRaises(ValueError):
                provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)


if __name__ == "__main__":
    unittest.main()
