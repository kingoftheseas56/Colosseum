"""Bounded recommendation contracts for the anonymous Vinyl provider."""
import importlib.util
import pathlib
import urllib.error
import urllib.parse
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "vinyl_recommendation_provider", ROOT / "scripts" / "vinyl" / "provider.py"
)
provider = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provider)


CHANNEL_ID = "UC" + "A" * 22
OTHER_CHANNEL_ID = "UC" + "B" * 22
MUSICBRAINZ_ARTIST_ID = "b95ce3ff-3d05-4e87-9e01-c97b66af13d4"
OTHER_MUSICBRAINZ_ARTIST_ID = "11111111-1111-4111-8111-111111111111"
MUSICBRAINZ_RECORDING_ID = "22222222-2222-4222-8222-222222222222"
SEED = {
    "provider": "youtube-music",
    "providerId": CHANNEL_ID,
    "kind": "artist",
    "name": "Fixture",
}


def artist_ref(channel_id=CHANNEL_ID, name="Fixture"):
    return {
        "provider": "youtube-music",
        "providerId": channel_id,
        "kind": "artist",
        "name": name,
    }


def raw_artist(
    channel_id=CHANNEL_ID,
    albums=None,
    singles=None,
    songs=None,
    related=None,
    description="Fixture biography",
):
    return {
        "channelId": channel_id,
        "name": "Fixture",
        "description": description,
        "thumbnails": [],
        "albums": {"results": albums if albums is not None else []},
        "singles": {"results": singles if singles is not None else []},
        "songs": {"results": songs if songs is not None else []},
        "related": {"results": related if related is not None else []},
    }


def raw_release(title, browse_id, channel_id=CHANNEL_ID, year="2024"):
    return {
        "title": title,
        "browseId": browse_id,
        "year": year,
        "artists": [{"name": "Fixture", "id": channel_id}],
    }


def raw_song(video_id, title="Fixture Song", channel_id=CHANNEL_ID, **extra):
    return {
        "videoId": video_id,
        "title": title,
        "artists": [{"name": "Fixture", "id": channel_id}],
        "duration_seconds": 180,
        **extra,
    }


class FakeClient:
    def __init__(self, artist=None, search_rows=None):
        self.artist = artist if artist is not None else raw_artist()
        self.search_rows = list(search_rows or [])
        self.calls = []

    def get_artist(self, channel_id):
        self.calls.append(("get_artist", channel_id))
        return self.artist

    def get_artist_albums(self, *args, **kwargs):
        self.calls.append(("get_artist_albums", args, kwargs))
        raise AssertionError("bounded recommendation work must not enumerate discography")

    def get_playlist(self, *args, **kwargs):
        self.calls.append(("get_playlist", args, kwargs))
        raise AssertionError("bounded recommendation work must not enumerate playlists")

    def search(self, *args, **kwargs):
        self.calls.append(("search", args, kwargs))
        return self.search_rows


class UnavailableMusicBrainz:
    """A no-network double: every attempted MusicBrainz call is unavailable."""

    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        def unavailable(*args, **kwargs):
            self.calls.append((name, args, kwargs))
            raise provider.ProviderUnavailableError("MusicBrainz is offline")

        return unavailable


class FakeMusicBrainz:
    """Offline MusicBrainz search/detail transport that records every URL."""

    def __init__(self, artists, details):
        self.artists = list(artists)
        self.details = dict(details)
        self.calls = []

    def get_json(self, url):
        self.calls.append(url)
        path = urllib.parse.urlparse(url).path
        if path.endswith("/artist/"):
            return {"artists": self.artists}
        artist_id = path.rsplit("/", 1)[-1]
        return self.details[artist_id]


class RateLimitedClient(FakeClient):
    def get_artist(self, channel_id):
        self.calls.append(("get_artist", channel_id))
        raise urllib.error.HTTPError(
            "https://music.youtube.com/artist", 429, "Too Many Requests",
            {"Retry-After": "123"}, None,
        )


class FakePopulationClient:
    def __init__(self, response):
        self.response = response
        self.calls = []

    def get(self, mbid):
        self.calls.append(mbid)
        return self.response


def candidate_request(seed=SEED):
    return {
        "operation": "artistCandidates",
        "value": CHANNEL_ID,
        "seed": seed,
    }


