"""Mocked contracts for bounded song-seeded Vinyl recommendation sources."""
import importlib.util
import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "vinyl_song_recommendations_provider", ROOT / "scripts" / "vinyl" / "provider.py"
)
provider = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provider)


CHANNEL_ID = "UC" + "A" * 22
OTHER_CHANNEL_ID = "UC" + "B" * 22
SEED = {
    "kind": "artist",
    "provider": "youtube-music",
    "providerId": CHANNEL_ID,
    "id": "ytm:artist:" + CHANNEL_ID,
    "name": "Fixture Artist",
}
VIDEO_ID = "SeedVideo01"
ALBUM_A = "MPRE_album_a"
ALBUM_B = "MPRE_album_b"


def request(seed=SEED, value=VIDEO_ID, recording=None):
    return {
        "operation": "songCandidates",
        "value": value,
        "seed": seed,
        "recording": recording if recording is not None else {
            "kind": "song",
            "provider": "youtube-music",
            "providerId": value,
            "title": "Seed Recording",
            "seedArtistRef": seed,
            "relation": "top-recording",
        },
        "fetchedAt": "2026-10-31T00:00:00+00:00",
    }


def raw_song(video_id, title="Candidate", album_id=ALBUM_A, **extra):
    return {
        "videoId": video_id,
        "title": title,
        "artists": [{"name": "Credited Artist", "id": OTHER_CHANNEL_ID}],
        "album": {"name": "Album title is not identity", "id": album_id},
        "duration_seconds": 181,
        "videoType": "MUSIC_VIDEO_TYPE_ATV",
        **extra,
    }


class FakeCandidateClient:
    def __init__(self, radio=None, related=None, radio_error=None, related_error=None,
                 song_responses=None, song_errors=None):
        self.radio = radio if radio is not None else {"tracks": [], "related": "RELATED_TOKEN"}
        self.related = related if related is not None else []
        self.radio_error = radio_error
        self.related_error = related_error
        self.song_responses = dict(song_responses or {})
        self.song_errors = dict(song_errors or {})
        self.calls = []

    def get_watch_playlist(self, videoId, radio, limit):
        self.calls.append(("get_watch_playlist", videoId, radio, limit))
        if self.radio_error:
            raise self.radio_error
        return self.radio

    def get_song_related(self, browse_id):
        self.calls.append(("get_song_related", browse_id))
        if self.related_error:
            raise self.related_error
        return self.related

    def get_song(self, videoId):
        self.calls.append(("get_song", videoId))
        if videoId in self.song_errors:
            raise self.song_errors[videoId]
        return self.song_responses[videoId]


def song_details(video_id, title, length_seconds, **extra):
    return {
        "playabilityStatus": {"status": "OK"},
        "videoDetails": {
            "videoId": video_id,
            "title": title,
            "lengthSeconds": str(length_seconds),
            **extra,
        },
    }


