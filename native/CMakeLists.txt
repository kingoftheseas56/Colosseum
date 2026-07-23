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

# --- shared offline-ML seam: ONNX Runtime (guided comics + audiobook alignment) ---
# Dormant by default so a fresh clone builds without the ~300 MB runtime staged.
# Arcs that link it configure with -DCOLOSSEUM_ENABLE_ONNX=ON after running
# scripts/native/fetch_onnxruntime.ps1 once per machine.
option(COLOSSEUM_ENABLE_ONNX "Enable targets that link ONNX Runtime" OFF)
if(COLOSSEUM_ENABLE_ONNX)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OnnxRuntime.cmake)
endif()

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
    CollectionStore.h
    SearchHistoryStore.h
    SessionStore.h
    AudioPairingStore.h
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h
    third_party/miniz/miniz.c      # gunzip for the Jikan Accept-Encoding workaround (needs C, enabled below)
    guided/GuidedCameraController.cpp
    guided/GuidedCameraController.h
    guided/GuidedTypes.cpp
    guided/GuidedTypes.h
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
    engine/MalCatalog.cpp
    engine/MalCatalog.h
    engine/LocalDownloads.cpp
    engine/LocalDownloads.h
    engine/ExtensionsStore.cpp
    engine/ExtensionsStore.h
    reader/BookBridge.cpp
    reader/BookBridge.h
    reader/BookStores.cpp
    reader/BookStores.h
    reader2/Reader2Bridge.cpp
    reader2/Reader2Bridge.h
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
    player/seekthumbnailer.cpp
    player/seekthumbnailer.h
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

