"""Bounded LRCLIB transport and track provenance contracts."""
import datetime
import importlib.util
import io
import json
import pathlib
import socket
import urllib.error
import urllib.parse
import urllib.request
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "vinyl_lyrics_provider", ROOT / "scripts" / "vinyl" / "provider.py"
)
provider = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provider)


REQUEST = {
    "operation": "lyrics",
    "value": "ytm:track:opaque-id_123",
    "trackName": "Song & Title",
    "artistName": "Artist / Name",
    "albumName": "Album? Name",
    "duration": 290,
}
RECORD = {
    "id": 32557,
    "trackName": "Without Me",
    "artistName": "Eminem",
    "albumName": "The Eminem Show",
    "duration": 290,
    "instrumental": False,
    "plainLyrics": "plain fixture",
    "syncedLyrics": "[00:01.00]timed fixture",
    "extra": "must not escape the known-field boundary",
}


class FakeResponse:
    def __init__(self, body, content_type="application/json; charset=utf-8", status=200):
        self._body = body
        self._stream = io.BytesIO(body)
        self.headers = {"Content-Type": content_type}
        self.status = status
        self.read_sizes = []

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self, size=-1):
        self.read_sizes.append(size)
        return self._stream.read(size)


class Opener:
    def __init__(self, response=None, error=None):
        self.response = response
        self.error = error
        self.calls = []

    def __call__(self, request, timeout=None):
        self.calls.append((request, timeout))
        if self.error:
            raise self.error
        return self.response


def request_for(**changes):
    value = dict(REQUEST)
    value.update(changes)
    return value


def json_response(data, **kwargs):
    return FakeResponse(json.dumps(data).encode("utf-8"), **kwargs)


