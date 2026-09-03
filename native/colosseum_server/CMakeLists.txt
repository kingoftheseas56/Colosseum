cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

if(NOT TARGET colosseum_libtorrent)
    find_package(LibtorrentRasterbar CONFIG QUIET)
    find_package(Boost CONFIG QUIET COMPONENTS system filesystem)
    find_package(OpenSSL QUIET)
    add_library(colosseum_libtorrent INTERFACE)
    if(LibtorrentRasterbar_FOUND AND Boost_FOUND AND OpenSSL_FOUND)
        target_link_libraries(colosseum_libtorrent INTERFACE
            LibtorrentRasterbar::torrent-rasterbar Boost::filesystem
            OpenSSL::SSL OpenSSL::Crypto)
        if(TARGET Boost::system)
            target_link_libraries(colosseum_libtorrent INTERFACE Boost::system)
        endif()
    elseif(WIN32)
        set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc")
        set(BOOST_ROOT "C:/tools/boost-1.90.0")
        set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc")
        find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp
            HINTS "${LIBTORRENT_ROOT}/include")
        find_library(LIBTORRENT_LIBRARY torrent-rasterbar
            HINTS "${LIBTORRENT_ROOT}/lib")
        find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")
        if(NOT LIBTORRENT_INCLUDE_DIR OR NOT LIBTORRENT_LIBRARY OR NOT BOOST_INCLUDE_DIR)
            message(FATAL_ERROR "Colosseum Server requires libtorrent + Boost")
        endif()
        target_include_directories(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_INCLUDE_DIR}" "${BOOST_INCLUDE_DIR}"
            "${OPENSSL_MSVC_ROOT}/include")
        target_link_libraries(colosseum_libtorrent INTERFACE
            "${LIBTORRENT_LIBRARY}"
            "${OPENSSL_MSVC_ROOT}/lib/libssl.lib"
            "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib"
            ws2_32 mswsock crypt32 iphlpapi bcrypt)
    else()
        message(FATAL_ERROR "Colosseum Server requires libtorrent + Boost + OpenSSL")
    endif()
    if(WIN32)
        target_compile_definitions(colosseum_libtorrent INTERFACE
            HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL TORRENT_USE_LIBCRYPTO
            TORRENT_SSL_PEERS OPENSSL_NO_SSL2 BOOST_ASIO_ENABLE_CANCELIO
            BOOST_ASIO_NO_DEPRECATED WIN32_LEAN_AND_MEAN BOOST_ALL_NO_LIB
            _SCL_SECURE_NO_DEPRECATE _CRT_SECURE_NO_DEPRECATE
            _WIN32_WINNT=0x0A00)
    else()
        target_compile_definitions(colosseum_libtorrent INTERFACE HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL)
    endif()
endif()
add_library(colosseum_server_native STATIC
    cache/CachePolicy.cpp
    core/ColosseumServer.cpp
    core/HttpConnection.cpp
    core/HttpRouter.cpp
    enginefs/EngineFsControlPlane.cpp
    enginefs/QtEngineFsTimerScheduler.cpp
    media/EmbeddedSamples.cpp
    media/HlsV2.cpp
    media/LegacyHls.cpp
    media/MediaPipeline.cpp
    media/MediaProbe.cpp
    media/SubtitleService.cpp
    media/TrackParser.cpp
    network_app/NetworkAppServices.cpp
    remote_archive/LzString.cpp
    remote_archive/RemoteArchive.cpp
    remote_archive/RemoteServices.cpp
    integration/PieceSourceBlockTransport.cpp
    integration/PieceSourceBlockTransport.h
    integration/LibtorrentBlockTransport.cpp
    integration/LibtorrentBlockTransport.h
    integration/TorrentPieceSource.cpp
    integration/TorrentPieceSource.h
    integration/TorrentHttpRouteAdapter.cpp
    integration/TorrentHttpRouteAdapter.h
    integration/AsyncMediaExecutor.cpp
    integration/AsyncMediaExecutor.h
    integration/FeatureRouteComposition.cpp
    integration/FeatureRouteComposition.h
    integration/ProductionTorrentBackend.cpp
    integration/ProductionTorrentBackend.h
    integration/SchedulerTransportBridge.cpp
    integration/SchedulerTransportBridge.h
    runtime/ColosseumServerRuntime.cpp
    runtime/ColosseumServerRuntime.h
    scheduler/FileStream.cpp
    scheduler/SchedulerSpine.cpp
    settings/ServerSettings.cpp
    torrent/DiscoverySession.cpp
    torrent/Storage.cpp
    torrent/model/MetadataExchange.cpp
    torrent/model/TorrentMetadata.cpp
    torrent/model/VirtualPieceMap.cpp
    torrent_http/StreamProgressTracker.cpp
    torrent_http/TorrentHttpSurface.cpp
    ../torrent/engine/TorrentEngine.cpp
    ../torrent/engine/TorrentEngine.h
    ../third_party/miniz/miniz.c
)

target_include_directories(colosseum_server_native PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniz"
)

target_link_libraries(colosseum_server_native PUBLIC
    Qt6::Core
    Qt6::Network
    colosseum_libtorrent
)

if(MSVC)
    target_compile_options(colosseum_server_native PRIVATE /W4 /permissive-)
else()
    target_compile_options(colosseum_server_native PRIVATE -Wall -Wextra -Wpedantic)
endif()
