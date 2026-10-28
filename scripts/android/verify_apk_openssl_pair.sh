#!/usr/bin/env bash
set -euo pipefail

apk="${1:?usage: verify_apk_openssl_pair.sh <apk> <abi>}"
abi="${2:?usage: verify_apk_openssl_pair.sh <apk> <abi>}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

[[ -f "$apk" ]] || { echo "[apk-openssl] APK missing: $apk" >&2; exit 2; }
tmp="$(mktemp -d "${TMPDIR:-/tmp}/colosseum-apk-openssl.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

python3 - "$apk" "$abi" "$tmp" <<'PY'
import sys, zipfile
from pathlib import Path
apk, abi, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
names = [f"lib/{abi}/libcrypto_3.so", f"lib/{abi}/libssl_3.so"]
with zipfile.ZipFile(apk) as z:
    entries = set(z.namelist())
    missing = [name for name in names if name not in entries]
    if missing:
        raise SystemExit("missing OpenSSL runtime(s) in APK: " + ", ".join(missing))
    for name in names:
        target = out / Path(name).name
        target.write_bytes(z.read(name))
PY

bash "$repo/scripts/android/verify_openssl_runtime_pair.sh" "$tmp"
echo "APK_OPENSSL_RUNTIME_PAIR_OK apk=$apk abi=$abi"
