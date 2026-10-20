#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 PACKAGE.zip x86_64|aarch64" >&2
    exit 2
fi
archive=$1
target_architecture=$2
engine_root=/source
case "$target_architecture" in
    x86_64) target_triple=x86_64-w64-mingw32; expected_machine='IMAGE_FILE_MACHINE_AMD64' ;;
    aarch64) target_triple=aarch64-w64-mingw32; expected_machine='IMAGE_FILE_MACHINE_ARM64' ;;
    *) echo "unsupported Windows architecture: $target_architecture" >&2; exit 2 ;;
esac
if ! (
    cd "$(dirname "$archive")"
    sha256sum --check "$(basename "$archive").sha256"
); then
    exit 1
fi

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
unzip -q "$archive" -d "$temporary"
package="$temporary/nuvio-engine-windows-$target_architecture"
library="$package/bin/nuvio_engine.dll"
readobj=/opt/llvm-mingw/bin/llvm-readobj
strings_tool=/opt/llvm-mingw/bin/llvm-strings
fail() {
    echo "Windows package verification failed: $*" >&2
    exit 1
}

[[ -f "$library" ]] || fail "DLL is missing"
[[ -f "$package/BUILD-INFO.txt" ]] || fail "build metadata is missing"
engine_version=$(sed -n 's/^Nuvio Engine: //p' "$package/BUILD-INFO.txt")
[[ "$engine_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "invalid engine version metadata"
[[ -f "$package/lib/libnuvio_engine.dll.a" ]] || fail "import library is missing"
cmp -s \
    "$package/lib/nuvio_engine.def" \
    "$engine_root/cmake/nuvio-engine-windows.def" || fail "export definition mismatch"
[[ -f "$package/tools/nuvio_engine_smoke.exe" ]] || fail "smoke executable is missing"
cmp -s \
    "$package/include/nuvio_engine/nuvio_engine.h" \
    "$engine_root/include/nuvio_engine/nuvio_engine.h" || fail "C header mismatch"
cmp -s \
    "$package/include/nuvio_engine/export.h" \
    "$engine_root/include/nuvio_engine/export.h" || fail "export header mismatch"
cmp -s "$package/README.md" "$engine_root/README.md" || fail "README mismatch"

headers=$($readobj --file-headers "$library")
grep -Fq "$expected_machine" <<< "$headers" || fail "wrong PE machine"
for mitigation in IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA IMAGE_DLL_CHARACTERISTICS_NX_COMPAT IMAGE_DLL_CHARACTERISTICS_GUARD_CF; do
    grep -Fq "$mitigation" <<< "$headers" || fail "missing PE mitigation $mitigation"
done

expected_symbols=$(cat <<'EOF'
nuvio_engine_add_torrent
nuvio_engine_api_version
nuvio_engine_config_init_sized
nuvio_engine_create
nuvio_engine_destroy
nuvio_engine_event_init_sized
nuvio_engine_file_init_sized
nuvio_engine_get_file
nuvio_engine_get_file_count
nuvio_engine_get_stats
nuvio_engine_get_stream_stats
nuvio_engine_poll_event
nuvio_engine_prepare_stream
nuvio_engine_protocol_backend_version
nuvio_engine_reclaim_disk_cache
nuvio_engine_remove_torrent
nuvio_engine_stats_init_sized
nuvio_engine_status_message
nuvio_engine_stop_stream
nuvio_engine_stream_request_init_sized
nuvio_engine_stream_stats_init_sized
nuvio_engine_torrent_request_init_sized
nuvio_engine_version_string
EOF
)
actual_symbols=$(
    $readobj --coff-exports "$library" \
        | sed -n 's/^[[:space:]]*Name: //p' \
        | LC_ALL=C sort
)
if [[ "$actual_symbols" != "$expected_symbols" ]]; then
    diff -u <(printf '%s\n' "$expected_symbols") <(printf '%s\n' "$actual_symbols") || true
    fail "DLL export surface differs from the C ABI"
fi

imports=$(
    $readobj --coff-imports "$library" \
        | sed -n 's/^  Name: //p' \
        | LC_ALL=C sort -u
)
unexpected_imports=$(
    grep -Eiv '^(api-ms-win-crt-[a-z0-9-]+|advapi32|bcrypt|crypt32|iphlpapi|kernel32|mswsock|ntdll|ole32|shell32|user32|ws2_32)\.dll$' <<< "$imports" || true
)
if [[ -n "$unexpected_imports" ]]; then
    fail "private runtime DLL dependency remains: $unexpected_imports"
fi
for required_import in bcrypt.dll crypt32.dll ws2_32.dll; do
    grep -Eiq "^${required_import}$" <<< "$imports" || fail "missing system import $required_import"
done

$strings_tool "$library" > "$temporary/library.strings"
grep -Eq 'OpenSSL 3\.5\.7' "$temporary/library.strings" || fail "pinned OpenSSL is missing"
if grep -Eq '/source(/|$)|/tmp/|/var/tmp/' "$temporary/library.strings"; then
    grep -E '/source(/|$)|/tmp/|/var/tmp/' "$temporary/library.strings" | sed -n '1,20p' >&2
    fail "DLL exposes a local build path"
fi

license_pairs=(
    "$package/licenses/NUVIO-ENGINE-LICENSE.txt:$engine_root/LICENSE"
    "$package/licenses/THIRD_PARTY_NOTICES.md:$engine_root/THIRD_PARTY_NOTICES.md"
    "$package/licenses/LIBTORRENT-LICENSE.txt:$engine_root/platform/windows/.deps/sources/libtorrent-2.0.12/LICENSE"
    "$package/licenses/LIBTORRENT-COPYING.txt:$engine_root/platform/windows/.deps/sources/libtorrent-2.0.12/COPYING"
    "$package/licenses/TRY_SIGNAL-LICENSE.txt:$engine_root/platform/windows/.deps/sources/libtorrent-2.0.12/deps/try_signal/LICENSE"
    "$package/licenses/BOOST-LICENSE_1_0.txt:$engine_root/platform/windows/.deps/sources/boost_1_86_0/LICENSE_1_0.txt"
    "$package/licenses/OPENSSL-LICENSE.txt:$engine_root/platform/windows/.deps/sources/openssl-3.5.7/LICENSE.txt"
    "$package/licenses/LLVM-MINGW-LICENSE.txt:/opt/llvm-mingw/LICENSE.TXT"
)
for pair in "${license_pairs[@]}"; do
    packaged=${pair%%:*}
    source=${pair#*:}
    [[ -f "$packaged" ]] || fail "missing license $(basename "$packaged")"
    cmp -s "$packaged" "$source" || fail "license mismatch: $(basename "$packaged")"
done

smoke_headers=$($readobj --file-headers "$package/tools/nuvio_engine_smoke.exe")
grep -Fq "$expected_machine" <<< "$smoke_headers" || fail "smoke executable has wrong PE machine"
smoke_imports=$($readobj --coff-imports "$package/tools/nuvio_engine_smoke.exe")
grep -Fq 'Name: nuvio_engine.dll' <<< "$smoke_imports" || fail "consumer did not link the DLL import library"

echo "verified Windows $target_architecture package, ABI, TLS, mitigations, and licenses"
