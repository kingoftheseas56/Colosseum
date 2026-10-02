#!/usr/bin/env bash
# Cross-build Colosseum's native Android dependencies with NDK r27c:
#
#   OpenSSL 3.5.9                   static + Qt runtime shared libraries
#   Boost 1.90.0                    headers + static filesystem (system is header-only)
#   libtorrent-rasterbar 2.0.14     static, C++17, OpenSSL-backed
#
# One install prefix per ABI: $OUT/<abi>/{include,lib,lib/cmake}. Pass that prefix to the
# Android configure via CMAKE_PREFIX_PATH/CMAKE_FIND_ROOT_PATH (scripts/android/build_apk.sh).
#
# Usage: scripts/android/build_deps.sh [abi...]      (default: arm64-v8a x86_64)
#
# Environment:
#   ANDROID_NDK_ROOT                 NDK 27.2.12479018 (required)
#   COLOSSEUM_ANDROID_DEPS           output root   (default: $HOME/colosseum-android/deps)
#   COLOSSEUM_ANDROID_SRC_CACHE      tarball cache (default: $HOME/colosseum-android/src-cache)
#   JOBS                             parallelism   (default: nproc)
#
# Sources are checksum-pinned. A finished ABI prefix carries a stamp keyed on this script's
# pins, so re-running is a no-op unless versions/flags change.
set -euo pipefail

API=28
OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
BOOST_VERSION=1.90.0
BOOST_SHA256=49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305
LIBTORRENT_VERSION=2.0.14
LIBTORRENT_SHA256=1b0b21b9755b5fbec23ca9ba2d2d10434ecb6711c39f37f5fc9d5aa25cf369c9
STAMP_KEY="api${API}-openssl${OPENSSL_VERSION}-boost${BOOST_VERSION}-libtorrent${LIBTORRENT_VERSION}-cxx17-qt-tls-v2"

OUT="${COLOSSEUM_ANDROID_DEPS:-$HOME/colosseum-android/deps}"
SRC_CACHE="${COLOSSEUM_ANDROID_SRC_CACHE:-$HOME/colosseum-android/src-cache}"
JOBS="${JOBS:-$(nproc)}"
ABIS=("$@")
((${#ABIS[@]})) || ABIS=(arm64-v8a x86_64)

log() { printf '[android-deps] %s\n' "$*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[[ -n "${ANDROID_NDK_ROOT:-}" && -f "$ANDROID_NDK_ROOT/source.properties" ]] \
    || die "ANDROID_NDK_ROOT must point at NDK 27.2.12479018"
grep -q 'Pkg.Revision = 27.2.12479018' "$ANDROID_NDK_ROOT/source.properties" \
    || die "NDK revision is not 27.2.12479018"
TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
command -v patchelf >/dev/null || die "patchelf is required to package Qt's OpenSSL runtime"

fetch() {  # url sha256 -> path
    local url="$1" sha="$2" file="$SRC_CACHE/${1##*/}"
    mkdir -p "$SRC_CACHE"
    if [[ ! -f "$file" ]] || ! echo "$sha  $file" | sha256sum -c --status; then
        log "fetch ${url##*/}"
        curl -fsSL --retry 4 -o "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum -c --status || die "checksum mismatch: $file"
    echo "$file"
}

OPENSSL_TGZ=$(fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_SHA256")
BOOST_TBZ=$(fetch "https://archives.boost.io/release/$BOOST_VERSION/source/boost_${BOOST_VERSION//./_}.tar.bz2" "$BOOST_SHA256")
LIBTORRENT_TGZ=$(fetch "https://github.com/arvidn/libtorrent/releases/download/v$LIBTORRENT_VERSION/libtorrent-rasterbar-$LIBTORRENT_VERSION.tar.gz" "$LIBTORRENT_SHA256")

abi_triple() {
    case "$1" in
        arm64-v8a) echo aarch64-linux-android ;;
        x86_64)    echo x86_64-linux-android ;;
        *) die "unsupported ABI: $1" ;;
    esac
}

build_openssl() {  # abi prefix work
    local abi="$1" prefix="$2" work="$3" target
    case "$abi" in arm64-v8a) target=android-arm64 ;; x86_64) target=android-x86_64 ;; esac
    log "$abi: OpenSSL $OPENSSL_VERSION"
    tar xzf "$OPENSSL_TGZ" -C "$work"
    (
        cd "$work/openssl-$OPENSSL_VERSION"
        export PATH="$TOOLCHAIN/bin:$PATH"
        ./Configure "$target" -D__ANDROID_API__=$API shared no-tests no-docs no-apps \
            -Wl,-z,max-page-size=16384 \
            --prefix="$prefix" --libdir=lib >"$work/openssl-configure.log" 2>&1 || exit 1
        make -j"$JOBS" build_libs >"$work/openssl-build.log" 2>&1 || exit 1
        make install_dev >"$work/openssl-install.log" 2>&1 || exit 1
        # Keep the static archives for libtorrent, and give Qt its own runtime
        # libraries instead of resolving Android's incompatible system OpenSSL.
        cp libcrypto.so "$prefix/lib/libcrypto_3.so" || exit 1
        cp libssl.so "$prefix/lib/libssl_3.so" || exit 1
        patchelf --page-size 16384 --set-soname libcrypto_3.so "$prefix/lib/libcrypto_3.so" || exit 1
        patchelf --page-size 16384 --set-soname libssl_3.so "$prefix/lib/libssl_3.so" || exit 1
        patchelf --page-size 16384 --replace-needed libcrypto.so libcrypto_3.so "$prefix/lib/libssl_3.so" || exit 1
    ) || { tail -40 "$work"/openssl-*.log >&2; die "$abi: OpenSSL failed"; }
}

