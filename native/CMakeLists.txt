cmake_minimum_required(VERSION 3.16)
# RC = compile the Windows .rc that carries the app/exe icon (Windows-only, harmless elsewhere)
project(colosseum LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compile app_resources.qrc (embeds the .ico for setWindowIcon)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui Sql WebEngineQuick WebChannel WebSockets)

# --- video player: KDE mpvqt (libmpv-in-QtQuick) + libmpv ---
# Consumes the PROVEN feasibility build (don't rebuild — see kindled-reel recap).
# Toolchain-aware: MSVC (Qt WebEngine path, homeward-foundry) consumes the msvc
# MpvQt build + mpv.lib; MinGW the original build + libmpv.dll.a. Same source tree
# builds under either kit. Override MPVQT_PREFIX on the command line to vendor later.
if(MSVC)
    set(_MPVQT_DEFAULT "C:/tools/mpvqt-feasibility/mpvqt-msvc-install")
    set(_LIBMPV_IMPLIB "mpv.lib")
else()
    set(_MPVQT_DEFAULT "C:/tools/mpvqt-feasibility/mpvqt-install")
    set(_LIBMPV_IMPLIB "libmpv.dll.a")
endif()
set(MPVQT_PREFIX  "${_MPVQT_DEFAULT}" CACHE PATH "MpvQt install prefix")
set(LIBMPV_PREFIX "C:/tools/mpvqt-feasibility/libmpv-prefix" CACHE PATH "libmpv dev prefix")
list(PREPEND CMAKE_PREFIX_PATH "${MPVQT_PREFIX}")
# Pre-seed libmpv so MpvQt's bundled FindLibmpv (pkg-config based) succeeds on Windows.
set(Libmpv_INCLUDE_DIRS "${LIBMPV_PREFIX}/include" CACHE PATH "" FORCE)
set(Libmpv_LIBRARIES    "${LIBMPV_PREFIX}/lib/${_LIBMPV_IMPLIB}" CACHE FILEPATH "" FORCE)
find_package(MpvQt REQUIRED)

# ── libtorrent-rasterbar (tankorent engine core) ─────────────────────────────
# Ported from TB2 CMakeLists.txt (TANKORENT_DOWNLOAD_FLOW_FIX lineage):
# find_package CONFIG first, hardcoded C:/tools fallback second. Wrapped in an
# INTERFACE target so the app and the engine harnesses share ONE definition.
# Colosseum adaptation (justified): missing libtorrent is FATAL here, not a
# soft-disable — Colosseum has no stub lane; the engine is a commissioned organ
# (spec 2026-07-13) and a silent stub would violate source-honesty doctrine.
add_library(colosseum_libtorrent INTERFACE)
find_package(LibtorrentRasterbar CONFIG QUIET)
find_package(Boost CONFIG QUIET COMPONENTS system filesystem)
find_package(OpenSSL QUIET)
if(LibtorrentRasterbar_FOUND AND Boost_FOUND AND OpenSSL_FOUND)
    message(STATUS "libtorrent + Boost + OpenSSL found via find_package")
    target_compile_definitions(colosseum_libtorrent INTERFACE
        HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
    target_link_libraries(colosseum_libtorrent INTERFACE
        LibtorrentRasterbar::torrent-rasterbar Boost::system Boost::filesystem
        OpenSSL::SSL OpenSSL::Crypto ws2_32 mswsock crypt32 iphlpapi bcrypt)
else()
    set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc" CACHE PATH "libtorrent MSVC install")
    set(BOOST_ROOT "C:/tools/boost-1.84.0" CACHE PATH "Boost headers")
    set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc" CACHE PATH "OpenSSL MSVC install")
    find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp HINTS "${LIBTORRENT_ROOT}/include")
    find_library(LIBTORRENT_LIBRARY torrent-rasterbar HINTS "${LIBTORRENT_ROOT}/lib")
    find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS "${BOOST_ROOT}")
    if(LIBTORRENT_INCLUDE_DIR AND LIBTORRENT_LIBRARY AND BOOST_INCLUDE_DIR)
        message(STATUS "libtorrent found via C:/tools fallback: ${LIBTORRENT_LIBRARY}")
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
        message(FATAL_ERROR "libtorrent NOT found (find_package or C:/tools/libtorrent-2.0-msvc) — the tankorent engine requires it. See TB2 CMakeLists TANKORENT_DOWNLOAD_FLOW_FIX comment.")
    endif()
endif()