class VinylRecommendationProviderTest(unittest.TestCase):
    def assert_candidate_envelope(self, result, seed=SEED):
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["seed"], seed)
        self.assertIsInstance(result["items"], list)
        self.assertEqual(result["provenance"]["source"], "youtube-music")
        self.assertTrue(result["fetchedAt"])
        self.assertIs(result["complete"], False)
        self.assertIn(result["status"], {"ready", "empty", "unavailable"})

    def test_youtube_only_artist_keeps_own_releases_when_musicbrainz_is_unavailable(self):
        artist = raw_artist(
            albums=[raw_release("Fixture Album", "ALBUM_fixture_01")],
            singles=[raw_release("Fixture EP", "SINGLE_fixture_01")],
            songs=[raw_song("VID00000001")],
            related=[{"browseId": OTHER_CHANNEL_ID, "title": "Related Fixture"}],
        )
        client = FakeClient(artist)
        mb_client = UnavailableMusicBrainz()

        result = provider.handle(candidate_request(), client=client, mb_client=mb_client)

        self.assert_candidate_envelope(result)
        self.assertEqual(result["status"], "ready")
        self.assertEqual([call[0] for call in client.calls], ["get_artist"])
        self.assertEqual(
            {item["relation"] for item in result["items"]},
            {"own-release", "top-recording", "related-artist"},
        )
        self.assertTrue(
            {"Fixture Album", "Fixture EP"}.issubset(
                {item.get("title") for item in result["items"]}
            )
        )

    def test_candidate_limit_deduplication_and_seed_binding(self):
        albums = [
            raw_release(f"Fixture Album {index}", f"ALBUM_fixture_{index:03d}")
            for index in range(65)
        ]
        albums.append(albums[0])
        client = FakeClient(raw_artist(albums=albums))

        result = provider.handle(candidate_request(), client=client, mb_client=UnavailableMusicBrainz())

        self.assert_candidate_envelope(result)
        self.assertLessEqual(len(result["items"]), 60)
        identities = [
            item.get("id")
            or item.get("providerId")
            or item.get("browseId")
            or item.get("videoId")
            for item in result["items"]
        ]
        self.assertTrue(all(identities))
        self.assertEqual(len(identities), len(set(identities)))

        wrong_owner = FakeClient(raw_artist(channel_id=OTHER_CHANNEL_ID, albums=albums[:1]))
        wrong_owner.artist["albums"]["browseId"] = "MPAD" + OTHER_CHANNEL_ID
        rejected = provider.handle(
            candidate_request(), client=wrong_owner, mb_client=UnavailableMusicBrainz()
        )
        self.assert_candidate_envelope(rejected)
        self.assertEqual(rejected["status"], "unavailable")
        self.assertEqual(rejected["items"], [])

    def test_subscription_channel_is_not_the_artist_browse_identity(self):
        raw = raw_artist(channel_id=OTHER_CHANNEL_ID, albums=[raw_release("Album", "MPRE_test")])
        raw["albums"]["browseId"] = "MPAD" + CHANNEL_ID
        result = provider.handle(candidate_request(), client=FakeClient(raw))
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["items"][0]["seedArtistRef"], SEED)
        self.assertEqual(result["seed"]["providerId"], CHANNEL_ID)

    def test_unrecognized_explicit_recording_version_is_not_a_studio_match(self):
        request = {"operation": "recordingMatch", "value": "Fixture Song", "recording": {
            "title": "Fixture Song", "seconds": 180, "artistRefs": [artist_ref()], "version": "radio edit"}}
        result = provider.handle(request, client=FakeClient(search_rows=[raw_song("GOOD0000001")]))
        self.assertEqual(result["status"], "unresolved")

    def test_malformed_and_oversized_inputs_fail_closed(self):
        client = FakeClient()
        bad_seed = dict(SEED, providerId="not-a-channel")
        malformed = provider.handle(
            candidate_request(seed=bad_seed), client=client, mb_client=UnavailableMusicBrainz()
        )
        self.assert_candidate_envelope(malformed, seed=bad_seed)
        self.assertEqual(malformed["status"], "unavailable")
        self.assertEqual(malformed["items"], [])

        missing_id = dict(SEED)
        missing_id.pop("providerId")
        malformed = provider.handle(
            candidate_request(seed=missing_id), client=client, mb_client=UnavailableMusicBrainz()
        )
        self.assert_candidate_envelope(malformed, seed=missing_id)
        self.assertEqual(malformed["status"], "unavailable")
        self.assertEqual(malformed["items"], [])

        oversized = FakeClient(raw_artist(description="x" * (4 * 1024 * 1024 + 1)))
        rejected = provider.handle(
            candidate_request(), client=oversized, mb_client=UnavailableMusicBrainz()
        )
        self.assert_candidate_envelope(rejected)
        self.assertEqual(rejected["status"], "unavailable")
        self.assertEqual(rejected["items"], [])

        too_many_refs = [artist_ref(CHANNEL_ID) for _ in range(65)]
        rejected_match = provider.handle(
            {
                "operation": "recordingMatch",
                "value": "Fixture Song",
                "recording": {
                    "kind": "recording",
                    "title": "Fixture Song",
                    "artistRefs": too_many_refs,
                    "seconds": 180,
                    "version": "studio",
                },
            },
            client=FakeClient(),
        )
        self.assertEqual(rejected_match["status"], "unavailable")

    def test_bounded_identity_does_not_enumerate_discography(self):
        client = FakeClient(raw_artist(albums=[raw_release("Fixture Album", "ALBUM_fixture_01")]))
        mb_client = UnavailableMusicBrainz()

        result = provider.handle(
            {
                "operation": "recommendationIdentity",
                "value": CHANNEL_ID,
                "seed": SEED,
            },
            client=client,
            mb_client=mb_client,
        )

        self.assertEqual([call[0] for call in client.calls], ["get_artist"])
        self.assertNotEqual(result.get("status"), "verified")
        self.assertIn(result.get("status"), {"unavailable", "unresolved"})
        self.assertFalse(result.get("mbid"))
        self.assertFalse(result.get("musicBrainzId"))

    def test_musicbrainz_external_id_relation_verifies_the_bound_artist(self):
        detail = {
            "id": MUSICBRAINZ_ARTIST_ID,
            "name": "Fixture",
            "relations": [{
                "url": {"resource": f"https://www.youtube.com/channel/{CHANNEL_ID}"}
            }],
            "release-groups": [],
        }
        mb_client = FakeMusicBrainz(
            [{"id": MUSICBRAINZ_ARTIST_ID, "name": "Fixture", "score": 100}],
            {MUSICBRAINZ_ARTIST_ID: detail},
        )
        result = provider.handle(
            {"operation": "recommendationIdentity", "value": CHANNEL_ID, "seed": SEED},
            client=FakeClient(raw_artist()),
            mb_client=mb_client,
        )

        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["mapping"]["kind"], "artist")
        self.assertEqual(result["mapping"]["method"], "external-id")
        self.assertEqual(result["mapping"]["providerId"], CHANNEL_ID)
        self.assertEqual(result["mapping"]["mbid"], MUSICBRAINZ_ARTIST_ID)
        self.assertEqual(len(mb_client.calls), 2)
        self.assertTrue(all("/release-group" not in urllib.parse.urlparse(url).path
                            for url in mb_client.calls))

    def test_same_name_musicbrainz_artists_remain_ambiguous_without_channel_relation(self):
        artist_ids = [MUSICBRAINZ_ARTIST_ID, OTHER_MUSICBRAINZ_ARTIST_ID]
        details = {
            artist_id: {
                "id": artist_id,
                "name": "Fixture",
                "relations": [],
                "release-groups": [],
            }
            for artist_id in artist_ids
        }
        mb_client = FakeMusicBrainz(
            [{"id": artist_id, "name": "Fixture", "score": 100} for artist_id in artist_ids],
            details,
        )

        result = provider.handle(
            {"operation": "recommendationIdentity", "value": CHANNEL_ID, "seed": SEED},
            client=FakeClient(raw_artist()),
            mb_client=mb_client,
        )

        self.assertEqual(result["status"], "ambiguous")
        self.assertEqual(result["mappingStatus"], "ambiguous")
        self.assertNotIn("mbid", result)
        self.assertNotIn("mapping", result)
        self.assertEqual(len(mb_client.calls), 3)
        self.assertTrue(all("/release-group" not in urllib.parse.urlparse(url).path
                            for url in mb_client.calls))

    def test_conflicting_channel_mappings_remain_ambiguous(self):
        ids = [MUSICBRAINZ_ARTIST_ID, OTHER_MUSICBRAINZ_ARTIST_ID]
        details = {key: {"id": key, "name": "Fixture", "relations": [{"url": {
            "resource": "https://www.youtube.com/channel/" + CHANNEL_ID}}]} for key in ids}
        result = provider.handle({"operation": "recommendationIdentity", "value": CHANNEL_ID, "seed": SEED},
                                 client=FakeClient(raw_artist()), mb_client=FakeMusicBrainz(
                                     [{"id": key, "name": "Fixture"} for key in ids], details))
        self.assertEqual(result["status"], "ambiguous")
        self.assertNotIn("mapping", result)
        self.assertNotIn("mbid", result)

    def test_unique_musicbrainz_name_without_external_or_release_corroboration_is_unresolved(self):
        mb_client = FakeMusicBrainz(
            [{"id": MUSICBRAINZ_ARTIST_ID, "name": "Fixture", "score": 100}],
            {MUSICBRAINZ_ARTIST_ID: {
                "id": MUSICBRAINZ_ARTIST_ID,
                "name": "Fixture",
                "relations": [],
                "release-groups": [],
            }},
        )

        result = provider.handle(
            {"operation": "recommendationIdentity", "value": CHANNEL_ID, "seed": SEED},
            client=FakeClient(raw_artist()),
            mb_client=mb_client,
        )

        self.assertEqual(result["status"], "unresolved")
        self.assertNotIn("mbid", result)
        self.assertNotIn("mapping", result)

    def test_partial_recording_search_does_not_cache_a_negative_match(self):
        recording = {
            "kind": "recording",
            "title": "Fixture Song",
            "artistRefs": [artist_ref()],
            "seconds": 180,
            "version": "studio",
        }
        client = FakeClient()
        client._send_request = lambda *args, **kwargs: None
        matched_row = raw_song("GOOD0000001", version="studio")
        with mock.patch.object(
            provider,
            "read_search_page",
            side_effect=[
                {"items": [], "continuation": "next-page"},
                {"items": [matched_row], "continuation": ""},
            ],
        ) as read_page:
            partial = provider.handle(
                {"operation": "recordingMatch", "value": "Fixture Song", "recording": recording},
                client=client,
            )
            verified = provider.handle(
                {"operation": "recordingMatch", "value": "Fixture Song", "recording": recording},
                client=client,
            )

        self.assertEqual(partial["status"], "partial")
        self.assertIs(partial["complete"], False)
        self.assertFalse(partial.get("videoId"))
        self.assertEqual(verified["status"], "verified")
        self.assertEqual(verified["videoId"], "GOOD0000001")
        self.assertEqual(read_page.call_count, 2)

    def test_artist_rate_limit_preserves_retry_after(self):
        client = RateLimitedClient()
        result = provider.handle(candidate_request(), client=client)

        self.assertEqual(result["sourceStatus"], "rate_limited")
        self.assertEqual(result["retryAfterSeconds"], 123)
        self.assertEqual([call[0] for call in client.calls], ["get_artist"])

    def test_population_adapter_returns_unavailable_canonical_recording_evidence(self):
        request = {
            "operation": "populationCandidates",
            "value": MUSICBRAINZ_ARTIST_ID,
            "seed": {
                "identity": "canonical",
                "kind": "artist",
                "mbid": MUSICBRAINZ_ARTIST_ID,
            },
        }
        population = FakePopulationClient({
            OTHER_MUSICBRAINZ_ARTIST_ID: [{
                "recording_mbid": MUSICBRAINZ_RECORDING_ID,
                "similar_artist_mbid": OTHER_MUSICBRAINZ_ARTIST_ID,
                "recording_name": "Fixture Population Song",
                "similar_artist_name": "Related Fixture",
            }],
        })

        result = provider.handle(request, population_client=population)

        self.assertEqual(population.calls, [MUSICBRAINZ_ARTIST_ID])
        self.assertEqual(result["status"], "ready")
        self.assertEqual(result["provenance"]["source"], "listenbrainz")
        self.assertEqual(result["items"][0]["ref"], {
            "identity": "canonical", "kind": "recording", "mbid": MUSICBRAINZ_RECORDING_ID,
        })
        self.assertEqual(result["items"][0]["destination"], {})
        self.assertIs(result["items"][0]["available"], False)

        malformed = FakePopulationClient({
            OTHER_MUSICBRAINZ_ARTIST_ID: [{
                "recording_mbid": MUSICBRAINZ_RECORDING_ID,
                "similar_artist_mbid": MUSICBRAINZ_ARTIST_ID,
                "recording_name": "Cross-artist result",
            }],
        })
        rejected = provider.handle(request, population_client=malformed)
        self.assertEqual(rejected["status"], "unavailable")
        self.assertEqual(rejected["items"], [])

    def test_recording_match_rejects_cover_wrong_version_and_ambiguity(self):
        recording = {
            "kind": "recording",
            "title": "Fixture Song",
            "artistRefs": [artist_ref()],
            "seconds": 180,
            "version": "studio",
        }
        rows = [
            raw_song("GOOD0000001", version="studio"),
            raw_song("COVER000001", "Fixture Song (Cover)", version="cover"),
            raw_song("LIVE0000001", "Fixture Song (Live)", version="live"),
            raw_song("REMAS000001", "Fixture Song (Remastered)", version="remaster"),
            raw_song("WRONG000001", channel_id=OTHER_CHANNEL_ID, version="studio"),
            raw_song("SHORT000001", version="studio", duration_seconds=150),
        ]
        client = FakeClient(search_rows=rows)

        result = provider.handle(
            {"operation": "recordingMatch", "value": "Fixture Song", "recording": recording},
            client=client,
        )

        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["videoId"], "GOOD0000001")

        client.search_rows.append(raw_song("GOOD0000002", version="studio"))
        ambiguous = provider.handle(
            {"operation": "recordingMatch", "value": "Fixture Song", "recording": recording},
            client=client,
        )
        self.assertEqual(ambiguous["status"], "ambiguous")
        self.assertFalse(ambiguous.get("videoId"))


if __name__ == "__main__":
    unittest.main()
