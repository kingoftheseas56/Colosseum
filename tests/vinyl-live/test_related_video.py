"""Official-video identity and version qualification; no provider networking."""
import copy
import importlib.util
import pathlib
import unittest
from unittest.mock import Mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("vinyl_provider", ROOT / "scripts/vinyl/provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
SOURCE, VIDEO = "tqxRidAWER8", "YVkUvmDQ3HY"
ARTIST = "UCedvOgsKFzcK3hA5taf3KoQ"


def song(video, kind="MUSIC_VIDEO_TYPE_ATV", title="Without Me"):
    return {"videoDetails": {"videoId": video, "title": title, "author": "Eminem",
            "channelId": ARTIST, "musicVideoType": kind, "isLiveContent": False},
            "playabilityStatus": {"status": "OK"}}


class RelatedVideoTests(unittest.TestCase):
    def setUp(self):
        self.source = song(SOURCE)
        self.video = song(VIDEO, "MUSIC_VIDEO_TYPE_OMV")
        self.candidate = {"videoId": VIDEO, "title": "Without Me", "videoType": "MUSIC_VIDEO_TYPE_OMV",
                          "artists": [{"id": ARTIST, "name": "Eminem"}]}
        self.client = Mock()
        self.client.get_song.side_effect = lambda video: self.source if video == SOURCE else self.video
        self.client.search.return_value = [self.candidate]
        self.request = {"title": "Without Me", "artistIds": ["ytm:artist:" + ARTIST]}

    def run_match(self):
        return provider.fetch_related_video(self.client, SOURCE, self.request)

    def test_official_same_artist_full_version_matches_without_inventing_offset(self):
        result = self.run_match()
        self.assertEqual((result["status"], result["videoId"]), ("ready", VIDEO))
        self.assertFalse(result["sameMediaClock"])
        self.assertNotIn("recordingTimeOffsetMs", result)
        self.client.search.assert_called_once_with("Eminem Without Me", filter="videos", limit=20)

    def test_same_official_video_has_identical_media_clock(self):
        self.source["videoDetails"]["musicVideoType"] = "MUSIC_VIDEO_TYPE_OMV"
        result = self.run_match()
        self.assertEqual(result["videoId"], SOURCE)
        self.assertEqual(result["mediaTimeOffsetMs"], 0)
        self.assertNotIn("recordingTimeOffsetMs", result)
        self.client.search.assert_not_called()

    def test_rejects_remix_live_instrumental_other_artist_and_user_upload(self):
        changes = [{"title": "Without Me (Remix)"}, {"title": "Without Me (Live)"},
                   {"title": "Without Me (Instrumental)"}, {"videoType": "MUSIC_VIDEO_TYPE_UGC"},
                   {"artists": [{"id": "UC" + "x" * 22, "name": "Eminem"}]},
                   {"artists": [{"id": None, "name": "Eminem"}]}, {"videoId": "../wrong"}]
        for change in changes:
            with self.subTest(change=change):
                self.client.search.return_value = [dict(self.candidate, **change)]
                self.assertEqual(self.run_match()["status"], "unavailable")

    def test_two_distinct_official_matches_remain_ambiguous(self):
        self.client.search.return_value.append(dict(self.candidate, videoId="abcdefghijk"))
        self.assertEqual(self.run_match()["status"], "ambiguous")
        self.assertEqual(self.client.get_song.call_count, 1)

    def test_duplicate_catalogue_rows_do_not_create_false_ambiguity(self):
        self.client.search.return_value.append(copy.deepcopy(self.candidate))
        self.assertEqual(self.run_match()["status"], "ready")

    def test_rechecks_candidate_id_availability_officiality_and_live_flag(self):
        for field, value in [("videoId", "abcdefghijk"), ("musicVideoType", "MUSIC_VIDEO_TYPE_UGC"),
                             ("isLiveContent", True), ("title", "Without Me (Remix)")]:
            with self.subTest(field=field):
                saved = copy.deepcopy(self.video)
                self.video["videoDetails"][field] = value
                self.assertEqual(self.run_match()["status"], "unavailable")
                self.video = saved
        self.video["playabilityStatus"]["status"] = "UNPLAYABLE"
        self.assertEqual(self.run_match()["status"], "unavailable")

    def test_source_identity_and_metadata_must_agree(self):
        for field, value in [("videoId", VIDEO), ("channelId", "UC" + "z" * 22),
                             ("title", "Without Me (Live)"), ("musicVideoType", "MUSIC_VIDEO_TYPE_UGC")]:
            with self.subTest(field=field):
                saved = copy.deepcopy(self.source)
                self.source["videoDetails"][field] = value
                self.assertEqual(self.run_match()["status"], "unavailable")
                self.source = saved

    def test_invalid_id_rejected_before_provider_request(self):
        for value in [None, 12, "", "https://youtube.com/watch?v=" + SOURCE, "--extract-audio"]:
            with self.subTest(value=value):
                with self.assertRaises(provider.InvalidArgumentError):
                    provider.fetch_related_video(self.client, value, self.request)
        self.client.get_song.assert_not_called()

    def test_missing_artist_identity_does_not_guess_from_uploader_name(self):
        for ids in [[], None, ["Eminem"], ["ytm:artist:"]]:
            self.request["artistIds"] = ids
            self.assertEqual(self.run_match()["status"], "unavailable")
        self.client.search.assert_not_called()

    def test_handle_dispatch_and_unicode_title(self):
        title = "మౌనమేలనోయి"
        self.request["title"] = title
        self.source["videoDetails"]["title"] = title
        self.candidate["title"] = title
        self.video["videoDetails"]["title"] = title
        result = provider.handle(dict(self.request, operation="relatedVideo", value=SOURCE), client=self.client)
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["title"], title)
        self.assertNotEqual(provider._video_title_key("శివ"), provider._video_title_key("శవ"))


if __name__ == "__main__":
    unittest.main()
