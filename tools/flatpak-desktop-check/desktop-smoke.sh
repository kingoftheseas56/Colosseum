#!/usr/bin/env bash
# Run in an isolated X11/D-Bus session with the Arc 93 A candidate installed.
# A normal /run/user/$UID runtime directory is required for document portals.
set -euo pipefail
out="$(realpath "$1")"
id=io.github.kingoftheseas56.Colosseum
ref="$id//arc93-a"
export XDG_DATA_DIRS="$HOME/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export XDG_CURRENT_DESKTOP=XFCE
dbus-update-activation-environment DISPLAY XAUTHORITY XDG_CURRENT_DESKTOP XDG_DATA_DIRS
xfwm4 --compositor=off > "$out/window-manager.log" 2>&1 &
capture=""
monitor=""
audio_module=""
previous_sink=""
fixture=""
cleanup() {
  import -window root "$out/final-desktop.png" 2>/dev/null || true
  xdotool search --onlyvisible --name '.*' getwindowname > "$out/final-window-names.txt" 2>/dev/null || true
  if [[ -n "$capture" ]]; then kill "$capture" 2>/dev/null || true; wait "$capture" 2>/dev/null || true; fi
  if [[ -n "$monitor" ]]; then kill "$monitor" 2>/dev/null || true; fi
  if [[ -n "$previous_sink" ]]; then pactl set-default-sink "$previous_sink" 2>/dev/null || true; fi
  if [[ -n "$audio_module" ]]; then pactl unload-module "$audio_module" 2>/dev/null || true; fi
  if [[ -n "$fixture" ]]; then rm -f -- "$fixture"; fi
  flatpak kill "$ref" 2>/dev/null || true
}
trap cleanup EXIT

# Reproduce existing checks unless resuming the desktop part after those checks passed.
if [[ ${2:-} != --desktop-only ]]; then
  bash tools/flatpak-feasibility/smoke.sh "$out" > "$out/repeated-runtime.log" 2>&1
fi
flatpak override --user --env=QT_QPA_PLATFORM=xcb --env=QSG_RHI_BACKEND=opengl \
  --env=LIBGL_ALWAYS_SOFTWARE=1 --env=COLOSSEUM_APPDATA_TAG=arc93-feasibility \
  --env=COLOSSEUM_DISABLE_BOOT_PREFETCH=1 "$id"
flatpak run --command=sh "$ref" -c 'cat /app/share/colosseum/SOURCE-COMMIT; cat /app/share/applications/io.github.kingoftheseas56.Colosseum.desktop' > "$out/desktop-entry.txt"
printf 'host_runtime_dir=%s\n' "$XDG_RUNTIME_DIR" > "$out/session-paths.txt"
flatpak run --command=sh "$ref" -c 'printf "sandbox_runtime_dir=%s\n" "$XDG_RUNTIME_DIR"' >> "$out/session-paths.txt"

# The file starts outside the sandbox's declared Downloads access.
mkdir -p "$HOME/Videos"
fixture="$HOME/Videos/Flatpak-desktop-test-$$.mp4"
ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=640x360:rate=25:duration=90' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=90' -map 0:v -map 1:a \
  -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac -threads 1 -y "$fixture"
if flatpak run --command=sh "$ref" -c 'test -r "$1"' sh "$fixture"; then
  echo 'FAIL: fixture readable before the portal grant' >&2
  exit 1
fi
echo 'PASS: fixture not readable before file selection' > "$out/portal-access.txt"

dbus-monitor --session "interface='org.freedesktop.portal.FileChooser'" > "$out/file-portal.log" 2>&1 &
monitor=$!
pulseaudio --check || pulseaudio --start --exit-idle-time=-1
previous_sink="$(pactl get-default-sink)"
sink="flatpak_desktop_check_$$"
audio_module="$(pactl load-module module-null-sink "sink_name=$sink" rate=48000 channels=2)"
printf '%s\n' "$audio_module" > "$out/audio-module.txt"
pactl set-default-sink "$sink"
parec --device="$sink.monitor" --raw --format=s16le --rate=48000 --channels=1 > "$out/desktop-audio.pcm" 2> "$out/audio-capture.log" &
capture=$!

# Use the graphical application finder and the exported desktop entry, with no QML driver.
xfce4-appfinder --disable-server > "$out/launcher-app.log" 2>&1 &
for _ in $(seq 1 40); do
  finder="$(xdotool search --onlyvisible --name 'Application Finder' 2>/dev/null | head -1 || true)"
  [[ -n "$finder" ]] && break
  sleep 0.5
done
test -n "$finder"
xdotool windowactivate --sync "$finder"
xdotool type --clearmodifiers --delay 80 'Colosseum'
sleep 2
import -window root "$out/application-finder.png"
xdotool key --clearmodifiers Return
app=""
for _ in $(seq 1 120); do
  app="$(xdotool search --onlyvisible --class '[Cc]olosseum' 2>/dev/null | head -1 || true)"
  [[ -n "$app" ]] && break
  sleep 0.5
done
test -n "$app"
sleep 12
import -window root "$out/desktop-home.png"
xdotool windowactivate --sync "$app"
xdotool key --clearmodifiers ctrl+o
picker=""
for _ in $(seq 1 60); do
  picker="$(xdotool search --onlyvisible --name '^Open Media$' 2>/dev/null | head -1 || true)"
  [[ -n "$picker" ]] && break
  sleep 0.5
done
test -n "$picker"
import -window root "$out/native-file-picker.png"
xdotool windowactivate --sync "$picker"
xdotool key --clearmodifiers ctrl+l
xdotool type --clearmodifiers --delay 20 "$fixture"
xdotool key --clearmodifiers Return
sleep 2
if xdotool search --onlyvisible --name '^Open Media$' >/dev/null 2>&1; then
  xdotool key --clearmodifiers Return
fi
sleep 10
import -window root "$out/after-file-selection.png"
grep -q 'member=OpenFile' "$out/file-portal.log"
echo 'PASS: real application Ctrl+O called the file portal' >> "$out/portal-access.txt"
flatpak document-list > "$out/document-grants.txt"
sleep 3
import -window root "$out/playback-later.png"
kill "$capture"
wait "$capture" 2>/dev/null || true
capture=""
python3 - "$out" <<'PY'
import array, json, math, pathlib, platform, sys
out = pathlib.Path(sys.argv[1])
raw = (out/'desktop-audio.pcm').read_bytes()
samples = array.array('h', raw[:len(raw)//2*2])
if sys.byteorder != 'little': samples.byteswap()
width = 4800
hits = 0
for start in range(0, len(samples)-width+1, width):
    block = samples[start:start+width]
    rms = math.sqrt(sum(x*x for x in block)/width)
    crossings = sum(a <= 0 < b for a,b in zip(block, block[1:]))
    hits += rms > 80 and 42 <= crossings <= 46
result = {'desktop_entry_launch': True, 'file_portal_called': True,
          'fixture_outside_declared_filesystem': True, 'observed_tone_seconds': hits/10,
          'environment': platform.platform() + '; Xvfb, XFWM, software OpenGL and PulseAudio sink',
          'physical_gpu_and_speakers_tested': False}
(out/'desktop-proof.json').write_text(json.dumps(result, indent=2)+'\n')
assert hits >= 4, result
print('FLATPAK_DESKTOP_LAUNCH_PORTAL_AUDIO_PASS')
PY
rm "$out/desktop-audio.pcm"