# ── Guided panel detector (ONNX Runtime) — dormant by default ─────────────────
# Only when COLOSSEUM_ENABLE_ONNX=ON: compile the real detector + the guided
# analysis domain into the app and light up the GuidedAnalysis service (main.cpp
# guards its own construction on the same macro). The default build ships the
# whole-page Guided shell with zero ONNX dependency, so siblings are untouched.
if(COLOSSEUM_ENABLE_ONNX)
    target_sources(colosseum PRIVATE
        guided/PanelDetector.h
        guided/PanelDetectorOnnx.cpp guided/PanelDetectorOnnx.h
        guided/PanelPlanner.cpp guided/PanelPlanner.h
        guided/PanelMapStore.cpp guided/PanelMapStore.h
        guided/PanelAnalysisService.cpp guided/PanelAnalysisService.h
        models/ModelManifest.cpp models/ModelManifest.h
        # Audiobook↔EPUB read-along domain (Agent 2, Tasks 3–12). miniz, the work
        # spine, ModelManifest and Qt6::Sql are already linked into colosseum above.
        alignment/AlignmentPipeline.cpp alignment/AlignmentPipeline.h
        alignment/AudioTextAlignmentService.cpp alignment/AudioTextAlignmentService.h
        alignment/AlignmentStore.cpp alignment/AlignmentStore.h
        alignment/AlignmentTypes.h
        alignment/AudiobookAnalysisDecoder.cpp alignment/AudiobookAnalysisDecoder.h
        alignment/CoarseTranscriber.cpp alignment/CoarseTranscriber.h
        alignment/CoarseTypes.h
        alignment/EpubSequenceMatcher.cpp alignment/EpubSequenceMatcher.h
        alignment/EpubTextIndexer.cpp alignment/EpubTextIndexer.h
        alignment/EnglishForcedAligner.cpp alignment/EnglishForcedAligner.h
        alignment/ReadAlongController.cpp alignment/ReadAlongController.h)
    target_link_libraries(colosseum PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(colosseum PRIVATE COLOSSEUM_ENABLE_ONNX)
    # ship onnxruntime.dll beside the exe so the app can load it
    add_custom_command(TARGET colosseum POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:onnxruntime::onnxruntime,IMPORTED_LOCATION>"
            "$<TARGET_FILE_DIR:colosseum>"
        VERBATIM)

    add_executable(panel_detector_harness
        ../tests/panel_detector_harness.cpp
        guided/PanelDetectorOnnx.cpp guided/PanelDetectorOnnx.h
        guided/GuidedTypes.cpp guided/GuidedTypes.h
        models/ModelManifest.cpp models/ModelManifest.h
        work/BackgroundWorkCoordinator.cpp work/BackgroundWorkCoordinator.h)
    target_include_directories(panel_detector_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(panel_detector_harness PRIVATE Qt6::Core Qt6::Gui onnxruntime::onnxruntime)
    target_compile_definitions(panel_detector_harness PRIVATE COLOSSEUM_ENABLE_ONNX)
    add_custom_command(TARGET panel_detector_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:onnxruntime::onnxruntime,IMPORTED_LOCATION>"
            "$<TARGET_FILE_DIR:panel_detector_harness>"
        VERBATIM)
endif()

add_executable(search_history_store_harness
    ../tests/search_history_store_harness.cpp
    SearchHistoryStore.h
)
target_include_directories(search_history_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_history_store_harness PRIVATE Qt6::Core)

add_executable(background_work_coordinator_harness
    ../tests/background_work_coordinator_harness.cpp
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(background_work_coordinator_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_work_coordinator_harness PRIVATE Qt6::Core)

add_executable(background_activity_registry_harness
    ../tests/background_activity_registry_harness.cpp
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h)
target_include_directories(background_activity_registry_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_activity_registry_harness PRIVATE Qt6::Core)

add_executable(model_manifest_harness
    ../tests/model_manifest_harness.cpp
    models/ModelManifest.cpp
    models/ModelManifest.h)
target_include_directories(model_manifest_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(model_manifest_harness PRIVATE Qt6::Core)

add_executable(reader2_stores_harness
    ../tests/reader2_stores_harness.cpp
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(reader2_stores_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_stores_harness PRIVATE Qt6::Core)

add_executable(reader2_bridge_harness
    ../tests/reader2_bridge_harness.cpp
    reader2/Reader2Bridge.cpp
    reader2/Reader2Bridge.h
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(reader2_bridge_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_bridge_harness PRIVATE Qt6::Core Qt6::Network)

# Task 12: audiobook auto-attach on download completion. Offline/deterministic —
# StreamServer is passed nullptr (its .cpp is only Qt Core/Network: QProcess + NAM,
# no libtorrent), so the attach logic is proven without a live download.
add_executable(reader2_autoattach_harness
    ../tests/reader2_autoattach_harness.cpp
    engine/AudiobookDownloader.cpp
    engine/AudiobookDownloader.h
    AudioPairingStore.h
    player/streamserver.cpp
    player/streamserver.h
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(reader2_autoattach_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_autoattach_harness PRIVATE Qt6::Core Qt6::Network)

# Task 5: standalone "first pixels" harness — boots the fresh reader (native QML
# chrome over the vendored foliate paper) with a book shelf, no Colosseum shell.
# Links only what the reader seam needs (Reader2Bridge + BookStores) plus the QML
# stack; deploy with windeployqt (WebEngine variant) before running.
add_executable(reader2_harness
    reader2/reader2_harness_main.cpp
    reader2/Reader2Bridge.cpp
    reader2/Reader2Bridge.h
    reader/BookStores.cpp
    reader/BookStores.h
    ClipboardHelper.h          # Q_OBJECT header → listed so AUTOMOC generates its meta-object
    # Task 13 read-along backends — the same types main.cpp registers. AudiobookDownloader
    # links with just Core/Network (StreamServer passed nullptr; its .cpp is QProcess+NAM,
    # no libtorrent). MpvItem gives AudiobookSession.qml its `Colosseum.Player` surface so
    # the harness drives the REAL shared session; libmpv-2.dll/MpvQt.dll already sit beside
    # the exe (deployed for colosseum.exe in the shared build dir).
    AudioPairingStore.h
    engine/AudiobookDownloader.cpp
    engine/AudiobookDownloader.h
    player/streamserver.cpp
    player/streamserver.h
    player/mpvitem.cpp
    player/mpvitem.h
)
target_include_directories(reader2_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reader2_harness PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::WebEngineQuick Qt6::WebChannel MpvQt::MpvQt)

add_executable(progress_store_harness
    ../tests/progress_store_harness.cpp
    ProgressStore.h
)
target_include_directories(progress_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(progress_store_harness PRIVATE Qt6::Core)

add_executable(progress_watched_override_harness
    ../tests/progress_watched_override_harness.cpp
    ProgressStore.h
)
target_include_directories(progress_watched_override_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(progress_watched_override_harness PRIVATE Qt6::Core)

add_executable(collection_store_harness
    ../tests/collection_store_harness.cpp
    CollectionStore.h
)
target_include_directories(collection_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(collection_store_harness PRIVATE Qt6::Core)

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

# Pure-logic contract for the MangaDex phantom-volume fold (the Berserk finding):
# compiles the client for foldPhantomCoverVolumes; Network only for its includes.
add_executable(mangadex_volume_fold_harness
    ../tests/mangadex_volume_fold_harness.cpp
    engine/MangaDexCatalogClient.cpp
    engine/MangaDexCatalogClient.h
)
target_include_directories(mangadex_volume_fold_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mangadex_volume_fold_harness PRIVATE Qt6::Core Qt6::Network)

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

# Live triage probe (2026-07-18): the audiobook delivery chain end-to-end —
# real AudiobookDownloader + StreamServer, network-dependent, NOT in the suite.
add_executable(audiobook_engine_probe
    ../tests/audiobook_engine_probe.cpp
    engine/AudiobookDownloader.cpp
    engine/AudiobookDownloader.h
    AudioPairingStore.h
    player/streamserver.cpp
    player/streamserver.h
)
target_include_directories(audiobook_engine_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(audiobook_engine_probe PRIVATE Qt6::Core Qt6::Network)

option(COLOSSEUM_BUILD_PLAYER2 "Build the isolated Player 2 laboratory" OFF)
if(COLOSSEUM_BUILD_PLAYER2)
    add_subdirectory(player2)
endif()

# --- Guided comic reader (Agent 1) : panel-aware guided reader harnesses ---
add_executable(guided_types_harness
    ../tests/guided_types_harness.cpp
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(guided_types_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(guided_types_harness PRIVATE Qt6::Core)

add_executable(panel_map_store_harness
    ../tests/panel_map_store_harness.cpp
    guided/PanelMapStore.cpp guided/PanelMapStore.h
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(panel_map_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(panel_map_store_harness PRIVATE Qt6::Core Qt6::Sql)

# ── Audiobook↔EPUB read-along alignment (Agent 2, plan 2026-07-22) ────────────
# Durable SQLite store of sentence/word cues per book↔audiobook pair. Qt Core+Sql
# only (no whisper/ONNX at this layer). New Agent-2 alignment targets append below.
add_executable(alignment_store_harness
    ../tests/alignment_store_harness.cpp
    alignment/AlignmentStore.cpp
    alignment/AlignmentStore.h
    alignment/AlignmentTypes.h)
target_include_directories(alignment_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(alignment_store_harness PRIVATE Qt6::Core Qt6::Sql)

add_executable(panel_planner_order_harness
    ../tests/panel_planner_order_harness.cpp
    guided/PanelPlanner.cpp guided/PanelPlanner.h
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(panel_planner_order_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(panel_planner_order_harness PRIVATE
    GUIDED_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/guided")
target_link_libraries(panel_planner_order_harness PRIVATE Qt6::Core)

add_executable(panel_planner_camera_harness
    ../tests/panel_planner_camera_harness.cpp
    guided/PanelPlanner.cpp guided/PanelPlanner.h
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(panel_planner_camera_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(panel_planner_camera_harness PRIVATE Qt6::Core)

# EPUB canonical text index (Agent 2). Vendored miniz opens the container; the walk
# and canonical fold mirror resources/reader2/alignment_text.js byte-for-byte. Qt Core.
# miniz.c is C — the project declares only CXX/RC, so enable C for the vendored reader.
enable_language(C)
add_executable(epub_text_indexer_harness
    ../tests/epub_text_indexer_harness.cpp
    alignment/EpubTextIndexer.cpp
    alignment/EpubTextIndexer.h
    alignment/AlignmentTypes.h
    third_party/miniz/miniz.c)
target_include_directories(epub_text_indexer_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(epub_text_indexer_harness PRIVATE
    ALIGNMENT_CANONICAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/canonical")
target_link_libraries(epub_text_indexer_harness PRIVATE Qt6::Core)

# Audio↔text alignment service (Agent 2). QML facade + background orchestration over the
# app-owned BackgroundWorkCoordinator: a worker-local AlignmentStore + a fake
# ChapterProcessor drive a deterministic priority / pause / failure / retry / restart proof.
# Service, coordinator, registry and AudioPairingStore are QObjects → AUTOMOC. Qt Core+Sql.
add_executable(alignment_service_harness
    ../tests/alignment_service_harness.cpp
    alignment/AudioTextAlignmentService.cpp alignment/AudioTextAlignmentService.h
    alignment/AlignmentStore.cpp alignment/AlignmentStore.h
    alignment/AlignmentTypes.h
    AudioPairingStore.h
    work/BackgroundWorkCoordinator.cpp work/BackgroundWorkCoordinator.h
    work/BackgroundActivityRegistry.cpp work/BackgroundActivityRegistry.h)
target_include_directories(alignment_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(alignment_service_harness PRIVATE Qt6::Core Qt6::Sql)

# Read-along controller (Agent 2). GUI-thread QObject state machine over the store:
# turns cueAtTime lookups into paint cues (with the confidence-gap clear rules) and
# converges every committed seek (scrub release, keyboard skip, chapter jump,
# double-click, programmatic seek) onto ONE audioSeekRequested. QObject -> AUTOMOC.
add_executable(read_along_controller_harness
    ../tests/read_along_controller_harness.cpp
    alignment/ReadAlongController.cpp alignment/ReadAlongController.h
    alignment/AlignmentStore.cpp alignment/AlignmentStore.h
    alignment/AlignmentTypes.h)
target_include_directories(read_along_controller_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(read_along_controller_harness PRIVATE Qt6::Core Qt6::Sql)

# EPUB sequence matcher (Agent 2). Pure, deterministic algorithm: monotonic phrase
# anchors between coarse speech and the canonical EPUB text, plus explicit gap regions,
# and a deliberate edition-mismatch rejection. No SQLite, no models, no I/O — the header
# structs (EpubIndex/CoarseSegment) are enough, so only EpubSequenceMatcher.cpp compiles.
# Qt Core alone.
add_executable(epub_sequence_matcher_harness
    ../tests/epub_sequence_matcher_harness.cpp
    alignment/EpubSequenceMatcher.cpp
    alignment/EpubSequenceMatcher.h
    alignment/CoarseTypes.h
    alignment/EpubTextIndexer.h
    alignment/AlignmentTypes.h)
target_include_directories(epub_sequence_matcher_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(epub_sequence_matcher_harness PRIVATE
    ALIGNMENT_MATCHING_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/matching")
target_link_libraries(epub_sequence_matcher_harness PRIVATE Qt6::Core)

# Audiobook analysis decoder (Agent 2). Bounded 16 kHz mono float PCM windows via
# the app-bundled tools/ffmpeg.exe (QProcess), a resumable on-disk cache, and a
# corrupt-input failure path. Reads ffmpeg stdout in bounded chunks, consulting the
# shared BackgroundWorkCoordinator's WorkContext between reads — so the coordinator
# .cpp compiles in (its WorkContext is coordinator-private, the only honest way to
# obtain one in the harness). Qt Core only (QProcess lives in Core).
add_executable(audiobook_analysis_decoder_harness
    ../tests/audiobook_analysis_decoder_harness.cpp
    alignment/AudiobookAnalysisDecoder.cpp
    alignment/AudiobookAnalysisDecoder.h
    alignment/AlignmentTypes.h
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(audiobook_analysis_decoder_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(audiobook_analysis_decoder_harness PRIVATE
    ALIGNMENT_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/audio")
target_link_libraries(audiobook_analysis_decoder_harness PRIVATE Qt6::Core)

# Coarse English transcriber (Agent 2). Adapter over the bundled CPU whisper.cpp
# binary (tools/whisper/whisper-cli.exe): writes a decoded PcmWindow to a temp 16 kHz
# mono s16 WAV, runs whisper-cli, parses its JSON transcription[] (ms offsets + text)
# into CoarseSegments — matching EVIDENCE only, never displayed. Model integrity is
# validated first via models::ModelManifest (ModelMissing / ModelChecksumFailed). The
# harness chains the real decoder (speech.wav -> PcmWindow) and routes the WorkContext
# through the shared BackgroundWorkCoordinator, so its .cpp + ModelManifest.cpp compile
# in. Qt Core only (QProcess lives in Core).
add_executable(coarse_transcriber_harness
    ../tests/coarse_transcriber_harness.cpp
    alignment/CoarseTranscriber.cpp
    alignment/CoarseTranscriber.h
    alignment/CoarseTypes.h
    alignment/AudiobookAnalysisDecoder.cpp
    alignment/AudiobookAnalysisDecoder.h
    alignment/AlignmentTypes.h
    models/ModelManifest.cpp
    models/ModelManifest.h
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(coarse_transcriber_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(coarse_transcriber_harness PRIVATE
    ALIGNMENT_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/audio"
    COARSE_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/models/alignment/coarse")
target_link_libraries(coarse_transcriber_harness PRIVATE Qt6::Core)

# ── English forced aligner (Agent 2) — ONNX Runtime, dormant by default ───────
# Only when COLOSSEUM_ENABLE_ONNX=ON: the precise per-word/per-sentence CTC forced
# aligner (wav2vec2 on ONNX Runtime). Guarded exactly like the guided detector so the
# default ONNX-OFF build never tries to link a nonexistent onnxruntime target. The
# harness is a PARITY test: it decodes the real speech.wav via the real decoder, aligns
# the known passage, and asserts agreement (onset median<=250ms / p95<=600ms) with the
# committed numpy oracle (tests/fixtures/alignment/ground_truth.json). It prepends a
# known ffmpeg dir to PATH (ALIGNMENT_FFMPEG_DIR) so the decoder self-decodes.
if(COLOSSEUM_ENABLE_ONNX)
    set(_ALIGN_FFMPEG_DIR "")
    foreach(_cand
        "C:/tools/ffmpeg-master-latest-win64-gpl-shared/bin"
        "${CMAKE_CURRENT_SOURCE_DIR}/build-msvc/tools")
        if(EXISTS "${_cand}/ffmpeg.exe")
            set(_ALIGN_FFMPEG_DIR "${_cand}")
            break()
        endif()
    endforeach()

    add_executable(english_forced_aligner_harness
        ../tests/english_forced_aligner_harness.cpp
        alignment/EnglishForcedAligner.cpp
        alignment/EnglishForcedAligner.h
        alignment/AudiobookAnalysisDecoder.cpp
        alignment/AudiobookAnalysisDecoder.h
        alignment/AlignmentTypes.h
        models/ModelManifest.cpp
        models/ModelManifest.h
        work/BackgroundWorkCoordinator.cpp
        work/BackgroundWorkCoordinator.h)
    target_include_directories(english_forced_aligner_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(english_forced_aligner_harness PRIVATE
        COLOSSEUM_ENABLE_ONNX
        ALIGNMENT_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/audio"
        FORCED_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/models/alignment/forced"
        ALIGNMENT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment"
        ALIGNMENT_FFMPEG_DIR="${_ALIGN_FFMPEG_DIR}")
    target_link_libraries(english_forced_aligner_harness PRIVATE
        Qt6::Core onnxruntime::onnxruntime)
    add_custom_command(TARGET english_forced_aligner_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:onnxruntime::onnxruntime,IMPORTED_LOCATION>"
            "$<TARGET_FILE_DIR:english_forced_aligner_harness>"
        VERBATIM)

    # ── End-to-end alignment pipeline (Agent 2, Task 12 capstone) ─────────────
    # The REAL chapter processor: composes the five alignment components (EPUB index,
    # audio decode, coarse whisper transcribe, sequence match, ONNX CTC forced align)
    # into the AudioTextAlignmentService's ChapterProcessor seam, and proves end-to-end
    # on a REALISTIC 41-word / three-sentence chapter that passage.wav + passage.epub ->
    # matcher anchors (matched=true) -> per-region forced alignment -> word/sentence cues
    # -> a Ready chapter, while an unrelated EPUB the matcher rejects yields no cues ->
    # CouldntSync. ONNX-linked (the aligner is), so it lives inside this guarded block. It
    # compiles all five components + the store + the service (for the ChapterProcessor types
    # + AUTOMOC) + the work spine + miniz. whisper-cli.exe resolves to the build-msvc staging
    # dir (its DLLs sit beside it).
    set(_ALIGN_WHISPER_EXE "")
    foreach(_wc
        "${CMAKE_CURRENT_SOURCE_DIR}/build-msvc/tools/whisper/whisper-cli.exe"
        "${CMAKE_CURRENT_BINARY_DIR}/tools/whisper/whisper-cli.exe")
        if(EXISTS "${_wc}")
            set(_ALIGN_WHISPER_EXE "${_wc}")
            break()
        endif()
    endforeach()

    add_executable(alignment_pipeline_harness
        ../tests/alignment_pipeline_harness.cpp
        alignment/AlignmentPipeline.cpp alignment/AlignmentPipeline.h
        alignment/AudioTextAlignmentService.cpp alignment/AudioTextAlignmentService.h
        alignment/AlignmentStore.cpp alignment/AlignmentStore.h
        alignment/AlignmentTypes.h
        alignment/AudiobookAnalysisDecoder.cpp alignment/AudiobookAnalysisDecoder.h
        alignment/CoarseTranscriber.cpp alignment/CoarseTranscriber.h
        alignment/CoarseTypes.h
        alignment/EpubSequenceMatcher.cpp alignment/EpubSequenceMatcher.h
        alignment/EpubTextIndexer.cpp alignment/EpubTextIndexer.h
        alignment/EnglishForcedAligner.cpp alignment/EnglishForcedAligner.h
        AudioPairingStore.h
        models/ModelManifest.cpp models/ModelManifest.h
        work/BackgroundWorkCoordinator.cpp work/BackgroundWorkCoordinator.h
        work/BackgroundActivityRegistry.cpp work/BackgroundActivityRegistry.h
        third_party/miniz/miniz.c)
    target_include_directories(alignment_pipeline_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(alignment_pipeline_harness PRIVATE
        COLOSSEUM_ENABLE_ONNX
        ALIGNMENT_AUDIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/audio"
        PIPELINE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/alignment/pipeline"
        COARSE_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/models/alignment/coarse"
        FORCED_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/models/alignment/forced"
        ALIGNMENT_FFMPEG_DIR="${_ALIGN_FFMPEG_DIR}"
        WHISPER_EXE="${_ALIGN_WHISPER_EXE}")
    target_link_libraries(alignment_pipeline_harness PRIVATE
        Qt6::Core Qt6::Sql onnxruntime::onnxruntime)
    add_custom_command(TARGET alignment_pipeline_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:onnxruntime::onnxruntime,IMPORTED_LOCATION>"
            "$<TARGET_FILE_DIR:alignment_pipeline_harness>"
        VERBATIM)
endif()

# ── Panel analysis service (Agent 1): QML facade + background orchestration over
#    the app-owned BackgroundWorkCoordinator; per-thread PanelMapStore + fake
#    detector drive a deterministic canvas visit-order proof. Qt Core/Gui/Sql. ──
add_executable(panel_analysis_service_harness
    ../tests/panel_analysis_service_harness.cpp
    guided/PanelAnalysisService.cpp guided/PanelAnalysisService.h
    guided/PanelDetector.h
    guided/PanelMapStore.cpp guided/PanelMapStore.h
    guided/PanelPlanner.cpp guided/PanelPlanner.h
    guided/GuidedTypes.cpp guided/GuidedTypes.h
    work/BackgroundWorkCoordinator.cpp work/BackgroundWorkCoordinator.h)
target_include_directories(panel_analysis_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(panel_analysis_service_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Sql)

# ── Panel Step / Auto Read state machine (Agent 1): the GuidedCameraController
#    that drives both manual Panel Step and timed Auto Read over ONE serialized
#    GuidedPath, with Auto Read deadlines on an injected clock (FakeGuidedClock in
#    the harness). QRectF/QPointF/QVariantMap/QJson/QTimer are all QtCore, and no
#    qmlRegisterType is used here — so Qt6::Core alone links (Gui/Qml unneeded). ──
add_executable(guided_camera_controller_harness
    ../tests/guided_camera_controller_harness.cpp
    guided/GuidedCameraController.cpp guided/GuidedCameraController.h
    guided/GuidedTypes.cpp guided/GuidedTypes.h)
target_include_directories(guided_camera_controller_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(guided_camera_controller_harness PRIVATE Qt6::Core)