add_executable(colosseum
    main.cpp
    app_icon.rc              # Windows exe/taskbar icon (colosseum.ico)
    app_resources.qrc        # embeds colosseum.ico for QGuiApplication::setWindowIcon
    ClipboardHelper.h
    MangaEngine.h
    ProgressStore.h
    SearchHistoryStore.h
    SessionStore.h
    engine/WeebCentralScraper.cpp
    engine/WeebCentralScraper.h
    engine/MangaScraper.h
    engine/MangaResult.h
    engine/MangaSeriesDetail.h
    engine/MangaDexCatalogClient.cpp
    engine/MangaDexCatalogClient.h
    engine/MangaDownloader.cpp
    engine/MangaDownloader.h
    engine/BookDownloader.cpp
    engine/BookDownloader.h
    engine/AudiobookDownloader.cpp
    engine/AudiobookDownloader.h
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicDlsParse.cpp
    engine/ComicDlsParse.h
    engine/LocalDownloads.cpp
    engine/LocalDownloads.h
    engine/ExtensionsStore.cpp
    engine/ExtensionsStore.h
    reader/BookBridge.cpp
    reader/BookBridge.h
    tts/EdgeTtsClient.cpp
    tts/EdgeTtsClient.h
    tts/EdgeTtsWorker.cpp
    tts/EdgeTtsWorker.h
    player/caststore.cpp
    player/caststore.h
    player/downloadstore.cpp
    player/downloadstore.h
    player/livestore.cpp
    player/livestore.h
    player/mpvitem.cpp
    player/mpvitem.h
    player/mpvproperties.h
    player/powerstore.cpp
    player/powerstore.h
    player/roomstore.cpp
    player/roomstore.h
    player/streamserver.cpp
    player/streamserver.h
    player/windowmodestore.cpp
    player/windowmodestore.h
    torrent/TorrentResult.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
    torrent/BookTorrentFilePicker.cpp
    torrent/BookTorrentFilePicker.h
    torrent/BookTorrentMagnet.h
    torrent/BookTorrentDownloader.cpp
    torrent/BookTorrentDownloader.h
    torrent/BookTorrents.cpp
    torrent/BookTorrents.h
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/TorrentRow.h
    torrent/engine/TorrentRepository.cpp
    torrent/engine/TorrentRepository.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player torrent)
target_link_libraries(colosseum PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui Qt6::Sql MpvQt::MpvQt
    Qt6::WebEngineQuick Qt6::WebChannel Qt6::WebSockets colosseum_libtorrent)

add_executable(search_history_store_harness
    ../tests/search_history_store_harness.cpp
    SearchHistoryStore.h
)
target_include_directories(search_history_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_history_store_harness PRIVATE Qt6::Core)

add_executable(comic_dls_parse_harness
    ../tests/comic_dls_parse_harness.cpp
    engine/ComicDlsParse.cpp
)
target_include_directories(comic_dls_parse_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_dls_parse_harness PRIVATE Qt6::Core)

add_executable(book_torrent_ranker_harness
    ../tests/book_torrent_ranker_harness.cpp
    torrent/BookTorrentRanker.cpp
    torrent/BookTorrentRanker.h
)
target_include_directories(book_torrent_ranker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_ranker_harness PRIVATE Qt6::Core)

add_executable(book_torrent_filepicker_harness
    ../tests/book_torrent_filepicker_harness.cpp
    torrent/BookTorrentFilePicker.cpp
    torrent/BookTorrentFilePicker.h
)
target_include_directories(book_torrent_filepicker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_filepicker_harness PRIVATE Qt6::Core)

add_executable(book_torrent_magnet_harness
    ../tests/book_torrent_magnet_harness.cpp
)
target_include_directories(book_torrent_magnet_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_magnet_harness PRIVATE Qt6::Core)

add_executable(torrent_engine_link_harness
    ../tests/torrent_engine_link_harness.cpp
)
target_link_libraries(torrent_engine_link_harness PRIVATE colosseum_libtorrent)

add_executable(torrent_engine_download_harness
    ../tests/torrent_engine_download_harness.cpp
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
)
target_include_directories(torrent_engine_download_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Network: TorrentEngine.cpp line 28 has a vestigial <QHostAddress> include
# (TB2-ism, never referenced) — Network's include dirs are required to compile it.
# Do NOT delete the include; that breaks byte-diffability with TB2 (review I1, 2026-07-13).
target_link_libraries(torrent_engine_download_harness PRIVATE Qt6::Core Qt6::Network colosseum_libtorrent)

if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    add_custom_command(TARGET colosseum POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:colosseum>/sqldrivers"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_qt_bin_dir}/Qt6Sql.dll"
            "$<TARGET_FILE_DIR:colosseum>/Qt6Sql.dll"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_qt_bin_dir}/../plugins/sqldrivers/qsqlite.dll"
            "$<TARGET_FILE_DIR:colosseum>/sqldrivers/qsqlite.dll"
        VERBATIM)
endif()
