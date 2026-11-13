"""Bounded recommendation contracts for the anonymous Vinyl provider."""
import importlib.util
import json
import uuid
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


class FakeDiscoveryClient:
    def __init__(self):
        self.calls = []

    def get_explore(self):
        self.calls.append(("get_explore", ()))
        return {"trending": {"items": []}}

    def get_charts(self, region):
        self.calls.append(("get_charts", (region,)))
        return {"artists": [], "videos": []}


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

    def __init__(self, artists, details, pages=None, search_count=None):
        self.artists = list(artists)
        self.details = dict(details)
        self.pages = pages or {0: {"release-groups": [], "release-group-count": 0, "release-group-offset": 0}}
        self.search_count = search_count
        self.calls = []

    def get_json(self, url):
        self.calls.append(url)
        path = urllib.parse.urlparse(url).path
        if path.endswith("/artist/"):
            return {"artists": self.artists, "count": len(self.artists) if self.search_count is None else self.search_count}
        if path.endswith("/release-group"):
            query = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
            if "query" not in query:
                assert query["artist"][0] in self.details
            assert query["limit"] == ["100"]
            page = self.pages[int(query["offset"][0])]
            if isinstance(page, Exception):
                raise page
            return page
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

    def test_regionless_requests_do_not_supply_a_default_music_market(self):
        self.assertEqual(provider.requested_region({}), "")
        self.assertEqual(provider.requested_region({"region": None}), "")
        self.assertEqual(provider.ytmusic_location_options(""), {})
        self.assertEqual(provider.requested_region({"region": "GB"}), "GB")
        self.assertEqual(provider.ytmusic_location_options("GB"), {"location": "GB"})

    def test_market_discovery_requires_and_preserves_an_explicit_market(self):
        client = FakeDiscoveryClient()
        with self.assertRaises(provider.InvalidArgumentError):
            provider.handle({"operation": "discover", "value": "artists"}, client=client)
        self.assertEqual(client.calls, [])

        result = provider.handle(
            {"operation": "discover", "value": "artists", "region": "GB"},
            client=client,
        )
        self.assertEqual(result["region"], "GB")
        self.assertEqual(client.calls, [("get_charts", ("GB",))])

    def test_global_discovery_keeps_regionless_request_regionless(self):
        client = FakeDiscoveryClient()
        result = provider.handle({"operation": "discover", "value": "trending"}, client=client)

        self.assertEqual(result["region"], "")
        self.assertEqual(client.calls, [("get_explore", ())])

    def test_artist_search_pages_need_no_market(self):
        client = FakeClient()
        with mock.patch.object(provider, "read_search_page", return_value={
            "items": [{"browseId": CHANNEL_ID, "title": "Fixture", "artist": "Fixture"}],
            "continuation": "next-page",
        }):
            result = provider.handle({
                "operation": "cataloguePage", "value": "Fixture", "kind": "artists",
                "cursor": {},
            }, client=client)

        self.assertEqual(result["region"], "")
        self.assertEqual(result["items"][0]["artistId"], "ytm:artist:" + CHANNEL_ID)
        self.assertEqual(result["nextCursor"]["region"], "")

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

    def test_null_or_missing_release_shelf_owner_does_not_block_artist_candidates(self):
        artist = raw_artist(
            albums=[raw_release("Fixture Album", "ALBUM_fixture_null")],
            singles=[raw_release("Fixture Single", "SINGLE_fixture_missing")],
        )
        artist["albums"]["browseId"] = None

        result = provider.handle(
            candidate_request(),
            client=FakeClient(artist),
            mb_client=UnavailableMusicBrainz(),
        )

        self.assert_candidate_envelope(result)
        self.assertEqual(result["status"], "ready")
        self.assertEqual(
            {item["title"] for item in result["items"] if item["relation"] == "own-release"},
            {"Fixture Album", "Fixture Single"},
        )

    def test_non_string_release_shelf_owner_remains_invalid(self):
        artist = raw_artist(albums=[raw_release("Fixture Album", "ALBUM_fixture_bad_owner")])
        artist["albums"]["browseId"] = 123

        result = provider.handle(
            candidate_request(),
            client=FakeClient(artist),
            mb_client=UnavailableMusicBrainz(),
        )

        self.assert_candidate_envelope(result)
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["items"], [])

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
        self.assertTrue(all("/release-group" not in urllib.parse.urlparse(url).path for url in mb_client.calls))

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

    def identity_fixture(self, *, name="Fixture", aliases=None, pages=None, search_count=None):
        detail = {"id": MUSICBRAINZ_ARTIST_ID, "name": name,
                  "sort-name": name, "aliases": aliases or [],
                  "relations": [{"url": {"resource": "https://www.youtube.com/channel/" + CHANNEL_ID}}],
                  "release-groups": []}
        return FakeMusicBrainz([{ "id": MUSICBRAINZ_ARTIST_ID, "name": name}],
                              {MUSICBRAINZ_ARTIST_ID: detail}, pages, search_count)

    def identify(self, mb, name="Fixture"):
        raw = raw_artist()
        raw["name"] = name
        return provider.handle({"operation":"recommendationIdentity", "value":CHANNEL_ID, "seed":SEED},
                               client=FakeClient(raw), mb_client=mb)

    def discography(self, mb, **options):
        request = {"operation":"recommendationDiscography", "value":MUSICBRAINZ_ARTIST_ID,
                   "seed":{"identity":"canonical", "kind":"artist", "mbid":MUSICBRAINZ_ARTIST_ID}}
        request.update(options)
        # The metadata-only operation must work with no YT packages installed.
        with mock.patch.dict("sys.modules", {"requests":None, "ytmusicapi":None}), \
             mock.patch.object(provider, "home_raw_artist", side_effect=AssertionError("No YT request permitted")) as yt:
            result = provider.handle(request, mb_client=mb)
        yt.assert_not_called()
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["provenance"]["source"], "musicbrainz")
        self.assertEqual(result["items"], [])
        self.assertEqual(result["seed"], request["seed"])
        self.assertNotIn("mapping", result)
        self.assertNotIn("mappingStatus", result)
        self.assertNotIn("mbid", result)
        self.assertEqual(result["artist"]["id"], MUSICBRAINZ_ARTIST_ID)
        self.assertIs(result["complete"], result["artist"]["releaseGroupsComplete"])
        return result

    def release_groups(self, start, end, title="Release"):
        dates = ["2026", "2026-03", "2026-03-12", "", "2026-??-12"]
        return [{"id":str(uuid.UUID(int=index + 10000)), "title":title + str(index),
                 "first-release-date":dates[index % len(dates)], "primary-type":"Album",
                 "secondary-types":[]} for index in range(start, end)]

    def recent_groups(self, start, end, **options):
        return [dict(row, **{"artist-credit":[{"name":"Native artist", "artist":{
            "id":MUSICBRAINZ_ARTIST_ID, "name":"Native artist"}}]})
            for row in self.release_groups(start, end, **options)]

    def recent_discography(self, mb):
        return self.discography(mb, since="2025-11-13", until="2026-11-13")

    def test_recent_discography_searches_bounded_dates_and_validates_scope(self):
        groups = self.recent_groups(0, 125)
        # A joint credit is still evidence for the explicitly requested artist.
        groups[0]["artist-credit"].insert(0, {"artist":{"id":OTHER_MUSICBRAINZ_ARTIST_ID}})
        mb = self.identity_fixture(pages={
            0:{"count":125, "offset":0, "release-groups":groups[:100]},
            100:{"count":125, "offset":100, "release-groups":groups[100:]}})
        result = self.recent_discography(mb)
        self.assertEqual(result["status"], "ready")
        self.assertIs(result["complete"], True)
        self.assertEqual(result["artist"]["releaseGroupScope"], "recent-releases")
        self.assertEqual(result["artist"]["releaseGroupWindow"], {"since":"2025-11-13", "until":"2026-11-13"})
        self.assertEqual(result["artist"]["releaseGroupCount"], 125)
        self.assertEqual(result["artist"]["releaseGroups"], self.release_groups(0, 125))
        self.assertEqual(len(mb.calls), 2)
        for offset, url in zip((0, 100), mb.calls):
            parsed = urllib.parse.urlparse(url)
            self.assertEqual(parsed.path, "/ws/2/release-group")
            self.assertEqual(urllib.parse.parse_qs(parsed.query), {
                "query":[f'arid:"{MUSICBRAINZ_ARTIST_ID}" AND firstreleasedate:[2025-11-13 TO 2026-11-13]'],
                "fmt":["json"], "limit":["100"], "offset":[str(offset)]})

    def test_recent_discography_preserves_partial_and_absent_dates(self):
        groups = self.recent_groups(0, 5)
        del groups[0]["first-release-date"]
        result = self.recent_discography(self.identity_fixture(pages={
            0:{"count":5, "offset":0, "release-groups":groups}}))
        expected = [{key:value for key,value in row.items() if key != "artist-credit"} for row in groups]
        self.assertEqual(result["artist"]["releaseGroups"], expected)
        self.assertIs(result["complete"], True)

    def test_recent_discography_successful_empty_subset_is_complete(self):
        mb = self.identity_fixture(pages={0:{"count":0, "offset":0, "release-groups":[]}})
        result = self.recent_discography(mb)
        self.assertEqual(result["status"], "empty")
        self.assertIs(result["complete"], True)
        self.assertEqual(result["artist"]["releaseGroupCount"], 0)
        self.assertEqual(result["artist"]["releaseGroupScope"], "recent-releases")
        self.assertEqual(len(mb.calls), 1)

    def test_recent_discography_rejects_invalid_options_before_transport(self):
        valid = {"since":"2025-11-13", "until":"2026-11-13"}
        cases = [{"since":"2025-11-13"}, {"until":"2026-11-13"},
                 dict(valid, extra=True), dict(valid, limit=100)]
        for value in (None, True, 20261113, "", "2026", "2026-11", "2026-02-30",
                      "2026-1-01", "20261113", "2026-11-13T00:00:00Z", " 2026-11-13",
                      "2026-11-13\n", "0000-01-01", "2026-11-13 TO *] OR arid:*"):
            cases.extend((dict(valid, since=value), dict(valid, until=value)))
        cases.append({"since":"2026-11-14", "until":"2026-11-13"})
        for options in cases:
            with self.subTest(options=options):
                mb = self.identity_fixture()
                request = {"operation":"recommendationDiscography", "value":MUSICBRAINZ_ARTIST_ID,
                           "seed":{"identity":"canonical", "kind":"artist", "mbid":MUSICBRAINZ_ARTIST_ID}, **options}
                with mock.patch.dict("sys.modules", {"requests":None, "ytmusicapi":None}):
                    result = provider.handle(request, mb_client=mb)
                self.assertEqual(result["status"], "unavailable")
                self.assertEqual(result["sourceStatus"], "invalid_argument")
                self.assertEqual(mb.calls, [])

    def test_recent_discography_requires_requested_artist_credit_on_every_row(self):
        valid = self.recent_groups(0, 1)[0]
        for credit in (None, [], "Native artist", [{"artist":{"id":OTHER_MUSICBRAINZ_ARTIST_ID}}],
                       [{"artist":{"id":"not-an-mbid"}}], [{"name":"Native artist"}]):
            with self.subTest(credit=credit):
                row = dict(valid, **{"artist-credit":credit})
                result = self.recent_discography(self.identity_fixture(pages={
                    0:{"count":1, "offset":0, "release-groups":[row]}}))
                self.assertEqual(result["status"], "unavailable")
                self.assertEqual(result["artist"]["releaseGroups"], [])
                self.assertIs(result["complete"], False)

    def test_recent_discography_rejects_malformed_search_count_and_offset(self):
        valid = {"count":1, "offset":0, "release-groups":self.recent_groups(0, 1)}
        pages = [dict(valid, count=value) for value in (None, False, 1.0, "1", -1)]
        pages += [dict(valid, offset=value) for value in (None, False, 0.0, "0", 1)]
        pages += [{"release-group-count":1, "release-group-offset":0, "release-groups":self.recent_groups(0, 1)}]
        for page in pages:
            with self.subTest(page=page):
                result = self.recent_discography(self.identity_fixture(pages={0:page}))
                self.assertEqual(result["status"], "unavailable")
                self.assertIs(result["complete"], False)
                self.assertEqual(result["artist"]["releaseGroups"], [])

    def test_recent_discography_retains_prior_page_on_changed_count_or_offset(self):
        first = self.recent_groups(0, 100)
        for count, offset in ((201,100), (200,99), (False,100)):
            with self.subTest(count=count, offset=offset):
                mb = self.identity_fixture(pages={0:{"count":200, "offset":0, "release-groups":first},
                    100:{"count":count, "offset":offset, "release-groups":self.recent_groups(100, 200)}})
                result = self.recent_discography(mb)
                self.assertEqual(result["status"], "partial")
                self.assertEqual(len(result["artist"]["releaseGroups"]), 100)
                self.assertEqual(result["artist"]["releaseGroupCount"], 200)
                self.assertIs(result["complete"], False)
                self.assertEqual(len(mb.calls), 2)

    def test_recent_discography_page_cap_partial_repeat_and_failed_pages(self):
        pages = {offset:{"count":600, "offset":offset, "release-groups":self.recent_groups(offset,offset+100)}
                 for offset in range(0,500,100)}
        mb = self.identity_fixture(pages=pages)
        result = self.recent_discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(len(result["artist"]["releaseGroups"]), 500)
        self.assertIs(result["complete"], False)
        self.assertEqual(len(mb.calls), 5)
        for second in (self.recent_groups(100,101), self.recent_groups(0,100), provider.ProviderTimeoutError()):
            with self.subTest(second=type(second).__name__):
                page = second if isinstance(second, Exception) else {"count":200,"offset":100,"release-groups":second}
                result = self.recent_discography(self.identity_fixture(pages={
                    0:{"count":200,"offset":0,"release-groups":self.recent_groups(0,100)},100:page}))
                self.assertEqual(result["status"], "partial")
                self.assertIs(result["complete"], False)
                self.assertGreaterEqual(len(result["artist"]["releaseGroups"]), 100)
                self.assertLessEqual(len(result["artist"]["releaseGroups"]), 101)

    def test_recent_discography_wire_budget_handles_multibyte_titles(self):
        mb = self.identity_fixture(pages={0:{"count":100,"offset":0,
            "release-groups":self.recent_groups(0,100,title="日本語" * 500)}})
        result = self.recent_discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertIs(result["complete"], False)
        self.assertGreater(len(result["artist"]["releaseGroups"]), 0)
        self.assertLess(len(result["artist"]["releaseGroups"]), 100)
        self.assertLessEqual(len(json.dumps({"ok":True,"data":result},ensure_ascii=True).encode("utf-8"))+1,239*1024)
        self.assertEqual(len(mb.calls), 1)

    def test_recent_discography_failed_or_unusable_first_page_is_not_empty(self):
        for page in (provider.ProviderTimeoutError(),
                     urllib.error.HTTPError("https://musicbrainz.org/ws/2/release-group",401,"Unauthorized",{},None),
                     {"count":1,"offset":0,"release-groups":self.recent_groups(0,1,title="日" * 300000)},
                     {"count":101,"offset":0,"release-groups":self.recent_groups(0,101)}):
            with self.subTest(page=type(page).__name__):
                mb = self.identity_fixture(pages={0:page})
                result = self.recent_discography(mb)
                self.assertEqual(result["status"], "unavailable")
                self.assertIs(result["complete"], False)
                self.assertEqual(result["artist"]["releaseGroups"], [])
                self.assertEqual(len(mb.calls), 1)

    def test_recent_discography_later_mismatched_credit_retains_only_prior_page(self):
        second = self.recent_groups(100,101)
        second[0]["artist-credit"]=[{"artist":{"id":OTHER_MUSICBRAINZ_ARTIST_ID}}]
        mb = self.identity_fixture(pages={0:{"count":101,"offset":0,"release-groups":self.recent_groups(0,100)},
                                          100:{"count":101,"offset":100,"release-groups":second}})
        result = self.recent_discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertIs(result["complete"], False)
        self.assertEqual(len(result["artist"]["releaseGroups"]), 100)
        self.assertEqual(len(mb.calls), 2)

    def test_recent_discography_same_day_window_and_legacy_browse_shape(self):
        mb = self.identity_fixture(pages={0:{"count":0,"offset":0,"release-groups":[]}})
        result = self.discography(mb,since="2024-02-29",until="2024-02-29")
        self.assertEqual(result["status"], "empty")
        legacy = self.discography(self.identity_fixture())
        self.assertNotIn("releaseGroupScope",legacy["artist"])
        self.assertNotIn("releaseGroupWindow",legacy["artist"])

    def test_native_name_verifies_through_english_alias_and_channel_relation(self):
        mb = self.identity_fixture(name="竹内まりや", aliases=[
            {"name":"Mariya Takeuchi", "sort-name":"Takeuchi, Mariya", "locale":None},
            {"name":"竹内まりや"}])
        result = self.identify(mb, "Mariya Takeuchi")
        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["mapping"]["method"], "external-id")
        self.assertEqual(result["artist"]["aliases"], ["竹内まりや", "Mariya Takeuchi", "Takeuchi, Mariya"])
        self.assertIs(result["artist"]["aliasesComplete"], True)
        query = urllib.parse.parse_qs(urllib.parse.urlparse(mb.calls[0]).query)["query"][0]
        self.assertEqual(query, '(artist:"Mariya Takeuchi" OR alias:"Mariya Takeuchi")')
        self.assertIn("aliases", urllib.parse.parse_qs(urllib.parse.urlparse(mb.calls[1]).query)["inc"][0])

    def test_receiving_verified_artist_id_matches_canonical_mapping(self):
        mb = self.identity_fixture(name="竹内まりや",aliases=[{"name":"Mariya Takeuchi"}])
        result = self.identify(mb,"Mariya Takeuchi")
        self.assertEqual(result["status"],"verified")
        self.assertEqual(result["artist"].get("id"),result["mbid"])
        self.assertEqual(result["artist"]["id"],result["mapping"]["mbid"])
        self.assertEqual(result["artist"]["aliases"],["竹内まりや","Mariya Takeuchi"])
        self.assertIs(result["artist"]["aliasesComplete"],True)
        self.assertEqual(len(mb.calls),2)

    def test_receiving_undated_discography_rejects_malformed_metadata_before_transport(self):
        invalid = [{"fetchedAt":value} for value in (None,False,123,[],{},"x"*129)]
        invalid += [{"refresh":value} for value in (None,0,1,"true",[],{})]
        for options in invalid:
            with self.subTest(options=options):
                mb = self.identity_fixture()
                request = {"operation":"recommendationDiscography","value":MUSICBRAINZ_ARTIST_ID,
                           "seed":{"identity":"canonical","kind":"artist","mbid":MUSICBRAINZ_ARTIST_ID},**options}
                with mock.patch.dict("sys.modules",{"requests":None,"ytmusicapi":None}):
                    result = provider.handle(request,mb_client=mb)
                self.assertEqual(result["status"],"unavailable")
                self.assertEqual(result["sourceStatus"],"invalid_argument")
                self.assertEqual(mb.calls,[])

    def test_alias_query_escapes_lucene_quotes_and_backslashes(self):
        name = 'A "B" \\ C'
        mb = self.identity_fixture(name="Native", aliases=[{"name":name}])
        result = self.identify(mb, name)
        self.assertEqual(result["status"], "verified")
        query = urllib.parse.parse_qs(urllib.parse.urlparse(mb.calls[0]).query)["query"][0]
        escaped = name.replace("\\", "\\\\").replace('"', '\\"')
        self.assertEqual(query, '(artist:"' + escaped + '" OR alias:"' + escaped + '")')

    def test_verified_discography_browses_beyond_lookup_limit_and_preserves_partial_dates(self):
        groups = self.release_groups(0, 125)
        mb = self.identity_fixture(pages={
            0:{"release-group-count":125, "release-group-offset":0, "release-groups":groups[:100]},
            100:{"release-group-count":125, "release-group-offset":100, "release-groups":groups[100:]}})
        result = self.discography(mb)
        self.assertEqual(result["status"], "ready")
        self.assertIs(result["complete"], True)
        self.assertEqual(result["artist"]["releaseGroups"], groups)
        self.assertIs(result["artist"]["releaseGroupsComplete"], True)
        self.assertEqual(result["artist"]["releaseGroupCount"], 125)

    def test_discography_failure_does_not_revoke_verified_identity(self):
        mb = self.identity_fixture(pages={0:RuntimeError("offline fixture failure")})
        identity = self.identify(mb)
        self.assertEqual(identity["status"], "verified")
        self.assertIs(identity["complete"], True)
        self.assertEqual(len(mb.calls), 2)
        self.assertNotIn("releaseGroups", identity["artist"])
        result = self.discography(mb)
        self.assertEqual(result["status"], "unavailable")
        self.assertIs(result["complete"], False)
        self.assertEqual(result["artist"]["releaseGroups"], [])
        self.assertIs(result["artist"]["releaseGroupsComplete"], False)
        self.assertIsNone(result["artist"]["releaseGroupCount"])
        self.assertEqual(identity["status"], "verified")
        self.assertEqual(len(mb.calls), 3)


    def test_discography_stops_on_partial_repeating_and_malformed_pages(self):
        first = self.release_groups(0, 100)
        for rows in (self.release_groups(100, 101), first, [{"id":"not-an-mbid", "title":"Bad"}]):
            with self.subTest(rows=len(rows)):
                mb = self.identity_fixture(pages={
                    0:{"release-group-count":200, "release-group-offset":0, "release-groups":first},
                    100:{"release-group-count":200, "release-group-offset":100, "release-groups":rows}})
                result = self.discography(mb)
                self.assertEqual(result["status"], "partial")
                self.assertIs(result["artist"]["releaseGroupsComplete"], False)
                self.assertEqual(result["artist"]["releaseGroups"][:100], first)
                self.assertLessEqual(len(result["artist"]["releaseGroups"]), 101)
                self.assertEqual(len(mb.calls), 2)

    def test_discography_rejects_wrong_offsets_and_changed_totals(self):
        first = self.release_groups(0, 100)
        for page in ({"release-group-count":200, "release-group-offset":0, "release-groups":first},
                     {"release-group-count":201, "release-group-offset":100, "release-groups":self.release_groups(100, 200)},
                     {"release-group-count":"200", "release-group-offset":100, "release-groups":[]},
                     {"release-group-count":200, "release-group-offset":100, "release-groups":None}):
            with self.subTest(page=page):
                mb = self.identity_fixture(pages={
                    0:{"release-group-count":200, "release-group-offset":0, "release-groups":first}, 100:page})
                result = self.discography(mb)
                self.assertEqual(result["status"], "partial")
                self.assertIs(result["artist"]["releaseGroupsComplete"], False)
                self.assertEqual(result["artist"]["releaseGroups"], first)

    def test_discography_page_cap_is_explicitly_incomplete(self):
        pages = {offset:{"release-group-count":501, "release-group-offset":offset,
                         "release-groups":self.release_groups(offset, offset + 100)} for offset in range(0, 500, 100)}
        mb = self.identity_fixture(pages=pages)
        result = self.discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(len(result["artist"]["releaseGroups"]), 500)
        self.assertEqual(result["artist"]["releaseGroupCount"], 501)
        self.assertIs(result["artist"]["releaseGroupsComplete"], False)
        self.assertEqual(len(mb.calls), 5)

    def test_metadata_multibyte_budget_preserves_identity_and_marks_truncation(self):
        groups = self.release_groups(0, 100, title="日本語" * 500)
        mb = self.identity_fixture(pages={0:{"release-group-count":100, "release-group-offset":0, "release-groups":groups}})
        result = self.discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertLess(len(result["artist"]["releaseGroups"]), 100)
        self.assertIs(result["artist"]["releaseGroupsComplete"], False)
        self.assertLessEqual(len(json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8")), 240 * 1024)
        result = self.identify(self.identity_fixture(aliases=[{"name":"日本語" * 40000}]))
        self.assertEqual(result["status"], "verified")
        self.assertIs(result["artist"]["aliasesComplete"], False)
        self.assertEqual(result["artist"]["aliases"], ["Fixture"])
        self.assertLessEqual(len(json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8")), 240 * 1024)

    def test_identity_never_browses_discography(self):
        mb = self.identity_fixture()
        result = self.identify(mb)
        self.assertEqual(result["status"], "verified")
        self.assertIs(result["artist"]["aliasesComplete"], True)
        self.assertNotIn("releaseGroups", result["artist"])
        self.assertNotIn("releaseGroupsComplete", result["artist"])
        self.assertNotIn("releaseGroupCount", result["artist"])
        self.assertEqual(len(mb.calls), 2)
        self.assertTrue(all("/release-group" not in urllib.parse.urlparse(url).path for url in mb.calls))

    def test_discography_empty_is_only_complete_zero_groups_and_never_calls_yt(self):
        mb = self.identity_fixture()
        result = self.discography(mb)
        self.assertEqual(result["status"], "empty")
        self.assertIs(result["complete"], True)
        self.assertEqual(result["artist"]["releaseGroupCount"], 0)
        self.assertEqual(result["artist"]["releaseGroups"], [])
        self.assertEqual(len(mb.calls), 1)
        self.assertTrue(urllib.parse.urlparse(mb.calls[0]).path.endswith("/release-group"))

    def test_discography_invalid_canonical_seed_stops_before_transport(self):
        canonical = {"identity":"canonical", "kind":"artist", "mbid":MUSICBRAINZ_ARTIST_ID}
        cases = [(None, canonical), (42, canonical), ("0" * 36, canonical),
                 ("00000000-0000-0000-0000-000000000000", canonical),
                 (MUSICBRAINZ_ARTIST_ID, None), (MUSICBRAINZ_ARTIST_ID, []),
                 (MUSICBRAINZ_ARTIST_ID, SEED),
                 (MUSICBRAINZ_ARTIST_ID, dict(canonical, mbid=OTHER_MUSICBRAINZ_ARTIST_ID)),
                 (MUSICBRAINZ_ARTIST_ID, dict(canonical, kind="recording")),
                 (MUSICBRAINZ_ARTIST_ID, dict(canonical, extra=True)),
                 (MUSICBRAINZ_ARTIST_ID, dict(canonical, identity=True))]
        for value, seed in cases:
            with self.subTest(value=value, seed=seed):
                mb = self.identity_fixture()
                with mock.patch.dict("sys.modules", {"requests":None, "ytmusicapi":None}):
                    result = provider.handle({"operation":"recommendationDiscography", "value":value,
                                              "seed":seed}, mb_client=mb)
                self.assertEqual(result["status"], "unavailable")
                self.assertEqual(result["sourceStatus"], "invalid_argument")
                self.assertIs(result["complete"], False)
                self.assertEqual(result["provenance"]["source"], "musicbrainz")
                self.assertNotIn("mappingStatus", result)
                self.assertEqual(mb.calls, [])

    def test_discography_unusable_first_page_cannot_be_reported_empty(self):
        for page in ({"release-groups":[], "release-group-count":1, "release-group-offset":0},
                     {"release-groups":[{"id":"bad", "title":"Bad"}], "release-group-count":1, "release-group-offset":0},
                     {"release-groups":self.release_groups(0,1,title="日" * 300000), "release-group-count":1, "release-group-offset":0}):
            with self.subTest(count=page["release-group-count"]):
                mb = self.identity_fixture(pages={0:page})
                result = self.discography(mb)
                self.assertEqual(result["status"], "unavailable")
                self.assertIs(result["complete"], False)
                self.assertEqual(result["artist"]["releaseGroups"], [])
                self.assertEqual(len(mb.calls), 1)

    def test_discography_partial_transport_failure_retains_evidence(self):
        first = self.release_groups(0,100)
        mb = self.identity_fixture(pages={
            0:{"release-groups":first, "release-group-count":200, "release-group-offset":0},
            100:provider.RateLimitError(retry_after=123)})
        result = self.discography(mb)
        self.assertEqual(result["status"], "partial")
        self.assertIs(result["complete"], False)
        self.assertEqual(result["artist"]["releaseGroups"], first)
        self.assertEqual(result["sourceStatus"], "rate_limited")
        self.assertEqual(result["retryAfterSeconds"], 123)
        self.assertEqual(len(mb.calls), 2)

    def test_alias_match_alone_does_not_verify_identity(self):
        mb = self.identity_fixture(name="Native", aliases=[{"name":"Fixture"}])
        mb.details[MUSICBRAINZ_ARTIST_ID]["relations"] = []
        result = self.identify(mb)
        self.assertEqual(result["status"], "unresolved")
        self.assertNotIn("mapping", result)
        self.assertEqual(len(mb.calls), 2)

    def test_incomplete_alias_search_cannot_verify_or_fetch_discography(self):
        mb = self.identity_fixture(name="Native", aliases=[{"name":"Fixture"}], search_count=6)
        result = self.identify(mb)
        self.assertNotEqual(result["status"], "verified")
        self.assertIs(result["complete"], False)
        self.assertNotIn("mapping", result)
        self.assertEqual(len(mb.calls), 2)

    def test_malformed_search_offsets_cannot_verify_or_fetch_discography(self):
        for offset in (False, 0.0, "0", True, 1):
            with self.subTest(offset=repr(offset)):
                mb = self.identity_fixture()
                get_json = mb.get_json

                def response(url):
                    data = get_json(url)
                    if urllib.parse.urlparse(url).path.endswith("/artist/"):
                        return dict(data, offset=offset)
                    return data

                with mock.patch.object(mb, "get_json", side_effect=response):
                    result = self.identify(mb)
                self.assertNotEqual(result["status"], "verified")
                self.assertIs(result["complete"], False)
                self.assertNotIn("mapping", result)
                self.assertEqual(len(mb.calls), 2)
                self.assertTrue(all("/release-group" not in urllib.parse.urlparse(url).path for url in mb.calls))

    def test_malformed_alias_metadata_cannot_hide_unexamined_name_matches(self):
        for aliases in ({"name":"Fixture"}, [None], [{"name":42}]):
            with self.subTest(aliases=aliases):
                mb = self.identity_fixture()
                mb.details[MUSICBRAINZ_ARTIST_ID]["aliases"] = aliases
                result = self.identify(mb)
                self.assertNotEqual(result["status"], "verified")
                self.assertIs(result["complete"], False)
                self.assertNotIn("mapping", result)
                self.assertEqual(len(mb.calls), 2)

    def test_discography_retains_absent_dates_without_inventing_a_full_date(self):
        groups = [{"id":str(uuid.UUID(int=10000)), "title":"Undated", "primary-type":None}]
        result = self.discography(self.identity_fixture(pages={
            0:{"release-group-count":1, "release-group-offset":0, "release-groups":groups}}))
        self.assertEqual(result["artist"]["releaseGroups"], groups)
        self.assertIs(result["artist"]["releaseGroupsComplete"], True)
        self.assertNotIn("first-release-date", result["artist"]["releaseGroups"][0])

    def test_conflicting_native_alias_channel_matches_stay_ambiguous(self):
        ids = [MUSICBRAINZ_ARTIST_ID, OTHER_MUSICBRAINZ_ARTIST_ID]
        mb = FakeMusicBrainz([{"id":key, "name":"Native"} for key in ids],
            {key:{"id":key, "name":"Native", "aliases":[{"name":"Fixture"}],
                  "relations":[{"url":{"resource":"https://www.youtube.com/channel/" + CHANNEL_ID}}]} for key in ids})
        result = self.identify(mb)
        self.assertEqual(result["status"], "ambiguous")
        self.assertNotIn("mapping", result)
        self.assertEqual(len(mb.calls), 3)

    def test_unique_alias_still_requires_two_dated_release_corroborations(self):
        mb = self.identity_fixture(name="Native", aliases=[{"name":"Fixture"}])
        detail = mb.details[MUSICBRAINZ_ARTIST_ID]
        detail["relations"] = []
        detail["release-groups"] = self.release_groups(0, 2)
        raw = raw_artist(albums=[raw_release(group["title"], "MPRE_" + str(index), year=group["first-release-date"][:4])
                                 for index, group in enumerate(detail["release-groups"])])
        result = provider.handle({"operation":"recommendationIdentity", "value":CHANNEL_ID, "seed":SEED},
                                 client=FakeClient(raw), mb_client=mb)
        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["mapping"]["method"], "corroborated-metadata")

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
