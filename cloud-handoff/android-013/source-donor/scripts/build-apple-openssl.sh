#!/usr/bin/env bash
set -euo pipefail

openssl_version=3.5.7
openssl_sha256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
openssl_url="https://github.com/openssl/openssl/releases/download/openssl-${openssl_version}/openssl-${openssl_version}.tar.gz"
text_template_version=1.61
text_template_sha256=a295ea7d1ef241ae2640c1f7864b628f8e6f99ec14fb1da781b2f5f2168dcf09
text_template_url="https://cpan.metacpan.org/authors/id/M/MS/MSCHOUT/Text-Template-${text_template_version}.tar.gz"
ios_deployment_target=16.1
macos_deployment_target=11.0
runtime_prefix=/nuvio-engine/openssl

if [[ $# -ne 1 ]]; then
    echo "usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
    echo "Apple OpenSSL builds require macOS" >&2
    exit 2
fi

real_output_root=$1
mkdir -p "$real_output_root"
real_output_root=$(cd "$real_output_root" && pwd -P)

# OpenSSL's generated make dependencies do not quote paths containing spaces.
link_key=$(printf '%s' "$real_output_root" | cksum | awk '{print $1}')
output_link="${TMPDIR:-/tmp}/nuvio-apple-openssl-$link_key"
if [[ -L "$output_link" ]]; then
    rm "$output_link"
elif [[ -e "$output_link" ]]; then
    echo "temporary OpenSSL build path is occupied: $output_link" >&2
    exit 2
fi
ln -s "$real_output_root" "$output_link"
trap 'rm -f "$output_link"' EXIT
output_root=$output_link

downloads="$output_root/downloads"
sources="$output_root/sources"
builds="$output_root/openssl/build"
installs="$output_root/openssl/install"
mkdir -p "$downloads" "$sources" "$builds" "$installs"

file_sha256() {
    shasum -a 256 "$1" | awk '{print $1}'
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

if [[ -n "${NUVIO_BUILD_JOBS:-}" ]]; then
    jobs=$NUVIO_BUILD_JOBS
else
    jobs=$(sysctl -n hw.ncpu)
fi
if (( jobs > 8 )); then
    jobs=8
fi

slice_names=(
    macos-arm64
    macos-x86_64
    ios-arm64
    ios-simulator-arm64
    ios-simulator-x86_64
)
configure_targets=(
    darwin64-arm64-cc
    darwin64-x86_64-cc
    ios64-xcrun
    iossimulator-arm64-xcrun
    iossimulator-x86_64-xcrun
)
deployment_flags=(
    -mmacosx-version-min=${macos_deployment_target}
    -mmacosx-version-min=${macos_deployment_target}
    -miphoneos-version-min=${ios_deployment_target}
    -mios-simulator-version-min=${ios_deployment_target}
    -mios-simulator-version-min=${ios_deployment_target}
)
sdks=(macosx macosx iphoneos iphonesimulator iphonesimulator)
architectures=(arm64 x86_64 arm64 arm64 x86_64)

for index in "${!slice_names[@]}"; do
    slice=${slice_names[$index]}
    configure_target=${configure_targets[$index]}
    deployment_flag=${deployment_flags[$index]}
    sdk=${sdks[$index]}
    architecture=${architectures[$index]}
    build_directory="$builds/$slice"
    install_directory="$installs/$slice"
    marker="$install_directory/.nuvio-openssl-${openssl_version}-v1"
    marker_value="$openssl_sha256 $configure_target $deployment_flag"
    if [[ -f "$marker" \
        && "$(<"$marker")" == "$marker_value" \
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
        CC="xcrun --sdk $sdk clang" perl "$source_root/Configure" \
            "$configure_target" \
            "$deployment_flag" \
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
    if [[ "$(xcrun lipo -archs "$install_directory/lib/libssl.a")" != "$architecture" \
        || "$(xcrun lipo -archs "$install_directory/lib/libcrypto.a")" != "$architecture" ]]; then
        echo "OpenSSL produced the wrong architecture for $slice" >&2
        exit 1
    fi
    printf '%s\n' "$marker_value" > "$marker"
done

create_universal_install() {
    local name=$1
    local first=$2
    local second=$3
    local destination="$installs/$name"
    local marker="$destination/.nuvio-openssl-${openssl_version}-universal-v1"
    local first_marker="$installs/$first/.nuvio-openssl-${openssl_version}-v1"
    local second_marker="$installs/$second/.nuvio-openssl-${openssl_version}-v1"
    local marker_value="$(<"$first_marker") | $(<"$second_marker")"
    if [[ -f "$marker" \
        && "$(<"$marker")" == "$marker_value" \
        && -f "$destination/include/openssl/ssl.h" \
        && -f "$destination/lib/libssl.a" \
        && -f "$destination/lib/libcrypto.a" ]]; then
        return
    fi
    cmake -E remove_directory "$destination"
    cmake -E copy_directory "$installs/$first/include" "$destination/include"
    mkdir -p "$destination/lib"
    xcrun lipo -create \
        "$installs/$first/lib/libssl.a" \
        "$installs/$second/lib/libssl.a" \
        -output "$destination/lib/libssl.a"
    xcrun lipo -create \
        "$installs/$first/lib/libcrypto.a" \
        "$installs/$second/lib/libcrypto.a" \
        -output "$destination/lib/libcrypto.a"
    printf '%s\n' "$marker_value" > "$marker"
}

create_universal_install macos macos-arm64 macos-x86_64
create_universal_install ios-simulator ios-simulator-arm64 ios-simulator-x86_64

echo "built OpenSSL ${openssl_version} for macOS, iOS, and iOS Simulator"
