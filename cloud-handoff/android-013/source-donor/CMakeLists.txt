cmake_minimum_required(VERSION 3.24)

project(
    nuvio_engine
    VERSION 0.1.5
    DESCRIPTION "Cross-platform peer-to-peer streaming engine for Nuvio"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(NUVIO_ENGINE_BUILD_SHARED "Build Nuvio Engine as a shared library" OFF)
option(NUVIO_ENGINE_ENABLE_LIBTORRENT "Build the pinned libtorrent protocol backend" OFF)
set(NUVIO_ENGINE_BUILD_TESTS_DEFAULT OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(NUVIO_ENGINE_BUILD_TESTS_DEFAULT ON)
endif()
option(
    NUVIO_ENGINE_BUILD_TESTS
    "Build Nuvio Engine tests"
    ${NUVIO_ENGINE_BUILD_TESTS_DEFAULT}
)

set(NUVIO_ENGINE_LIBRARY_TYPE STATIC)
if(NUVIO_ENGINE_BUILD_SHARED)
    set(NUVIO_ENGINE_LIBRARY_TYPE SHARED)
endif()

add_library(nuvio_engine ${NUVIO_ENGINE_LIBRARY_TYPE})
add_library(Nuvio::Engine ALIAS nuvio_engine)

target_sources(
    nuvio_engine
    PRIVATE
        src/core/engine.cpp
        src/core/engine_runtime.cpp
        src/cache/verified_piece_cache.cpp
        src/cache/disk_cache_manager.cpp
        src/http/byte_range.cpp
        src/http/http_protocol.cpp
        src/http/loopback_server.cpp
        src/scheduler/piece_scheduler.cpp
        src/scheduler/stream_demand_plan.cpp
        src/security/random_bytes.cpp
        src/storage/atomic_file.cpp
        src/storage/payload_reader.cpp
        src/torrent/file_selection.cpp
)

if(NUVIO_ENGINE_ENABLE_LIBTORRENT)
    include(cmake/Dependencies.cmake)
    nuvio_engine_add_libtorrent()
    target_sources(
        nuvio_engine
        PRIVATE
            src/torrent/libtorrent_backend.cpp
            src/torrent/libtorrent_stream_bridge.cpp
    )
    target_link_libraries(nuvio_engine PRIVATE $<BUILD_INTERFACE:torrent-rasterbar>)
    target_compile_definitions(nuvio_engine PRIVATE NUVIO_ENGINE_HAS_LIBTORRENT=1)
    if(NUVIO_ENGINE_BUILD_TESTS)
        # Internal waiter/HTTP-writer control exists only in test builds.
        target_compile_definitions(nuvio_engine PRIVATE NUVIO_ENGINE_INTEGRITY_TEST_SEAM=1)
    endif()
    if(APPLE)
        find_package(OpenSSL REQUIRED)
        find_library(NUVIO_SECURITY_FRAMEWORK Security REQUIRED)
        target_sources(nuvio_engine PRIVATE src/security/apple_trust.cpp)
        target_link_libraries(
            nuvio_engine
            PRIVATE
                OpenSSL::SSL
                "${NUVIO_SECURITY_FRAMEWORK}"
        )
    elseif(WIN32)
        find_package(OpenSSL REQUIRED)
        target_sources(nuvio_engine PRIVATE src/security/windows_trust.cpp)
        target_link_libraries(nuvio_engine PRIVATE OpenSSL::SSL)
    endif()
else()
    target_sources(nuvio_engine PRIVATE src/torrent/protocol_backend_stub.cpp)
endif()

target_include_directories(
    nuvio_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(nuvio_engine PUBLIC cxx_std_20)
# NDK r27c libc++ provides C++20 stop_token/jthread, but gates them behind
# this opt-in until the implementation is no longer experimental. Retain the
# same cancellation/lifecycle implementation used by the host build.
if(ANDROID)
    target_compile_definitions(nuvio_engine PRIVATE _LIBCPP_ENABLE_EXPERIMENTAL=1)
endif()
target_link_libraries(nuvio_engine PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(nuvio_engine PRIVATE ws2_32 bcrypt crypt32)
    set_target_properties(nuvio_engine PROPERTIES PREFIX "")
    if(NUVIO_ENGINE_BUILD_SHARED)
        target_sources(nuvio_engine PRIVATE cmake/nuvio-engine-windows.def)
    endif()
endif()
if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID) AND NUVIO_ENGINE_BUILD_SHARED)
    target_link_options(
        nuvio_engine
        PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/nuvio-engine-linux.map"
            "LINKER:--exclude-libs,ALL"
            "LINKER:-z,defs"
            "LINKER:-z,relro,-z,now"
    )
endif()
if(APPLE AND NUVIO_ENGINE_BUILD_SHARED)
    set(NUVIO_ENGINE_APPLE_EXPORTS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/nuvio-engine-apple.exports"
    )
    target_link_options(
        nuvio_engine
        PRIVATE
            "LINKER:-exported_symbols_list,${NUVIO_ENGINE_APPLE_EXPORTS}"
            "LINKER:-dead_strip"
    )
    set_property(
        TARGET nuvio_engine
        APPEND
        PROPERTY LINK_DEPENDS "${NUVIO_ENGINE_APPLE_EXPORTS}"
    )
    set_target_properties(nuvio_engine PROPERTIES INSTALL_NAME_DIR "@rpath")
endif()
set_target_properties(
    nuvio_engine
    PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(NUVIO_ENGINE_BUILD_SHARED)
    target_compile_definitions(nuvio_engine PRIVATE NUVIO_ENGINE_BUILDING_SHARED)
    target_compile_definitions(nuvio_engine INTERFACE NUVIO_ENGINE_USING_SHARED)
endif()

if(MSVC)
    target_compile_options(nuvio_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(nuvio_engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(NUVIO_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    if(NUVIO_ENGINE_ENABLE_LIBTORRENT)
        target_compile_definitions(
            nuvio_engine_tests
            PRIVATE NUVIO_ENGINE_EXPECT_LIBTORRENT=1
        )
    endif()
endif()

install(
    TARGETS nuvio_engine
    EXPORT NuvioEngineTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT NuvioEngineTargets
    FILE NuvioEngineTargets.cmake
    NAMESPACE Nuvio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NuvioEngine
)