class SongRecommendationProviderTests(unittest.TestCase):
    def test_radio_and_related_are_bounded_and_preserve_deduplicated_typed_cards(self):
        shared = raw_song("SharedVid01", "Shared song")
        radio_rows = [raw_song("RadioVid001", "Radio song"), shared]
        related_rows = [shared, raw_song("RelatedVid1", "Related song", ALBUM_B)]
        client = FakeCandidateClient(
            radio={"tracks": radio_rows, "related": "RELATED_TOKEN"},
            related=[{"contents": related_rows}],
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["version"], 1)
        self.assertEqual(result["seed"], SEED)
        self.assertEqual(result["fetchedAt"], "2026-10-31T00:00:00+00:00")
        self.assertFalse(result["complete"])
        self.assertEqual(result["status"], "ready")
        self.assertEqual(client.calls, [
            ("get_watch_playlist", VIDEO_ID, True, 25),
            ("get_song_related", "RELATED_TOKEN"),
        ])
        self.assertEqual(len(result["items"]), 3)

        by_video = {item["providerId"]: item for item in result["items"]}
        self.assertEqual(by_video["SharedVid01"]["candidateSources"], ["radio", "related"])
        self.assertEqual(by_video["RadioVid001"]["relation"], "song-radio")
        self.assertEqual(by_video["RelatedVid1"]["relation"], "song-related")
        shared_card = by_video["SharedVid01"]
        self.assertEqual(shared_card["seedArtistRef"], SEED)
        self.assertEqual(shared_card["albumRef"], {
            "kind": "album", "provider": "youtube-music", "providerId": ALBUM_A,
            "id": "ytm:album:" + ALBUM_A,
        })
        self.assertEqual(shared_card["artistRefs"], [{
            "kind": "artist", "provider": "youtube-music", "providerId": OTHER_CHANNEL_ID,
            "id": "ytm:artist:" + OTHER_CHANNEL_ID, "name": "Credited Artist",
        }])
        self.assertEqual(shared_card["seconds"], 181)
        self.assertEqual(shared_card["videoType"], "MUSIC_VIDEO_TYPE_ATV")
        self.assertEqual(shared_card["record"]["tracks"][0]["videoId"], "SharedVid01")
        self.assertEqual(shared_card["record"]["tracks"][0]["seconds"], 181)
        self.assertEqual(result["capabilities"], {"songRadio": True, "songRelated": True})

    def test_top_recording_card_keeps_only_a_real_album_id(self):
        album_recording = provider.home_card("song", raw_song(VIDEO_ID), "top-recording")
        standalone_recording = provider.home_card(
            "song", raw_song(VIDEO_ID, album_id=None), "top-recording"
        )

        self.assertEqual(album_recording["albumRef"]["providerId"], ALBUM_A)
        self.assertNotIn("albumRef", standalone_recording)

    def test_rejects_malformed_request_and_invalid_or_nonmusic_rows(self):
        invalid_requests = [
            request(value="bad"),
            request(seed=dict(SEED, kind="band")),
            request(recording={"kind": "song", "provider": "youtube-music",
                               "providerId": "DifferentId1", "seedArtistRef": SEED}),
            request(recording={"kind": "song", "provider": "youtube-music",
                               "providerId": VIDEO_ID,
                               "seedArtistRef": dict(SEED, providerId=OTHER_CHANNEL_ID)}),
        ]
        for malformed in invalid_requests:
            with self.subTest(malformed=malformed):
                client = FakeCandidateClient()
                result = provider.handle(malformed, client)
                self.assertEqual(result["sourceStatus"], "invalid_argument")
                self.assertEqual(client.calls, [])

        rows = [
            raw_song("short", "Bad ID"),
            raw_song("PodcastVid1", "Podcast", type="Podcast"),
            raw_song("Unavailable", "Unavailable", isAvailable=False),
            raw_song("NonMusicID1", "Other video", type="Video"),
            raw_song("NoCredit001", "No credits", artists=[]),
            raw_song("ZeroTime001", "No duration", duration_seconds=0),
            raw_song("Malformed01", "Bad duration", duration_seconds="not-seconds"),
            raw_song("BadType0001", "Unknown type", videoType="MUSIC_VIDEO_TYPE_EPISODE"),
            raw_song("ValidRow001", "Valid"),
        ]
        client = FakeCandidateClient(
            radio={"tracks": rows, "related": "RELATED_TOKEN"}, related=[]
        )
        result = provider.handle(request(), client)
        self.assertEqual([item["providerId"] for item in result["items"]], ["ValidRow001"])

    def test_normalizes_watch_length_and_related_duration_without_mutating_rows(self):
        radio_row = raw_song("LengthVid01", "Watch length")
        radio_row.pop("duration_seconds")
        radio_row["length"] = "3:07"
        related_row = raw_song("DuratVid001", "Related duration", ALBUM_B)
        related_row.pop("duration_seconds")
        related_row["duration"] = "4:02"
        client = FakeCandidateClient(
            radio={"tracks": [radio_row], "related": "RELATED_TOKEN"},
            related=[{"contents": [related_row]}],
        )

        result = provider.handle(request(), client)

        cards = {card["providerId"]: card for card in result["items"]}
        self.assertEqual(cards["LengthVid01"]["seconds"], 187)
        self.assertEqual(cards["LengthVid01"]["record"]["tracks"][0]["seconds"], 187)
        self.assertEqual(cards["DuratVid001"]["seconds"], 242)
        self.assertEqual(cards["DuratVid001"]["record"]["tracks"][0]["seconds"], 242)
        self.assertNotIn("duration_seconds", radio_row)
        self.assertNotIn("duration_seconds", related_row)

    def test_rejects_explicit_invalid_duration_instead_of_using_an_alternate_field(self):
        rows = [
            raw_song("BadTime001", "Zero seconds", duration_seconds=0, length="3:07"),
            raw_song("NoDuration1", "Malformed length", duration_seconds="bad", length="3:07"),
            raw_song("ZeroLen0001", "Zero duration", duration_seconds=None, duration="0:00"),
        ]
        client = FakeCandidateClient(
            radio={"tracks": rows, "related": "RELATED_TOKEN"}, related=[]
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["items"], [])
        self.assertEqual(result["status"], "empty")
        self.assertFalse(any(call[0] == "get_song" for call in client.calls))

    def test_hydrates_missing_related_duration_from_exact_video_metadata(self):
        row = raw_song("HydrateVid1", "Hydrated Related", ALBUM_B)
        row.pop("duration_seconds")
        client = FakeCandidateClient(
            radio={"tracks": [], "related": "RELATED_TOKEN"},
            related=[{"contents": [row]}],
            song_responses={"HydrateVid1": song_details(
                "HydrateVid1", "Hydrated Related", 235,
                channelId="UC" + "C" * 22, author="Uploader only",
            )},
        )

        result = provider.handle(request(), client)

        self.assertEqual([call[0] for call in client.calls], [
            "get_watch_playlist", "get_song_related", "get_song",
        ])
        self.assertEqual(len(result["items"]), 1)
        card = result["items"][0]
        self.assertEqual(card["providerId"], "HydrateVid1")
        self.assertEqual(card["candidateSources"], ["related"])
        self.assertEqual(card["seconds"], 235)
        self.assertEqual(card["record"]["tracks"][0]["seconds"], 235)
        self.assertEqual(card["artistRefs"][0]["providerId"], OTHER_CHANNEL_ID)
        self.assertEqual(card["albumRef"]["providerId"], ALBUM_B)
        self.assertNotIn("duration_seconds", row)
        self.assertEqual(result["provenance"]["sources"]["related"]["hydration"]["accepted"], 1)

    def test_rejects_hydration_for_a_different_video_id(self):
        row = raw_song("HydrateVid1", "Original title")
        row.pop("duration_seconds")
        client = FakeCandidateClient(
            radio={"tracks": [], "related": "RELATED_TOKEN"},
            related=[{"contents": [row]}],
            song_responses={"HydrateVid1": song_details("OtherVid001", "Original title", 235)},
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["items"], [])
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "video_id_mismatch")

    def test_rejects_hydration_with_a_conflicting_title(self):
        row = raw_song("HydrateVid1", "Original title")
        row.pop("duration_seconds")
        client = FakeCandidateClient(
            radio={"tracks": [], "related": "RELATED_TOKEN"},
            related=[{"contents": [row]}],
            song_responses={"HydrateVid1": song_details("HydrateVid1", "Different recording", 235)},
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["items"], [])
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "title_mismatch")

    def test_hydration_timeout_keeps_radio_candidates_and_reports_partial_related(self):
        row = raw_song("HydrateVid1", "Timed Out Related")
        row.pop("duration_seconds")
        client = FakeCandidateClient(
            radio={"tracks": [raw_song("RadioVid001", "Usable Radio")], "related": "RELATED_TOKEN"},
            related=[{"contents": [row]}],
            song_errors={"HydrateVid1": TimeoutError("request timed out")},
        )

        result = provider.handle(request(), client)

        self.assertEqual([item["providerId"] for item in result["items"]], ["RadioVid001"])
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "timeout")

    def test_exact_radio_overlap_uses_radio_duration_without_hydration(self):
        radio_row = raw_song("OverlapVid1", "Shared metadata")
        related_row = raw_song("OverlapVid1", "Shared metadata")
        related_row.pop("duration_seconds")
        client = FakeCandidateClient(
            radio={"tracks": [radio_row], "related": "RELATED_TOKEN"},
            related=[{"contents": [related_row]}],
        )

        result = provider.handle(request(), client)

        self.assertEqual([call[0] for call in client.calls], ["get_watch_playlist", "get_song_related"])
        self.assertEqual(len(result["items"]), 1)
        self.assertEqual(result["items"][0]["candidateSources"], ["radio", "related"])

    def test_hydration_attempts_at_most_two_distinct_related_videos(self):
        rows = []
        responses = {}
        for video_id, title in (("HydrateVid1", "First"), ("HydrateVid2", "Second"),
                                ("HydrateVid3", "Third")):
            row = raw_song(video_id, title)
            row.pop("duration_seconds")
            rows.append(row)
            responses[video_id] = song_details(video_id, title, 200)
        client = FakeCandidateClient(
            radio={"tracks": [], "related": "RELATED_TOKEN"},
            related=[{"contents": rows}],
            song_responses=responses,
        )

        result = provider.handle(request(), client)

        self.assertEqual([call[0] for call in client.calls].count("get_song"), 2)
        self.assertEqual([item["providerId"] for item in result["items"]], ["HydrateVid1", "HydrateVid2"])
        hydration = result["provenance"]["sources"]["related"]["hydration"]
        self.assertEqual(hydration["attempted"], 2)
        self.assertEqual(hydration["budget"], 2)
        self.assertEqual(hydration["deferred"], 1)
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "hydration_budget")

    def test_normalizes_plural_and_singular_song_artwork_and_skips_malformed_thumbnails(self):
        singular = raw_song("SingPic0001", "Singular artwork")
        singular["thumbnail"] = [{"url": "https://img.example/singular.jpg", "width": 120}]
        plural = raw_song("PlurlPic001", "Plural artwork")
        plural["thumbnails"] = [
            {"url": "https://img.example/plural.jpg", "width": 240},
            {"url": "https://img.example/overflow.jpg", "width": 10**1000},
        ]
        fallback = raw_song("BadArtif001", "Fallback artwork")
        fallback["thumbnails"] = [None, {"url": 42, "width": "broken"}, "invalid"]
        fallback["thumbnail"] = [None, {"url": "https://img.example/fallback.jpg", "width": "broken"}]
        empty = raw_song("EmptyArt001", "No artwork")
        empty["thumbnails"] = [None, {"url": "", "width": None}, "invalid"]
        client = FakeCandidateClient(
            radio={"tracks": [singular, fallback], "related": "RELATED_TOKEN"},
            related=[{"contents": [plural, empty]}],
        )

        result = provider.handle(request(), client)

        cards = {card["providerId"]: card for card in result["items"]}
        self.assertEqual(cards["SingPic0001"]["cover"], "https://img.example/singular.jpg")
        self.assertEqual(cards["SingPic0001"]["record"]["cover"], "https://img.example/singular.jpg")
        self.assertEqual(cards["PlurlPic001"]["cover"], "https://img.example/plural.jpg")
        self.assertEqual(cards["PlurlPic001"]["record"]["cover"], "https://img.example/plural.jpg")
        self.assertEqual(cards["BadArtif001"]["cover"], "https://img.example/fallback.jpg")
        self.assertEqual(cards["EmptyArt001"]["cover"], "")
        self.assertNotIn("thumbnails", singular)

    def test_hydrated_related_song_keeps_its_provider_artwork(self):
        related = raw_song("HydrtArt001", "Hydrated artwork")
        related.pop("duration_seconds")
        related["thumbnail"] = [{"url": "https://img.example/related.jpg", "width": 320}]
        client = FakeCandidateClient(
            radio={"tracks": [], "related": "RELATED_TOKEN"},
            related=[{"contents": [related]}],
            song_responses={"HydrtArt001": song_details("HydrtArt001", "Hydrated artwork", 205)},
        )

        result = provider.handle(request(), client)

        self.assertEqual(client.calls[-1], ("get_song", "HydrtArt001"))
        card = result["items"][0]
        self.assertEqual(card["relation"], "song-related")
        self.assertEqual(card["cover"], "https://img.example/related.jpg")
        self.assertEqual(card["record"]["cover"], "https://img.example/related.jpg")

    def test_related_timeout_keeps_radio_items_and_reports_partial_availability(self):
        client = FakeCandidateClient(
            radio={"tracks": [raw_song("RadioVid001")], "related": "RELATED_TOKEN"},
            related_error=TimeoutError("request timed out"),
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["status"], "partial")
        self.assertEqual([item["providerId"] for item in result["items"]], ["RadioVid001"])
        self.assertEqual(result["capabilities"], {"songRadio": True, "songRelated": False})
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "timeout")

    def test_radio_failure_reports_unavailable_without_attempting_related(self):
        client = FakeCandidateClient(radio_error=TimeoutError("request timed out"))

        result = provider.handle(request(), client)

        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["items"], [])
        self.assertEqual(client.calls, [("get_watch_playlist", VIDEO_ID, True, 25)])
        self.assertEqual(result["provenance"]["sources"]["radio"]["errorCode"], "timeout")
        self.assertEqual(result["capabilities"], {"songRadio": False, "songRelated": False})

    def test_invalid_radio_rows_and_related_timeout_report_both_sources_unavailable(self):
        client = FakeCandidateClient(
            radio={"tracks": "invalid rows", "related": "RELATED_TOKEN"},
            related_error=TimeoutError("request timed out"),
        )

        result = provider.handle(request(), client)

        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["items"], [])
        self.assertEqual(result["provenance"]["sources"]["radio"]["errorCode"], "invalid_response")
        self.assertEqual(result["provenance"]["sources"]["related"]["errorCode"], "timeout")
        self.assertEqual(result["capabilities"], {"songRadio": False, "songRelated": False})

    def test_both_available_empty_sources_are_a_valid_empty_result(self):
        client = FakeCandidateClient(radio={"tracks": [], "related": "RELATED_TOKEN"}, related=[])

        result = provider.handle(request(), client)

        self.assertEqual(result["status"], "empty")
        self.assertEqual(result["items"], [])
        self.assertEqual(result["capabilities"], {"songRadio": True, "songRelated": True})

    def test_rate_limit_is_reported_with_bounded_retry_after(self):
        client = FakeCandidateClient(radio_error=provider.RateLimitError(retry_after=41))

        result = provider.handle(request(), client)

        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["sourceStatus"], "rate_limited")
        self.assertEqual(result["retryAfterSeconds"], 41)
        self.assertEqual(result["provenance"]["sources"]["radio"]["errorCode"], "rate_limited")

    def test_oversized_input_does_not_create_an_oversized_response(self):
        client = FakeCandidateClient()
        oversized = request(seed=dict(SEED, extra="x" * (256 * 1024)))

        result = provider.handle(oversized, client)

        self.assertEqual(result["sourceStatus"], "invalid_argument")
        self.assertLessEqual(
            len(json.dumps({"ok": True, "data": result}, ensure_ascii=True).encode("utf-8")),
            256 * 1024,
        )
        self.assertEqual(client.calls, [])

    def test_each_source_and_combined_response_are_bounded(self):
        radio_rows = [raw_song(f"R{index:010d}", f"Radio {index}") for index in range(25)]
        related_rows = [raw_song(f"D{index:010d}", f"Related {index}", ALBUM_B) for index in range(25)]
        client = FakeCandidateClient(
            radio={"tracks": radio_rows, "related": "RELATED_TOKEN"},
            related=[{"contents": related_rows}],
        )

        result = provider.handle(request(), client)

        self.assertEqual(len(result["items"]), 40)
        self.assertEqual(sum("radio" in item["candidateSources"] for item in result["items"]), 20)
        self.assertEqual(sum("related" in item["candidateSources"] for item in result["items"]), 20)
        self.assertLessEqual(len(json.dumps(result, ensure_ascii=True).encode("utf-8")), 256 * 1024)
        self.assertLessEqual(
            len(json.dumps({"ok": True, "data": result}, ensure_ascii=True).encode("utf-8")),
            256 * 1024,
        )

    def test_large_candidate_records_are_trimmed_to_the_response_byte_limit(self):
        rows = [raw_song(f"L{index:010d}", "x" * 4096) for index in range(25)]
        client = FakeCandidateClient(radio={"tracks": rows, "related": "RELATED_TOKEN"}, related=[])

        result = provider.handle(request(), client)

        self.assertLess(len(result["items"]), 25)
        self.assertLessEqual(len(json.dumps(result, ensure_ascii=True).encode("utf-8")), 256 * 1024)
        self.assertLessEqual(
            len(json.dumps({"ok": True, "data": result}, ensure_ascii=True).encode("utf-8")),
            256 * 1024,
        )


if __name__ == "__main__":
    unittest.main()
