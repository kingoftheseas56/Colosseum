#!/usr/bin/env bash
set -euo pipefail
id=io.github.kingoftheseas56.Colosseum
ref="$id//arc93-a"
out="$(realpath "$1")"
proof="$HOME/.var/app/$id/data/arc93-proof"
mkdir -p "$out" "$proof"
flatpak info --user "$ref" > "$out/installed-info.txt"
flatpak info --user --show-permissions "$ref" > "$out/permissions.txt"
flatpak run --command=sh "$ref" -c 'set -e; cat /app/share/colosseum/SOURCE-COMMIT; ldd /app/share/colosseum/usr/bin/colosseum; ldd /app/share/colosseum/usr/bin/colosseum-nuvio-host; for t in ffmpeg ffprobe; do command -v "$t" || echo "MISSING_RUNTIME_TOOL=$t"; done; ls -ld /usr/lib/qml/QtQuick3D /app/lib/qml/QtQuick3D 2>/dev/null || true' > "$out/installed-closure.txt"
if grep -q 'not found' "$out/installed-closure.txt"; then exit 1; fi
flatpak run "$ref" --version | tee "$out/version.txt"
grep -Fx 'Colosseum 1.5.0' "$out/version.txt"
printf '%s\n' '<!doctype html><h1 id="proof">Colosseum WebEngine qualification 130</h1>' > "$proof/index.html"
python3 -m http.server 18793 --bind 127.0.0.1 --directory "$proof" > "$out/http.log" 2>&1 &
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for phase in first restart; do
  timeout 140 flatpak run --env=QT_QPA_PLATFORM=xcb --env=QT_FORCE_STDERR_LOGGING=1 \
    --env=QSG_RHI_BACKEND=opengl --env=LIBGL_ALWAYS_SOFTWARE=1 \
    --env=COLOSSEUM_APPDATA_TAG=arc93-feasibility --env=COLOSSEUM_DISABLE_BOOT_PREFETCH=1 \
    "$ref" /app/share/colosseum/proofs/RuntimeProof.qml "$phase" \
    "$proof/home-$phase.png" "$proof/web-$phase.png" http://127.0.0.1:18793/index.html \
    2>&1 | tee "$out/runtime-$phase.log"
  grep -F 'INSTALLED_RUNTIME_PROOF=' "$out/runtime-$phase.log"
  for frame in home web; do test -s "$proof/$frame-$phase.png"; cp "$proof/$frame-$phase.png" "$out/"; done
done
python3 - "$out" <<'PY'
import json, pathlib, sys
p=pathlib.Path(sys.argv[1])
for phase in ("first", "restart"):
    rows=[json.loads(x.split("INSTALLED_RUNTIME_PROOF=",1)[1]) for x in (p/f"runtime-{phase}.log").read_text().splitlines() if "INSTALLED_RUNTIME_PROOF=" in x]
    assert len(rows)==1, rows
    r=rows[0]
    assert r["phase"]==phase and r["webResult"]=="Colosseum WebEngine qualification 130"
    assert r["mode"]=="localOnly" and r["onboardingRequired"] is False
    if phase=="restart": assert r["profileRetained"] is True
    for frame in ("home","web"):
        b=(p/f"{frame}-{phase}.png").read_bytes()
        assert b.startswith(b"\x89PNG\r\n\x1a\n") and len(b)>1024
PY
# This is an mpv decode smoke, not Colosseum Player 1 or audible-output acceptance.
flatpak run --command=sh "$ref" -c 'test -s /app/share/colosseum/proofs/Colosseum.mp4; exec mpv --no-config --vo=null --ao=null --frames=30 --end=2 /app/share/colosseum/proofs/Colosseum.mp4' 2>&1 | tee "$out/mpv-decode.log"
grep -q 'Video:' "$out/mpv-decode.log"
echo "ARC93_A_RUNTIME_SMOKE_PASS (software display, null audio/video outputs; real Player 1 still unqualified)"
