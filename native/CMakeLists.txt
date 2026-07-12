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
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player torrent)
target_link_libraries(colosseum PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui Qt6::Sql MpvQt::MpvQt
    Qt6::WebEngineQuick Qt6::WebChannel Qt6::WebSockets)

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
