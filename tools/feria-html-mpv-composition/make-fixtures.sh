#!/usr/bin/env bash
# Synthetic, unencrypted media only. No external provider or downloaded media.
set -euo pipefail
OUT="${1:?Usage: make-fixtures.sh OUTPUT_DIRECTORY}"
mkdir -p "$OUT"
ffmpeg -hide_banner -y -f lavfi -i 'testsrc2=size=640x360:rate=24:duration=12' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=12' \
  -c:v libx264 -preset fast -crf 20 -pix_fmt yuv420p -c:a aac -b:a 128k \
  -movflags +faststart -shortest "$OUT/h264-aac.mp4"
ffmpeg -hide_banner -y -i "$OUT/h264-aac.mp4" -c:v ffv1 -level 3 \
  -c:a pcm_s16le "$OUT/ffv1-pcm.mkv"
sha256sum "$OUT/h264-aac.mp4" "$OUT/ffv1-pcm.mkv"
