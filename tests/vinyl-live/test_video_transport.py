"""Offline resolveVideo transport tests against the real project provider.

An injected fake yt_dlp module serves canned extract_info results, so no
network, provider dependencies, or credentials are needed.
"""
import copy
import importlib.util
import json
import pathlib
import sys
import time
import types
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

spec = importlib.util.spec_from_file_location("provider", ROOT / "scripts/vinyl/provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)

VIDEO_ID = "dQw4w9WgXcQ"
WATCH_URL = "https://www.youtube.com/watch?v=" + VIDEO_ID
EXPECTED_FORMAT = "bv[height<=1080][protocol=https]+ba[protocol=https]/b[height<=1080][protocol=https]"
FUTURE_EXPIRE = 9999999999  # Epoch seconds far past any test run.
GV_HOST = "rr5---sn-npoe7ney.googlevideo.com"
COMMON_HEADERS = {"User-Agent": "VinylTransportTest/1.0", "Accept": "*/*"}


def gv_url(expire=FUTURE_EXPIRE, host=GV_HOST):
    return f"https://{host}/videoplayback?expire={expire}&mn=sn-npoe7ney&source=youtube&ip=203.0.113.7"


def muxed_result(**overrides):
    data = {
        "id": VIDEO_ID, "_type": "video", "title": "Transport fixture",
        "url": gv_url(), "ext": "mp4", "format_id": "18", "protocol": "https",
        "vcodec": "avc1.640028", "acodec": "mp4a.40.2", "height": 720, "width": 1280,
        "duration": 213, "is_live": False, "http_headers": dict(COMMON_HEADERS),
    }
    data.update(overrides)
    return data


def separate_result(video_headers=None, audio_headers=None, common_headers=None,
                    video_url=None, audio_url=None, video_expire=FUTURE_EXPIRE,
                    audio_expire=FUTURE_EXPIRE):
    resolved_video_url = video_url if video_url is not None else gv_url(expire=video_expire)
    resolved_audio_url = audio_url if audio_url is not None else gv_url(expire=audio_expire)
    video_format = {
        "format_id": "137", "ext": "mp4", "protocol": "https", "url": resolved_video_url,
        "vcodec": "avc1.640028", "acodec": "none", "height": 1080, "width": 1920,
    }
    audio_format = {
        "format_id": "251", "ext": "webm", "protocol": "https", "url": resolved_audio_url,
        "vcodec": "none", "acodec": "opus", "abr": 160,
    }
    if video_headers is not None:
        video_format["http_headers"] = dict(video_headers)
    if audio_headers is not None:
        audio_format["http_headers"] = dict(audio_headers)
    data = {
        "id": VIDEO_ID, "_type": "video", "duration": 212.5, "is_live": False,
        "url": resolved_video_url, "requested_formats": [video_format, audio_format],
    }
    if common_headers is not None:
        data["http_headers"] = dict(common_headers)
    return data


class FakeYoutubeDLFactory:
    """Records YoutubeDL construction options and serves canned extract_info results."""

    def __init__(self, result):
        self.result = result
        self.constructions = []
        self.calls = []

    def YoutubeDL(self, options):
        factory = self

        class Client:
            def __init__(self):
                factory.constructions.append(dict(options))

            def extract_info(self, url, download=True):
                factory.calls.append({"url": url, "download": download})
                result = factory.result
                if isinstance(result, Exception):
                    raise result
                return copy.deepcopy(result(url) if callable(result) else result)

            def __enter__(self):
                return self

            def __exit__(self, *exc_info):
                return False

        return Client()


class VideoTransportTests(unittest.TestCase):
    def install_yt_dlp(self, result):
        factory = FakeYoutubeDLFactory(result)
        module = types.ModuleType("yt_dlp")
        module.YoutubeDL = factory.YoutubeDL
        patcher = mock.patch.dict(sys.modules, {"yt_dlp": module})
        patcher.start()
        self.addCleanup(patcher.stop)
        return factory

    def resolve_success(self, result, video=VIDEO_ID):
        factory = self.install_yt_dlp(result)
        return provider.resolve_video(video), factory

    def assertResolutionFails(self, result, video=VIDEO_ID):
        self.install_yt_dlp(result)
        with self.assertRaises(ValueError) as ctx:
            provider.resolve_video(video)
        message = str(ctx.exception)
        self.assertNotIn("googlevideo", message)
        self.assertNotIn("User-Agent", message)
        return ctx.exception

    def test_muxed_success_and_contract_fields(self):
        result, factory = self.resolve_success(muxed_result())
        self.assertEqual(set(result), {"version", "videoId", "url", "audioUrl", "headers",
                                       "hasVideo", "hasAudio", "qualityLabel", "resolvedAtMs",
                                       "expiresAtMs", "durationMs"})
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["videoId"], VIDEO_ID)
        self.assertEqual(result["url"], gv_url())
        self.assertEqual(result["audioUrl"], "")
        self.assertEqual(result["headers"], COMMON_HEADERS)
        self.assertIs(result["hasVideo"], True)
        self.assertIs(result["hasAudio"], True)
        self.assertEqual(result["qualityLabel"], "720p")
        self.assertEqual(result["durationMs"], 213000)
        self.assertEqual(result["expiresAtMs"], FUTURE_EXPIRE * 1000)
        self.assertIsInstance(result["resolvedAtMs"], int)
        self.assertLess(abs(result["resolvedAtMs"] - int(time.time() * 1000)), 60000)
        self.assertEqual(json.loads(json.dumps(result)), result)
        self.assertEqual(factory.calls, [{"url": WATCH_URL, "download": False}])

    def test_separate_streams_success(self):
        data = separate_result(video_headers=COMMON_HEADERS, audio_headers=COMMON_HEADERS,
                               audio_expire=FUTURE_EXPIRE - 1)
        result, _ = self.resolve_success(data)
        video_url = data["requested_formats"][0]["url"]
        audio_url = data["requested_formats"][1]["url"]
        self.assertEqual(result["url"], video_url)
        self.assertEqual(result["audioUrl"], audio_url)
        self.assertNotEqual(result["url"], result["audioUrl"])
        self.assertEqual(result["qualityLabel"], "1080p")
        self.assertEqual(result["headers"], COMMON_HEADERS)
        self.assertEqual(result["expiresAtMs"], (FUTURE_EXPIRE - 1) * 1000)
        self.assertEqual(result["durationMs"], 212500)

    def test_exact_extracted_id_required(self):
        self.assertResolutionFails(muxed_result(id="otherVideo1d"))
        swapped = separate_result()
        swapped["id"] = "0123456789a"
        self.assertResolutionFails(swapped)
        factory = self.install_yt_dlp(muxed_result(id="otherVideo1d"))
        with self.assertRaises(ValueError):
            provider.handle({"operation": "resolveVideo", "value": VIDEO_ID})
        self.assertEqual(factory.calls, [{"url": WATCH_URL, "download": False}])

    def test_audio_only_or_missing_streams_rejected(self):
        self.assertResolutionFails(muxed_result(vcodec="none"))
        self.assertResolutionFails(muxed_result(acodec="none"))
        no_codecs = muxed_result()
        del no_codecs["vcodec"]
        del no_codecs["acodec"]
        self.assertResolutionFails(no_codecs)
        two_audio = separate_result()
        two_audio["requested_formats"][0].update({"vcodec": "none", "acodec": "opus", "height": None})
        self.assertResolutionFails(two_audio)
        two_video = separate_result()
        two_video["requested_formats"][1].update({"vcodec": "avc1.640028", "acodec": "none", "height": 720})
        self.assertResolutionFails(two_video)
        three = separate_result()
        three["requested_formats"].append(dict(three["requested_formats"][1]))
        self.assertResolutionFails(three)
        muxed_plus_audio = separate_result()
        muxed_plus_audio["requested_formats"][0]["acodec"] = "mp4a.40.2"
        self.assertResolutionFails(muxed_plus_audio)
        junk = separate_result()
        junk["requested_formats"][1] = "https://rr5---sn-npoe7ney.googlevideo.com/videoplayback"
        self.assertResolutionFails(junk)
        missing_codec = separate_result()
        missing_codec["requested_formats"][1].pop("acodec")
        self.assertResolutionFails(missing_codec)

    def test_unsafe_or_invalid_urls_rejected(self):
        bad_urls = [
            "http://" + GV_HOST + "/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "https://user@" + GV_HOST + "/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "https://user:pw@" + GV_HOST + "/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "https://" + GV_HOST + ":8080/videoplayback?expire=" + str(FUTURE_EXPIRE),
            gv_url() + "#fragment",
            gv_url() + " ",
            gv_url() + "\r\nX-Injected: 1",
            "https://example.com/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "https://" + GV_HOST + ".evil.example/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "https://evilgooglevideo.com/videoplayback?expire=" + str(FUTURE_EXPIRE),
            "file://" + GV_HOST + "/videoplayback",
            "edl:" + gv_url(),
            gv_url() + "\x00",
            gv_url() + "\u00a0",
            gv_url() + "?duplicate=\t",
            "",
            5,
            None,
        ]
        for bad in bad_urls:
            with self.subTest(url=bad):
                self.assertResolutionFails(muxed_result(url=bad))
        separate_bad = separate_result(audio_url="http://" + GV_HOST + "/videoplayback")
        self.assertResolutionFails(separate_bad)

    def test_explicit_default_port_443_accepted(self):
        url_443 = "https://" + GV_HOST + ":443/videoplayback?expire=" + str(FUTURE_EXPIRE)
        result, _ = self.resolve_success(muxed_result(url=url_443))
        self.assertEqual(result["url"], url_443)

    def test_invalid_headers_rejected(self):
        bad_headers = [
            {"User-Agent": "A", "user-agent": "B"},
            {"User-Agent": "Vinyl/1.0\r\nX-Evil: injected"},
            {"User-Agent": "Vinyl/1.0\nX-Evil: injected"},
            {"Bad Header": "x"},
            {"User-Agent\x00": "x"},
            {"": "x"},
            {"User-Agent": 5},
            {"User-Agent": None},
            {"User-Agent": "   "},
            {"User-Agent": "ok\x00value"},
            {"User-Agent": "tab\tvalue"},
            {"User-Agent": "del\x7f"},
            "not-a-dict",
            {"X" + "a" * 200: "v"},
            {"User-Agent": "v" * 5000},
        ]
        for headers in bad_headers:
            with self.subTest(headers=headers):
                self.assertResolutionFails(muxed_result(http_headers=headers))
        too_many = {f"X-Header-{i}": str(i) for i in range(33)}
        self.assertResolutionFails(muxed_result(http_headers=too_many))

    def test_separate_header_agreement(self):
        bare = separate_result()
        result, _ = self.resolve_success(bare)
        self.assertEqual(result["headers"], {})
        mismatched = separate_result(video_headers={"User-Agent": "A"}, audio_headers={"User-Agent": "B"})
        self.assertResolutionFails(mismatched)
        orphan = separate_result(video_headers=COMMON_HEADERS)
        self.assertResolutionFails(orphan)
        inherited = separate_result(video_headers=COMMON_HEADERS, common_headers=COMMON_HEADERS)
        result, _ = self.resolve_success(inherited)
        self.assertEqual(result["headers"], COMMON_HEADERS)
        cased = separate_result(video_headers={"USER-AGENT": "same/1.0"},
                                audio_headers={"User-Agent": "same/1.0"})
        result, _ = self.resolve_success(cased)
        self.assertEqual(result["headers"], {"USER-AGENT": "same/1.0"})

    def test_expiry_earliest_or_absent(self):
        no_expire = muxed_result(url=f"https://{GV_HOST}/videoplayback?mn=sn&source=youtube")
        result, _ = self.resolve_success(no_expire)
        self.assertNotIn("expiresAtMs", result)
        duplicated = muxed_result(url=gv_url() + f"&expire={FUTURE_EXPIRE - 5}")
        result, _ = self.resolve_success(duplicated)
        self.assertEqual(result["expiresAtMs"], (FUTURE_EXPIRE - 5) * 1000)
        result, _ = self.resolve_success(separate_result(audio_expire=FUTURE_EXPIRE - 100))
        self.assertEqual(result["expiresAtMs"], (FUTURE_EXPIRE - 100) * 1000)

    def test_malformed_or_expired_expiry_rejected(self):
        for bad in ("abc", "", "NaN", "1e999", "0", "1000000000", "-5", "1440p"):
            with self.subTest(expire=bad):
                self.assertResolutionFails(muxed_result(url=gv_url(expire=bad)))

    def test_duration_ms_only_for_finite_positive_reals(self):
        for bad in (float("nan"), float("inf"), -1, 0, True, "213", None, [213]):
            with self.subTest(duration=bad):
                result, _ = self.resolve_success(muxed_result(duration=bad))
                self.assertNotIn("durationMs", result)
        result, _ = self.resolve_success(muxed_result(duration=212.5))
        self.assertEqual(result["durationMs"], 212500)
        result, _ = self.resolve_success(muxed_result(duration=60))
        self.assertEqual(result["durationMs"], 60000)

    def test_invalid_video_id_rejected_before_yt_dlp(self):
        with mock.patch.dict(sys.modules):
            sys.modules.pop("yt_dlp", None)
            for bad in ("", "short", VIDEO_ID + "x", VIDEO_ID + " ", "https://youtu.be/x",
                        "../etc/passwd", "--exec=calc", "ÿ" * 11, None, 5, {"id": VIDEO_ID}):
                with self.subTest(value=bad):
                    with self.assertRaises(ValueError):
                        provider.resolve_video(bad)

    def test_transport_configuration_is_bounded_and_anonymous(self):
        _, factory = self.resolve_success(muxed_result())
        self.assertEqual(len(factory.constructions), 1)
        options = factory.constructions[0]
        self.assertEqual(options["format"], EXPECTED_FORMAT)
        self.assertTrue(options["quiet"])
        self.assertTrue(options["no_warnings"])
        self.assertTrue(options["skip_download"])
        self.assertTrue(options["noplaylist"])
        self.assertEqual(options["socket_timeout"], 20)
        self.assertEqual(options["retries"], 1)
        self.assertEqual(options["extractor_retries"], 1)
        for forbidden in ("cookiefile", "cookies", "cookiejar", "cookiesfrombrowser",
                          "username", "password", "username2", "password2", "proxy"):
            self.assertNotIn(forbidden, options)
        logger = options["logger"]
        for method in ("debug", "warning", "error"):
            self.assertIsNone(getattr(logger, method)("ignored"))
        self.assertEqual(factory.calls, [{"url": WATCH_URL, "download": False}])

    def test_handle_dispatch_before_ytmusic_client(self):
        factory = self.install_yt_dlp(muxed_result())
        with mock.patch.dict(sys.modules):
            sys.modules.pop("ytmusicapi", None)
            result = provider.handle({"operation": "resolveVideo", "value": VIDEO_ID})
        self.assertEqual(result["videoId"], VIDEO_ID)
        self.assertTrue(result["hasVideo"])
        self.assertTrue(result["hasAudio"])
        self.assertEqual(factory.calls, [{"url": WATCH_URL, "download": False}])
        with self.assertRaises(ValueError):
            provider.handle({"operation": "resolveVideo", "value": "not-a-video-id"})
        self.assertEqual(len(factory.calls), 1)

    def test_live_or_playlist_outputs_rejected(self):
        self.assertResolutionFails(muxed_result(is_live=True))
        self.assertResolutionFails(muxed_result(live_status="is_live"))
        playlist = muxed_result()
        playlist["_type"] = "playlist"
        playlist["entries"] = []
        self.assertResolutionFails(playlist)
        self.assertResolutionFails(muxed_result(_type="multi_video"))
        self.assertResolutionFails(muxed_result(_type="url"))
        entries_only = muxed_result()
        del entries_only["_type"]
        entries_only["entries"] = [{}]
        self.assertResolutionFails(entries_only)
        self.assertResolutionFails("not-a-dict")

    def test_stream_transport_must_be_direct_https(self):
        hls_video = separate_result()
        hls_video["requested_formats"][0]["protocol"] = "m3u8_native"
        self.assertResolutionFails(hls_video)
        self.assertResolutionFails(muxed_result(protocol="m3u8"))
        self.assertResolutionFails(muxed_result(protocol="http"))
        no_protocol = muxed_result()
        no_protocol.pop("protocol")
        result, _ = self.resolve_success(no_protocol)
        self.assertTrue(result["url"].startswith("https://"))

    def test_video_height_is_bounded_and_observed(self):
        for bad in (1440, 2160, 0, -1080, True, None, "1080", float("nan")):
            with self.subTest(height=bad):
                self.assertResolutionFails(muxed_result(height=bad))
        tall = separate_result()
        tall["requested_formats"][0]["height"] = 1440
        self.assertResolutionFails(tall)
        result, _ = self.resolve_success(muxed_result(height=1080.0))
        self.assertEqual(result["qualityLabel"], "1080p")

    def test_existing_resolve_contract_unchanged(self):
        audio = {"id": VIDEO_ID, "url": gv_url(), "acodec": "opus", "abr": 128.3,
                 "http_headers": dict(COMMON_HEADERS)}
        factory = self.install_yt_dlp(audio)
        resolved = provider.resolve(VIDEO_ID)
        self.assertEqual(set(resolved), {"videoId", "url", "headers", "qualityLabel"})
        self.assertEqual(resolved["videoId"], VIDEO_ID)
        self.assertEqual(resolved["url"], audio["url"])
        self.assertEqual(resolved["headers"], COMMON_HEADERS)
        self.assertEqual(resolved["qualityLabel"], "OPUS · 128 kbps")
        self.assertEqual(factory.constructions[0]["format"], "bestaudio")
        self.assertEqual(factory.calls,
                         [{"url": "https://music.youtube.com/watch?v=" + VIDEO_ID, "download": False}])
        with mock.patch.dict(sys.modules):
            sys.modules.pop("yt_dlp", None)
            for bad in ("https://example.com", "--exec calc", "", "../foo"):
                with self.assertRaises(ValueError):
                    provider.resolve(bad)

    def test_output_is_json_safe(self):
        for data in (muxed_result(), separate_result()):
            result, _ = self.resolve_success(data)
            encoded = json.dumps(result, ensure_ascii=True)
            self.assertEqual(json.loads(encoded), result)
            for key in ("resolvedAtMs", "expiresAtMs", "durationMs"):
                self.assertIsInstance(result[key], int)
                self.assertNotIsInstance(result[key], bool)
            self.assertIsInstance(result["headers"], dict)
            self.assertTrue(all(isinstance(k, str) and isinstance(v, str)
                                for k, v in result["headers"].items()))


if __name__ == "__main__":
    unittest.main()
