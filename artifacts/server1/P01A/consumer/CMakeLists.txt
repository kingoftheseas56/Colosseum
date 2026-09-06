cmake_minimum_required(VERSION 3.16)
project(colosseum_libtorrent_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
add_library(colosseum_libtorrent INTERFACE)
find_package(LibtorrentRasterbar CONFIG QUIET)
find_package(Boost CONFIG QUIET COMPONENTS system filesystem)
find_package(OpenSSL QUIET)
if(LibtorrentRasterbar_FOUND AND Boost_FOUND AND OpenSSL_FOUND)
    target_link_libraries(colosseum_libtorrent INTERFACE
        LibtorrentRasterbar::torrent-rasterbar Boost::filesystem
        OpenSSL::SSL OpenSSL::Crypto)
    if(TARGET Boost::system)
        target_link_libraries(colosseum_libtorrent INTERFACE Boost::system)
    endif()
elseif(WIN32)
    set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc" CACHE PATH "libtorrent MSVC install")
    set(BOOST_ROOT "C:/tools/boost-1.84.0" CACHE PATH "Boost headers")
    set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc" CACHE PATH "OpenSSL MSVC install")
    find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp HINTS "${LIBTORRENT_ROOT}/include")
    find_library(LIBTORRENT_LIBRARY torrent-rasterbar HINTS "${LIBTORRENT_ROOT}/lib")
    find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")
    if(LIBTORRENT_INCLUDE_DIR AND LIBTORRENT_LIBRARY AND BOOST_INCLUDE_DIR)
        target_include_directories(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_INCLUDE_DIR}" "${BOOST_INCLUDE_DIR}" "${OPENSSL_MSVC_ROOT}/include")
        target_compile_definitions(colosseum_libtorrent INTERFACE
            HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
        target_link_libraries(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_LIBRARY}"
            "${OPENSSL_MSVC_ROOT}/lib/libssl.lib"
            "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib"
            ws2_32 mswsock crypt32 iphlpapi bcrypt)
    else()
        message(FATAL_ERROR "libtorrent NOT found (find_package or C:/tools fallback)")
    endif()
else()
    message(FATAL_ERROR "libtorrent + Boost + OpenSSL NOT found")
endif()
add_executable(libtorrent_identity_probe main.cpp)
target_link_libraries(libtorrent_identity_probe PRIVATE colosseum_libtorrent)
