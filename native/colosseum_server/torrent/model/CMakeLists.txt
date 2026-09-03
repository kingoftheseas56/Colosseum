cmake_minimum_required(VERSION 3.24)
project(arc44_w04_torrent_model LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 REQUIRED COMPONENTS Core)

if(NOT TARGET colosseum_libtorrent)
    find_package(LibtorrentRasterbar CONFIG QUIET)
    find_package(Boost CONFIG QUIET COMPONENTS system filesystem)
    find_package(OpenSSL QUIET)
    add_library(colosseum_libtorrent INTERFACE)
    if(LibtorrentRasterbar_FOUND AND Boost_FOUND AND OpenSSL_FOUND)
        target_link_libraries(colosseum_libtorrent INTERFACE
            LibtorrentRasterbar::torrent-rasterbar Boost::filesystem OpenSSL::SSL OpenSSL::Crypto)
        if(TARGET Boost::system)
            target_link_libraries(colosseum_libtorrent INTERFACE Boost::system)
        endif()
    elseif(WIN32)
        set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc")
        set(BOOST_ROOT "C:/tools/boost-1.84.0")
        set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc")
        find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp HINTS "${LIBTORRENT_ROOT}/include")
        find_library(LIBTORRENT_LIBRARY torrent-rasterbar HINTS "${LIBTORRENT_ROOT}/lib")
        find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")
        if(NOT LIBTORRENT_INCLUDE_DIR OR NOT LIBTORRENT_LIBRARY OR NOT BOOST_INCLUDE_DIR)
            message(FATAL_ERROR "Arc44 W04 harness requires libtorrent + Boost")
        endif()
        target_include_directories(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_INCLUDE_DIR}" "${BOOST_INCLUDE_DIR}" "${OPENSSL_MSVC_ROOT}/include")
        target_link_libraries(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_LIBRARY}"
            "${OPENSSL_MSVC_ROOT}/lib/libssl.lib"
            "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib"
            ws2_32 mswsock crypt32 iphlpapi bcrypt)
    else()
        message(FATAL_ERROR "Arc44 W04 harness requires libtorrent + Boost + OpenSSL")
    endif()
    if(WIN32)
        target_compile_definitions(colosseum_libtorrent INTERFACE
            HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
    else()
        target_compile_definitions(colosseum_libtorrent INTERFACE HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL)
    endif()
endif()

set(WORKTREE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
add_executable(arc44_w04_torrent_model_harness
    "${WORKTREE_ROOT}/tests/arc44_w04_torrent_model_harness.cpp"
    TorrentMetadata.cpp
    TorrentMetadata.h
    VirtualPieceMap.cpp
    VirtualPieceMap.h
    MetadataExchange.cpp
    MetadataExchange.h
)
target_include_directories(arc44_w04_torrent_model_harness PRIVATE
    "${WORKTREE_ROOT}/native")
target_compile_definitions(arc44_w04_torrent_model_harness PRIVATE
    ARC44_W04_FIXTURE_DIR="${WORKTREE_ROOT}/tests/fixtures/arc44_w04")
target_link_libraries(arc44_w04_torrent_model_harness PRIVATE
    Qt6::Core colosseum_libtorrent)
