# Feria native player checks

The ordinary contract test has no network or mpv playback requirement. Build
`feria_native_player_contract` through the application's CMake graph, or configure
this directory separately with Qt 6.11 (Quick, WebEngineQuick, WebChannel, Test),
MpvQt and libmpv in `CMAKE_PREFIX_PATH`/`Libmpv_*`.

The runtime executable takes three arguments: absolute scene path, entry URL,
and an existing evidence directory. Run on a graphical desktop using the same
Qt/MpvQt/libmpv DLLs as the build; do not disable Chromium's sandbox. Each scene
writes `outcomes.json` and pictures. A process exit alone is not a picture check.

For `Smoke.qml`, generate these fixtures outside the checkout with ffmpeg:

```sh
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=24 -f lavfi -i sine=frequency=440 -t 36 -c:v libx264 -pix_fmt yuv420p -c:a aac h264-aac.mp4
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=24 -f lavfi -i sine=frequency=660 -t 12 -c:v ffv1 -c:a pcm_s16le ffv1-pcm.mkv
python serve.py /absolute/fixture-directory
```

Use the printed loopback URL as the entry URL. The scene tests saved resume,
trusted HTML play/pause, seek, handback, hidden-tab rejection, direct FFV1
decoding, stale sessions, navigation and closing.

`YouTube.qml` uses a real public watch URL, for example
`https://www.youtube.com/watch?v=aqz-KE-bpKQ` or
`https://www.youtube.com/watch?v=jNQXAC9IVRw`. Install an official local yt-dlp
release and a supported JavaScript runtime (Node or Deno). Put yt-dlp in the
application directory, its `tools` directory, or PATH; alternatively set
`COLOSSEUM_YTDLP_PATH` to its absolute path. Node must be in PATH. The resolver
ignores user configuration and transfers no browser cookies. Packaging these
optional tools remains separate from this integration.

`AppYouTube.qml` runs through the assembled Colosseum executable, whose arguments
are scene, evidence directory, watch URL. Use a disposable `COLOSSEUM_APPDATA_TAG`
and a unique `COLOSSEUM_LANISTA_PIPE`. Its product log must contain
`FERIA_APP_YOUTUBE_PASS`; inspect `app-youtube.png` as well.

`Drm.qml` uses the existing licensed, codec-enabled Qt runtime. Entry URLs are
`https://bitmovin.com/demos/drm` and `https://demo.castlabs.com/wv-vmp-lab/`.
Append `#production` for the Castlabs production proxy. It checks encrypted
browser playback and refusal to hand encrypted media to mpv. It does not test
DRM decoding through mpv, Netflix/Prime compatibility, or redistribution rights.
