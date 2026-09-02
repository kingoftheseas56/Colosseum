cmake_minimum_required(VERSION 3.16)
# C = the vendored third_party/miniz/miniz.c (gunzip for the Jikan Accept-Encoding workaround).
# RC = compile the Windows .rc that carries the app/exe icon. Windows-only: enabled below via
# enable_language(RC) once WIN32 is known, instead of unconditionally in the base project()
# declaration (arc-17 Linux port, first configure pass).
project(colosseum VERSION 1.1.6 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

option(COLOSSEUM_UPDATE_TESTING
    "Use the test-only update public key for isolated fixture builds"
    OFF)

# Empty by default: the public source carries no invented production endpoint.
# AccountServiceEndpoint reads the environment variable first and only falls
# back to this define, so a dev/test run can still point a baked release build
# at a local service without reconfiguring it.
set(COLOSSEUM_ACCOUNT_SERVICE_URL "" CACHE STRING
    "Account service base URL compiled into this build; empty leaves COLOSSEUM_ACCOUNT_SERVICE_URL in the environment as the only source")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compile app_resources.qrc (embeds the .ico for setWindowIcon)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COLOSSEUM_NO_COMPILER_CACHE
    "Disable automatic compiler-cache launcher discovery"
    OFF)
if(NOT COLOSSEUM_NO_COMPILER_CACHE)
    find_program(COMPILER_CACHE_PROGRAM NAMES sccache ccache)
    if(COMPILER_CACHE_PROGRAM AND EXISTS "${COMPILER_CACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${COMPILER_CACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${COMPILER_CACHE_PROGRAM}")
        get_filename_component(COMPILER_CACHE_NAME "${COMPILER_CACHE_PROGRAM}" NAME)
        message(STATUS "Compiler cache: ${COMPILER_CACHE_NAME} (${COMPILER_CACHE_PROGRAM})")
    else()
        message(STATUS "Compiler cache: sccache/ccache not found; building without compiler cache")
    endif()
else()
    message(STATUS "Compiler cache: disabled by COLOSSEUM_NO_COMPILER_CACHE=ON")
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick Network Qml Gui Sql WebEngineQuick WebChannel WebSockets Concurrent)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

