cmake_minimum_required(VERSION 3.16)
# RC = compile the Windows .rc that carries the app/exe icon (Windows-only, harmless elsewhere)
project(colosseum LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compile app_resources.qrc (embeds the .ico for setWindowIcon)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui Sql WebEngineQuick WebChannel WebSockets Concurrent)

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
    AudioPairingStore.h
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
    engine/ComicsCatalog.cpp
    engine/ComicsCatalog.h
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
    player/windowstatepolicy.cpp
    player/windowstatepolicy.h
    torrent/TorrentResult.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/KnabenIndexer.cpp
    torrent/KnabenIndexer.h
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
    torrent/ComicTorrentRanker.cpp
    torrent/ComicTorrentRanker.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    torrent/ComicTorrentQueryPlanner.cpp
    torrent/ComicTorrentQueryPlanner.h
    torrent/ComicTorrentFilePicker.cpp
    torrent/ComicTorrentFilePicker.h
    torrent/ComicTorrentMagnet.h
    torrent/ComicTorrentDownloader.cpp
    torrent/ComicTorrentDownloader.h
    torrent/ComicEditionFileSelector.cpp
    torrent/ComicEditionFileSelector.h
    torrent/ComicRequestLedger.cpp
    torrent/ComicRequestLedger.h
    engine/ComicEditionAssembler.cpp
    engine/ComicEditionAssembler.h
    torrent/ComicTorrents.cpp
    torrent/ComicTorrents.h
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/TorrentRow.h
    torrent/engine/TorrentRepository.cpp
    torrent/engine/TorrentRepository.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
    # ── Tankoban volume mode (Tasks 1–8): now shipped in the app, not just harnesses ──
    engine/MangaTankobanTypes.h
    engine/MangaTankobanLogic.cpp
    engine/MangaTankobanLogic.h
    torrent/MangaNyaaSource.cpp
    torrent/MangaNyaaSource.h
    engine/MangaSynopsisEnricher.cpp
    engine/MangaSynopsisEnricher.h
    torrent/MangaVolumeFilePicker.cpp
    torrent/MangaVolumeFilePicker.h
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeIndex.h
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaVolumeArchiveIngestor.h
    torrent/MangaVolumeTorrentDownloader.cpp
    torrent/MangaVolumeTorrentDownloader.h
    torrent/MangaVolumeRequestLedger.cpp
    torrent/MangaVolumeRequestLedger.h
    engine/MangaVolumePacker.cpp
    engine/MangaVolumePacker.h
    engine/MangaTankobanService.cpp
    engine/MangaTankobanService.h
    # ── Keyless anime ordering (spec 2026-07-15) ──
    anime/AnimeOrderIndex.cpp
    anime/AnimeOrderIndex.h
    anime/AnimeOrderService.cpp
    anime/AnimeOrderService.h
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player torrent)
target_link_libraries(colosseum PRIVATE Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui Qt6::Sql Qt6::Concurrent MpvQt::MpvQt
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

add_executable(comic_torrent_filepicker_harness
    ../tests/comic_torrent_filepicker_harness.cpp
    torrent/ComicTorrentFilePicker.cpp
    torrent/ComicTorrentFilePicker.h
)
target_include_directories(comic_torrent_filepicker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_filepicker_harness PRIVATE Qt6::Core)

add_executable(comic_torrent_ranker_harness
    ../tests/comic_torrent_ranker_harness.cpp
    torrent/ComicTorrentRanker.cpp
    torrent/ComicTorrentRanker.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    app_resources.qrc
)
target_include_directories(comic_torrent_ranker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_ranker_harness PRIVATE Qt6::Core)

add_executable(comic_torrent_query_planner_harness
    ../tests/comic_torrent_query_planner_harness.cpp
    torrent/ComicTorrentQueryPlanner.cpp
    torrent/ComicTorrentQueryPlanner.h
)
target_include_directories(comic_torrent_query_planner_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_query_planner_harness PRIVATE Qt6::Core)

add_executable(comic_edition_identity_harness
    ../tests/comic_edition_identity_harness.cpp
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
)
target_include_directories(comic_edition_identity_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_edition_identity_harness PRIVATE Qt6::Core)

# Manifest-selection decision: safe payload isolation inside a chosen pack
# (spec 2026-07-15 "Manifest selection").
add_executable(comic_edition_file_selector_harness
    ../tests/comic_edition_file_selector_harness.cpp
    torrent/ComicEditionFileSelector.cpp
    torrent/ComicEditionFileSelector.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
)
target_include_directories(comic_edition_file_selector_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_edition_file_selector_harness PRIVATE Qt6::Core)

# Restart-safe collected-edition request ledger (spec 2026-07-15 "Durable
# shared-infohash transport" -> ComicRequestLedger). Pure QSaveFile/QJson
# persistence, ported from the proven MangaVolumeRequestLedger discipline.
add_executable(comic_request_ledger_harness
    ../tests/comic_request_ledger_harness.cpp
    torrent/ComicRequestLedger.cpp
    torrent/ComicRequestLedger.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
)
target_include_directories(comic_request_ledger_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_request_ledger_harness PRIVATE Qt6::Core)

# Assembly: selected payload (single archive / issue-archive set / loose-image
# subtree / confirmed combined archive) -> one validated page staging dir,
# never publishing partial output (spec 2026-07-15 "Assembly and
# publication"). Only needs ComicEditionFileSelector.h's POD types (enum +
# struct) — no linkage against ComicEditionFileSelector.cpp/ComicEditionIdentity
# is required since this harness never calls into that selection logic.
add_executable(comic_edition_assembler_harness
    ../tests/comic_edition_assembler_harness.cpp
    engine/ComicEditionAssembler.cpp
    engine/ComicEditionAssembler.h
)
target_include_directories(comic_edition_assembler_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(comic_edition_assembler_harness PRIVATE
    COMICS_PACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/comics-pack")
target_link_libraries(comic_edition_assembler_harness PRIVATE Qt6::Core)

# Coverage grammar + uploader trust reader (spec 2026-07-15 "Coverage and
# trust"). app_resources.qrc is listed as a source so AUTORCC compiles the
# bundled comics_uploader_trust.json into THIS harness too — otherwise
# ComicUploaderTrust::load() would find no ":/tankorent/..." resource here.
add_executable(comic_coverage_trust_harness
    ../tests/comic_coverage_trust_harness.cpp
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    app_resources.qrc
)
target_include_directories(comic_coverage_trust_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_coverage_trust_harness PRIVATE Qt6::Core)

add_executable(comic_torrent_magnet_harness
    ../tests/comic_torrent_magnet_harness.cpp
    torrent/ComicTorrentMagnet.h
)
target_include_directories(comic_torrent_magnet_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_magnet_harness PRIVATE Qt6::Core)

add_executable(comic_torrent_seed_harness
    ../tests/comic_torrent_seed_harness.cpp
)
target_include_directories(comic_torrent_seed_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_seed_harness PRIVATE Qt6::Core colosseum_libtorrent)

# Task 11 (real-engine DLTEST gate): a legal loopback seeder for the
# select->download->assemble->publish->restart chain's Compendium/issue-set/
# shared-infohash/restart-replay scenarios. Needs libtorrent — same
# colosseum_libtorrent INTERFACE target as its single-file sibling above
# (this build FATAL_ERRORs already if libtorrent is missing, so there is no
# separate CMake-level HAS_LIBTORRENT branch to gate; the .cpp itself carries
# no #ifdef because it always builds alongside the rest of this torrent code).
add_executable(comic_torrent_pack_seed_harness
    ../tests/comic_torrent_pack_seed_harness.cpp
)
target_include_directories(comic_torrent_pack_seed_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_pack_seed_harness PRIVATE Qt6::Core colosseum_libtorrent)

add_executable(comic_downloader_ingest_harness
    ../tests/comic_downloader_ingest_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicDlsParse.cpp
    engine/ComicDlsParse.h
    engine/ComicEditionAssembler.cpp
    engine/ComicEditionAssembler.h
    torrent/ComicTorrents.cpp
    torrent/ComicTorrents.h
    torrent/ComicTorrentDownloader.cpp
    torrent/ComicTorrentDownloader.h
    torrent/ComicEditionFileSelector.cpp
    torrent/ComicEditionFileSelector.h
    torrent/ComicRequestLedger.cpp
    torrent/ComicRequestLedger.h
    torrent/ComicTorrentFilePicker.cpp
    torrent/ComicTorrentFilePicker.h
    torrent/ComicTorrentRanker.cpp
    torrent/ComicTorrentRanker.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    torrent/ComicTorrentQueryPlanner.cpp
    torrent/ComicTorrentQueryPlanner.h
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/KnabenIndexer.cpp
    torrent/KnabenIndexer.h
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
    app_resources.qrc
)
target_include_directories(comic_downloader_ingest_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_downloader_ingest_harness PRIVATE Qt6::Core Qt6::Network colosseum_libtorrent)

add_executable(comic_torrents_search_harness
    ../tests/comic_torrents_search_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicDlsParse.cpp
    engine/ComicDlsParse.h
    engine/ComicEditionAssembler.cpp
    engine/ComicEditionAssembler.h
    torrent/ComicTorrents.cpp
    torrent/ComicTorrents.h
    torrent/ComicTorrentDownloader.cpp
    torrent/ComicTorrentDownloader.h
    torrent/ComicEditionFileSelector.cpp
    torrent/ComicEditionFileSelector.h
    torrent/ComicRequestLedger.cpp
    torrent/ComicRequestLedger.h
    torrent/ComicTorrentFilePicker.cpp
    torrent/ComicTorrentFilePicker.h
    torrent/ComicTorrentRanker.cpp
    torrent/ComicTorrentRanker.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    torrent/ComicTorrentQueryPlanner.cpp
    torrent/ComicTorrentQueryPlanner.h
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/KnabenIndexer.cpp
    torrent/KnabenIndexer.h
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
    app_resources.qrc
)
target_include_directories(comic_torrents_search_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrents_search_harness PRIVATE Qt6::Core Qt6::Network colosseum_libtorrent)

# Task 9 (spec 2026-07-15 "Durable shared-infohash transport"): drives
# ComicTorrentDownloader's edition pack transport through a FAKE
# IComicTorrentEngine (no real libtorrent session touched at runtime) plus a
# REAL ComicEditionAssembler + REAL ComicDownloader ingest target (both are
# already-proven synchronous/local components — see comic_edition_assembler_
# harness and comic_downloader_ingest_harness). Links the same comics stack as
# comic_downloader_ingest_harness because ComicDownloader.cpp transitively
# requires ComicTorrents.cpp -> TorrentEngine.cpp/colosseum_libtorrent to
# link, even though this harness's own ComicTorrentDownloader instances are
# always constructed over the fake engine, never the real adapter.
add_executable(comic_torrent_pack_transport_harness
    ../tests/comic_torrent_pack_transport_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicDlsParse.cpp
    engine/ComicDlsParse.h
    engine/ComicEditionAssembler.cpp
    engine/ComicEditionAssembler.h
    torrent/ComicTorrents.cpp
    torrent/ComicTorrents.h
    torrent/ComicTorrentDownloader.cpp
    torrent/ComicTorrentDownloader.h
    torrent/ComicEditionFileSelector.cpp
    torrent/ComicEditionFileSelector.h
    torrent/ComicRequestLedger.cpp
    torrent/ComicRequestLedger.h
    torrent/ComicEditionIdentity.cpp
    torrent/ComicEditionIdentity.h
    torrent/ComicCoverage.cpp
    torrent/ComicCoverage.h
    torrent/ComicUploaderTrust.cpp
    torrent/ComicUploaderTrust.h
    torrent/ComicTorrentFilePicker.cpp
    torrent/ComicTorrentFilePicker.h
    torrent/ComicTorrentRanker.cpp
    torrent/ComicTorrentRanker.h
    torrent/ComicTorrentQueryPlanner.cpp
    torrent/ComicTorrentQueryPlanner.h
    torrent/TankorentSearchService.cpp
    torrent/TankorentSearchService.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
    torrent/PirateBayIndexer.cpp
    torrent/PirateBayIndexer.h
    torrent/ExtTorrentsIndexer.cpp
    torrent/ExtTorrentsIndexer.h
    torrent/TorrentsCsvIndexer.cpp
    torrent/TorrentsCsvIndexer.h
    torrent/KnabenIndexer.cpp
    torrent/KnabenIndexer.h
    torrent/engine/TorrentEngine.cpp
    torrent/engine/TorrentEngine.h
    torrent/engine/DebugLogBuffer.cpp
    torrent/engine/DebugLogBuffer.h
    app_resources.qrc
)
target_include_directories(comic_torrent_pack_transport_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_torrent_pack_transport_harness PRIVATE Qt6::Core Qt6::Network colosseum_libtorrent)

add_executable(book_torrent_magnet_harness
    ../tests/book_torrent_magnet_harness.cpp
)
target_include_directories(book_torrent_magnet_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_magnet_harness PRIVATE Qt6::Core)

add_executable(manga_volume_filepicker_harness
    ../tests/manga_volume_filepicker_harness.cpp
    torrent/MangaVolumeFilePicker.cpp
)
target_include_directories(manga_volume_filepicker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_volume_filepicker_harness PRIVATE Qt6::Core)

add_executable(manga_tankoban_logic_harness
    ../tests/manga_tankoban_logic_harness.cpp
    engine/MangaTankobanLogic.cpp
    engine/MangaSynopsisEnricher.cpp
    torrent/MangaNyaaSource.cpp
)
target_include_directories(manga_tankoban_logic_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_tankoban_logic_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_tankoban_logic_harness PRIVATE Qt6::Core Qt6::Network)

# knaben_probe — in-process Cloudflare/results gate for the knaben.org meta-search
# aggregator, a candidate Tankorent indexer. Hits the API through Qt's QNAM (the
# same TLS/HTTP-2 fingerprint the live indexers present) so the CF verdict is real,
# not curl's. Qt6::Network only, no libtorrent.
add_executable(knaben_probe
    ../tests/knaben_probe.cpp
)
target_include_directories(knaben_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(knaben_probe PRIVATE Qt6::Core Qt6::Network)

# knaben_indexer_harness — pure contract test for KnabenIndexer's request shape
# (relevance, never seeder-sorted) and JSON->TorrentResult parse. No network.
add_executable(knaben_indexer_harness
    ../tests/knaben_indexer_harness.cpp
    torrent/KnabenIndexer.cpp
    torrent/KnabenIndexer.h
    torrent/TorrentIndexer.cpp
    torrent/TorrentIndexer.h
)
target_include_directories(knaben_indexer_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(knaben_indexer_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(manga_volume_index_harness
    ../tests/manga_volume_index_harness.cpp
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaTankobanLogic.cpp
)
target_include_directories(manga_volume_index_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_volume_index_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_volume_index_harness PRIVATE Qt6::Core)

add_executable(manga_volume_packer_harness
    ../tests/manga_volume_packer_harness.cpp
    engine/MangaVolumePacker.cpp
    engine/MangaScraper.h
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaTankobanLogic.cpp
)
target_include_directories(manga_volume_packer_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_volume_packer_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_volume_packer_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(manga_volume_torrent_harness
    ../tests/manga_volume_torrent_harness.cpp
    torrent/MangaVolumeTorrentDownloader.cpp
    torrent/MangaVolumeRequestLedger.cpp
    torrent/MangaVolumeFilePicker.cpp
)
target_include_directories(manga_volume_torrent_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_volume_torrent_harness PRIVATE Qt6::Core)

add_executable(torrent_engine_link_harness
    ../tests/torrent_engine_link_harness.cpp
)
target_link_libraries(torrent_engine_link_harness PRIVATE colosseum_libtorrent)

# The façade harness composes the whole pipeline with a FAKE torrent engine +
# FAKE Nyaa search, so it must NOT link colosseum_libtorrent. With HAS_LIBTORRENT
# undefined here, MangaTankobanService's real TorrentEngine adapter + production
# ctor compile out, and the DI ctor drives the real index/ingestor/enricher.
add_executable(manga_tankoban_service_harness
    ../tests/manga_tankoban_service_harness.cpp
    engine/MangaTankobanService.cpp
    engine/MangaTankobanService.h
    engine/MangaTankobanLogic.cpp
    engine/MangaSynopsisEnricher.cpp
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaVolumePacker.cpp
    engine/MangaScraper.h
    torrent/MangaNyaaSource.cpp
    torrent/MangaVolumeTorrentDownloader.cpp
    torrent/MangaVolumeRequestLedger.cpp
    torrent/MangaVolumeFilePicker.cpp
)
target_include_directories(manga_tankoban_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_tankoban_service_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_tankoban_service_harness PRIVATE Qt6::Core Qt6::Network)

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

# ── Keyless anime ordering (spec 2026-07-15): pure parser/resolver harness ──
add_executable(anime_order_index_harness
    ../tests/anime_order_index_harness.cpp
    anime/AnimeOrderIndex.cpp
    anime/AnimeOrderIndex.h)
target_include_directories(anime_order_index_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(anime_order_index_harness PRIVATE Qt6::Core)

# ── Anime ordering cache/refresh service harness (local HTTP server, no net) ──
add_executable(anime_order_service_harness
    ../tests/anime_order_service_harness.cpp
    anime/AnimeOrderIndex.cpp
    anime/AnimeOrderService.cpp)
target_include_directories(anime_order_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(anime_order_service_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)

# ── Secret windowed developer mode (spec 2026-07-15): pure geometry policy +
#    persisted shell-state harness (extended in Task 2 to link Gui/Quick) ──
add_executable(window_state_policy_harness
    ../tests/window_state_policy_harness.cpp
    player/windowstatepolicy.cpp
    player/windowstatepolicy.h
    player/windowmodestore.cpp
    player/windowmodestore.h)
target_include_directories(window_state_policy_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(window_state_policy_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

# GUI harness (offscreen QPA): PiP round-trips + live-reload re-attach on a real QQuickWindow.
add_executable(window_shell_gui_harness
    ../tests/window_shell_gui_harness.cpp
    player/windowstatepolicy.cpp
    player/windowstatepolicy.h
    player/windowmodestore.cpp
    player/windowmodestore.h)
target_include_directories(window_shell_gui_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(window_shell_gui_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

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

add_executable(comics_catalog_engine_harness
    ../tests/comics_catalog_engine_harness.cpp
    engine/ComicsCatalog.cpp
    engine/ComicsCatalog.h)
target_include_directories(comics_catalog_engine_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comics_catalog_engine_harness PRIVATE Qt6::Core Qt6::Sql)
