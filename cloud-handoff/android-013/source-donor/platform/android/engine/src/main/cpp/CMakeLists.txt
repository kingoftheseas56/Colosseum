cmake_minimum_required(VERSION 3.24)

project(nuvio_engine_android LANGUAGES C CXX)

get_filename_component(NUVIO_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)

set(NUVIO_ENGINE_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(NUVIO_ENGINE_ENABLE_LIBTORRENT ON CACHE BOOL "" FORCE)
set(NUVIO_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(NUVIO_OPENSSL_ROOT
    "${NUVIO_ENGINE_ROOT}/platform/android/.deps/openssl-3.5.7/install/${ANDROID_ABI}"
)
set(OPENSSL_ROOT_DIR "${NUVIO_OPENSSL_ROOT}" CACHE PATH "" FORCE)
set(OPENSSL_INCLUDE_DIR "${NUVIO_OPENSSL_ROOT}/include" CACHE PATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${NUVIO_OPENSSL_ROOT}/lib/libssl.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${NUVIO_OPENSSL_ROOT}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)

add_subdirectory("${NUVIO_ENGINE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/nuvio-engine-core")

target_sources(nuvio_engine PRIVATE jni_bridge.cpp)
target_include_directories(nuvio_engine PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_options(
    nuvio_engine
    PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map"
        "-Wl,--exclude-libs,ALL"
)
