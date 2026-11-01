import importlib.util
import pathlib
import unittest
from unittest.mock import Mock, patch


ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "vinyl_provider_catalogue_pages", ROOT / "scripts/vinyl/provider.py"
)
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)

CHANNEL = "UCfM3zsQsOnfWNUppiycmBuw"
PLAYLIST = "PL_page_contract_123"
VIDEO_A = "abcdefghijk"
VIDEO_B = "lmnopqrstuv"


def artist(name="Page Artist", channel=CHANNEL):
    return {"name": name, "id": channel}


def song(video_id=VIDEO_A, title="Page Song", **extra):
    row = {
        "videoId": video_id,
        "title": title,
        "artists": [artist()],
        "duration_seconds": 183,
        "videoType": "MUSIC_VIDEO_TYPE_ATV",
        "isAvailable": True,
    }
    row.update(extra)
    return row


def release(browse_id="MPRE_page_album", title="Page Album"):
    return {
        "browseId": browse_id,
        "title": title,
        "artists": [artist()],
        "year": 2026,
        "thumbnails": [{"url": "https://img.example/page-cover.jpg", "width": 500}],
    }


def playlist(playlist_id=PLAYLIST, title="Page Playlist"):
    return {
        "playlistId": playlist_id,
        "title": title,
        "thumbnails": [{"url": "https://img.example/playlist-cover.jpg", "width": 500}],
    }