# --- video player: KDE mpvqt (libmpv-in-QtQuick) + libmpv ---
# Consumes the PROVEN feasibility build (don't rebuild — see kindled-reel recap).
# Toolchain-aware: MSVC (Qt WebEngine path, homeward-foundry) consumes the msvc
# MpvQt build + mpv.lib; MinGW the original build + libmpv.dll.a. Same source tree
# builds under either kit. Override MPVQT_PREFIX on the command line to vendor later.
if(WIN32)
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
else()
    # Linux (arc-17 WSL-first build): no libmpvqt-dev apt candidate, so MpvQt is source-built
    # into a controlled prefix (see Preflight-Architect/arcs/17-linux-build/guides/WSL-FIRST-BUILD.md).
    # Default matches that guide's prefix; override MPVQT_PREFIX on the command line if vendored
    # elsewhere. Ubuntu's libmpv-dev provides real pkg-config metadata, so no Libmpv_* pre-seed
    # is needed here — MpvQt's bundled FindLibmpv.cmake resolves it directly via pkg-config.
    set(MPVQT_PREFIX "$ENV{HOME}/.local/colosseum-deps" CACHE PATH "MpvQt install prefix (Linux)")
    if(MPVQT_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${MPVQT_PREFIX}")
    endif()
endif()
find_package(MpvQt REQUIRED)
if(NOT WIN32 AND (NOT Libmpv_LIBRARIES OR NOT Libmpv_INCLUDE_DIRS))
    # MpvQt's own FindLibmpv already populated these when it found libmpv-dev; fall back to a
    # direct pkg-config probe only if that didn't happen, so MediaAdmissionProbe.cpp's raw
    # <mpv/client.h> include/link (below) still resolves.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Libmpv REQUIRED IMPORTED_TARGET mpv)
    set(Libmpv_LIBRARIES PkgConfig::Libmpv)
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
    target_link_libraries(colosseum_libtorrent INTERFACE
        LibtorrentRasterbar::torrent-rasterbar Boost::filesystem
        OpenSSL::SSL OpenSSL::Crypto)
    if(TARGET Boost::system)
        # Boost::system is a real compiled-library target on the Windows Boost 1.84 install this
        # project was built against. Ubuntu 26.04 packages Boost 1.90, where boost_system has been
        # header-only (no compiled library, no CMake component target) since ~1.85; guard instead
        # of hard-requiring the target so the Linux configure does not fail on an artifact that
        # version no longer ships (arc-17 Linux port, first-configure discovery).
        target_link_libraries(colosseum_libtorrent INTERFACE Boost::system)
    endif()
    if(WIN32)
        # WinSock/crypto system libs and the Windows-targeted _WIN32_WINNT/BOOST_ALL_NO_LIB
        # defines are Windows-only; Linux's apt libtorrent-rasterbar-dev/libboost-all-dev/
        # libssl-dev need neither (arc-17 Linux port).
        target_compile_definitions(colosseum_libtorrent INTERFACE
            HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL BOOST_ALL_NO_LIB _WIN32_WINNT=0x0A00)
        target_link_libraries(colosseum_libtorrent INTERFACE
            ws2_32 mswsock crypt32 iphlpapi bcrypt)
    else()
        target_compile_definitions(colosseum_libtorrent INTERFACE
            HAS_LIBTORRENT=1 TORRENT_USE_OPENSSL)
    endif()
elseif(WIN32)
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
else()
    # Linux: no C:/tools fallback exists off Windows. apt's libtorrent-rasterbar-dev/
    # libboost-all-dev/libssl-dev should satisfy find_package above; a failure here is a
    # genuine missing-dependency on this machine, not a build-system gap (arc-17 Linux port).
    message(FATAL_ERROR "libtorrent + Boost + OpenSSL NOT found via find_package on this platform — install libtorrent-rasterbar-dev, libboost-all-dev and libssl-dev (see guides/WSL-FIRST-BUILD.md).")
endif()

add_library(colosseum_update_crypto INTERFACE)
if(TARGET OpenSSL::Crypto)
    target_link_libraries(colosseum_update_crypto INTERFACE OpenSSL::Crypto)
elseif(OPENSSL_MSVC_ROOT AND EXISTS "${OPENSSL_MSVC_ROOT}/include" AND
       EXISTS "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib")
    target_include_directories(colosseum_update_crypto INTERFACE "${OPENSSL_MSVC_ROOT}/include")
    target_link_libraries(colosseum_update_crypto INTERFACE
        "${OPENSSL_MSVC_ROOT}/lib/libcrypto.lib" ws2_32 crypt32)
else()
    message(FATAL_ERROR "OpenSSL crypto is required for Colosseum update manifest verification")
endif()

add_executable(colosseum
    main.cpp
    # Bundle 8C account + cloud-sync runtime (adopted 2026-08-16)
    # Arc 36 Wave 4B N-16: resumable attachment flow orchestrator.
    account/AccountAttachmentCoordinator.cpp
    # Arc 36 Wave 4A N-14: crash-safe cloud attachment receipt (persistence only).
    account/AccountAttachmentReceipt.cpp
    account/AccountBootstrapStore.cpp
    account/AccountClient.cpp
    account/AccountController.cpp
    account/AccountDeviceIdentity.cpp
    account/AccountHttpTransport.cpp
    account/AccountRecoveryKeyPresenter.cpp
    account/AccountRuntime.cpp
    account/AccountServiceEndpoint.cpp
    # Slice D1: native ActivityProjector (JS-oracle golden parity port).
    account/ActivityProjector.cpp
    # Slice D2: profile-owned SQLite ActivityStore (durable ledger writer).
    account/ActivityStore.cpp
    account/ActivitySyncAdapter.cpp
    # Slice D3: transient ActivityPlaybackTracker (not QML-registered this slice).
    account/ActivityPlaybackTracker.cpp
    account/CollectionSyncAdapter.cpp
    account/CoreStateSyncProjection.cpp
    account/FirstAccountProfileCoordinator.cpp
    account/HistoryStore.cpp
    account/HistorySyncAdapter.cpp
    account/LegacyPersonalStateStorage.cpp
    account/ProfileAdoption.cpp
    account/ProfileContext.cpp
    account/ProfilePaths.cpp
    account/ProfilePreferencesStore.cpp
    account/ProfilePreferencesSyncAdapter.cpp
    account/ProfileStoreRuntime.cpp
    account/ConsumptionHistoryBridge.cpp
    account/ProgressSyncAdapter.cpp
    account/WatchStateSyncAdapter.cpp
    account/SharedPcProfileCoordinator.cpp
    account/SyncAdapterRegistry.cpp
    account/SyncEngine.cpp
    account/SyncHybridClock.cpp
    account/SyncOwnershipInventory.cpp
    account/SyncPayloadFirewall.cpp
    account/SyncProtocol.cpp
    account/SyncStateStore.cpp
    account/TankobanReaderPreferencesCodec.cpp
    account/TankobanReaderPreferencesSyncAdapter.cpp
    account/WindowsAccountCredentialStore.cpp
    account/WindowsAccountSensitiveClipboard.cpp
    # Arc 36 Wave 4B N-16: Q_OBJECT — moc needs the header listed.
    account/AccountAttachmentCoordinator.h
    account/AccountAttachmentReceipt.h
    account/AccountBootstrapStore.h
    account/AccountClient.h
    account/AccountController.h
    account/AccountCredentialStore.h
    account/AccountDeviceIdentity.h
    account/AccountHttpTransport.h
    account/AccountOneTimeSecretSink.h
    account/AccountProfileCoordinator.h
    account/AccountRecoveryKeyPresenter.h
    account/AccountRuntime.h
    account/AccountSensitiveClipboard.h
    account/AccountServiceEndpoint.h
    account/AccountTransport.h
    # Slice D2: profile-owned SQLite ActivityStore (Q_OBJECT — moc needs the header listed).
    account/ActivityStore.h
    account/ActivitySyncAdapter.h
    # Slice D3: transient ActivityPlaybackTracker (Q_OBJECT — moc needs the header listed).
    account/ActivityPlaybackTracker.h
    account/CollectionSyncAdapter.h
    account/CoreStateSyncProjection.h
    account/FirstAccountProfileCoordinator.h
    account/HistoryStore.h
    account/HistorySyncAdapter.h
    account/LegacyPersonalStateStorage.h
    account/ProfileAdoption.h
    account/ProfileContext.h
    account/ProfilePaths.h
    account/ProfilePreferencesStore.h
    account/ProfilePreferencesSyncAdapter.h
    account/ProfileStoreRuntime.h
    account/ConsumptionHistoryBridge.h
    account/ProgressSyncAdapter.h
    account/WatchStateSyncAdapter.h
    account/SharedPcProfileCoordinator.h
    account/SyncAdapter.h
    account/SyncAdapterRegistry.h
    account/SyncEngine.h
    account/SyncHybridClock.h
    account/SyncOwnershipInventory.h
    account/SyncPayloadFirewall.h
    account/SyncProtocol.h
    account/SyncStateStore.h
    account/TankobanReaderPreferencesCodec.h
    account/TankobanReaderPreferencesOwner.h
    account/TankobanReaderPreferencesSyncAdapter.h
    account/WindowsAccountCredentialStore.h
    account/WindowsAccountSensitiveClipboard.h
    update/UpdateVersion.cpp
    update/UpdateVersion.h
    update/UpdateManifest.cpp
    update/UpdateManifest.h
    update/UpdateTrust.cpp
    update/UpdateTrust.h
    update/UpdatePublicKey.h
    update/UpdateTestPublicKey.h
    update/InstalledChronicle.cpp
    update/InstalledChronicle.h
    update/UpdateCache.cpp
    update/UpdateCache.h
    update/UpdateDownload.cpp
    update/UpdateDownload.h
    update/UpdateReleaseClient.cpp
    update/UpdateReleaseClient.h
    update/UpdateService.cpp
    update/UpdateService.h
    update/UpdateInstallBridge.cpp
    update/UpdateInstallBridge.h
    app_resources.qrc        # embeds colosseum.ico for QGuiApplication::setWindowIcon
    installed_chronicle.qrc  # bundled installed-release chronicle (signed manifest + artwork)
    ClipboardHelper.h
    GuiStallProbe.h           # Q_OBJECT QML-facing diagnostic context bridge
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
    bootstrap/AppDataMigration.cpp
    bootstrap/AppDataMigration.h
    bootstrap/StartupLayout.cpp
    bootstrap/StartupLayout.h
    net/Ipv4PinStore.cpp
    net/Ipv4PinStore.h
    net/LoopbackPinProxy.cpp       # instant posters: pin the connection, keep the hostname (HTTP/2)
    net/LoopbackPinProxy.h
    net/PinProxyFactory.h
    net/PosterScoreboard.cpp       # felt-speed Stage 0: arrived/failed/undecodable per host
    net/PosterScoreboard.h
    net/BiblioImageDiag.cpp        # per-URL image diagnostics (Lanista biblio.imageDiag probe)
    net/BiblioImageDiag.h
    third_party/miniz/miniz.c      # gunzip for the Jikan Accept-Encoding workaround (needs C, enabled below)
    engine/AppLog.cpp              # always-on rolling log -> <AppData>/logs/colosseum.log
    engine/AppLog.h
    engine/WeebCentralScraper.cpp
    engine/WeebCentralScraper.h
    engine/MangaScraper.h
    engine/MangaResult.h
    engine/MangaSeriesDetail.h
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
    engine/ComickCatalogClient.cpp
    engine/ComickCatalogClient.h
    engine/MangaDownloader.cpp
    engine/MangaDownloader.h
    engine/MangaImageHostResolver.cpp
    engine/MangaImageHostResolver.h
    engine/BookDownloader.cpp
    engine/BookDownloader.h
    engine/AudiobookDownloader.cpp
    engine/AudiobookDownloader.h
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverProvider.cpp  # image://comiccover/ -- stateless cover decode for archive-shaped rows
    engine/ComicCoverProvider.h
    engine/VaultBookCoverProvider.cpp # image://vaultbookcover/ -- stateless EPUB cover decode
    engine/VaultBookCoverProvider.h
    engine/ComicCoverId.cpp        # id build/parse, Core-only (shared with ComicDownloader.cpp)
    engine/ComicCoverId.h
    engine/ComicDlsParse.cpp
    engine/ComicDlsParse.h
    engine/ComicsCatalog.cpp
    engine/ComicsCatalog.h
    engine/MalCatalog.cpp
    engine/MalCatalog.h
    engine/TankobanCatalog.cpp     # baked Tankoban volume catalogue (catalogue-independence Slice 1)
    engine/TankobanCatalog.h
    engine/TankobanChapterMigration.cpp  # one-time WC-era chapter purge (catalogue-independence Slice 5)
    engine/TankobanChapterMigration.h
    engine/ImdbCatalog.cpp
    engine/ImdbCatalog.h
    engine/CatalogVaultClient.cpp  # data-vault adoption, Slice 1/2 (2026-08-22)
    engine/CatalogVaultClient.h
    engine/LocalDownloads.cpp
    engine/LocalDownloads.h
    engine/ExtensionsStore.cpp
    engine/ExtensionsStore.h
    # ── Vault: local-media engine organs wired into the app (execution plan Slice 8) ──
    engine/VaultKit.cpp            # shared classifier / SxxExx grammar / walker (Slice 1)
    engine/VaultKit.h
    engine/VaultStoreIo.h          # header-only atomic save/load w/ .bak (Slice 2)
    engine/VaultIdentity.cpp       # content-addressed id — LocalLaunch::route keys progress to it (Slice 2)
    engine/VaultIdentity.h
    engine/VaultRecent.cpp         # Open Media recent-files store (Slice 9)
    engine/VaultRecent.h
    engine/LocalLaunch.cpp         # launch router + QML open() orchestration (Slice 7/8/9)
    engine/LocalLaunch.h
    engine/VaultPageStore.cpp      # ComicReaderShell injected-store adapter for local CBZ (Slice 7)
    engine/VaultPageStore.h
    engine/VaultIndex.cpp          # rebuildable scan product — the queryable index (Slice 3), app-wired for the read-model (Slice 10)
    engine/VaultIndex.h
    engine/VaultConfig.cpp         # user-intent store: roots + kind overrides (Slice 2), app-wired for scan/confirm (Slice 11)
    engine/VaultConfig.h
    engine/VaultScanner.cpp        # cancellable off-thread census + aggregate publish (Slice 4/11)
    engine/VaultScanner.h
    engine/VaultLibrary.cpp        # thin QML read-model over VaultIndex — the Vault presentation seam (Slice 10)
    engine/VaultLibrary.h
    engine/VaultBrowseAway.cpp     # browse-projection away propagation + offline fallback (browse-face execution plan Slice 6)
    engine/VaultBrowseAway.h
    engine/VaultBrowseDetail.cpp   # the detail sheet's ONE projection: copies/companions/extras/evidence (browse-face execution plan Slice 7)
    engine/VaultBrowseDetail.h
    engine/VaultBrowseEmpty.cpp    # the grid's empty-cause classification: no-roots/empty-folder/all-away (browse-face execution plan Slice 9)
    engine/VaultBrowseEmpty.h
    engine/VaultIdentifier.cpp
    engine/VaultIdentifier.h
    engine/VaultWatcher.cpp        # live-shelf arrivals: per-root QFileSystemWatcher + debounce + law-checked upsert (Slice 15)
    engine/VaultWatcher.h
    engine/VaultDownloadsRoot.cpp  # synthetic downloads root — derives FileRows from backbones (Slice 18)
    engine/VaultDownloadsRoot.h
    engine/VaultEnricher.cpp       # cover/duration enrichment — app-wired for comic covers (Slice 12)
    engine/VaultEnricher.h
    # ── Vault browse artwork (browse-artwork execution plan, Slices 1-3, 2026-08-13): the
    # resolver ladder VaultLibrary now composes for browseAt()/items()/series() cover projection ──
    engine/VaultCacheKey.h         # shared (path,size,mtimeMs) cache-key derivation (Slice 1)
    engine/VaultThumbnailer.cpp    # persistent frame-grab producer, ffmpeg off-thread (Slice 1)
    engine/VaultThumbnailer.h
    engine/VaultPosterFetcher.cpp  # canonical poster fetcher + cache, QNetworkAccessManager (Slice 2)
    engine/VaultPosterFetcher.h
    engine/VaultArtworkResolver.cpp # the fixed-priority ladder core VaultLibrary calls per row (Slice 3)
    engine/VaultArtworkResolver.h
    engine/VaultBookStateMigrator.cpp # Reader 2 path-key migration at the Vault identity boundary (Slice 16)
    engine/VaultBookStateMigrator.h
    engine/VaultForensics.cpp      # bounded read-only Vault projection over VaultLibrary (Agent Visibility Phase 2, F1-Core/F1-Bridge)
    engine/VaultForensics.h
    player/MediaAdmissionProbe.cpp # decoded-frame video admission gate, raw libmpv (Slice 6)
    player/MediaAdmissionProbe.h
    # ── devtools: the lanista dev-control bridge (always on, reads only) ──
    devtools/LanistaServer.cpp
    devtools/LanistaServer.h
    devtools/LanistaEventLog.cpp
    devtools/LanistaEventLog.h
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
    # Watch Party cumulative Slices 1-6: isolated room core, credential-free
    # source eligibility, narrow Player 1 timeline sync policy, repo-local
    # transport/service boundary, account-owned identity/invite seam, and the
    # QML-facing room/UI coordinator. Concrete account/cloud binding and
    # room-service deployment remain external adoption prerequisites.
    watchparty/WatchPartyTypes.cpp
    watchparty/WatchPartyTypes.h
    watchparty/WatchPartyProtocol.cpp
    watchparty/WatchPartyProtocol.h
    watchparty/WatchPartySource.cpp
    watchparty/WatchPartySource.h
    watchparty/WatchPartyServiceEndpoint.cpp
    watchparty/WatchPartyServiceEndpoint.h
    watchparty/WatchPartyPlayerSync.cpp
    watchparty/WatchPartyPlayerSync.h
    watchparty/WatchPartyTransport.cpp
    watchparty/WatchPartyTransport.h
    watchparty/WatchPartySocket.h
    watchparty/QtWatchPartySocket.cpp
    watchparty/QtWatchPartySocket.h
    watchparty/WebSocketWatchPartyTransport.cpp
    watchparty/WebSocketWatchPartyTransport.h
    watchparty/FakeWatchPartyTransport.cpp
    watchparty/FakeWatchPartyTransport.h
    watchparty/WatchPartyRoomServiceClient.cpp
    watchparty/WatchPartyRoomServiceClient.h
    watchparty/WatchPartyIdentity.cpp
    watchparty/WatchPartyIdentity.h
    watchparty/WatchPartyUiController.cpp
    watchparty/WatchPartyUiController.h
    watchparty/WatchPartyRoomController.cpp
    watchparty/WatchPartyRoomController.h
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
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
    # ── Arc 18 (2026-08-23): Torrentio-style volume identity + durable index ──
    torrent/MangaTorrentDiscovery.cpp
    torrent/MangaTorrentDiscovery.h
    torrent/IMangaTorrentMetainfoResolver.h
    torrent/MangaTorrentMetainfoResolver.cpp
    torrent/MangaTorrentMetainfoResolver.h
    torrent/MangaTorrentIndex.cpp
    torrent/MangaTorrentIndex.h
    torrent/MangaTorrentIndexer.cpp
    torrent/MangaTorrentIndexer.h
    torrent/MangaVolumeFilePicker.cpp
    torrent/MangaVolumeFilePicker.h
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeIndex.h
    engine/CbzArchive.cpp
    engine/CbzArchive.h
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
    # ── Comic Reader (Agent 1, plan 2026-07-23): from-scratch native reader.
    #    ComicReaderCore is the app-facing backend (QML `ComicReaderCore` context
    #    property, registered in main.cpp) orchestrating the five pure engine
    #    units; ComicReaderProvider is the read-only image://comicreader/ seam. ──
    comicreader/ComicReaderTypes.cpp
    comicreader/ComicReaderTypes.h
    comicreader/ComicReaderPairing.cpp
    comicreader/ComicReaderPairing.h
    comicreader/ComicReaderCoupling.cpp
    comicreader/ComicReaderCoupling.h
    comicreader/ComicReaderPageCache.cpp
    comicreader/ComicReaderPageCache.h
    comicreader/ComicReaderScaleCache.cpp
    comicreader/ComicReaderScaleCache.h
    comicreader/ComicReaderRenderProfile.cpp
    comicreader/ComicReaderRenderProfile.h
    comicreader/ComicReaderDecode.cpp
    comicreader/ComicReaderDecode.h
    comicreader/ComicReaderStripModel.cpp
    comicreader/ComicReaderStripModel.h
    comicreader/ComicReaderProvider.cpp
    comicreader/ComicReaderProvider.h
    comicreader/ComicReaderImageResponse.cpp
    comicreader/ComicReaderImageResponse.h
    comicreader/ComicReaderCore.cpp
    comicreader/ComicReaderCore.h
    # ── BiblioCatalog Discover/Explore (plan 2026-08-03, Tasks 1–4): the native
    #    catalogue engine wired into the real app for the first time here —
    #    Tasks 1–3 only ever built their own standalone harnesses. BiblioCatalog
    #    (Task 4) is the sole consumer that pulls the whole chain (taxonomy,
    #    ranking, providers, canonicalizer, SQLite store) into the colosseum
    #    executable. ──
    engine/BiblioCatalogTypes.h
    engine/BiblioTaxonomy.cpp
    engine/BiblioTaxonomy.h
    engine/BiblioRanking.cpp
    engine/BiblioRanking.h
    engine/BiblioArtworkUrl.h
    engine/BiblioProviders.cpp
    engine/BiblioProviders.h
    engine/BiblioCanonicalizer.cpp
    engine/BiblioCanonicalizer.h
    engine/BiblioCatalogStore.cpp
    engine/BiblioCatalogStore.h
    engine/BiblioCatalog.cpp
    engine/BiblioCatalog.h
)
target_include_directories(colosseum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} engine player torrent
    ${Libmpv_INCLUDE_DIRS})   # MediaAdmissionProbe.cpp uses the raw <mpv/client.h> C API (Slice 8)
