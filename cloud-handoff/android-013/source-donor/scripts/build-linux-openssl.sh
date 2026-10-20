#!/usr/bin/env bash
set -euo pipefail

openssl_version=3.5.7
openssl_sha256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz"
text_template_version=1.61
text_template_sha256=a295ea7d1ef241ae2640c1f7864b628f8e6f99ec14fb1da781b2f5f2168dcf09
text_template_url="https://cpan.metacpan.org/authors/id/M/MS/MSCHOUT/Text-Template-${text_template_version}.tar.gz"
runtime_prefix=/nuvio-engine/openssl

if [[ $# -ne 2 ]]; then
    echo "usage: $0 OUTPUT_DIRECTORY TARGET_ARCHITECTURE" >&2
    exit 2
fi
if [[ "$(uname -s)" != Linux ]]; then
    echo "Linux OpenSSL builds require Linux" >&2
    exit 2
fi

output_root=$1
target_architecture=$2
case "$target_architecture" in
    x86_64) configure_target=linux-x86_64 ;;
    aarch64) configure_target=linux-aarch64 ;;
    *) echo "unsupported Linux architecture: $target_architecture" >&2; exit 2 ;;
esac
if [[ "$(uname -m)" != "$target_architecture" ]]; then
    echo "container architecture does not match $target_architecture" >&2
    exit 2
fi

downloads="$output_root/downloads"
sources="$output_root/sources"
build_directory="$output_root/openssl/build/$target_architecture"
install_directory="$output_root/openssl/install/$target_architecture"
mkdir -p "$downloads" "$sources"

file_sha256() {
    sha256sum "$1" | awk '{print $1}'
}

download_and_verify() {
    local url=$1
    local archive=$2
    local expected=$3
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 3 --silent --show-error \
            --output "$archive" \
            "$url"
    fi
    if [[ "$(file_sha256 "$archive")" != "$expected" ]]; then
        echo "checksum mismatch for $archive" >&2
        exit 1
    fi
}

openssl_archive="$downloads/openssl-${openssl_version}.tar.gz"
download_and_verify "$openssl_url" "$openssl_archive" "$openssl_sha256"
text_template_archive="$downloads/Text-Template-${text_template_version}.tar.gz"
download_and_verify \
    "$text_template_url" \
    "$text_template_archive" \
    "$text_template_sha256"

text_template_root="$sources/Text-Template-${text_template_version}"
if [[ ! -f "$text_template_root/lib/Text/Template.pm" ]]; then
    tar -xzf "$text_template_archive" -C "$sources"
fi
export PERL5LIB="$text_template_root/lib${PERL5LIB:+:$PERL5LIB}"
if ! perl -MText::Template -e 1 >/dev/null 2>&1; then
    echo "pinned Perl Text::Template could not be loaded" >&2
    exit 2
fi

source_root="$sources/openssl-${openssl_version}"
if [[ ! -f "$source_root/Configure" ]]; then
    tar -xzf "$openssl_archive" -C "$sources"
fi

compiler_version=$(cc -dumpfullversion -dumpversion)
marker="$install_directory/.nuvio-openssl-${openssl_version}-linux-v1"
marker_value="$openssl_sha256 $configure_target cc-$compiler_version"
if [[ -f "$marker" \
    && "$(<"$marker")" == "$marker_value" \
    && -f "$install_directory/include/openssl/ssl.h" \
    && -f "$install_directory/lib/libssl.a" \
    && -f "$install_directory/lib/libcrypto.a" ]]; then
    echo "reused OpenSSL ${openssl_version} for Linux $target_architecture"
    exit 0
fi

jobs=${NUVIO_BUILD_JOBS:-$(nproc)}
if (( jobs > 8 )); then
    jobs=8
fi
cmake -E remove_directory "$build_directory"
cmake -E remove_directory "$install_directory"
mkdir -p "$build_directory"
build_log="$build_directory/build.log"
if ! (
    cd "$build_directory"
    perl "$source_root/Configure" \
        "$configure_target" \
        -fPIC \
        -ffile-prefix-map=/source=/nuvio-engine/source \
        --prefix="$runtime_prefix" \
        --openssldir=/etc/ssl \
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
        no-zstd
    make -s -j"$jobs" build_sw
    make -s install_sw DESTDIR="$build_directory/stage"
) >"$build_log" 2>&1; then
    cat "$build_log" >&2
    exit 1
fi
cmake -E copy_directory \
    "$build_directory/stage$runtime_prefix" \
    "$install_directory"
printf '%s\n' "$marker_value" > "$marker"

echo "built OpenSSL ${openssl_version} for Linux $target_architecture"
