#!/usr/bin/env bash
set -euo pipefail
if [[ $EUID == 0 ]]; then echo "Run the installed Flatpak as an ordinary user" >&2; exit 1; fi
id=io.github.kingoftheseas56.Colosseum
ref="${COLOSSEUM_FLATPAK_REF:-$id//arc93-a}"
out="$(realpath "$1")"
proof="$HOME/.var/app/$id/data/arc93-proof"
mkdir -p "$proof" "$out"
install -m644 "$(dirname "$0")/PlayerProof.qml" "$proof/PlayerProof.qml"

ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=640x360:rate=25:duration=12' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=12' -map 0:v -map 1:a \
  -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -b:a 96k \
  -movflags +faststart -threads 1 -y "$proof/player-fixture.mp4"
sha256sum "$proof/player-fixture.mp4" | tee "$out/player-fixture-sha256.txt"
ffprobe -v error -show_entries stream=codec_type,codec_name:format=duration -of json \
  "$proof/player-fixture.mp4" > "$out/player-fixture-info.json"

pulseaudio --check || pulseaudio --start --exit-idle-time=-1
sink="arc93_player_$$"
module="$(pactl load-module module-null-sink "sink_name=$sink" rate=48000 channels=2)"
capture=""
pcm="$(mktemp)"
cleanup() {
  if [[ -n "$capture" ]]; then kill "$capture" 2>/dev/null || true; wait "$capture" 2>/dev/null || true; fi
  pactl unload-module "$module" 2>/dev/null || true
  rm -f "$pcm"
}
trap cleanup EXIT
pactl list short sinks > "$out/player-audio-sinks.txt"
parec --device="$sink.monitor" --raw --format=s16le --rate=48000 --channels=1 > "$pcm" 2> "$out/player-parec.log" &
capture=$!
kill -0 "$capture"
rm -f "$proof/player-frame.png"
timeout 135 flatpak run --env=QT_QPA_PLATFORM=xcb --env=QT_FORCE_STDERR_LOGGING=1 \
  --env=QSG_RHI_BACKEND=opengl --env=LIBGL_ALWAYS_SOFTWARE=1 \
  --env=PULSE_SINK="$sink" "--env=COLOSSEUM_APPDATA_TAG=${COLOSSEUM_FLATPAK_TEST_TAG:-arc93-feasibility}" \
  --env=COLOSSEUM_DISABLE_BOOT_PREFETCH=1 "$ref" "$proof/PlayerProof.qml" \
  "file://$proof/player-fixture.mp4" "$proof/player-frame.png" 2>&1 | tee "$out/player-runtime.log"
kill "$capture" 2>/dev/null || true
wait "$capture" 2>/dev/null || true
capture=""
test -s "$proof/player-frame.png"
cp "$proof/player-frame.png" "$out/"
python3 - "$out" "$pcm" <<'PY'
import array, json, math, pathlib, sys
out, pcm = map(pathlib.Path, sys.argv[1:])
marker = 'INSTALLED_PLAYER_PROOF='
rows = [json.loads(line.split(marker, 1)[1]) for line in (out/'player-runtime.log').read_text().splitlines() if marker in line]
assert len(rows) == 1 and rows[0]['status'] == 'PASS' and rows[0]['exit_code'] == 0, rows
player = rows[0]['player']
assert all(player[key] is True for key in ('loaderReady', 'playerReady', 'playbackStarted')), player
assert player['decodedWidth'] > 0 and player['decodedHeight'] > 0
assert player['audioChannels'] > 0 and player['audioBitrate'] > 0
assert player['position'] > player['firstPosition'] + 0.2
assert player['seekTarget'] > player['position'] + 1.5
assert abs(player['positionAfterSeek'] - player['seekTarget']) <= 0.6
assert player['positionAfterResume'] > player['positionAfterSeek'] + 0.2
pic = (out/'player-frame.png').read_bytes()
assert len(pic) > 1024 and pic.startswith(b'\x89PNG\r\n\x1a\n')
raw = pcm.read_bytes()
samples = array.array('h', raw[:len(raw)//2*2])
if sys.byteorder != 'little':
    samples.byteswap()
rate = 48000
matching_windows = []
width = rate // 10
for index in range(0, len(samples) - width + 1, width):
    block = samples[index:index+width]
    rms = math.sqrt(sum(x*x for x in block) / width)
    upward_crossings = sum(a <= 0 < b for a, b in zip(block, block[1:]))
    if rms > 80 and 42 <= upward_crossings <= 46:
        matching_windows.append(round(rms, 1))
audio = {'sample_rate_hz': rate, 'pcm_samples': len(samples), 'tone_hz': 440,
         'minimum_observed_tone_seconds': len(matching_windows)/10,
         'peak_matching_window_rms': max(matching_windows, default=0),
         'output': 'PulseAudio isolated software sink monitor; no physical speaker or GPU claim'}
(out/'player-proof.json').write_text(json.dumps({'player_proof': rows[0], 'audio_monitor': audio}, indent=2)+'\n')
assert len(matching_windows) >= 4, audio
print('ARC93_A_INSTALLED_PLAYER1_AV_SEEK_PASS')
PY
