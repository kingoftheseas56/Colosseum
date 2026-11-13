"""Deterministic warm helper protocol and client-lifetime regressions (no network)."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import unittest
import types
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROVIDER = ROOT / "scripts/vinyl/provider.py"
spec = importlib.util.spec_from_file_location("vinyl_provider_server", PROVIDER)
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)


class FakeYoutubeDL:
    constructed = 0
    def __init__(self, options):
        self.options = options
        self.constructed += 1
        FakeYoutubeDL.constructed += 1
    def __enter__(self): return self
    def __exit__(self, *args): return False
    def extract_info(self, address, download=False):
        video = address.split("v=")[-1]
        if self.options["format"] == "bestaudio":
            return {"id": video, "url": "https://media.googlevideo.com/audio",
                    "acodec": "opus", "abr": 128, "http_headers": {"Referer": "https://www.youtube.com/"}}
        return {"id": video, "requested_formats": [
            {"url": "https://media.googlevideo.com/video", "vcodec": "av01",
             "acodec": "none", "height": 1080, "protocol": "https"},
            {"url": "https://media.googlevideo.com/audio", "vcodec": "none",
             "acodec": "opus", "protocol": "https"}]}


class WarmProviderTests(unittest.TestCase):
    def setUp(self):
        self.old_mode=getattr(provider,"_SERVER_MODE",False)
        provider._SERVER_MODE=True
        provider._SERVER_AUDIO_DL=None
        provider._SERVER_VIDEO_DL=None
        provider._SERVER_MUSIC_CLIENT=None
        provider._SERVER_MUSIC_REGION=None
        FakeYoutubeDL.constructed=0
        self.dependencies=mock.patch.dict(sys.modules, {"yt_dlp":types.SimpleNamespace(YoutubeDL=FakeYoutubeDL),
                                                       "ytmusicapi":types.SimpleNamespace(YTMusic=None)})
        self.dependencies.start()

    def tearDown(self):
        self.dependencies.stop()
        provider._SERVER_MODE=self.old_mode
        provider._SERVER_AUDIO_DL=None
        provider._SERVER_VIDEO_DL=None
        provider._SERVER_MUSIC_CLIENT=None
        provider._SERVER_MUSIC_REGION=None

    def test_exact_streams_reuse_per_format_without_substitution(self):
        with mock.patch("yt_dlp.YoutubeDL", FakeYoutubeDL):
            first=provider.resolve("Zi_XLOBDo_Y")
            next_track=provider.resolve("9bZkp7q19f0")
            video=provider.resolve_video("Zi_XLOBDo_Y")
            another_video=provider.resolve_video("9bZkp7q19f0")
            self.assertEqual(FakeYoutubeDL.constructed,2)
            self.assertEqual((first["videoId"],next_track["videoId"]),
                             ("Zi_XLOBDo_Y","9bZkp7q19f0"))
            self.assertEqual((video["videoId"],another_video["videoId"]),
                             ("Zi_XLOBDo_Y","9bZkp7q19f0"))
            self.assertEqual(video["qualityLabel"],"1080p")
            self.assertEqual(first["headers"]["Referer"],"https://www.youtube.com/")
            with self.assertRaises((ValueError,provider.InvalidArgumentError)):
                provider.resolve("../invalid")

    def test_warmup_initializes_both_clients_without_content_extraction(self):
        with mock.patch("yt_dlp.YoutubeDL", FakeYoutubeDL), mock.patch.object(FakeYoutubeDL, "extract_info", side_effect=AssertionError("warmup extracted content")):
            ready=provider.handle({"operation":"mediaWarmup","value":""})
            self.assertEqual(ready,{"ready":True,"audioReady":True,"videoReady":True})
            self.assertEqual(FakeYoutubeDL.constructed,2)
            provider.handle({"operation":"mediaWarmup","value":""})
            self.assertEqual(FakeYoutubeDL.constructed,2)
            with self.assertRaises(ValueError):
                provider.handle({"operation":"mediaWarmup","value":"Zi_XLOBDo_Y"})

    def test_oneshot_still_constructs_new_extractor(self):
        provider._SERVER_MODE=False
        with mock.patch("yt_dlp.YoutubeDL", FakeYoutubeDL):
            provider.resolve("Zi_XLOBDo_Y")
            provider.resolve("9bZkp7q19f0")
            self.assertEqual(FakeYoutubeDL.constructed,2)

    def test_catalogue_client_is_region_fenced(self):
        class FakeMusic:
            def __init__(self,**kwargs): self.kwargs=kwargs
        with mock.patch("ytmusicapi.YTMusic", FakeMusic):
            india=provider._ytmusic_client("IN")
            self.assertIs(india,provider._ytmusic_client("IN"))
            other=provider._ytmusic_client("US")
            self.assertIsNot(india,other)
            self.assertIs(other,provider._ytmusic_client("US"))

    def test_ndjson_recovers_from_invalid_request_without_live_provider(self):
        p=subprocess.Popen([sys.executable,"-I",str(PROVIDER),"--serve"],
                           stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for request in [
                {"operation":"resolve","value":"not-a-video-id"},
                {"operation":"unknown","value":"ignored"},
                {"operation":"resolveVideo","value":"another-invalid"}]:
                p.stdin.write(json.dumps(request).encode()+b"\n")
                p.stdin.flush()
                response=json.loads(p.stdout.readline())
                self.assertIs(response.get("ok"),False)
                self.assertFalse("url" in response)
                self.assertIsNone(p.poll())
            p.stdin.close()
            self.assertEqual(p.wait(timeout=10),0)
            self.assertFalse(p.stderr.read())
        finally:
            if p.poll() is None: p.kill();p.wait()
            p.stdout.close();p.stderr.close()
            if not p.stdin.closed:p.stdin.close()

    def test_one_shot_invalid_input_keeps_old_response_shape(self):
        result=subprocess.run([sys.executable,"-I",str(PROVIDER)],
                              input=b'{"operation":"resolve","value":"invalid"}',
                              capture_output=True,timeout=12,check=True)
        response=json.loads(result.stdout)
        self.assertEqual(response,{"ok":False,"error":"Invalid YouTube video ID"})

if __name__=="__main__":
    unittest.main()
