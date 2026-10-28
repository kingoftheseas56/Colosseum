#!/usr/bin/env bash
# Inputs are extracted, hash-verified official sources from DEPENDENCIES.json.
set -euo pipefail
: "${LT_SOURCE:?Set LT_SOURCE to libtorrent-rasterbar-2.0.11 source}"
: "${BOOST_ROOT:?Set BOOST_ROOT to boost_1_83_0}"
: "${LT_ROOT:?Set LT_ROOT to the desired static-library install prefix}"
LT_BUILD="${LT_BUILD:-$LT_SOURCE/build-release}"
cmake -S "$LT_SOURCE" -B "$LT_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF \
  -DBUILD_SHARED_LIBS=OFF -Dbuild_tests=OFF -Dbuild_examples=OFF -Dbuild_tools=OFF \
  -Dencryption=ON -Dstatic_runtime=OFF -DCMAKE_POLICY_DEFAULT_CMP0167=OLD \
  -DBOOST_ROOT="$BOOST_ROOT" -DBoost_NO_SYSTEM_PATHS=ON -DBoost_NO_BOOST_CMAKE=ON \
  -DCMAKE_INSTALL_PREFIX="$LT_ROOT"
cmake --build "$LT_BUILD" --parallel "${BUILD_JOBS:-3}"
cmake --install "$LT_BUILD"
