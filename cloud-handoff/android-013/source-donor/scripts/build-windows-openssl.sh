#!/usr/bin/env bash
set -euo pipefail

openssl_version=3.5.7
openssl_sha256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz"
text_template_version=1.61
text_template_sha256=a295ea7d1ef241ae2640c1f7864b628f8e6f99ec14fb1da781b2f5f2168dcf09
text_template_url="https://cpan.metacpan.org/authors/id/M/MS/MSCHOUT/Text-Template-${text_template_version}.tar.gz"
runtime_prefix='C:/nuvio-engine/openssl'

if [[ $# -ne 2 ]]; then
    echo "usage: $0 OUTPUT_DIRECTORY x86_64|aarch64" >&2
    exit 2
fi
if [[ "$(uname -s)" != Linux ]]; then
    echo "Windows OpenSSL cross-builds require the Linux package container" >&2
    exit 2
fi

output_root=$1
target_architecture=$2
case "$target_architecture" in
    x86_64)
        target_triple=x86_64-w64-mingw32
        configure_target=mingw64
        ;;
    aarch64)
        target_triple=aarch64-w64-mingw32
        configure_target=mingwarm64
        ;;
    *) echo "unsupported Windows architecture: $target_architecture" >&2; exit 2 ;;
esac

downloads="$output_root/downloads"
sources="$output_root/sources"
build_directory="$output_root/openssl/build/$target_architecture"
install_directory="$output_root/openssl/install/$target_architecture"
mkdir -p "$downloads" "$sources"

download_and_verify() {
    local url=$1
    local archive=$2
    local expected=$3
    if [[ ! -f "$archive" \
        || "$(sha256sum "$archive" | awk '{print $1}')" != "$expected" ]]; then
        rm -f "$archive.download"
        curl --fail --location --retry 5 --retry-all-errors --silent --show-error \
            --output "$archive.download" \
            "$url"
        echo "$expected  $archive.download" | sha256sum --check --strict >/dev/null
        mv "$archive.download" "$archive"
    fi
    echo "$expected  $archive" | sha256sum --check --strict >/dev/null
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
perl -MText::Template -e 1

source_root="$sources/openssl-${openssl_version}"
if [[ ! -f "$source_root/Configure" ]]; then
    tar -xzf "$openssl_archive" -C "$sources"
fi
cp /source/cmake/openssl-windows-targets.conf \
    "$source_root/Configurations/99-nuvio-windows.conf"

compiler_version=$("/opt/llvm-mingw/bin/${target_triple}-clang" --version | awk 'NR == 1 { print $4 }')
marker="$install_directory/.nuvio-openssl-${openssl_version}-windows-v1"
marker_value="$openssl_sha256 $configure_target llvm-$compiler_version"
if [[ -f "$marker" \
    && "$(<"$marker")" == "$marker_value" \
    && -f "$install_directory/include/openssl/ssl.h" \
    && -f "$install_directory/lib/libssl.a" \
    && -f "$install_directory/lib/libcrypto.a" ]]; then
    echo "reused OpenSSL ${openssl_version} for Windows $target_architecture"
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
        "--cross-compile-prefix=/opt/llvm-mingw/bin/${target_triple}-" \
        --prefix="$runtime_prefix" \
        --openssldir='C:/ProgramData/Nuvio/OpenSSL' \
        --libdir=lib \
        -D_WIN32_WINNT=0x0A00 \
        -ffile-prefix-map=/source=/nuvio-engine/source \
        no-asm \
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

staged_library=$(
    find "$build_directory" \
        -type f \
        -path '*/lib/libssl.a' \
        -print \
        -quit
)
if [[ -z "$staged_library" ]]; then
    echo "OpenSSL did not install its static libraries" >&2
    exit 1
fi
staged_prefix=${staged_library%/lib/libssl.a}
if [[ ! -f "$staged_prefix/include/openssl/ssl.h" \
    || ! -f "$staged_prefix/lib/libcrypto.a" ]]; then
    echo "OpenSSL install is incomplete" >&2
    exit 1
fi
cmake -E copy_directory "$staged_prefix" "$install_directory"
printf '%s\n' "$marker_value" > "$marker"

echo "built OpenSSL ${openssl_version} for Windows $target_architecture"
