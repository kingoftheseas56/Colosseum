#!/usr/bin/env bash
set -euo pipefail
: "${SOURCE:?}" "${OUT:?}" "${EVIDENCE:?}"
here=$(cd -- "$(dirname -- "$0")" && pwd)
exec > >(tee "$EVIDENCE/clean-runtime.log") 2>&1
archive="$OUT/Colosseum-1.1.7-linux-x86_64-candidate.tar.gz"
mkdir "$OUT/clean-extract" "$EVIDENCE/runtime"
(cd "$OUT" && sha256sum -c "$(basename "$archive").sha256")
tar -xzf "$archive" -C "$OUT/clean-extract"
appdir="$OUT/clean-extract/Colosseum-1.1.7-linux-x86_64"
python "$SOURCE/scripts/linux_runtime_dependency_gate.py" --appdir "$appdir"
# Long enough for real decoded-frame checks and a screenshot, not a network asset.
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=64x64:rate=24 \
  -t 120 -c:v libx264 -pix_fmt yuv420p "$OUT/playback-fixture.mp4"
docker build -f "$here/clean-runtime.Dockerfile" -t colosseum-117-runtime "$here"
docker image inspect colosseum-117-runtime > "$EVIDENCE/runtime-image.json"
# Only the extracted package, qualifier and fixture are mounted. No builder
# checkout, SDK, build directory, credentials or inherited environment enters.
sudo chown -R 1000:1000 "$EVIDENCE/runtime"
docker run --rm --cap-drop=ALL --security-opt=no-new-privileges --shm-size=1g \
  --mount "type=bind,source=$appdir,target=/opt/colosseum,readonly" \
  --mount "type=bind,source=$here/qualify.py,target=/qualify.py,readonly" \
  --mount "type=bind,source=$OUT/playback-fixture.mp4,target=/fixture.mp4,readonly" \
  --mount "type=bind,source=$EVIDENCE/runtime,target=/evidence" \
  colosseum-117-runtime \
  xvfb-run -a -s '-screen 0 1280x900x24' \
  python3 /qualify.py --appdir /opt/colosseum --evidence /evidence --fixture /fixture.mp4
