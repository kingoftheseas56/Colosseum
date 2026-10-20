#!/usr/bin/env bash
set -euo pipefail

script_directory=$(cd "$(dirname "$0")" && pwd -P)
engine_root=$(cd "$script_directory/.." && pwd -P)

if [[ $# -ne 1 ]]; then
    echo "usage: $0 path/to/nuvio-engine.aar" >&2
    exit 2
fi

aar=$1
ndk_root=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_HOME:-}/ndk/29.0.14206865}}
case "$(uname -s)" in
    Darwin) host_tag=darwin-x86_64 ;;
    Linux) host_tag=linux-x86_64 ;;
    *) echo "unsupported NDK host" >&2; exit 2 ;;
esac
toolchain="$ndk_root/toolchains/llvm/prebuilt/$host_tag/bin"
readelf="$toolchain/llvm-readelf"
nm="$toolchain/llvm-nm"
strings_tool="$toolchain/llvm-strings"

if [[ ! -f "$aar" || ! -x "$readelf" || ! -x "$nm" || ! -x "$strings_tool" ]]; then
    echo "AAR or NDK 29 LLVM tools not found" >&2
    exit 2
fi

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
unzip -q "$aar" -d "$temporary"

license_entries=$(unzip -Z1 "$temporary/classes.jar")
if ! nuvio_license=$(unzip -p "$temporary/classes.jar" META-INF/NUVIO_ENGINE_LICENSE.txt) \
    || [[ "$nuvio_license" != "$(<"$engine_root/LICENSE")" ]]; then
    echo "AAR project license is missing or does not match" >&2
    exit 1
fi
if ! rg -q '^META-INF/NUVIO_ENGINE_THIRD_PARTY_NOTICES\.md$' <<< "$license_entries"; then
    echo "AAR does not package Nuvio third-party notices" >&2
    exit 1
fi
if ! openssl_license=$(unzip -p "$temporary/classes.jar" META-INF/OPENSSL_LICENSE.txt); then
    echo "AAR does not package the OpenSSL license" >&2
    exit 1
fi
if ! rg -q 'Apache License' <<< "$openssl_license"; then
    echo "AAR does not package the OpenSSL license" >&2
    exit 1
fi
if ! libtorrent_license=$(unzip -p "$temporary/classes.jar" META-INF/LIBTORRENT_LICENSE.txt) \
    || ! rg -q 'Apple Public Source License' <<< "$libtorrent_license"; then
    echo "AAR does not package libtorrent and its bundled license notices" >&2
    exit 1
fi
if ! try_signal_license=$(unzip -p "$temporary/classes.jar" META-INF/TRY_SIGNAL_LICENSE.txt) \
    || ! rg -q 'BSD 3-Clause License' <<< "$try_signal_license"; then
    echo "AAR does not package the try_signal license" >&2
    exit 1
fi
if ! boost_license=$(unzip -p "$temporary/classes.jar" META-INF/BOOST_LICENSE_1_0.txt) \
    || ! rg -q 'Boost Software License' <<< "$boost_license"; then
    echo "AAR does not package the Boost license" >&2
    exit 1
fi

for abi in armeabi-v7a arm64-v8a x86 x86_64; do
    library="$temporary/jni/$abi/libnuvio_engine.so"
    if [[ ! -f "$library" ]]; then
        echo "missing $abi/libnuvio_engine.so" >&2
        exit 1
    fi

    while read -r alignment; do
        if (( alignment < 0x4000 )); then
            echo "$abi has a load segment aligned below 16 KiB" >&2
            exit 1
        fi
    done < <("$readelf" -lW "$library" | awk '$1 == "LOAD" { print $NF }')

    unexpected=$(
        "$readelf" -d "$library" \
            | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' \
            | rg -v '^(libc|libdl|libm|liblog|libandroid)\.so$' || true
    )
    if [[ -n "$unexpected" ]]; then
        echo "$abi has unexpected dynamic dependencies: $unexpected" >&2
        exit 1
    fi

    symbols=$("$nm" -D --defined-only "$library")
    rg -q ' nuvio_engine_create(@@NUVIO_ENGINE_0\.1)?$' <<< "$symbols"
    rg -q ' nuvio_engine_reclaim_disk_cache(@@NUVIO_ENGINE_0\.1)?$' <<< "$symbols"
    rg -q ' Java_com_nuvio_engine_internal_NativeBridge_nativeCreate(@@NUVIO_ENGINE_0\.1)?$' <<< "$symbols"
    unexpected_symbols=$(
        awk '{print $NF}' <<< "$symbols" \
            | rg -v '^(NUVIO_ENGINE_0\.1|nuvio_engine_[A-Za-z0-9_]+(@@NUVIO_ENGINE_0\.1)?|Java_com_nuvio_engine_internal_NativeBridge_[A-Za-z0-9_]+(@@NUVIO_ENGINE_0\.1)?)$' \
            || true
    )
    if [[ -n "$unexpected_symbols" ]]; then
        echo "$abi exports private native symbols: $unexpected_symbols" >&2
        exit 1
    fi

    binary_strings="$temporary/$abi.strings"
    "$strings_tool" "$library" > "$binary_strings"
    if ! rg -q '^OpenSSL 3\.5\.7 ' "$binary_strings"; then
        echo "$abi does not contain the pinned OpenSSL 3.5.7 provider" >&2
        exit 1
    fi
    if rg -q '/nuvio-openssl-|/var/folders/' "$binary_strings"; then
        echo "$abi leaks an OpenSSL build-machine path" >&2
        exit 1
    fi
done

echo "verified four Android ABIs and 16 KiB ELF alignment"