build_boost() {  # abi prefix work
    local abi="$1" prefix="$2" work="$3" triple arch abiflag
    triple=$(abi_triple "$abi")
    case "$abi" in
        arm64-v8a) arch=arm; abiflag=aapcs ;;
        x86_64)    arch=x86; abiflag=sysv ;;
    esac
    log "$abi: Boost $BOOST_VERSION (headers + filesystem)"
    tar xjf "$BOOST_TBZ" -C "$work"
    local src="$work/boost_${BOOST_VERSION//./_}"
    (
        cd "$src"
        ./bootstrap.sh --with-toolset=gcc >"$work/boost-bootstrap.log" 2>&1
        cat > "$work/user-config.jam" <<EOF
using clang : android : $TOOLCHAIN/bin/${triple}${API}-clang++ :
    <archiver>$TOOLCHAIN/bin/llvm-ar
    <ranlib>$TOOLCHAIN/bin/llvm-ranlib ;
EOF
        ./b2 -j"$JOBS" -q --user-config="$work/user-config.jam" \
            toolset=clang-android target-os=android architecture="$arch" address-model=64 \
            abi="$abiflag" binary-format=elf \
            link=static runtime-link=shared threading=multi variant=release \
            cxxstd=17 cxxflags="-fPIC" \
            --with-filesystem --with-system \
            --prefix="$prefix" --build-dir="$work/boost-build" \
            install >"$work/boost-build.log" 2>&1
    ) || { tail -40 "$work"/boost-*.log >&2; die "$abi: Boost failed"; }
}

build_libtorrent() {  # abi prefix work
    local abi="$1" prefix="$2" work="$3"
    log "$abi: libtorrent-rasterbar $LIBTORRENT_VERSION"
    tar xzf "$LIBTORRENT_TGZ" -C "$work"
    (
        cmake -S "$work/libtorrent-rasterbar-$LIBTORRENT_VERSION" -B "$work/libtorrent-build" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$API" -DANDROID_STL=c++_shared \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DBUILD_SHARED_LIBS=OFF -Dbuild_tests=OFF -Dbuild_examples=OFF -Dbuild_tools=OFF \
            -Dpython-bindings=OFF \
            -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
            -DCMAKE_FIND_ROOT_PATH="$prefix" \
            -DOPENSSL_ROOT_DIR="$prefix" -DOPENSSL_USE_STATIC_LIBS=ON \
            -DBoost_ROOT="$prefix" \
            >"$work/libtorrent-configure.log" 2>&1
        cmake --build "$work/libtorrent-build" --parallel "$JOBS" >"$work/libtorrent-build.log" 2>&1
        cmake --install "$work/libtorrent-build" >"$work/libtorrent-install.log" 2>&1
    ) || { grep -iE 'error|fatal' "$work"/libtorrent-*.log | head -40 >&2; die "$abi: libtorrent failed"; }
}

for abi in "${ABIS[@]}"; do
    abi_triple "$abi" >/dev/null
    prefix="$OUT/$abi"
    stamp="$prefix/.colosseum-deps-stamp"
    if [[ -f "$stamp" && "$(cat "$stamp")" == "$STAMP_KEY" ]]; then
        log "$abi: up to date ($prefix)"
        continue
    fi
    rm -rf "$prefix"
    mkdir -p "$prefix"
    work=$(mktemp -d "${TMPDIR:-/tmp}/colosseum-android-deps-$abi.XXXXXX")
    build_openssl "$abi" "$prefix" "$work"
    build_boost "$abi" "$prefix" "$work"
    build_libtorrent "$abi" "$prefix" "$work"
    rm -rf "$work"
    for f in lib/libssl.a lib/libcrypto.a lib/libssl_3.so lib/libcrypto_3.so \
             lib/libboost_filesystem.a lib/libtorrent-rasterbar.a \
             lib/cmake/LibtorrentRasterbar/LibtorrentRasterbarConfig.cmake; do
        [[ -f "$prefix/$f" ]] || die "$abi: missing $prefix/$f"
    done
    echo "$STAMP_KEY" > "$stamp"
    log "$abi: done ($prefix)"
done
