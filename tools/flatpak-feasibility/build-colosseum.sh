#!/usr/bin/env bash
set -euo pipefail
pin=0f6c3c9c2d799b1ac2889806a238fa6bea3a41ce
if [[ -d .git ]]; then test "$(git rev-parse HEAD)" = "$pin"; fi
cmake -S native -B arc93-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCOLOSSEUM_BUILD_PLAYER2=OFF -DCOLOSSEUM_PLAYER2_IN_APP=OFF \
  -DCOLOSSEUM_NO_ACCOUNT_EDITION=OFF -DCOLOSSEUM_UPDATE_TESTING=OFF \
  -DCOLOSSEUM_FERIA_IN_APP=OFF -DCOLOSSEUM_VINYL_IN_APP=OFF \
  -DCOLOSSEUM_ENABLE_VINYL_QUALIFICATION=OFF -DCOLOSSEUM_ENABLE_VINYL_TEST_FIXTURES=OFF \
  -DCOLOSSEUM_CONTENT_API_URL=https://colosseum-content-api.colosseum-watchparty-relay.workers.dev \
  -DCMAKE_PREFIX_PATH=/app
cmake --build arc93-build --target colosseum_runtime_ready --parallel 2
root=/app/share/colosseum
mkdir -p "$root/usr/bin" "$root/proofs" /app/bin
install -m755 arc93-build/colosseum arc93-build/colosseum-nuvio-host "$root/usr/bin/"
install -m644 arc93-build/qml-build.manifest "$root/usr/bin/"
cp -a qml assets resources data "$root/"
python3 scripts/stage_api_content.py --stage "$root" --apply | tee "$root/content-stage.json"
for tool in ffmpeg ffprobe; do
  if command -v "$tool" >/dev/null; then ln -s "$(command -v "$tool")" "$root/usr/bin/$tool"; else echo "MISSING_RUNTIME_TOOL=$tool"; fi
done
printf '%s\n' '#!/bin/sh' 'cd /app/share/colosseum' 'exec /app/share/colosseum/usr/bin/colosseum "$@"' > /app/bin/colosseum
chmod 755 /app/bin/colosseum
sed -e 's@file:///opt/colosseum/qml@file:///app/share/colosseum/qml@' \
    -e 's@app.proofStage = 5@app.proofStage = 5; Qt.callLater(function() { Qt.exit(0) })@' \
    tools/linux-release/InstalledRuntimeProof.qml > "$root/proofs/RuntimeProof.qml"
if [[ -f tests/lanista_capture/assets/Colosseum.mp4 ]]; then
  install -m644 tests/lanista_capture/assets/Colosseum.mp4 "$root/proofs/Colosseum.mp4"
fi
printf '%s\n' "$pin" > "$root/SOURCE-COMMIT"
mkdir -p /app/share/licenses/io.github.kingoftheseas56.Colosseum
cp LICENSE THIRD_PARTY_NOTICES.md /app/share/licenses/io.github.kingoftheseas56.Colosseum/
cp -r native/nuvio/vendor/js/licenses /app/share/licenses/io.github.kingoftheseas56.Colosseum/Nuvio-JS
install -m644 native/nuvio/vendor/quickjs/LICENSE /app/share/licenses/io.github.kingoftheseas56.Colosseum/QuickJS-LICENSE
mkdir -p /app/share/applications
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Colosseum (Arc 93 feasibility)' 'Exec=colosseum' 'Categories=AudioVideo;' > /app/share/applications/io.github.kingoftheseas56.Colosseum.desktop
