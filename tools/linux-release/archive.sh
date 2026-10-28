#!/bin/bash
# Use only after final package checks; no binary changes after GUI verification.
set -euo pipefail
[ "$#" -eq 2 ] || { echo 'Usage: archive.sh STAGE OUTPUT.tar.gz' >&2; exit 2; }
[ -d "$1" ] || { echo 'Stage directory does not exist' >&2; exit 2; }
STAGE=$(realpath "$1")
OUT=$(realpath -m "$2")
[ -f "$STAGE/BUILD-IDENTITY.json" ] && [ -x "$STAGE/AppRun" ] && [ -x "$STAGE/usr/bin/colosseum" ] || { echo 'Stage is not an assembled candidate' >&2; exit 2; }
[ ! -e "$OUT" ] && [ ! -e "$OUT.sha256" ] || { echo 'Refusing to overwrite existing archive or checksum' >&2; exit 2; }
TMP=$(mktemp "${OUT}.partial.XXXXXX")
trap 'rm -f -- "$TMP"' EXIT
# pipefail prevents a failed tar from becoming a successfully checksummed empty gzip.
tar --sort=name --mtime='2026-10-09 00:00:00Z' --owner=0 --group=0 --numeric-owner -C "$(dirname "$STAGE")" -cf - "$(basename "$STAGE")" | gzip -n -6 > "$TMP"
gzip -t "$TMP"
mv -- "$TMP" "$OUT"
sha256sum "$OUT" > "$OUT.sha256"
wc -c "$OUT"