target_compile_definitions(colosseum PRIVATE COLOSSEUM_VERSION="${PROJECT_VERSION}")
if(COLOSSEUM_UPDATE_TESTING)
    target_compile_definitions(colosseum PRIVATE COLOSSEUM_UPDATE_TESTING=1)
endif()
if(COLOSSEUM_ACCOUNT_SERVICE_URL)
    # AccountHttpTransport::isAllowedBaseUrl accepts https anywhere but http only
    # on loopback. Anything else compiles fine and then fails every account
    # request at runtime with "The account service configuration is invalid",
    # which is exactly the shipped-1.1.4 failure this seam exists to end — so
    # reject it here rather than baking an unusable endpoint into a release.
    if(COLOSSEUM_ACCOUNT_SERVICE_URL MATCHES "^https://[^/:]")
        set(colosseum_account_url_usable TRUE)
    elseif(COLOSSEUM_ACCOUNT_SERVICE_URL MATCHES
           "^http://(localhost|127[.]0[.]0[.]1|\\[::1\\])([:/]|$)")
        set(colosseum_account_url_usable TRUE)
    else()
        set(colosseum_account_url_usable FALSE)
    endif()
    if(NOT colosseum_account_url_usable)
        message(FATAL_ERROR
            "COLOSSEUM_ACCOUNT_SERVICE_URL=\"${COLOSSEUM_ACCOUNT_SERVICE_URL}\" is not a usable "
            "account endpoint. Use https://<host>, or http:// on localhost/127.0.0.1/[::1] for "
            "local runs against tests/mock-account-service.")
    endif()
    target_compile_definitions(colosseum PRIVATE
        COLOSSEUM_ACCOUNT_SERVICE_URL="${COLOSSEUM_ACCOUNT_SERVICE_URL}")
    message(STATUS "Account service baked in: ${COLOSSEUM_ACCOUNT_SERVICE_URL}")
endif()
target_link_libraries(colosseum PRIVATE colosseum_update_crypto
    Qt6::Quick Qt6::Network Qt6::Qml Qt6::Gui Qt6::Sql Qt6::Concurrent MpvQt::MpvQt
    Qt6::WebEngineQuick Qt6::WebChannel Qt6::WebSockets colosseum_libtorrent
    ${Libmpv_LIBRARIES})   # MediaAdmissionProbe links libmpv directly, not via MpvQt (Slice 8)
if(UNIX AND NOT APPLE)
    target_link_libraries(colosseum PRIVATE Qt6::DBus)