class CataloguePageContract(unittest.TestCase):
    def setUp(self):
        self.client = Mock()

    def catalogue_page(self, query="needle", kind="albums", region="IN", cursor=None):
        request = {
            "operation": "cataloguePage",
            "value": query,
            "kind": kind,
            "region": region,
        }
        if cursor is not None:
            request["cursor"] = cursor
        return provider.handle(request, client=self.client)

    def playlist_page(self, playlist_id=PLAYLIST, region="IN", cursor=None):
        request = {
            "operation": "playlistPage",
            "value": playlist_id,
            "region": region,
        }
        if cursor is not None:
            request["cursor"] = cursor
        return provider.handle(request, client=self.client)

    def test_search_pages_normalize_all_supported_kinds_with_stable_identities(self):
        pages = {
            "albums": {"items": [release()], "continuation": ""},
            "artists": {"items": [{"browseId": CHANNEL, "title": "Page Artist"}], "continuation": ""},
            "songs": {"items": [song()], "continuation": ""},
            "playlists": {"items": [playlist()], "continuation": ""},
        }
        for kind, raw_page in pages.items():
            with self.subTest(kind=kind):
                with patch.object(provider, "read_search_page", return_value=raw_page) as read_page:
                    result = self.catalogue_page(kind=kind)

                read_page.assert_called_once()
                self.assertEqual(result["version"], 1)
                self.assertEqual(result["query"], "needle")
                self.assertEqual(result["kind"], kind)
                self.assertEqual(result["offset"], 0)
                self.assertTrue(result["complete"])
                self.assertEqual(result["nextCursor"], {})
                self.assertIsInstance(result["items"], list)
                self.assertIsInstance(result["records"], list)
                self.assertEqual(len(result["items"]), 1)
                item = result["items"][0]
                self.assertEqual(item["kind"], "catalogue-playlists" if kind == "playlists" else kind)

                if kind == "albums":
                    identity = provider.identity("release", "MPRE_page_album")
                    self.assertEqual(item["id"], identity)
                    self.assertEqual(result["records"][0]["id"], identity)
                elif kind == "artists":
                    self.assertEqual(item["id"], "ytm:artist:" + CHANNEL)
                    self.assertEqual(result["records"], [])
                elif kind == "songs":
                    recording_id = provider.identity("recording", VIDEO_A)
                    self.assertEqual(item["id"], recording_id)
                    self.assertEqual(item["trackId"], recording_id)
                    self.assertEqual(result["records"][0]["tracks"][0]["videoId"], VIDEO_A)
                else:
                    self.assertEqual(item["id"], provider.identity("playlist", PLAYLIST))
                    self.assertEqual(item["browseId"], "ytm:playlist:" + PLAYLIST)
                    self.assertEqual(result["records"][0]["playlistId"], PLAYLIST)

    def test_search_pagination_forwards_only_the_upstream_continuation(self):
        first = {"items": [release("MPRE_first", "First")], "continuation": "search-token-2"}
        second = {"items": [release("MPRE_second", "Second")], "continuation": ""}
        with patch.object(provider, "read_search_page", side_effect=[first, second]) as read_page:
            page_one = self.catalogue_page()
            page_two = self.catalogue_page(cursor=page_one["nextCursor"])

        self.assertFalse(page_one["complete"])
        self.assertEqual(page_one["offset"], 0)
        self.assertEqual(page_one["nextOffset"], 1)
        self.assertEqual([row["title"] for row in page_one["items"]], ["First"])
        self.assertTrue(page_two["complete"])
        self.assertEqual(page_two["offset"], 1)
        self.assertEqual(page_two["nextOffset"], 2)
        self.assertEqual([row["title"] for row in page_two["items"]], ["Second"])
        self.assertEqual([call.args[3] for call in read_page.call_args_list], ["", "search-token-2"])
        self.assertEqual(read_page.call_count, 2)

    def test_search_offsets_count_raw_provider_rows_before_filtering(self):
        first = {"items": [song("bad-id", "Filtered"), song(VIDEO_A, "Kept")], "continuation": "token-2"}
        second = {"items": [song(VIDEO_B, "Next")], "continuation": ""}
        with patch.object(provider, "read_search_page", side_effect=[first, second]) as read_page:
            page_one = self.catalogue_page(kind="songs")
            page_two = self.catalogue_page(kind="songs", cursor=page_one["nextCursor"])

        self.assertEqual(len(page_one["items"]), 1)
        self.assertEqual(page_one["offset"], 0)
        self.assertEqual(page_one["nextOffset"], 2)
        self.assertEqual(page_two["offset"], 2)
        self.assertEqual(page_two["nextOffset"], 3)
        self.assertEqual([call.args[3] for call in read_page.call_args_list], ["", "token-2"])

    def test_search_deduplicates_page_results_by_source_identity_and_preserves_order(self):
        rows = [release("MPRE_a", "A"), release("MPRE_b", "B"), release("MPRE_a", "Duplicate A")]
        with patch.object(provider, "read_search_page", return_value={"items": rows, "continuation": ""}):
            result = self.catalogue_page(kind="albums")

        self.assertEqual([row["title"] for row in result["items"]], ["A", "B"])
        self.assertEqual([row["id"] for row in result["items"]], [
            provider.identity("release", "MPRE_a"), provider.identity("release", "MPRE_b")
        ])

    def test_cursor_is_bound_to_operation_query_kind_region_and_offset(self):
        with patch.object(provider, "read_search_page", return_value={
            "items": [release()], "continuation": "search-token-2"
        }) as read_page:
            first = self.catalogue_page(query="needle", kind="albums", region="IN")
        cursor = first["nextCursor"]

        invalid_requests = [
            {"operation": "cataloguePage", "value": "different", "kind": "albums", "region": "IN", "cursor": cursor},
            {"operation": "cataloguePage", "value": "needle", "kind": "songs", "region": "IN", "cursor": cursor},
            {"operation": "cataloguePage", "value": "needle", "kind": "albums", "region": "US", "cursor": cursor},
            {"operation": "playlistPage", "value": PLAYLIST, "region": "IN", "cursor": cursor},
        ]
        with patch.object(provider, "read_search_page") as read_search, patch.object(provider, "read_playlist_page") as read_playlist:
            for request in invalid_requests:
                with self.subTest(operation=request["operation"], value=request["value"], kind=request.get("kind"), region=request["region"]):
                    with self.assertRaises(ValueError):
                        provider.handle(request, client=self.client)
                    self.assertEqual(read_search.call_count, 0, request)
                    self.assertEqual(read_playlist.call_count, 0, request)
            read_search.assert_not_called()
            read_playlist.assert_not_called()

        self.assertEqual(read_page.call_count, 1)

    def test_malformed_and_out_of_bounds_cursors_fail_before_upstream_reads(self):
        malformed = [
            "not-a-cursor",
            *[
                {"version": 1, "operation": "cataloguePage", "value": "needle", "kind": "albums",
                 "region": "IN", "token": "next", "offset": offset}
                for offset in (-1, "1", 10000)
            ],
        ]
        with patch.object(provider, "read_search_page") as read_search:
            for cursor in malformed:
                with self.subTest(cursor=cursor):
                    with self.assertRaises(ValueError):
                        self.catalogue_page(cursor=cursor)
            read_search.assert_not_called()

    def test_playlist_page_preserves_duplicate_positions_and_marks_missing_tracks(self):
        raw = {
            "items": [song(VIDEO_A, "First Position"), song(VIDEO_A, "Second Position"), song(None, "Unavailable")],
            "continuation": "",
            "meta": {
                "title": "Native Playlist",
                "thumbnails": [{"url": "https://img.example/playlist.jpg", "width": 700}],
                "trackCount": 3,
            },
        }
        with patch.object(provider, "read_playlist_page", return_value=raw) as read_page:
            result = self.playlist_page()

        read_page.assert_called_once()
        self.assertEqual(result["id"], provider.identity("playlist", PLAYLIST))
        self.assertEqual(result["browseId"], "ytm:playlist:" + PLAYLIST)
        self.assertEqual(result["title"], "Native Playlist")
        self.assertEqual(len(result["tracks"]), 3)
        self.assertEqual([row["title"] for row in result["tracks"]], [
            "First Position", "Second Position", "Unavailable"
        ])
        self.assertEqual([row["videoId"] for row in result["tracks"][:2]], [VIDEO_A, VIDEO_A])
        self.assertFalse(result["tracks"][2]["available"])
        self.assertEqual(result["tracks"][2]["artistIds"], ["ytm:artist:" + CHANNEL])
        self.assertTrue(result["complete"])
        self.assertEqual(result["totalTracks"], 3)

    def test_playlist_page_fetches_one_provider_page_and_never_claims_partial_complete(self):
        first = {
            "items": [song(VIDEO_A, "One")],
            "continuation": "playlist-token-2",
            "meta": {"title": "Growing Playlist", "trackCount": 3},
        }
        second = {
            "items": [song(VIDEO_B, "Two"), song("mnopqrstuvw", "Three")],
            "continuation": "",
            "meta": None,
        }
        with patch.object(provider, "read_playlist_page", side_effect=[first, second]) as read_page:
            page_one = self.playlist_page()
            page_two = self.playlist_page(cursor=page_one["nextCursor"])

        self.assertFalse(page_one["complete"])
        self.assertEqual(page_one["totalTracks"], 3)
        self.assertEqual(page_one["offset"], 0)
        self.assertEqual(page_one["nextOffset"], 1)
        self.assertEqual([row["title"] for row in page_one["tracks"]], ["One"])
        self.assertTrue(page_two["complete"])
        self.assertEqual(page_two["title"], "Growing Playlist")
        self.assertEqual(page_two["offset"], 1)
        self.assertEqual(page_two["nextOffset"], 3)
        self.assertEqual([row["title"] for row in page_two["tracks"]], ["Two", "Three"])
        self.assertEqual([call.args[2] for call in read_page.call_args_list], ["", "playlist-token-2"])
        self.assertEqual(read_page.call_count, 2)

    def test_playlist_cursor_carries_header_and_rejects_repeated_upstream_token(self):
        first = {
            "items": [song(VIDEO_A, "One")],
            "continuation": "repeat-token",
            "meta": {"title": "Header Once", "trackCount": 2},
        }
        repeated = {"items": [song(VIDEO_B, "Two")], "continuation": "repeat-token", "meta": None}
        with patch.object(provider, "read_playlist_page", side_effect=[first, repeated]) as read_page:
            page_one = self.playlist_page()
            self.assertEqual(page_one["nextCursor"]["meta"]["title"], "Header Once")
            with self.assertRaises(ValueError):
                self.playlist_page(cursor=page_one["nextCursor"])
        self.assertEqual(read_page.call_count, 2)

    def test_playlist_cursor_cannot_be_reused_for_another_playlist_or_region(self):
        with patch.object(provider, "read_playlist_page", return_value={
            "items": [song()],
            "continuation": "playlist-token-2",
            "meta": {"title": "Native Playlist", "trackCount": 2},
        }) as read_page:
            first = self.playlist_page(region="IN")
        cursor = first["nextCursor"]

        with patch.object(provider, "read_playlist_page") as read_playlist:
            for request in (
                {"operation": "playlistPage", "value": "PL_other_123", "region": "IN", "cursor": cursor},
                {"operation": "playlistPage", "value": PLAYLIST, "region": "US", "cursor": cursor},
            ):
                with self.subTest(value=request["value"], region=request["region"]):
                    with self.assertRaises(ValueError):
                        provider.handle(request, client=self.client)
            read_playlist.assert_not_called()

        self.assertEqual(read_page.call_count, 1)

    def test_playlist_page_rejects_malformed_and_reports_terminal_count_gap(self):
        for raw in (None, {"items": "wrong", "continuation": "", "meta": {"title": "Wrong items"}}):
            with self.subTest(raw=raw):
                with patch.object(provider, "read_playlist_page", return_value=raw):
                    with self.assertRaises(ValueError):
                        self.playlist_page()

        terminal_gap = {
            "items": [song(VIDEO_A, "Still returned")],
            "continuation": "",
            "meta": {"title": "Header with omitted rows", "trackCount": 2},
        }
        with patch.object(provider, "read_playlist_page", return_value=terminal_gap):
            result = self.playlist_page()

        self.assertTrue(result["complete"])
        self.assertEqual(result["totalTracks"], 2)
        self.assertEqual(len(result["tracks"]), 1)
        self.assertEqual(result["omittedTracks"], 1)

    def test_full_playlist_fetch_follows_pages_preserves_duplicates_and_reports_omissions(self):
        first = {
            "items": [song(VIDEO_A, "First position"), song(VIDEO_A, "Duplicate position")],
            "continuation": "full-playlist-page-2",
            "meta": {"title": "Long Complete Playlist", "trackCount": 5},
        }
        terminal = {
            "items": [song(VIDEO_B, "Other song"), song(VIDEO_A, "Third duplicate")],
            "continuation": "",
            "meta": None,
        }
        with patch.object(provider, "read_playlist_page", side_effect=[first, terminal]) as read_page:
            result = provider.handle({"operation": "playlist", "value": PLAYLIST, "region": "US"}, client=self.client)

        self.assertEqual([call.args[2] for call in read_page.call_args_list], ["", "full-playlist-page-2"])
        self.assertEqual(read_page.call_count, 2)
        self.client.get_playlist.assert_not_called()
        self.assertTrue(result["complete"])
        self.assertEqual(result["region"], "US")
        self.assertEqual(result["title"], "Long Complete Playlist")
        self.assertEqual(result["totalTracks"], 5)
        self.assertEqual(result["omittedTracks"], 1)
        self.assertEqual([row["title"] for row in result["tracks"]], [
            "First position", "Duplicate position", "Other song", "Third duplicate"
        ])
        self.assertEqual([row["videoId"] for row in result["tracks"]], [VIDEO_A, VIDEO_A, VIDEO_B, VIDEO_A])
        self.assertEqual([row["id"] for row in result["tracks"]].count(provider.identity("recording", VIDEO_A)), 3)

    def test_full_playlist_rejects_header_over_10k_before_following_cursor(self):
        first = {
            "items": [song()],
            "continuation": "must-not-follow",
            "meta": {"title": "Too Long", "trackCount": 10001},
        }
        with patch.object(provider, "read_playlist_page", return_value=first) as read_page:
            with self.assertRaises(ValueError):
                provider.handle({"operation": "playlist", "value": PLAYLIST}, client=self.client)

        self.assertEqual(read_page.call_count, 1)
        self.client.get_playlist.assert_not_called()

    def test_playlist_raw_parser_keeps_returned_deleted_positions(self):
        contents = [{"musicResponsiveListItemRenderer": {}},
                    {"musicResponsiveListItemRenderer": {"track": "available"}},
                    {"continuationItemRenderer": {}}]
        with patch("ytmusicapi.parsers.playlists.parse_playlist_items",
                   side_effect=[[], [song(VIDEO_A, "Kept")], []]):
            rows = provider.playlist_page_items(contents)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["title"], "Unavailable track")
        self.assertFalse(rows[0]["isAvailable"])
        self.assertIsNone(rows[0]["videoId"])
        self.assertEqual(rows[1]["videoId"], VIDEO_A)


if __name__ == "__main__":
    unittest.main()