class VinylLyricsProviderTest(unittest.TestCase):
    def test_get_encodes_only_bounded_metadata_and_returns_known_record_fields(self):
        opener = Opener(json_response(RECORD))

        result = provider.fetch_lyrics(REQUEST, opener=opener)

        self.assertEqual(len(opener.calls), 1)
        request, timeout = opener.calls[0]
        self.assertEqual(timeout, 8)
        parsed = urllib.parse.urlparse(request.full_url)
        self.assertEqual(parsed.scheme, "https")
        self.assertEqual(parsed.netloc, "lrclib.net")
        self.assertEqual(parsed.path, "/api/get")
        self.assertEqual(urllib.parse.parse_qs(parsed.query), {
            "track_name": [REQUEST["trackName"]],
            "artist_name": [REQUEST["artistName"]],
            "album_name": [REQUEST["albumName"]],
            "duration": ["290"],
        })
        self.assertIn("Colosseum", request.get_header("User-agent"))
        self.assertEqual(request.get_header("Accept"), "application/json")
        self.assertEqual(opener.response.read_sizes, [512 * 1024 + 1])
        self.assertEqual(result["source"], "lrclib")
        self.assertEqual(result["status"], "found")
        fetched_at = datetime.datetime.fromisoformat(result["fetchedAt"].replace("Z", "+00:00"))
        self.assertIsNotNone(fetched_at.tzinfo)
        self.assertEqual(result["record"], {key: RECORD[key] for key in (
            "id", "trackName", "artistName", "albumName", "duration",
            "instrumental", "plainLyrics", "syncedLyrics",
        )})

    def test_plain_and_timed_lyric_shapes_are_preserved_independently(self):
        plain = dict(RECORD, plainLyrics="plain only", syncedLyrics=None)
        timed = dict(RECORD, plainLyrics=None, syncedLyrics="[00:02.00]timed only")
        for index, record in enumerate((plain, timed)):
            with self.subTest(index=index):
                result = provider.fetch_lyrics(REQUEST, opener=Opener(json_response(record)))
                self.assertEqual(result["record"]["plainLyrics"], record["plainLyrics"])
                self.assertEqual(result["record"]["syncedLyrics"], record["syncedLyrics"])

    def test_instrumental_record_is_preserved_without_inventing_lyrics(self):
        instrumental = dict(RECORD, instrumental=True, plainLyrics=None, syncedLyrics=None)

        result = provider.fetch_lyrics(REQUEST, opener=Opener(json_response(instrumental)))

        self.assertIs(result["record"]["instrumental"], True)
        self.assertIsNone(result["record"]["plainLyrics"])
        self.assertIsNone(result["record"]["syncedLyrics"])

    def test_http_404_is_successful_not_found_envelope(self):
        error = urllib.error.HTTPError(
            "https://lrclib.net/api/get", 404, "Not Found", {}, None,
        )

        result = provider.fetch_lyrics(REQUEST, opener=Opener(error=error))

        self.assertEqual(result["source"], "lrclib")
        self.assertEqual(result["status"], "not_found")
        self.assertIsNone(result["record"])
        datetime.datetime.fromisoformat(result["fetchedAt"].replace("Z", "+00:00"))

    def test_optional_album_and_value_are_not_sent_as_provider_overrides(self):
        request = request_for(albumName="")
        opener = Opener(json_response(RECORD))

        provider.fetch_lyrics(request, opener=opener)

        parsed = urllib.parse.urlparse(opener.calls[0][0].full_url)
        params = urllib.parse.parse_qs(parsed.query)
        self.assertNotIn("album_name", params)
        self.assertNotIn("value", params)
        self.assertNotIn("url", params)

    def test_invalid_required_fields_duration_and_oversized_metadata_fail_before_io(self):
        invalid = [
            {"trackName": ""},
            {"trackName": "x" * 301},
            {"artistName": ""},
            {"artistName": "x" * 301},
            {"albumName": "x" * 301},
            {"duration": True},
            {"duration": False},
            {"duration": float("nan")},
            {"duration": float("inf")},
            {"duration": 0},
            {"duration": 3601},
            {"duration": 10 ** 1000},
            {"duration": "290"},
        ]
        for index, changes in enumerate(invalid):
            with self.subTest(index=index):
                opener = Opener(json_response(RECORD))
                with self.assertRaises(provider.InvalidArgumentError):
                    provider.fetch_lyrics(request_for(**changes), opener=opener)
                self.assertEqual(opener.calls, [])

    def test_invalid_opaque_value_is_rejected_without_network(self):
        for index, value in enumerate((None, 123, "x" * 301)):
            with self.subTest(index=index):
                opener = Opener(json_response(RECORD))
                with self.assertRaises(provider.InvalidArgumentError):
                    provider.fetch_lyrics(request_for(value=value), opener=opener)
                self.assertEqual(opener.calls, [])

    def test_wrong_content_type_malformed_json_and_oversized_body_are_sanitized(self):
        cases = [
            FakeResponse(json.dumps(RECORD).encode(), content_type="text/plain"),
            FakeResponse(b"not json"),
            FakeResponse(b"{" + b" " * (512 * 1024)),
        ]
        for index, response in enumerate(cases):
            with self.subTest(index=index):
                with self.assertRaises(provider.ProviderUnavailableError) as caught:
                    provider.fetch_lyrics(REQUEST, opener=Opener(response))
                self.assertNotIn("plain fixture", str(caught.exception))

    def test_malformed_record_fields_are_rejected(self):
        malformed_records = [
            dict(RECORD, id=True),
            dict(RECORD, id=0),
            dict(RECORD, trackName=""),
            dict(RECORD, artistName=5),
            dict(RECORD, albumName=5),
            dict(RECORD, duration=True),
            dict(RECORD, duration=float("nan")),
            dict(RECORD, instrumental="false"),
            dict(RECORD, plainLyrics=5),
            dict(RECORD, syncedLyrics="x" * (256 * 1024 + 1)),
        ]
        for index, record in enumerate(malformed_records):
            with self.subTest(index=index):
                with self.assertRaises(provider.ProviderUnavailableError):
                    provider.fetch_lyrics(REQUEST, opener=Opener(json_response(record)))

    def test_null_album_name_is_canonical_and_qt_safe_record_ids_are_bounded(self):
        null_album = dict(RECORD, albumName=None)
        result = provider.fetch_lyrics(REQUEST, opener=Opener(json_response(null_album)))
        self.assertEqual(result["record"]["albumName"], "")

        safe_max = dict(RECORD, id=2**53 - 1)
        accepted = provider.fetch_lyrics(REQUEST, opener=Opener(json_response(safe_max)))
        self.assertEqual(accepted["record"]["id"], 2**53 - 1)

        unsafe = dict(RECORD, id=2**53)
        with self.assertRaises(provider.ProviderUnavailableError):
            provider.fetch_lyrics(REQUEST, opener=Opener(json_response(unsafe)))

    def test_success_requires_exact_http_200_status(self):
        for index, status in enumerate((None, 201, 204)):
            with self.subTest(index=index):
                response = json_response(RECORD)
                response.status = status
                with self.assertRaises(provider.ProviderUnavailableError):
                    provider.fetch_lyrics(REQUEST, opener=Opener(response))
    def test_provider_error_is_sanitized_and_retry_after_is_bounded(self):
        for status, header, expected in ((429, "9000", 3600), (503, "0", 1), (429, "bad", 60)):
            with self.subTest(status=status, header=header):
                error = urllib.error.HTTPError(
                    "https://lrclib.net/api/get", status, "provider detail", {"Retry-After": header}, None,
                )
                with self.assertRaises(provider.RateLimitError) as caught:
                    provider.fetch_lyrics(REQUEST, opener=Opener(error=error))
                self.assertEqual(caught.exception.retry_after, expected)
                self.assertNotIn("provider detail", str(caught.exception))

    def test_http_errors_and_transport_failures_are_typed_and_sanitized(self):
        cases = [
            (urllib.error.HTTPError("https://lrclib.net/api/get", 500, "private", {}, None),
             provider.ProviderUnavailableError),
            (socket.timeout("private host detail"), provider.ProviderTimeoutError),
            (urllib.error.URLError("private host detail"), provider.ProviderUnavailableError),
        ]
        for error, error_type in cases:
            with self.subTest(error_type=error_type.__name__):
                with self.assertRaises(error_type) as caught:
                    provider.fetch_lyrics(REQUEST, opener=Opener(error=error))
                self.assertNotIn("private", str(caught.exception))

    def test_handle_dispatches_lyrics_before_youtube_client_import(self):
        marker = {"source": "lrclib", "status": "not_found", "record": None}
        original_import = __import__

        def import_guard(name, *args, **kwargs):
            if name == "ytmusicapi" or name.startswith("ytmusicapi."):
                raise AssertionError("lyrics dispatch tried to import YouTube Music")
            return original_import(name, *args, **kwargs)

        with mock.patch.object(provider, "fetch_lyrics", return_value=marker) as fetch:
            with mock.patch("builtins.__import__", side_effect=import_guard):
                result = provider.handle(REQUEST)
        self.assertIs(result, marker)
        fetch.assert_called_once_with(REQUEST)

    def test_track_album_and_video_type_provenance_are_preserved_without_guessing(self):
        album_track = {
            "videoId": "abcdefghijk",
            "title": "Album song",
            "duration_seconds": 290,
            "videoType": "MUSIC_VIDEO_TYPE_ATV",
            "album": {"name": "Track album provenance"},
        }
        album = provider.normalize_album({
            "title": "Parent release",
            "type": "Album",
            "artists": [{"name": "Artist", "id": "UC" + "A" * 22}],
            "tracks": [album_track],
        }, "ALBUM_fixture")
        self.assertEqual(album["tracks"][0]["albumName"], "Track album provenance")
        self.assertEqual(album["tracks"][0]["videoType"], "MUSIC_VIDEO_TYPE_ATV")

        parent_fallback = provider.normalize_album({
            "title": "Actual parent release", "type": "Album", "tracks": [{
                "videoId": "abcdefghijl", "title": "Album track", "duration_seconds": 90,
                "videoType": "MUSIC_VIDEO_TYPE_OMV",
            }],
        }, "ALBUM_fixture_2")
        self.assertEqual(parent_fallback["tracks"][0]["albumName"], "Actual parent release")
        self.assertEqual(parent_fallback["tracks"][0]["videoType"], "MUSIC_VIDEO_TYPE_OMV")

        standalone = provider.song_record({
            "videoId": "abcdefghijk", "title": "Standalone song",
            "artists": [{"name": "Artist", "id": "UC" + "A" * 22}],
        })
        self.assertNotIn("albumName", standalone["tracks"][0])
        self.assertNotIn("videoType", standalone["tracks"][0])

        self.assertIsNone(provider.song_record({
            "videoId": "abcdefghijk", "title": "Explicitly unknown", "videoType": None,
        }))

        for parent_type in ("Song", "Playlist"):
            record = provider.normalize_album({
                "title": "Not an album title", "type": parent_type, "tracks": [{
                    "videoId": "abcdefghijl", "title": "A track", "duration_seconds": 1,
                }],
            }, "PARENT_fixture")
            self.assertNotIn("albumName", record["tracks"][0])


if __name__ == "__main__":
    unittest.main()