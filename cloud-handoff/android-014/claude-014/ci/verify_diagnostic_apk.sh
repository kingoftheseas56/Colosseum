#!/usr/bin/env bash
# Task014: final checks on the signed Nuvio ARM64 diagnostic APK, after
# build_apk.sh's own correspondence / runtime bundle / OpenSSL gates.
#
# Usage: verify_diagnostic_apk.sh <apk> <expected-package> <engine-prefix> <out-dir>
# Requires the toolchain env.sh (ANDROID_SDK_ROOT, COLOSSEUM_QT_HOST_ROOT).
set -euo pipefail
APK="$1"; PKG="$2"; PREFIX="$3"; OUT="$4"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BT="$ANDROID_SDK_ROOT/build-tools/36.0.0"
V="$OUT/final-apk-verify.txt"
mkdir -p "$OUT"; : > "$V"
say() { echo "$*" | tee -a "$V"; }

say "source_commit=${GITHUB_SHA:-local} run_id=${GITHUB_RUN_ID:-local} run_attempt=${GITHUB_RUN_ATTEMPT:-0}"
say "apk_bytes=$(stat -c %s "$APK") apk_sha256=$(sha256sum "$APK" | cut -d' ' -f1)"
"$BT/aapt" dump badging "$APK" > "$OUT/aapt-badging.txt"
grep -E '^(package|sdkVersion|targetSdkVersion|native-code|launchable-activity)' "$OUT/aapt-badging.txt" | tee -a "$V"
grep -q "^package: name='$PKG'" "$OUT/aapt-badging.txt" || { say "FAIL package is not $PKG"; exit 1; }
grep -q "^native-code: 'arm64-v8a'$" "$OUT/aapt-badging.txt" || { say "FAIL native-code is not arm64-v8a only"; exit 1; }

"$BT/apksigner" verify --verbose --print-certs "$APK" > "$OUT/apksigner.txt"
grep -E '^(Verified using|Signer #1 certificate (DN|SHA-256))' "$OUT/apksigner.txt" | tee -a "$V"
grep -q '^Verified using v2 scheme (APK Signature Scheme v2): true' "$OUT/apksigner.txt" \
    || { say "FAIL apksigner v2"; exit 1; }
"$BT/zipalign" -c -P 16 4 "$APK" && say "zipalign_16k=OK"

X="$(mktemp -d)"
unzip -q "$APK" 'lib/*' -d "$X"
[[ "$(ls "$X/lib")" == "arm64-v8a" ]] || { say "FAIL lib ABIs: $(ls "$X/lib" | tr '\n' ' ')"; exit 1; }
count=0
for so in "$X"/lib/arm64-v8a/*.so; do
    readelf -h "$so" | grep -q 'Machine:.*AArch64' || { say "FAIL non-AArch64 ELF: ${so##*/}"; exit 1; }
    # 16 KiB page compatibility: every PT_LOAD segment aligned to >= 0x4000.
    loads=0
    while read -r align; do
        loads=$((loads + 1))
        (( align >= 16384 )) || { say "FAIL PT_LOAD alignment $align < 16 KiB: ${so##*/}"; exit 1; }
    done < <(readelf -lW "$so" | awk '$1=="LOAD"{ print $NF }' | while read -r h; do echo $((h)); done)
    (( loads > 0 )) || { say "FAIL no PT_LOAD segments: ${so##*/}"; exit 1; }
    count=$((count + 1))
done
say "elf_members=$count all_aarch64=OK all_load_align_16k=OK"

member=$(sha256sum "$X/lib/arm64-v8a/libnuvio_engine.so" | cut -d' ' -f1)
prefix=$(sha256sum "$PREFIX/android/arm64-v8a/lib/libnuvio_engine.so" | cut -d' ' -f1)
say "engine_member_sha256=$member"
say "engine_prefix_sha256=$prefix"
[[ "$member" == "$prefix" ]] || { say "FAIL engine member != stamp-verified prefix"; exit 1; }
rm -rf "$X"

python3 "$REPO/tests/verify_android_apk_runtime_bundle.py" "$APK" \
    --qmlcachegen "$COLOSSEUM_QT_HOST_ROOT/libexec/qmlcachegen" 2>&1 | tee -a "$V"
say "DIAGNOSTIC_APK_VERIFY_OK"
