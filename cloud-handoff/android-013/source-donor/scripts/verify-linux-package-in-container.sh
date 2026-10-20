#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 PACKAGE.tar.gz x86_64|aarch64" >&2
    exit 2
fi
archive=$1
target_architecture=$2
engine_root=/source
if [[ "$(uname -s)" != Linux || "$(uname -m)" != "$target_architecture" ]]; then
    echo "verification must run in the matching Linux container" >&2
    exit 2
fi
if ! (
    cd "$(dirname "$archive")"
    sha256sum --check "$(basename "$archive").sha256"
); then
    exit 1
fi

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
tar -xzf "$archive" -C "$temporary"
package="$temporary/nuvio-engine-linux-$target_architecture"
fail() {
    echo "Linux package verification failed: $*" >&2
    exit 1
}

[[ -f "$package/BUILD-INFO.txt" ]] || fail "build metadata is missing"
engine_version=$(sed -n 's/^Nuvio Engine: //p' "$package/BUILD-INFO.txt")
[[ "$engine_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "invalid engine version metadata"
engine_soversion=${engine_version%%.*}
library_name="libnuvio_engine.so.$engine_version"
library="$package/lib/$library_name"
[[ -f "$library" ]] || fail "shared library is missing"
[[ "$(readlink "$package/lib/libnuvio_engine.so")" == "libnuvio_engine.so.$engine_soversion" ]] || \
    fail "unversioned library symlink is wrong"
[[ "$(readlink "$package/lib/libnuvio_engine.so.$engine_soversion")" == "$library_name" ]] || \
    fail "SONAME library symlink is wrong"
cmp -s \
    "$package/include/nuvio_engine/nuvio_engine.h" \
    "$engine_root/include/nuvio_engine/nuvio_engine.h" || fail "C header mismatch"
cmp -s \
    "$package/include/nuvio_engine/export.h" \
    "$engine_root/include/nuvio_engine/export.h" || fail "export header mismatch"
cmp -s "$package/README.md" "$engine_root/README.md" || fail "README mismatch"
case "$target_architecture" in
    x86_64) expected_machine='Advanced Micro Devices X86-64' ;;
    aarch64) expected_machine='AArch64' ;;
    *) fail "unsupported architecture" ;;
esac
readelf -h "$library" | grep -E "Machine:.*$expected_machine" >/dev/null || fail "wrong ELF machine"
readelf -d "$library" \
    | grep -F "SONAME" \
    | grep -F "[libnuvio_engine.so.$engine_soversion]" >/dev/null || fail "wrong SONAME"
if readelf -d "$library" | grep -E 'RPATH|RUNPATH' >/dev/null; then
    fail "shared library contains an RPATH"
fi
readelf -lW "$library" | grep -E 'GNU_RELRO' >/dev/null || fail "GNU RELRO is missing"
readelf -d "$library" | grep -E 'BIND_NOW|FLAGS.*NOW' >/dev/null || fail "immediate binding is missing"
if readelf -d "$library" | grep -E 'TEXTREL' >/dev/null; then
    fail "shared library contains text relocations"
fi
stack_flags=$(readelf -lW "$library" | awk '$1 == "GNU_STACK" { print $(NF - 1) }')
[[ -n "$stack_flags" ]] || fail "GNU_STACK metadata is missing"
if [[ "$stack_flags" == *E* ]]; then
    fail "shared library requests an executable stack"
fi

needed=$(readelf -d "$library" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
if grep -Eq 'libssl|libcrypto|libtorrent|libboost' <<< "$needed"; then
    fail "private native dependency remains dynamically linked"
fi
unexpected_needed=$(grep -Ev '^(libstdc\+\+\.so\.6|libgcc_s\.so\.1|libm\.so\.6|libc\.so\.6|ld-linux.*\.so.*)$' <<< "$needed" || true)
if [[ -n "$unexpected_needed" ]]; then
    fail "unexpected dynamic dependencies: $unexpected_needed"
fi

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
    nm -D --defined-only "$library" \
        | awk '$2 ~ /^[TDB]$/ { print $3 }' \
        | sed 's/@@NUVIO_ENGINE_1\.0$//' \
        | LC_ALL=C sort
)
if [[ "$actual_symbols" != "$expected_symbols" ]]; then
    diff -u <(printf '%s\n' "$expected_symbols") <(printf '%s\n' "$actual_symbols") || true
    fail "dynamic export surface differs from the C ABI"
fi
readelf --version-info "$library" | grep -E 'NUVIO_ENGINE_1\.0' >/dev/null || fail "ABI version node is missing"

maximum_glibc=$(
    readelf --version-info "$library" \
        | grep -Eo 'GLIBC_[0-9]+\.[0-9]+' \
        | sed 's/GLIBC_//' \
        | sort -Vu \
        | tail -n 1
)
if [[ "$(printf '%s\n' "$maximum_glibc" 2.35 | sort -V | tail -n 1)" != 2.35 ]]; then
    fail "binary requires glibc $maximum_glibc"
fi

strings "$library" > "$temporary/library.strings"
if grep -Eq '^/source(/|$)|/tmp/|/var/tmp/' "$temporary/library.strings"; then
    grep -E '^/source(/|$)|/tmp/|/var/tmp/' "$temporary/library.strings" | sed -n '1,20p' >&2
    fail "binary exposes a local build path"
fi
grep -Eq 'OpenSSL 3\.5\.7' "$temporary/library.strings" || fail "pinned OpenSSL is missing"

license_pairs=(
    "$package/licenses/NUVIO-ENGINE-LICENSE.txt:$engine_root/LICENSE"
    "$package/licenses/THIRD_PARTY_NOTICES.md:$engine_root/THIRD_PARTY_NOTICES.md"
    "$package/licenses/LIBTORRENT-LICENSE.txt:$engine_root/platform/linux/.deps/sources/libtorrent-2.0.12/LICENSE"
    "$package/licenses/LIBTORRENT-COPYING.txt:$engine_root/platform/linux/.deps/sources/libtorrent-2.0.12/COPYING"
    "$package/licenses/TRY_SIGNAL-LICENSE.txt:$engine_root/platform/linux/.deps/sources/libtorrent-2.0.12/deps/try_signal/LICENSE"
    "$package/licenses/BOOST-LICENSE_1_0.txt:$engine_root/platform/linux/.deps/sources/boost_1_86_0/LICENSE_1_0.txt"
    "$package/licenses/OPENSSL-LICENSE.txt:$engine_root/platform/linux/.deps/sources/openssl-3.5.7/LICENSE.txt"
)
for pair in "${license_pairs[@]}"; do
    packaged=${pair%%:*}
    source=${pair#*:}
    [[ -f "$packaged" ]] || fail "missing license $(basename "$packaged")"
    cmp -s "$packaged" "$source" || fail "license mismatch: $(basename "$packaged")"
done

cc \
    "$engine_root/tests/c_api_smoke.c" \
    -I "$package/include" \
    -L "$package/lib" \
    -lnuvio_engine \
    -Wl,-rpath,"$package/lib" \
    -o "$temporary/package-smoke"
"$temporary/package-smoke"

echo "verified Linux $target_architecture package, ABI, TLS, licenses, and runtime"
