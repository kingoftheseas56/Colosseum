#!/usr/bin/env bash
set -euo pipefail

openssl_version=3.5.7
openssl_sha256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz"
text_template_version=1.61
text_template_sha256=a295ea7d1ef241ae2640c1f7864b628f8e6f99ec14fb1da781b2f5f2168dcf09
text_template_url="https://cpan.metacpan.org/authors/id/M/MS/MSCHOUT/Text-Template-${text_template_version}.tar.gz"
libtorrent_license_sha256=34c359bb2297512ec3de21952ac668f1d981514fd63361ffdcca85294127ef6b
libtorrent_license_url=https://raw.githubusercontent.com/arvidn/libtorrent/740a0b9aeabe00e762cc0efe4a0f27593db2550b/LICENSE
try_signal_license_sha256=18a7c1435a7a7ebc120a181c7309d0d7dfcbec76be4950ff23c9aa37f9c93725
try_signal_license_url=https://raw.githubusercontent.com/arvidn/try_signal/105cce59972f925a33aa6b1c3109e4cd3caf583d/LICENSE
boost_license_sha256=c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566
boost_license_url=https://raw.githubusercontent.com/boostorg/boost/boost-1.86.0/LICENSE_1_0.txt

if [[ $# -ne 2 ]]; then
    echo "usage: $0 ANDROID_NDK_ROOT OUTPUT_DIRECTORY" >&2
    exit 2
fi

ndk_root=$1
real_output_root=$2
case "$(uname -s)" in
    Darwin) host_tag=darwin-x86_64 ;;
    Linux) host_tag=linux-x86_64 ;;
    *) echo "unsupported Android build host" >&2; exit 2 ;;
esac

toolchain="$ndk_root/toolchains/llvm/prebuilt/$host_tag/bin"
if [[ ! -x "$toolchain/clang" ]]; then
    echo "Android NDK LLVM toolchain not found at $toolchain" >&2
    exit 2
fi
mkdir -p "$real_output_root"

# OpenSSL's generated make dependencies do not quote paths with spaces. Use a
# short-lived, deterministic symlink while keeping every artifact in the
# requested Gradle-owned output directory.
link_key=$(printf '%s' "$real_output_root" | cksum | awk '{print $1}')
output_link="${TMPDIR:-/tmp}/nuvio-openssl-$link_key"
if [[ -L "$output_link" ]]; then
    rm "$output_link"
elif [[ -e "$output_link" ]]; then
    echo "temporary OpenSSL build path is occupied: $output_link" >&2
    exit 2
fi
ln -s "$real_output_root" "$output_link"
trap 'rm -f "$output_link"' EXIT
output_root=$output_link
mkdir -p "$output_root/downloads" "$output_root/src" "$output_root/build" "$output_root/install"

file_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

download_and_verify() {
    local url=$1
    local archive=$2
    local expected=$3
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 --silent --show-error --output "$archive" "$url"
    fi
    if [[ "$(file_sha256 "$archive")" != "$expected" ]]; then
        echo "checksum mismatch for $archive" >&2
        exit 1
    fi
}

archive="$output_root/downloads/openssl-${openssl_version}.tar.gz"
download_and_verify "$openssl_url" "$archive" "$openssl_sha256"

text_template_archive="$output_root/downloads/Text-Template-${text_template_version}.tar.gz"
download_and_verify "$text_template_url" "$text_template_archive" "$text_template_sha256"
download_and_verify \
    "$libtorrent_license_url" \
    "$output_root/downloads/libtorrent-LICENSE.txt" \
    "$libtorrent_license_sha256"
download_and_verify \
    "$try_signal_license_url" \
    "$output_root/downloads/try_signal-LICENSE.txt" \
    "$try_signal_license_sha256"
download_and_verify \
    "$boost_license_url" \
    "$output_root/downloads/boost-LICENSE_1_0.txt" \
    "$boost_license_sha256"
text_template_root="$output_root/src/Text-Template-${text_template_version}"
if [[ ! -f "$text_template_root/lib/Text/Template.pm" ]]; then
    tar -xzf "$text_template_archive" -C "$output_root/src"
fi
export PERL5LIB="$text_template_root/lib${PERL5LIB:+:$PERL5LIB}"
if ! perl -MText::Template -e 1 >/dev/null 2>&1; then
    echo "pinned Perl Text::Template could not be loaded" >&2
    exit 2
fi

source_root="$output_root/src/openssl-${openssl_version}"
if [[ ! -f "$source_root/Configure" ]]; then
    tar -xzf "$archive" -C "$output_root/src"
fi

if [[ -n "${NUVIO_BUILD_JOBS:-}" ]]; then
    jobs=$NUVIO_BUILD_JOBS
elif [[ "$(uname -s)" == Darwin ]]; then
    jobs=$(sysctl -n hw.ncpu)
else
    jobs=$(nproc)
fi
if (( jobs > 8 )); then
    jobs=8
fi

abis=(armeabi-v7a arm64-v8a x86 x86_64)
targets=(android-arm android-arm64 android-x86 android-x86_64)
export ANDROID_NDK_ROOT="$ndk_root"
export PATH="$toolchain:$PATH"
runtime_prefix=/nuvio-engine/openssl

for index in "${!abis[@]}"; do
    abi=${abis[$index]}
    target=${targets[$index]}
    build_directory="$output_root/build/$abi"
    install_directory="$output_root/install/$abi"
    marker="$install_directory/.nuvio-openssl-${openssl_version}-v2"
    if [[ -f "$marker" \
        && "$(<"$marker")" == "$openssl_sha256" \
        && -f "$install_directory/include/openssl/ssl.h" \
        && -f "$install_directory/lib/libssl.a" \
        && -f "$install_directory/lib/libcrypto.a" ]]; then
        continue
    fi

    cmake -E remove_directory "$build_directory"
    cmake -E remove_directory "$install_directory"
    mkdir -p "$build_directory"
    build_log="$build_directory/build.log"
    if ! (
        cd "$build_directory"
        perl "$source_root/Configure" \
            "$target" \
            -D__ANDROID_API__=24 \
            --prefix="$runtime_prefix" \
            --openssldir="$runtime_prefix/ssl" \
            --libdir=lib \
            no-shared \
            no-tests \
            no-apps \
            no-docs \
            no-module \
            no-legacy \
            no-fips \
            no-comp \
            no-zlib \
            no-zstd \
            -Wno-macro-redefined
        make -s -j"$jobs" build_sw
        make -s install_sw DESTDIR="$build_directory/stage"
    ) >"$build_log" 2>&1; then
        cat "$build_log" >&2
        exit 1
    fi
    cmake -E copy_directory \
        "$build_directory/stage$runtime_prefix" \
        "$install_directory"
    printf '%s\n' "$openssl_sha256" > "$marker"
done

echo "built OpenSSL ${openssl_version} for ${abis[*]}"
