from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST_PATH = (
    ROOT / "native" / "platform" / "android" / "src"
    / "org" / "colosseum" / "player" / "Media3PlayerHost.java"
)


def host_text() -> str:
    if not HOST_PATH.exists():
        raise AssertionError(f"Media3 Java host is missing: {HOST_PATH}")
    return HOST_PATH.read_text(encoding="utf-8")


class AndroidMedia3JavaHostContract(unittest.TestCase):
    def test_host_is_instance_owned_and_main_looper_bound(self):
        text = host_text()
        self.assertIn("private final ExoPlayer player;", text)
        self.assertNotRegex(text, r"\bstatic\s+(?:final\s+)?ExoPlayer\b")
        self.assertIn("Looper.getMainLooper()", text)
        self.assertIn(".setLooper(Looper.getMainLooper())", text)
        self.assertIn("runOnMain(", text)

    def test_per_load_source_factories_and_header_isolation(self):
        text = host_text()
        self.assertIn(
            "public void load(long generation, String sourceUrl, Map<String, String> headers)",
            text,
        )
        self.assertIn("new LinkedHashMap<>()", text)
        self.assertIn("Collections.unmodifiableMap", text)
        self.assertIn("new DefaultHttpDataSource.Factory()", text)
        self.assertIn(".setAllowCrossProtocolRedirects(false)", text)
        self.assertIn(".setDefaultRequestProperties(safeHeaders)", text)
        self.assertIn("new DefaultDataSource.Factory(appContext, httpFactory)", text)
        self.assertIn("new DefaultMediaSourceFactory(dataSourceFactory)", text)
        self.assertNotIn("setAllowCrossProtocolRedirects(true)", text)
        self.assertIn('contains("\\r")', text)
        self.assertIn('contains("\\n")', text)

    def test_uri_policy_is_defense_in_depth(self):
        text = host_text()
        for token in (
            '"https"', '"http"', '"127.0.0.1"', '"content"', '"file"',
            "isForbiddenPrivateHost", "isAllowedSource",
        ):
            self.assertIn(token, text)
        self.assertIn("uri.getPort()", text)
        self.assertIn('normalized.equals("localhost")', text)
        self.assertIn('normalized.endsWith(".localhost")', text)
        self.assertIn('normalized.endsWith(".local")', text)
        self.assertIn("isPrivateIpv4Literal(normalized)", text)
        self.assertIn("isPrivateIpv6Literal(normalized)", text)
        self.assertIn("resolvesToPrivateAddress(normalized)", text)

    def test_player_configuration_uses_media3_focus_noisy_and_decoder_fallback(self):
        text = host_text()
        self.assertIn("new DefaultRenderersFactory(appContext)", text)
        self.assertIn(".setEnableDecoderFallback(true)", text)
        self.assertIn(".setAudioAttributes(audioAttributes, true)", text)
        self.assertIn(".setHandleAudioBecomingNoisy(true)", text)
        self.assertNotIn("requestAudioFocus", text)

    def test_core_commands_and_surface_seam_are_main_marshaled(self):
        text = host_text()
        for signature in (
            "public void play()", "public void pause()", "public void stopAndClear()",
            "public void seekTo(long positionMs)", "public void setVolume(float volume)",
            "public void setMuted(boolean muted)", "public void setSpeed(float speed)",
            "public void setVideoSurface(Surface surface)", "public void clearVideoSurface()",
            "public void release()",
        ):
            with self.subTest(signature=signature):
                self.assertIn(signature, text)
        for call in (
            "player.play()", "player.pause()", "player.stop()", "player.clearMediaItems()",
            "player.seekTo(", "player.setVolume(", "player.setPlaybackSpeed(",
            "player.setVideoSurface(", "player.clearVideoSurface()", "player.release()",
        ):
            with self.subTest(call=call):
                self.assertIn(call, text)
        self.assertIn("public void clearVideoSurface(Surface surface)", text)
        self.assertIn("player.clearVideoSurface(surface)", text)
        self.assertRegex(text, r"public void clearVideoSurface\(Surface surface\)\s*\{\s*runOnMainSync\(")
        self.assertRegex(text, r"public void release\(\)\s*\{\s*runOnMainSync\(")

    def test_generation_tagged_callbacks_cover_required_events(self):
        text = host_text()
        callbacks = (
            "nativeOnReady", "nativeOnEnded", "nativeOnError", "nativeOnTimeline",
            "nativeOnVideoSize", "nativeOnFirstFrame", "nativeOnSeekDiscontinuity",
            "nativeOnTracks", "nativeOnMetadata", "nativeOnCues",
        )
        for callback in callbacks:
            with self.subTest(callback=callback):
                self.assertRegex(
                    text,
                    rf"{callback}\s*\(\s*long\s+nativeHandle\s*,\s*long\s+generation",
                )
        for listener in (
            "onPlaybackStateChanged(int playbackState)",
            "onPlayerError(PlaybackException error)",
            "onTimelineChanged(Timeline timeline, int reason)",
            "onVideoSizeChanged(VideoSize videoSize)",
            "onRenderedFirstFrame()", "onPositionDiscontinuity(",
            "onTracksChanged(Tracks tracks)",
            "onMediaMetadataChanged(MediaMetadata mediaMetadata)",
            "onCues(CueGroup cueGroup)",
        ):
            self.assertIn(listener, text)

    def test_error_policy_is_bounded_and_ui_classes_are_forbidden(self):
        text = host_text()
        for family in (
            '"network"', '"source"', '"decoder"',
            '"audio"', '"drm"', '"unexpected"',
        ):
            self.assertIn(family, text)
        for forbidden in (
            "PlayerView", "SurfaceView", "TextureView", "requestAudioFocus",
            "getClass().getName()", "setAllowCrossProtocolRedirects(true)",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, text)
        self.assertNotRegex(text, r"\bstatic\s+(?:final\s+)?ExoPlayer\b")


if __name__ == "__main__":
    unittest.main()