endif()
# Function 0001 bootstrap contract: bind the native executable to the exact QML
# tree present when it was built. Normal startup refuses a live QML tree whose
# fingerprint differs; explicit dev/test QML overrides bypass this production gate.
add_custom_command(TARGET colosseum POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DQML_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../qml"
            "-DOUTPUT_FILE=$<TARGET_FILE_DIR:colosseum>/qml-build.manifest"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/write_qml_build_manifest.cmake"
    VERBATIM)
if(WIN32)
    # Windows exe/taskbar icon (colosseum.ico) + Windows-only system libs (arc-17 Linux port:
    # both are meaningless off Windows, so they move here instead of the shared source/link list).
    target_sources(colosseum PRIVATE app_icon.rc)
    target_link_libraries(colosseum PRIVATE advapi32 user32)
    set_target_properties(colosseum PROPERTIES WIN32_EXECUTABLE TRUE)
endif()


add_executable(update_version_harness
    ../tests/update_version_harness.cpp
    update/UpdateVersion.cpp
    update/UpdateVersion.h
)
target_include_directories(update_version_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(update_version_harness PRIVATE Qt6::Core)

add_executable(update_manifest_trust_harness
    ../tests/update_manifest_trust_harness.cpp
    update/UpdateVersion.cpp
    update/UpdateVersion.h
    update/UpdateManifest.cpp
    update/UpdateManifest.h
    update/UpdateTrust.cpp
    update/UpdateTrust.h
    update/UpdatePublicKey.h
    update/UpdateTestPublicKey.h
)
target_include_directories(update_manifest_trust_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(update_manifest_trust_harness PRIVATE COLOSSEUM_UPDATE_TESTING=1)
target_link_libraries(update_manifest_trust_harness PRIVATE Qt6::Core colosseum_update_crypto)

add_executable(update_release_client_harness
    ../tests/update_release_client_harness.cpp
    update/UpdateVersion.cpp
    update/UpdateVersion.h
    update/UpdateManifest.cpp
    update/UpdateManifest.h
    update/UpdateTrust.cpp
    update/UpdateTrust.h
    update/UpdatePublicKey.h
    update/UpdateTestPublicKey.h
    update/UpdateReleaseClient.cpp
    update/UpdateReleaseClient.h
)
target_include_directories(update_release_client_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(update_release_client_harness PRIVATE COLOSSEUM_UPDATE_TESTING=1)
target_link_libraries(update_release_client_harness PRIVATE Qt6::Core Qt6::Network colosseum_update_crypto)

# CatalogVaultClient (data-vault adoption, Slice 1, 2026-08-22): keeps the four
# Colosseum-Data catalogue dbs fresh in AppData. Local QTcpServer fixture, no live network.
add_executable(catalog_vault_client_harness
    ../tests/catalog_vault_client_harness.cpp
    engine/CatalogVaultClient.cpp
    engine/CatalogVaultClient.h
)
target_include_directories(catalog_vault_client_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catalog_vault_client_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(update_download_harness
    ../tests/update_download_harness.cpp
    update/UpdateVersion.cpp
    update/UpdateVersion.h
    update/UpdateCache.cpp
    update/UpdateCache.h
    update/UpdateDownload.cpp
    update/UpdateDownload.h
)
target_include_directories(update_download_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(update_download_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)

add_executable(update_service_harness
    ../tests/update_service_harness.cpp
    update/UpdateVersion.cpp
    update/UpdateVersion.h
    update/UpdateManifest.cpp
    update/UpdateManifest.h
    update/UpdateTrust.cpp
    update/UpdateTrust.h
    update/UpdatePublicKey.h
    update/UpdateTestPublicKey.h
    update/InstalledChronicle.cpp
    update/InstalledChronicle.h
    update/UpdateReleaseClient.cpp
    update/UpdateReleaseClient.h
    update/UpdateCache.cpp
    update/UpdateCache.h
    update/UpdateDownload.cpp
    update/UpdateDownload.h
    update/UpdateService.cpp
    update/UpdateService.h
)
target_include_directories(update_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(update_service_harness PRIVATE
    COLOSSEUM_UPDATE_TESTING=1
    COLOSSEUM_UPDATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/lanista_fixtures")
target_link_libraries(update_service_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent colosseum_update_crypto)

add_executable(update_install_bridge_harness
    ../tests/update_install_bridge_harness.cpp
    update/UpdateInstallBridge.cpp
    update/UpdateInstallBridge.h
    update/UpdateCache.cpp
    update/UpdateCache.h
    update/UpdateVersion.cpp
    update/UpdateVersion.h
)
target_include_directories(update_install_bridge_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(update_install_bridge_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(account_credential_persistence_harness
    ../tests/account_credential_persistence_harness.cpp
    account/WindowsAccountCredentialStore.cpp
    account/WindowsAccountCredentialStore.h
    account/AccountCredentialStore.h
)
target_include_directories(account_credential_persistence_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(account_credential_persistence_harness PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(account_credential_persistence_harness PRIVATE Advapi32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(account_credential_persistence_harness PRIVATE Qt6::DBus)
endif()


add_executable(search_history_store_harness
    ../tests/search_history_store_harness.cpp
    SearchHistoryStore.h
)
target_include_directories(search_history_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(search_history_store_harness PRIVATE Qt6::Core)

add_executable(session_store_harness
    ../tests/session_store_harness.cpp
    SessionStore.h
)
target_include_directories(session_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(session_store_harness PRIVATE Qt6::Core)

add_executable(background_work_coordinator_harness
    ../tests/background_work_coordinator_harness.cpp
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(background_work_coordinator_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_work_coordinator_harness PRIVATE Qt6::Core)

add_executable(background_activity_registry_harness
    ../tests/background_activity_registry_harness.cpp
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(background_activity_registry_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_activity_registry_harness PRIVATE Qt6::Core)

add_executable(background_work_composition_harness
    ../tests/background_work_composition_harness.cpp
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(background_work_composition_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_work_composition_harness PRIVATE Qt6::Core Qt6::Qml)

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

# Function 0007: focused Reader 2 harness builds must be runnable without first
# building/deploying the full app. Stage their exact Qt runtime dependencies.
if(WIN32)
    add_custom_command(TARGET reader2_stores_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>"
                "$<TARGET_FILE_DIR:reader2_stores_harness>"
        VERBATIM)
    add_custom_command(TARGET reader2_bridge_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>"
                "$<TARGET_FILE:Qt6::Network>"
                "$<TARGET_FILE_DIR:reader2_bridge_harness>"
        VERBATIM)
    add_custom_command(TARGET reader2_autoattach_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>"
                "$<TARGET_FILE:Qt6::Network>"
                "$<TARGET_FILE_DIR:reader2_autoattach_harness>"
        VERBATIM)
endif()

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

add_executable(loopback_pin_proxy_harness
    ../tests/loopback_pin_proxy_harness.cpp
    net/Ipv4PinStore.cpp
    net/Ipv4PinStore.h
    net/LoopbackPinProxy.cpp
    net/LoopbackPinProxy.h
)
target_include_directories(loopback_pin_proxy_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(loopback_pin_proxy_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(pin_proxy_factory_harness
    ../tests/pin_proxy_factory_harness.cpp
    net/Ipv4PinStore.cpp
    net/Ipv4PinStore.h
    net/PinProxyFactory.h
)
target_include_directories(pin_proxy_factory_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pin_proxy_factory_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(startup_layout_harness
    ../tests/startup_layout_harness.cpp
    bootstrap/StartupLayout.cpp
    bootstrap/StartupLayout.h
)
target_include_directories(startup_layout_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(startup_layout_harness PRIVATE Qt6::Core)

add_executable(app_data_migration_harness
    ../tests/app_data_migration_harness.cpp
    bootstrap/AppDataMigration.cpp
    bootstrap/AppDataMigration.h
)
target_include_directories(app_data_migration_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(app_data_migration_harness PRIVATE Qt6::Core)

add_executable(ipv4_pin_store_harness
    ../tests/ipv4_pin_store_harness.cpp
    net/Ipv4PinStore.cpp
    net/Ipv4PinStore.h
)
target_include_directories(ipv4_pin_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ipv4_pin_store_harness PRIVATE Qt6::Core Qt6::Network)

add_executable(poster_scoreboard_harness
    ../tests/poster_scoreboard_harness.cpp
    net/PosterScoreboard.cpp
    net/PosterScoreboard.h
)
target_include_directories(poster_scoreboard_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(poster_scoreboard_harness PRIVATE Qt6::Core)

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

# Task 2 (CBZ-in-place plan): Entry::archive, atomic saveIndex(), fixed
# loadIndex() prune. Links the same comics stack as comic_downloader_ingest_
# harness for the same reason (ComicDownloader.cpp transitively requires
# ComicTorrents.cpp -> TorrentEngine.cpp/colosseum_libtorrent to link).
add_executable(comic_downloader_archive_ingest_harness
    ../tests/comic_downloader_archive_ingest_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId() -- Core-only, Task 3
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic() directly, Task 4
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_include_directories(comic_downloader_archive_ingest_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_downloader_archive_ingest_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui colosseum_libtorrent)

# Task 7 (CBZ-in-place plan): boot-time legacy migration -- migrateLegacyComics
# InPlace() runs from loadIndex(). Same comics-stack source list + links as the
# archive-ingest harness (ComicDownloader.cpp transitively requires
# ComicTorrents.cpp -> TorrentEngine.cpp/colosseum_libtorrent to link).
add_executable(comic_downloader_legacy_migration_harness
    ../tests/comic_downloader_legacy_migration_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId() -- Core-only, Task 3
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic() directly, Task 4
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_include_directories(comic_downloader_legacy_migration_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_downloader_legacy_migration_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui colosseum_libtorrent)

add_executable(comic_downloader_ingest_harness
    ../tests/comic_downloader_ingest_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId() -- Core-only, Task 3
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic() directly, Task 4
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_link_libraries(comic_downloader_ingest_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui colosseum_libtorrent)

# Multi-volume pack demux harness (Slice 1, 2026-08-06 — see
# docs/superpowers/plans/2026-08-06-comics-multivolume-pack-demux.md).
# At Slice 1 this exercises the volume label parser (engine/ComicPackLabels.h)
# directly + the index Entry round-trip through the REAL loadIndex()/saveIndex()
# (declared on agents/chat.md before this edit landed). The source list mirrors
# comic_downloader_ingest_harness because it drives the same ComicDownloader
# stack; the harness compiles ComicPackLabels.h inline (pure-function header).
add_executable(comic_downloader_pack_demux_harness
    ../tests/comic_downloader_pack_demux_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId()
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic()
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_include_directories(comic_downloader_pack_demux_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_downloader_pack_demux_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui colosseum_libtorrent)

add_executable(comic_torrents_search_harness
    ../tests/comic_torrents_search_harness.cpp
    engine/ComicDownloader.cpp
    engine/ComicDownloader.h
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId() -- Core-only, Task 3
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic() directly, Task 4
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_link_libraries(comic_torrents_search_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent colosseum_libtorrent)

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
    engine/ComicCoverId.cpp        # downloadedIssues() calls buildComicCoverId() -- Core-only, Task 3
    engine/ComicCoverId.h
    engine/CbzArchive.cpp          # ComicDownloader.cpp calls probe()/writeImagesAtomic() directly, Task 4
    engine/CbzArchive.h
    third_party/miniz/miniz.c
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
target_link_libraries(comic_torrent_pack_transport_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent colosseum_libtorrent)

add_executable(book_torrent_magnet_harness
    ../tests/book_torrent_magnet_harness.cpp
)
target_include_directories(book_torrent_magnet_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(book_torrent_magnet_harness PRIVATE Qt6::Core)

# Arc 18 M1 — the ONE shared volume-identity grammar every search/runtime path
# must call instead of a private regex (contract: VOLUME-IDENTITY-AND-INDEX-
# CONTRACT §2). Pure Qt Core: canonical decimal strings, never int/double.
add_executable(manga_volume_identity_harness
    ../tests/manga_volume_identity_harness.cpp
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
)
target_include_directories(manga_volume_identity_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_volume_identity_harness PRIVATE Qt6::Core)

# Deterministic runtime proof for MangaDownloader's lazy CDN host resolution.
# The resolver is injected with a delayed callback so the harness can prove
# coalescing, retry-token preservation, fallback, cancellation, and exactly-once
# completion without live DNS or the application process.
add_executable(manga_image_host_resolver_harness
    ../tests/manga_image_host_resolver_harness.cpp
    engine/MangaImageHostResolver.cpp
    engine/MangaImageHostResolver.h
)
target_include_directories(manga_image_host_resolver_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_image_host_resolver_harness PRIVATE Qt6::Core Qt6::Network)

# Deterministic runtime proof for MangaDownloader cancellation cleanup. The
# harness injects only the recursive-delete callback so it can hold the worker
# at the filesystem boundary and assert lifetime/signal ordering without live
# network or production data.
add_executable(manga_downloader_cancel_harness
    ../tests/manga_downloader_cancel_harness.cpp
    # unity-compiled into the harness TU (see its include note): engine/MangaDownloader.cpp
    engine/MangaDownloader.h
    engine/DownloadFileOps.h
    engine/WeebCentralScraper.cpp
    engine/WeebCentralScraper.h
    engine/MangaScraper.h
    engine/MangaImageHostResolver.cpp
    engine/MangaImageHostResolver.h
)
target_include_directories(manga_downloader_cancel_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_downloader_cancel_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)

add_executable(manga_volume_filepicker_harness
    ../tests/manga_volume_filepicker_harness.cpp
    torrent/MangaVolumeFilePicker.cpp
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
)
target_include_directories(manga_volume_filepicker_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_volume_filepicker_harness PRIVATE Qt6::Core)

add_executable(manga_tankoban_logic_harness
    ../tests/manga_tankoban_logic_harness.cpp
    engine/MangaTankobanLogic.cpp
    engine/MangaSynopsisEnricher.cpp
    torrent/MangaNyaaSource.cpp
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
    torrent/MangaTorrentDiscovery.cpp
    torrent/MangaTorrentDiscovery.h
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
    engine/CbzArchive.cpp
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaTankobanLogic.cpp
    third_party/miniz/miniz.c
)
target_include_directories(manga_volume_index_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_volume_index_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_volume_index_harness PRIVATE Qt6::Core Qt6::Concurrent)

add_executable(cbz_archive_harness
    ../tests/cbz_archive_harness.cpp
    engine/CbzArchive.cpp
    engine/CbzArchive.h
    third_party/miniz/miniz.c
)
target_include_directories(cbz_archive_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cbz_archive_harness PRIVATE Qt6::Core)

add_executable(cbz_archive_probe_harness
    ../tests/cbz_archive_probe_harness.cpp
    engine/CbzArchive.cpp
    engine/CbzArchive.h
    third_party/miniz/miniz.c
)
target_include_directories(cbz_archive_probe_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cbz_archive_probe_harness PRIVATE Qt6::Core)

add_executable(comic_cover_provider_harness
    ../tests/comic_cover_provider_harness.cpp
    engine/ComicCoverProvider.cpp
    engine/ComicCoverProvider.h
    engine/ComicCoverId.cpp
    engine/ComicCoverId.h
    engine/CbzArchive.cpp
    engine/CbzArchive.h
    third_party/miniz/miniz.c
)
target_include_directories(comic_cover_provider_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comic_cover_provider_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

add_executable(manga_volume_packer_harness
    ../tests/manga_volume_packer_harness.cpp
    engine/CbzArchive.cpp
    engine/MangaVolumePacker.cpp
    engine/MangaScraper.h
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaTankobanLogic.cpp
    third_party/miniz/miniz.c
)
target_include_directories(manga_volume_packer_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_volume_packer_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_volume_packer_harness PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)

add_executable(manga_volume_torrent_harness
    ../tests/manga_volume_torrent_harness.cpp
    torrent/MangaVolumeTorrentDownloader.cpp
    torrent/MangaVolumeRequestLedger.cpp
    torrent/MangaVolumeFilePicker.cpp
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
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
    engine/CbzArchive.cpp
    engine/MangaTankobanService.cpp
    engine/MangaTankobanService.h
    engine/MangaTankobanLogic.cpp
    engine/MangaSynopsisEnricher.cpp
    engine/MangaVolumeIndex.cpp
    engine/MangaVolumeArchiveIngestor.cpp
    engine/MangaVolumePacker.cpp
    engine/MangaScraper.h
    torrent/MangaNyaaSource.cpp
    torrent/MangaTorrentDiscovery.cpp
    torrent/MangaTorrentDiscovery.h
    torrent/MangaVolumeTorrentDownloader.cpp
    torrent/MangaVolumeRequestLedger.cpp
    torrent/MangaVolumeFilePicker.cpp
    torrent/MangaVolumeIdentity.cpp
    torrent/MangaVolumeIdentity.h
    torrent/MangaTorrentIndex.cpp
    torrent/MangaTorrentIndex.h
    torrent/MangaTorrentIndexer.cpp
    torrent/MangaTorrentIndexer.h
    torrent/IMangaTorrentMetainfoResolver.h
    third_party/miniz/miniz.c
)
target_include_directories(manga_tankoban_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(manga_tankoban_service_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/tankoban")
target_link_libraries(manga_tankoban_service_harness PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent)

# Arc 18 M2 — production metainfo resolver harness. Builds a REAL .torrent
# offline via libtorrent's create_torrent, then proves enumeration-without-
# payload, raw-index parity with the engine's metadata JSON, and the fail-closed
# candidate↔metainfo infoHash bridge. Needs colosseum_libtorrent — the same
# single engine dependency TorrentEngine uses.
add_executable(manga_torrent_metainfo_resolver_harness
    ../tests/manga_torrent_metainfo_resolver_harness.cpp
    torrent/MangaTorrentMetainfoResolver.cpp
    torrent/MangaTorrentMetainfoResolver.h
    torrent/IMangaTorrentMetainfoResolver.h
)
target_include_directories(manga_torrent_metainfo_resolver_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_torrent_metainfo_resolver_harness PRIVATE Qt6::Core colosseum_libtorrent)

# Arc 18 M3 — durable identity store harness. Pure QtSql, fully offline: a
# warm store persists pack/file/mapping rows, a COLD second instance rereads
# them, and outage/error paths prove identity survives while availability
# stales. No libtorrent — the store never touches torrents, only rows.
add_executable(manga_torrent_index_harness
    ../tests/manga_torrent_index_harness.cpp
    torrent/MangaTorrentIndex.cpp
    torrent/MangaTorrentIndex.h
)
target_include_directories(manga_torrent_index_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_torrent_index_harness PRIVATE Qt6::Core Qt6::Sql)

# Arc 18 M4 — indexing-coordinator harness. Real libtorrent-built .torrents
# through the REAL resolver + REAL SQLite store, fully offline: one pack → many
# verified mappings, combined/ambiguous/title-claim/named refusals, infoHash
# mismatch fail-closed. The indexer consumes the MangaNyaaCandidate aggregate
# header-only (no Nyaa source symbols needed), so no Qt6::Network here.
add_executable(manga_torrent_indexer_harness
    ../tests/manga_torrent_indexer_harness.cpp
    torrent/MangaTorrentIndexer.cpp
    torrent/MangaTorrentIndex.cpp
    torrent/MangaTorrentMetainfoResolver.cpp
    torrent/MangaVolumeIdentity.cpp
    engine/MangaTankobanLogic.cpp
)
target_include_directories(manga_torrent_indexer_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manga_torrent_indexer_harness PRIVATE
    Qt6::Core Qt6::Sql colosseum_libtorrent)

# ── lanista bridge harness ─────────────────────────────────────────────────
# Boots a real QQmlApplicationEngine over tests/lanista_harness_scene.qml,
# serves LanistaServer on the TEST pipe, and either self-checks (default,
# sentinel LANISTA_OK) or stays alive under --serve for CLI/scenario tests.
#
# F1-Bridge (2026-08-13) additive note: the harness now also builds a REAL VaultLibrary
# fixture (tests/lanista_harness.cpp) to exercise the "vault-forensics" bridge command
# end-to-end, so it needs VaultForensics plus the same full VaultLibrary link closure
# tst_vault_forensics already pays for above (F0 names VaultLibrary itself, not a lower
# helper module, as the seam a forensic projection composes) — Qt6::Sql/Qt6::Concurrent
# added to target_link_libraries below for exactly that reason.
# Browse-artwork execution plan, Slice 3 part 2 (2026-08-13) additive note: VaultLibrary.cpp now
# constructs VaultThumbnailer/VaultPosterFetcher/VaultArtworkResolver directly, so this harness
# needs the same three organs it already needs VaultLibrary itself for — Qt6::Network below
# was already linked (VaultForensics/VaultLibrary's own transitive needs), so no new link lib.
#
# Ground-truthed live (2026-08-14): a clean build of this closure fails without MpvQt —
# VaultThumbnailer.cpp calls MpvItem::findFfmpeg() (native/player/mpvitem.cpp), so the real
# MpvItem TU and MpvQt::MpvQt must both be linked in, same shape as tst_window_set_state
# (tests/CMakeLists.txt) — mpvitem.cpp + mpvproperties.h + http_header_fields.h, MpvQt::MpvQt.
# No MpvItem is ever constructed by any case here — findFfmpeg() is a static PATH lookup
# nothing in this harness calls — but the SYMBOL still has to resolve at link time.
add_executable(lanista_harness
    ../tests/lanista_harness.cpp
    devtools/LanistaServer.cpp
    devtools/LanistaServer.h
    devtools/LanistaEventLog.cpp
    devtools/LanistaEventLog.h
    player/mpvitem.cpp
    player/mpvitem.h
    player/mpvproperties.h
    player/http_header_fields.h
    engine/VaultForensics.cpp
    engine/VaultForensics.h
    engine/VaultLibrary.cpp
    engine/VaultLibrary.h
    engine/VaultCacheKey.h
    engine/VaultThumbnailer.cpp
    engine/VaultThumbnailer.h
    engine/VaultPosterFetcher.cpp
    engine/VaultPosterFetcher.h
    engine/VaultArtworkResolver.cpp
    engine/VaultArtworkResolver.h
    engine/VaultIndex.cpp
    engine/VaultIndex.h
    engine/VaultScanner.cpp
    engine/VaultScanner.h
    engine/VaultConfig.cpp
    engine/VaultConfig.h
    engine/VaultIdentity.cpp
    engine/VaultIdentity.h
    engine/VaultWatcher.cpp
    engine/VaultWatcher.h
    engine/VaultDownloadsRoot.cpp
    engine/VaultDownloadsRoot.h
    engine/VaultIdentifier.cpp
    engine/VaultIdentifier.h
    engine/ComicCoverId.cpp
    engine/ComicCoverId.h
    engine/VaultKit.cpp
    engine/VaultKit.h
    engine/VaultBrowseAway.cpp
    engine/VaultBrowseAway.h
    engine/VaultBrowseDetail.cpp
    engine/VaultBrowseDetail.h
    engine/VaultBrowseEmpty.cpp
    engine/VaultBrowseEmpty.h
    engine/VaultBookStateMigrator.cpp
    engine/VaultBookStateMigrator.h
    engine/VaultStoreIo.h
    engine/ComicsCatalog.cpp
    engine/ComicsCatalog.h
    engine/ImdbCatalog.cpp
    engine/ImdbCatalog.h
    engine/MalCatalog.cpp
    engine/MalCatalog.h
    reader/BookStores.cpp
    reader/BookStores.h
)
target_include_directories(lanista_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/player)
# The windeployqt'd platforms/ dir beside the exe carries ONLY qwindows.dll and
# shadows the Qt install's plugin dir, so an offscreen run cannot find qoffscreen
# (documented trap: tests/window_shell_gui_harness.cpp). Bake the real platforms
# path in at configure time (house pattern: TANKOBAN_FIXTURES_DIR); the harness
# applies it only when QT_QPA_PLATFORM_PLUGIN_PATH is unset.
get_target_property(_lanista_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_lanista_qtbin "${_lanista_qmake}" DIRECTORY)
get_filename_component(_lanista_qtprefix "${_lanista_qtbin}" DIRECTORY)
# The scene path is baked in too: __FILE__ is absolute for THIS generator but
# that is not guaranteed, and a relative __FILE__ would resolve against the CWD.
get_filename_component(_lanista_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../tests" ABSOLUTE)
target_compile_definitions(lanista_harness PRIVATE
    LANISTA_QT_PLATFORMS_DIR="${_lanista_qtprefix}/plugins/platforms"
    LANISTA_SCENE_DIR="${_lanista_tests_dir}")
target_link_libraries(lanista_harness PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Sql Qt6::Concurrent MpvQt::MpvQt)

# ── lanista CLI (tankoctl's sibling) ───────────────────────────────────────
add_executable(lanista
    tools/lanista.cpp
    tools/LanistaHash.cpp
    tools/LanistaHash.h
)
target_include_directories(lanista PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lanista PRIVATE Qt6::Core Qt6::Gui Qt6::Network)

# Pure-logic contract for Comick grouping + the tankoban completeness gate:
# mirrors colosseum-volume-db's volume_builder tests case-for-case.
add_executable(comick_volume_grouper_harness
    ../tests/comick_volume_grouper_harness.cpp
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
)
target_include_directories(comick_volume_grouper_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comick_volume_grouper_harness PRIVATE Qt6::Core)

# Pure-parse contract for the Comick catalog client (DB record + live chapters).
add_executable(comick_catalog_parse_harness
    ../tests/comick_catalog_parse_harness.cpp
    engine/ComickCatalogClient.cpp
    engine/ComickCatalogClient.h
    engine/ComickVolumeGrouper.cpp
    engine/ComickVolumeGrouper.h
)
target_include_directories(comick_catalog_parse_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comick_catalog_parse_harness PRIVATE Qt6::Core Qt6::Network)

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

# Arc 21 comics catalogue intelligence (curated_source/coverage layer, 2026-08-23):
# curated acquisition/coverage reads, format+availability discover facets, four new
# coverage catalogues in discoverPage, and stale-db (pre-Arc-21) degradation.
add_executable(comics_catalog_arc21_harness
    ../tests/comics_catalog_arc21_harness.cpp
    engine/ComicsCatalog.cpp
    engine/ComicsCatalog.h)
target_include_directories(comics_catalog_arc21_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comics_catalog_arc21_harness PRIVATE Qt6::Core Qt6::Sql)

# ── Deep Theatre catalogue (spec 2026-08-01): paged offline anime query contract ──
add_executable(mal_catalog_rows_harness
    ../tests/mal_catalog_rows_harness.cpp
    engine/MalCatalog.cpp
    engine/MalCatalog.h
    engine/VaultKit.cpp
    engine/VaultKit.h)
target_include_directories(mal_catalog_rows_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mal_catalog_rows_harness PRIVATE Qt6::Core Qt6::Sql)

# ── Deep Theatre catalogue (spec 2026-08-02): imdb index query contract ──
add_executable(imdb_catalog_harness
    ../tests/imdb_catalog_harness.cpp
    engine/ImdbCatalog.cpp
    engine/ImdbCatalog.h
    engine/VaultKit.cpp
    engine/VaultKit.h)
target_include_directories(imdb_catalog_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imdb_catalog_harness PRIVATE Qt6::Core Qt6::Sql)

# ── Tankoban Discover (spec 2026-08-01): paged offline MANGA discovery contract ──
add_executable(mal_catalog_discover_harness
    ../tests/mal_catalog_discover_harness.cpp
    engine/MalCatalog.cpp
    engine/MalCatalog.h
    engine/VaultKit.cpp
    engine/VaultKit.h)
target_include_directories(mal_catalog_discover_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mal_catalog_discover_harness PRIVATE Qt6::Core Qt6::Sql)

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
if(COLOSSEUM_BUILD_PLAYER2 AND NOT WIN32)
    message(FATAL_ERROR "COLOSSEUM_BUILD_PLAYER2 is Windows-only (D3D11/WASAPI) — Player 2 is not ported to Linux (arc-17). Leave it OFF on this platform.")
endif()
if(COLOSSEUM_BUILD_PLAYER2 AND WIN32)
    add_subdirectory(player2)
endif()

# ── Player 2 in-app backend (Task 17/18, spec 2026-07-21-colosseum-player2) ──
# ON by default as of TASK 18, the default flip (Hemanth's explicit go, 2026-07-26). The app now
# SHIPS the new video engine and boots it by default; the old mpv player remains linked and one
# environment variable (COLOSSEUM_PLAYER1=1) or one Desktop launcher away.
# Deliberately still a SECOND target_link_libraries call rather than an edit to the shared link line
# above, so this cannot collide with another agent's change there.
#
# ⚠ PACKAGING GATE: with this ON, a release build depends on the FFmpeg DLLs. The lab used a GPL
# build; a shipped Colosseum wants the LGPL drop-in (Player 2 never encodes). Settle that before
# cutting an installer - flagged 2026-07-25, still open.
# OFF by Hemanth's ruling 2026-08-09: "player 2 has been largely discontinued, it has no reason to
# exist inside the main build." Player 1 (MpvItem) is the only shipped player; the player2 lab and
# its CTest suite stay in the tree, opt-in, for the day player 1 hits a wall it cannot solve.
# Flipping this ON restores the old wiring intact (link + COLOSSEUM_PLAYER2 def + FFmpeg-62 copies).
option(COLOSSEUM_PLAYER2_IN_APP "Link the Player 2 backend into the app (default engine)" OFF)
if(COLOSSEUM_PLAYER2_IN_APP AND NOT WIN32)
    message(FATAL_ERROR "COLOSSEUM_PLAYER2_IN_APP is Windows-only (D3D11/WASAPI) — Player 2 is not ported to Linux (arc-17). Player 1/MpvQt is the Linux player. Leave it OFF on this platform.")
endif()
if(COLOSSEUM_PLAYER2_IN_APP AND WIN32)
    if(NOT TARGET player2_core)
        add_subdirectory(player2)
    endif()
    target_link_libraries(colosseum PRIVATE player2_core)
    target_compile_definitions(colosseum PRIVATE COLOSSEUM_PLAYER2=1)
    # Put the FFmpeg runtime NEXT TO the exe. Windows searches the executable's own directory before
    # PATH, so this makes the app launchable from anywhere - a Desktop shortcut, Explorer, the Start
    # menu - instead of only from a shell that happens to have FFmpeg on PATH.
    #
    # This is not a nicety. The moment Task 18 made Player 2 the default, the exe began REQUIRING
    # these DLLs at load time, so every launcher without FFmpeg on PATH stopped opening the app at
    # all - "avutil-60.dll was not found" before a single window appeared. Hemanth hit that on his own
    # shortcut within minutes of the flip, because the flip had only ever been verified from a shell
    # that had already exported the path. Verify launches the way HE launches, not the way you do.
    set(_player2_runtime_dlls avcodec-62 avdevice-62 avfilter-11 avformat-62 avutil-60
        swresample-6 swscale-9)
    foreach(_dll IN LISTS _player2_runtime_dlls)
        if(EXISTS "${FFMPEG_ROOT}/bin/${_dll}.dll")
            add_custom_command(TARGET colosseum POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${FFMPEG_ROOT}/bin/${_dll}.dll" "$<TARGET_FILE_DIR:colosseum>"
                COMMENT "Staging ${_dll}.dll beside colosseum.exe")
        endif()
    endforeach()
endif()

# --- Comic Reader (Agent 1, plan 2026-07-23): engine + backend harnesses ---
# ── Comic Reader (Agent 1, plan 2026-07-23): pure canonical pairing + typed model.
#    The from-scratch reader's combinatorial core — no Qt GUI, no decode, no cache,
#    no I/O. Every comicreader/ unit builds on these value types + the pairing walk.
#    QString/QVector/QSize/QVariantMap all live in Qt6::Core, so Core alone links. ──
add_executable(comicreader_pairing_harness
    ../tests/comicreader_pairing_harness.cpp
    comicreader/ComicReaderTypes.cpp comicreader/ComicReaderTypes.h
    comicreader/ComicReaderPairing.cpp comicreader/ComicReaderPairing.h)
target_include_directories(comicreader_pairing_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_pairing_harness PRIVATE Qt6::Core)

# ── Comic Reader (Agent 1, plan 2026-07-23): pinned, budgeted LRU page cache.
#    Keyed by (generation, page); a mutex-guarded QHash + LRU list, never held
#    while doing image work. Depends only on QImage (Gui) — no other
#    comicreader/ unit, no decode, no I/O. ──
add_executable(comicreader_cache_harness
    ../tests/comicreader_cache_harness.cpp
    comicreader/ComicReaderPageCache.cpp comicreader/ComicReaderPageCache.h
    comicreader/ComicReaderScaleCache.cpp comicreader/ComicReaderScaleCache.h)
target_include_directories(comicreader_cache_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_cache_harness PRIVATE Qt6::Core Qt6::Gui)

# ── Comic Reader (Agent 1, plan 2026-07-23): generation-safe decode coordinator.
#    The concurrency crux — a QThreadPool (max 2) decodes off-thread with
#    QImageReader; every result is re-checked against the live generation on the
#    owning thread before it can touch the cache or fire a signal, so a fast
#    entry switch never paints a stale page. QThreadPool/QRunnable are Core;
#    QImage/QImageReader are Gui. Depends on the types + page cache units. ──
add_executable(comicreader_decode_harness
    ../tests/comicreader_decode_harness.cpp
    comicreader/ComicReaderDecode.cpp comicreader/ComicReaderDecode.h
    comicreader/ComicReaderPageCache.cpp comicreader/ComicReaderPageCache.h
    comicreader/ComicReaderTypes.cpp comicreader/ComicReaderTypes.h
    engine/CbzArchive.cpp engine/CbzArchive.h
    third_party/miniz/miniz.c)
target_include_directories(comicreader_decode_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_decode_harness PRIVATE Qt6::Core Qt6::Gui)

# ── Comic Reader (Agent 1, plan 2026-07-23): auto-coupling probe (Task 5).
#    Pure scoring + decision: edgeContinuityCost samples the touching inner
#    edges of a displayed pair (8x96 luminance strip); chooseCouplingPhase picks
#    Normal vs Shifted from the summed per-pair costs under each phase. Depends
#    only on QImage (Gui) + Task 1's CouplingPhase enum — no other comicreader/
#    unit, no decode, no I/O. ──
add_executable(comicreader_coupling_harness
    ../tests/comicreader_coupling_harness.cpp
    comicreader/ComicReaderCoupling.cpp comicreader/ComicReaderCoupling.h
    comicreader/ComicReaderTypes.cpp comicreader/ComicReaderTypes.h)
target_include_directories(comicreader_coupling_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_coupling_harness PRIVATE Qt6::Core Qt6::Gui)

# ── Comic Reader (Agent 1, plan 2026-07-23): strip geometry model (Task 6).
#    Pure QAbstractListModel over PageMeta: estimated-vs-real page sizing,
#    running tops with a gap, the ±marginScreens decode window, pageAtCenter's
#    binary search, and TB2/QTGW's anti-jump scroll compensation. QObject ->
#    AUTOMOC (enabled project-wide above). No QImage/decode/IO here; Gui is
#    linked alongside Core to mirror this comicreader/ family's other harnesses. ──
add_executable(comicreader_strip_harness
    ../tests/comicreader_strip_harness.cpp
    comicreader/ComicReaderStripModel.cpp comicreader/ComicReaderStripModel.h
    comicreader/ComicReaderTypes.cpp comicreader/ComicReaderTypes.h)
target_include_directories(comicreader_strip_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_strip_harness PRIVATE Qt6::Core Qt6::Gui)

# ── Comic Reader (Agent 1, plan 2026-07-23): the app-facing backend + provider
#    (Task 7). ComicReaderCore orchestrates ALL five engine units behind the
#    QML `ComicReaderCore` context property; ComicReaderProvider is the read-only
#    image://comicreader/ seam. The harness drives that orchestration over real
#    decoded PNGs, pumping a QCoreApplication event loop for the decode
#    coordinator's queued report-backs. QQuickImageProvider lives in Qt6::Quick;
#    QImage/QImageReader in Qt6::Gui; the coordinator's QThreadPool in Qt6::Core. ──
add_executable(comicreader_core_harness
    ../tests/comicreader_core_harness.cpp
    comicreader/ComicReaderCore.cpp comicreader/ComicReaderCore.h
    comicreader/ComicReaderProvider.cpp comicreader/ComicReaderProvider.h
    comicreader/ComicReaderImageResponse.cpp comicreader/ComicReaderImageResponse.h
    comicreader/ComicReaderRenderProfile.cpp comicreader/ComicReaderRenderProfile.h
    comicreader/ComicReaderStripModel.cpp comicreader/ComicReaderStripModel.h
    comicreader/ComicReaderDecode.cpp comicreader/ComicReaderDecode.h
    comicreader/ComicReaderCoupling.cpp comicreader/ComicReaderCoupling.h
    comicreader/ComicReaderPageCache.cpp comicreader/ComicReaderPageCache.h
    comicreader/ComicReaderScaleCache.cpp comicreader/ComicReaderScaleCache.h
    comicreader/ComicReaderPairing.cpp comicreader/ComicReaderPairing.h
    comicreader/ComicReaderTypes.cpp comicreader/ComicReaderTypes.h
    engine/CbzArchive.cpp engine/CbzArchive.h
    third_party/miniz/miniz.c)
target_include_directories(comicreader_core_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Qt6::Qml is named explicitly (not leaned on through Qt6::Quick) because T27
# builds a real QQmlComponent to prove QML can still call imageUrl() with one
# argument now that the tier rides on a default argument.
target_link_libraries(comicreader_core_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

# ── Comic Reader overhaul (plan 2026-07-28, Task 1): the image-provider seam on
#    its own. Isolates ComicReaderProvider + ComicReaderImageResponse against a
#    hand-filled page cache and a bare live-generation atomic — no core, no
#    decode coordinator, no archive IO — so the response LIFECYCLE is what gets
#    tested: async publication, cancel-before-publish, the retired-generation
#    guard, and unservable requests still finishing exactly once. Publication is
#    queued back to the response's thread, so an event loop is pumped to deliver
#    it — a QGuiApplication one, because QQuickTextureFactory needs a GUI app to
#    resolve the scenegraph adaptation backend (so a headless run of this and of
#    comicreader_core_harness wants QT_QPA_PLATFORM=offscreen). QQuickImageResponse
#    and QQuickTextureFactory live in Qt6::Quick; QImage scaling in Qt6::Gui.
#    No Qt6::Test: QVERIFY/QTRY_COMPARE only compile inside a void QTest slot,
#    not in this family's failure-collecting main(). See the harness header. ──
add_executable(comicreader_provider_harness
    ../tests/comicreader_provider_harness.cpp
    comicreader/ComicReaderProvider.cpp comicreader/ComicReaderProvider.h
    comicreader/ComicReaderImageResponse.cpp comicreader/ComicReaderImageResponse.h
    comicreader/ComicReaderRenderProfile.cpp comicreader/ComicReaderRenderProfile.h
    comicreader/ComicReaderPageCache.cpp comicreader/ComicReaderPageCache.h
    comicreader/ComicReaderScaleCache.cpp comicreader/ComicReaderScaleCache.h)
target_include_directories(comicreader_provider_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(comicreader_provider_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

add_executable(download_file_ops_harness
    ../tests/download_file_ops_harness.cpp
)
target_include_directories(download_file_ops_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(download_file_ops_harness PRIVATE Qt6::Core)

# ── BiblioCatalog Discover/Explore foundation (plan 2026-08-03, Task 1): the
#    controlled taxonomy/facet mapper (spec 6.3 / 4.3) and the deterministic,
#    pure ranking functions (spec 7) in isolation. No providers, no SQLite, no
#    QML — value types + mapping tables + pure functions only, so the harness is
#    a reliable RED/GREEN oracle. Pure Qt6::Core (QString/QStringList/QDateTime). ──
add_executable(biblio_catalog_logic_harness
    ../tests/biblio_catalog_logic_harness.cpp
    engine/BiblioCatalogTypes.h
    engine/BiblioTaxonomy.cpp
    engine/BiblioTaxonomy.h
    engine/BiblioRanking.cpp
    engine/BiblioRanking.h
    # Task 2: keyless Apple/Open Library parsing + canonical work identity.
    engine/BiblioArtworkUrl.h
    engine/BiblioProviders.cpp
    engine/BiblioProviders.h
    engine/BiblioCanonicalizer.cpp
    engine/BiblioCanonicalizer.h
)
target_include_directories(biblio_catalog_logic_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Task 2 fixtures (Apple RSS/Search + Open Library) are read at runtime via this
# baked-in absolute path (house pattern: TANKOBAN_FIXTURES_DIR).
target_compile_definitions(biblio_catalog_logic_harness PRIVATE
    BIBLIO_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/biblio")
target_link_libraries(biblio_catalog_logic_harness PRIVATE Qt6::Core)

# ── BiblioCatalog Discover/Explore (plan 2026-08-03, Task 3): the SQLite
#    snapshot store (spec 8). Atomic publication of daily catalogue snapshots
#    (works/editions/sources/facets/rankings + eight-day ranking history),
#    transactional validation with atomic active-snapshot-ID swap, stable paged
#    reads, exact facet filtering, explicit gating, and seven-day retention
#    pruning. Pure Qt6::Core + Qt6::Sql; the harness opens a fresh temp DB per
#    test so no real app database is ever clobbered. Runs from build-msvc so the
#    bundled qsqlite.dll resolves at runtime. ──
add_executable(biblio_catalog_store_harness
    ../tests/biblio_catalog_store_harness.cpp
    engine/BiblioCatalogStore.cpp
    engine/BiblioCatalogStore.h
    engine/BiblioCatalogTypes.h
    engine/BiblioArtworkUrl.h
    engine/BiblioTaxonomy.cpp
    engine/BiblioTaxonomy.h)
target_include_directories(biblio_catalog_store_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biblio_catalog_store_harness PRIVATE Qt6::Core Qt6::Sql)

# ── BiblioCatalog Discover/Explore (plan 2026-08-03, Task 4): the daily
#    keyless refresh coordinator in isolation, against a FAKE transport (no
#    real network in a test). Proves request coalescing, once-per-local-day
#    gating, forced refresh, the <=4 concurrency cap, cached first paint,
#    partial-provider failure, total failure preserving the prior snapshot,
#    and first-run failure leaving ready==false. Pulls in the full engine
#    chain (taxonomy/ranking/providers/canonicalizer/store) since the
#    coordinator drives all of it. Qt6::Sql for the store; Qt6::Network only
#    for the header declaring BiblioNetworkTransport (unused by the fake path,
#    but the translation unit still needs QNetworkAccessManager/QNetworkReply
#    to compile). ──
add_executable(biblio_catalog_service_harness
    ../tests/biblio_catalog_service_harness.cpp
    engine/BiblioCatalog.cpp
    engine/BiblioCatalog.h
    engine/BiblioCatalogStore.cpp
    engine/BiblioCatalogStore.h
    engine/BiblioCatalogTypes.h
    engine/BiblioTaxonomy.cpp
    engine/BiblioTaxonomy.h
    engine/BiblioRanking.cpp
    engine/BiblioRanking.h
    engine/BiblioArtworkUrl.h
    engine/BiblioProviders.cpp
    engine/BiblioProviders.h
    engine/BiblioCanonicalizer.cpp
    engine/BiblioCanonicalizer.h
)
target_include_directories(biblio_catalog_service_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(biblio_catalog_service_harness PRIVATE Qt6::Core Qt6::Network Qt6::Sql)

# ── CTest registration seam (test ledger, migration slice 2 — 2026-08-06) ────
# One standard discovery layer over the EXISTING harness estate; registrations
# live in tests/CMakeLists.txt, never here. include(CTest) defines BUILD_TESTING
# (default ON); OFF skips registration entirely and changes nothing else — the
# harness targets above build either way, exactly as before this seam existed.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../tests"
                     "${CMAKE_CURRENT_BINARY_DIR}/tests")

    # Arc 36 N-07 owns CMake registration but not tests/CMakeLists.txt. Add the
    # watch-state production source to the existing focused domain-adapter target
    # after that child directory has defined it.
    target_sources(tst_core_sync_adapters PRIVATE
        account/WatchStateSyncAdapter.cpp
        account/WatchStateSyncAdapter.h)
    target_sources(account_first_light PRIVATE
        account/WatchStateSyncAdapter.cpp
        account/WatchStateSyncAdapter.h)
endif()
