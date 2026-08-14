# tests/CMakeLists.txt — CTest registration for the EXISTING harness estate.
#
# This file registers; it never defines targets or modifies test sources. The
# harnesses keep their house contract (stdout sentinel + exit code) — CTest
# consumes the exit code, which is authoritative. What registration buys:
# standard discovery (`ctest -N`), selection (`ctest -R name` / `-L label`),
# uniform red/green reporting, and a path to machine-readable output — the
# fuse box over detectors that already existed.
#
# Ledger: docs/colosseum-test-verification.md (keep it true in the same commit
# as any change here). Pilot set = the ledger's "lowest-risk first
# registrations": deterministic, QTemporaryDir/test-mode isolated, no network,
# no GUI-platform traps. Registrations grow deliberately — never register
# live-network probes, seeders, or destructive gates as ordinary tests.

# One place to grow the pilot set. Each entry: target name + labels.
function(colosseum_register_harness target)
    add_test(NAME "colosseum.${target}" COMMAND ${target})
    set_tests_properties("colosseum.${target}" PROPERTIES
        LABELS "${ARGN}"
        # GUI-subsystem binaries are silent without this — ledger, env traps.
        ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
        # The house contract: exit 0 is the ONLY green.
        TIMEOUT 120)
endfunction()

# ── Pilot set (ledger: "Migration candidates — pilot CTest registration") ────
colosseum_register_harness(window_state_policy_harness   unit;windows)
colosseum_register_harness(search_history_store_harness  unit)
colosseum_register_harness(progress_store_harness        unit)
colosseum_register_harness(collection_store_harness      unit)
colosseum_register_harness(cbz_archive_harness           unit)
colosseum_register_harness(poster_scoreboard_harness     unit)
colosseum_register_harness(comicreader_cache_harness     unit)
colosseum_register_harness(biblio_catalog_logic_harness  unit)
# Multi-volume pack demux (Slice 1): parser table + Entry round-trip. No net,
# QTemporaryDir/isolated-AppData — deterministic, sentinel+exit-code contract.
colosseum_register_harness(comic_downloader_pack_demux_harness unit)
colosseum_register_harness(update_version_harness unit)
colosseum_register_harness(update_manifest_trust_harness unit)
colosseum_register_harness(update_release_client_harness unit)
colosseum_register_harness(update_download_harness unit)
colosseum_register_harness(update_service_harness unit)
colosseum_register_harness(update_install_bridge_harness unit;windows)

# ── Qt Test targets (slice 3+) ───────────────────────────────────────────────
# Test-only Qt components: discovered HERE, under BUILD_TESTING, never linked
# into the shipping app (arc non-goal). Targets defined here are real Qt Tests:
# per-case reporting, data rows, selectable by function/tag.
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)

# Slice-3 pilot: windowstatepolicy/windowmodestore contracts. Same production
# units and link set as the legacy window_state_policy_harness, which stays
# built and registered until parity review retires it (ledger, migration
# policy).
set(COLOSSEUM_NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../native")
add_executable(tst_window_state_policy
    auto/window/tst_window_state_policy.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/windowstatepolicy.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/windowstatepolicy.h"
    "${COLOSSEUM_NATIVE_DIR}/player/windowmodestore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/windowmodestore.h")
target_include_directories(tst_window_state_policy PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_window_state_policy
    PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)
# Land beside the app and the legacy harnesses: the Qt/FFmpeg DLLs are deployed
# into build-msvc/ (two POST_BUILD steps on the app target), and an exe in a
# subdirectory dies 0xc0000135 before main() — the exact disease the ledger
# records for the Player 2 lab. Verified live on this target's first run.
set_target_properties(tst_window_state_policy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
# Qt6Test.dll is a test-only dependency no app deploy step ever staged — copy
# it beside the exe exactly like the app target stages Qt6Sql.dll. Without
# this the test dies 0xc0000135 before main() (verified live, both causes).
add_custom_command(TARGET tst_window_state_policy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.window_state_policy
         COMMAND tst_window_state_policy)
set_tests_properties(colosseum.qttest.window_state_policy PROPERTIES
    LABELS "unit;windows;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Theatre House HTTP Source slice 1: the map→mpv http-header-fields formatter as data rows
# (comma/colon safety + the third-party-JSON injection guards). Header-only unit under test, so
# the target compiles just the test TU. Same deploy pattern as tst_window_state_policy: land beside
# the app's Qt DLLs and stage Qt6Test.dll, or it dies 0xc0000135 before main().
add_executable(tst_http_header_fields auto/player/tst_http_header_fields.cpp)
target_include_directories(tst_http_header_fields PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_http_header_fields PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_http_header_fields PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_http_header_fields POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.http_header_fields
         COMMAND tst_http_header_fields)
set_tests_properties(colosseum.qttest.http_header_fields PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Lanista layout verdict (Agent Visibility Phase 2, Slice L2, 2026-08-13) ──────────
# LanistaLayoutVerdict — the pure, header-only geometry evaluator behind the runner-local
# `layout_verdict` scenario step in native/tools/lanista.cpp: actionableNonzero/contained/
# noPeerOverlap rules plus the single-generation guarantee, resolved against dump-ui-shaped
# JSON with no bridge, no app, no QGuiApplication. Header-only under test, so the target
# compiles just the test TU — same deploy pattern as tst_http_header_fields: land beside the
# app's Qt DLLs and stage Qt6Test.dll, or the exe dies 0xc0000135 before main().
add_executable(tst_layout_verdict auto/lanista/tst_layout_verdict.cpp)
target_include_directories(tst_layout_verdict PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_layout_verdict PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_layout_verdict PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_layout_verdict POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.layout_verdict
         COMMAND tst_layout_verdict)
set_tests_properties(colosseum.qttest.layout_verdict PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Downloads failure-title seam (2026-08-11 handoff): a fake volume owner exposes
# activeVolumeJobs() plus failed(id, reason), so the test drives the real
# LocalDownloads retention path without constructing the network/torrent stack.
add_executable(tst_local_downloads_failure
    auto/downloads/tst_local_downloads_failure.cpp
    auto/downloads/local_downloads_test_stubs.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/LocalDownloads.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/LocalDownloads.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/MangaDownloader.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/BookDownloader.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicDownloader.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/MangaTankobanService.h"
    "${COLOSSEUM_NATIVE_DIR}/player/downloadstore.h")
target_include_directories(tst_local_downloads_failure PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_local_downloads_failure PRIVATE
    Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui Qt6::Test)
set_target_properties(tst_local_downloads_failure PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_local_downloads_failure POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.local_downloads_failure
         COMMAND tst_local_downloads_failure)
set_tests_properties(colosseum.qttest.local_downloads_failure PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault kit (execution plan Slice 1, 2026-08-08) ───────────────────────────
# The Vault's pure-logic kit (VaultKit): census classifier (kind inference,
# mixed-leaf flag, loose capture, scanIgnore needles), the ported title cleaner,
# and the season/episode grammar + season-climb guard. Pure Qt6::Core — VaultKit
# has no app deps — and GUILESS. The committed fixture tree is baked in via
# VAULT_FIXTURES_DIR (house pattern: TANKOBAN_FIXTURES_DIR). Same deploy pattern
# as tst_window_state_policy: land beside the app's Qt DLLs and stage Qt6Test.dll
# or the exe dies 0xc0000135 before main().
add_executable(tst_vault_kit
    auto/vault/tst_vault_kit.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h")
target_include_directories(tst_vault_kit PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_vault_kit PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_kit PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_vault_kit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_kit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_kit
         COMMAND tst_vault_kit)
set_tests_properties(colosseum.qttest.vault_kit PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault stores (execution plan Slice 2, 2026-08-08) ────────────────────────
# VaultConfig (user-intent JSON: roots/kinds/scanIgnore/hidden, atomic write with
# last-known-good .bak recovery) and VaultIdentity (content-addressed ids +
# unique-signature rename/move re-attachment, ambiguity never silently merged),
# sharing VaultStoreIo. Pure Qt6::Core, GUILESS; QTemporaryDir isolation, no net.
add_executable(tst_vault_stores
    auto/vault/tst_vault_stores.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultStoreIo.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultRecent.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultRecent.h"
    # Slice 16: the Vault-owned alias migration consumes the existing public Reader 2 store API.
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.cpp"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.h")
target_include_directories(tst_vault_stores PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_stores PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_vault_stores PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_stores POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_stores
         COMMAND tst_vault_stores)
set_tests_properties(colosseum.qttest.vault_stores PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault downloads root (execution plan Slice 18, 2026-08-10) ───────────────
# VaultDownloadsRoot derives the synthetic downloads root's VaultIndex::FileRows
# from Colosseum's own download backbones (videos + CBZ comics + CBZ tankoban
# volumes + epub/pdf books). Backbone calls go through QMetaObject::invokeMethod
# (the same path QML uses), so the test drives the real derivation with QObject
# fakes. Pure Qt6::Core, GUILESS; QTemporaryDir isolation, no net.
add_executable(tst_vault_downloads_root
    auto/vault/tst_vault_downloads_root.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.h")
target_include_directories(tst_vault_downloads_root PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_downloads_root PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_downloads_root PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_downloads_root POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_downloads_root POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_downloads_root
         COMMAND tst_vault_downloads_root)
set_tests_properties(colosseum.qttest.vault_downloads_root PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Extensions first-run consent (Living Guide Task 11, 2026-08-09) ──────────
# The fresh-profile acquisition-source consent contract: core catalogues seeded
# enabled, every removable acquisition/playback well (non-core + `stream`) seeded
# INSTALLED but DISABLED until the user enables it; non-fetching capabilities stay
# enabled. Derivation is by manifest resources + core, never string-matching.
# Compiles ExtensionsStore.cpp; isolated AppData via test mode + a wiped index.
add_executable(tst_extensions_first_run
    auto/extensions/tst_extensions_first_run.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/ExtensionsStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ExtensionsStore.h")
target_include_directories(tst_extensions_first_run PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_extensions_first_run PRIVATE Qt6::Core Qt6::Network Qt6::Test)
set_target_properties(tst_extensions_first_run PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_extensions_first_run POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.extensions_first_run
         COMMAND tst_extensions_first_run)
set_tests_properties(colosseum.extensions_first_run PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault index (execution plan Slice 3, 2026-08-08) ─────────────────────────
# VaultIndex — the rebuildable SQLite scan product: transactional full-replace
# publish (a cancelled/errored publish rolls back, previous contents intact),
# incremental upsert, and numeric-aware folder-order listing. Qt6::Sql; the
# qsqlite driver resolves because the exe lands in build-msvc/ beside the
# app-deployed plugin (ledger deploy note). GUILESS; QTemporaryDir DB per run.
add_executable(tst_vault_index
    auto/vault/tst_vault_index.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h")
target_include_directories(tst_vault_index PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_index PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_index PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_index POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_index
         COMMAND tst_vault_index)
set_tests_properties(colosseum.qttest.vault_index PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault scanner (execution plan Slice 4, 2026-08-08) ───────────────────────
# VaultScanner — the cancellable off-thread census: pure buildScan() over the
# fixture tree, generation-guarded applyResult() (stale drop, cancelled no-publish),
# and the threaded scanRoot()/buffered-rescan wrapper. Composes VaultKit +
# VaultIdentity + VaultIndex. Qt6::Concurrent for the pool; Qt6::Sql for the index;
# fixtures via VAULT_FIXTURES_DIR. GUILESS; QTemporaryDir index+identity per run.
add_executable(tst_vault_scanner
    auto/vault/tst_vault_scanner.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h"
    # Slice 15: the live-shelf watcher (VaultWatcher) + the user-intent store it reads
    # (VaultConfig — confirmed roots, scanIgnore, kind-override law).
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.h"
    # Slice 16: the scanner consumes the Vault-owned Reader 2 migration boundary.
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.cpp"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.h")
target_include_directories(tst_vault_scanner PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_vault_scanner PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_scanner PRIVATE Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Test)
set_target_properties(tst_vault_scanner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_scanner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_scanner
         COMMAND tst_vault_scanner)
set_tests_properties(colosseum.qttest.vault_scanner PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault browse-away (browse-face execution plan Slice 6, 2026-08-13) ───────
# VaultBrowseAway — the browse projection's away-propagation seam, pulled out of VaultLibrary
# specifically so it is testable without constructing the full façade (whose constructor
# unconditionally builds a VaultWatcher, dragging in the scanner/downloads-root/identifier
# tree). ownerRootPath/ownerRootAway resolve which confirmed root owns a browse path (including
# the "::show::" sibling-collapse sentinel) and whether that root's rows are currently marked
# away; offlineBrowseAt() is the durable-index fallback for a level whose owning root can no
# longer be walked at all (design §4.7 "nothing disappears"). Qt6::Sql for VaultIndex; GUILESS;
# QTemporaryDir index + real small fixture trees per run.
add_executable(tst_vault_browse_away
    auto/vault/tst_vault_browse_away.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.h")
target_include_directories(tst_vault_browse_away PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_browse_away PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_browse_away PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_browse_away POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_browse_away
         COMMAND tst_vault_browse_away)
set_tests_properties(colosseum.qttest.vault_browse_away PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault browse-empty (browse-face execution plan Slice 9, 2026-08-13) ─────
# VaultBrowseEmpty::classify — the grid's empty-cause enum (no-roots vs empty-folder vs
# all-away; design §4.5's four distinct causes minus the deferred "filtered"). Pure logic, no
# VaultIndex/VaultConfig needed — table-driven over the three booleans
# VaultLibrary::browseEmptyCause() derives from rootsDetail()/browseAt()/
# VaultBrowseAway::ownerRootAway(). GUILESS, pure Qt6::Core.
add_executable(tst_vault_browse_empty
    auto/vault/tst_vault_browse_empty.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.h")
target_include_directories(tst_vault_browse_empty PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_browse_empty PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_vault_browse_empty PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_browse_empty POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_browse_empty
         COMMAND tst_vault_browse_empty)
set_tests_properties(colosseum.qttest.vault_browse_empty PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 60)

# ── Vault browse-detail (browse-face execution plan Slice 7, 2026-08-13) ─────
# VaultBrowseDetail — the detail sheet's ONE projection, browseDetail(key): copies you hold
# (same canonical identity across roots where identity exists, else the single physical group),
# companions, extras, evidence, best-quality line. Pulled out of VaultLibrary for the same
# reason VaultBrowseAway was (Slice 6): testable without the full façade's watcher/scanner/
# identifier tree. Compiles VaultKit.cpp too (describeFilmFolder's companion/extras walk).
# Qt6::Sql for VaultIndex; GUILESS; QTemporaryDir index + the real browse-film/ fixture tree.
add_executable(tst_vault_browse_detail
    auto/vault/tst_vault_browse_detail.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h")
target_include_directories(tst_vault_browse_detail PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_vault_browse_detail PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_browse_detail PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_browse_detail PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_browse_detail POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_browse_detail
         COMMAND tst_vault_browse_detail)
set_tests_properties(colosseum.qttest.vault_browse_detail PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault enricher (execution plan Slice 5, 2026-08-08) ──────────────────────
# VaultEnricher — thin fact-filler reusing CbzArchive (comic pages + cover entry
# for image://comiccover/) and ffprobe (duration cache); writes facts back to the
# index. Compiles CbzArchive + miniz (its ZIP backend). Qt6::Sql for the index;
# real CBZ via TANKOBAN_FIXTURES_DIR, EPUB/corrupt fixtures via VAULT_FIXTURES_DIR.
# The cover providers are exercised without a GUI window.
add_executable(tst_vault_enricher
    auto/vault/tst_vault_enricher.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultEnricher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultEnricher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookCoverProvider.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookCoverProvider.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.h"
    "${COLOSSEUM_NATIVE_DIR}/third_party/miniz/miniz.c"
    # vault-admission slice: enrich() now video-admits via the real probe (raw libmpv),
    # so the enricher test links MediaAdmissionProbe + libmpv, same as the probe/launch harnesses.
    "${COLOSSEUM_NATIVE_DIR}/player/MediaAdmissionProbe.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/MediaAdmissionProbe.h")
target_include_directories(tst_vault_enricher PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" ${Libmpv_INCLUDE_DIRS})
target_compile_definitions(tst_vault_enricher PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tankoban"
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_enricher PRIVATE
    Qt6::Core Qt6::Sql Qt6::Gui Qt6::Quick Qt6::Test ${Libmpv_LIBRARIES})
set_target_properties(tst_vault_enricher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_enricher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_enricher
         COMMAND tst_vault_enricher)
set_tests_properties(colosseum.qttest.vault_enricher PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault browse artwork: persistent frame-grab producer (browse-artwork execution
#    plan, Slice 1, 2026-08-13) ───────────────────────────────────────────────────
# VaultThumbnailer — a sibling of the hover-only SeekThumbnailer (native/player/
# seekthumbnailer.cpp, untouched) that writes a persisted, keyed still to disk instead
# of piping a data: URL. Needs MpvItem::findFfmpeg() (same ffmpeg locator
# SeekThumbnailer uses), so this target compiles mpvitem.cpp and links MpvQt/libmpv,
# same as http_header_channel_harness / tst_journey_play_video above. Real ffmpeg,
# real fixture MP4 (tests/fixtures/vault/media/tiny.mp4 — the same one
# vault_admission_probe_harness proves decodes a real frame); QTemporaryDir isolation.
#
# Slice 2, 2026-08-13 adds VaultPosterFetcher (canonical poster fetcher + cache) to the
# SAME target/binary — a sibling producer, identity-id keyed instead of (path,size,
# mtime) keyed, transported over QNetworkAccessManager (hence Qt6::Network below)
# instead of an ffmpeg QProcess. Tests drive a local file:// URL — no live network in
# the gate.
add_executable(tst_vault_artwork
    auto/vault/tst_vault_artwork.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultCacheKey.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvproperties.h"
    "${COLOSSEUM_NATIVE_DIR}/player/http_header_fields.h")
target_include_directories(tst_vault_artwork PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/player")
target_compile_definitions(tst_vault_artwork PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_artwork
    PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::Test MpvQt::MpvQt)
set_target_properties(tst_vault_artwork PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_artwork POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_artwork
         COMMAND tst_vault_artwork)
set_tests_properties(colosseum.qttest.vault_artwork PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault MAL title identity (Slice 17 foundation) ─────────────────────────
# Read-only normalized title lookup: punctuation folding, ambiguity retention,
# year/medium narrowing, and missing-catalogue safety.
add_executable(tst_vault_mal_match
    auto/vault/tst_vault_mal_match.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h")
target_include_directories(tst_vault_mal_match PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_mal_match PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_mal_match PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_mal_match POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_mal_match
         COMMAND tst_vault_mal_match)
set_tests_properties(colosseum.qttest.vault_mal_match PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault IMDb title identity (Slice 17 foundation) ────────────────────────
# Read-only normalized title lookup: punctuation folding, ambiguity retention,
# year narrowing, honest synopsis absence, legacy-schema safety, and no-db safety.
add_executable(tst_vault_imdb_match
    auto/vault/tst_vault_imdb_match.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h")
target_include_directories(tst_vault_imdb_match PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_imdb_match PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_imdb_match PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_imdb_match POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_imdb_match
         COMMAND tst_vault_imdb_match)
set_tests_properties(colosseum.qttest.vault_imdb_match PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault identity adoption (Slice 17) ──────────────────────────────────────
# Fixture-backed certainty gate: one candidate adopts, ambiguity and absent catalogues stay
# filename-honest, identity removal preserves progress/file ids, and reshelving changes kind only.
add_executable(tst_vault_identifier
    auto/vault/tst_vault_identifier.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.h")
target_include_directories(tst_vault_identifier PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_vault_identifier PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_vault_identifier PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_identifier POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_identifier
         COMMAND tst_vault_identifier)
set_tests_properties(colosseum.qttest.vault_identifier PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault admission probe (execution plan Slice 6, 2026-08-08) ───────────────
# MediaAdmissionProbe drives its OWN headless libmpv handle and admits only on a
# DECODED FRAME (dwidth>0), never FILE_LOADED. This harness runs it against real
# ffmpeg-generated fixtures (valid MP4 admits; non-video bytes / truncated MP4 /
# audio-only reject). House sentinel/exit-code idiom (like http_header_channel);
# QCoreApplication only (the raw handle is headless). Links libmpv directly (not
# MpvQt) — libmpv-2.dll already sits in build-msvc/ beside the exe.
add_executable(vault_admission_probe_harness
    vault_admission_probe_harness.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/MediaAdmissionProbe.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/MediaAdmissionProbe.h")
target_include_directories(vault_admission_probe_harness PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" ${Libmpv_INCLUDE_DIRS})
target_compile_definitions(vault_admission_probe_harness PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(vault_admission_probe_harness PRIVATE Qt6::Core ${Libmpv_LIBRARIES})
set_target_properties(vault_admission_probe_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.vault_admission_probe_harness
         COMMAND vault_admission_probe_harness)
set_tests_properties(colosseum.vault_admission_probe_harness PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault launch router (execution plan Slice 7, 2026-08-08) ─────────────────
# LocalLaunch (classify -> backend-validate -> route, reject before session) +
# VaultPageStore (the ComicReaderShell injected-store adapter returning
# [{index,archive,entry,group}] descriptors). The harness drives both over real
# fixtures: valid CBZ/MP4/epub accept; corrupt/non-video/png/missing reject; the
# page store lists the CBZ in natural order. Composes MediaAdmissionProbe (real
# libmpv) + CbzArchive + miniz + VaultKit + VaultIdentity. House idiom; headless.
add_executable(vault_launch_router_harness
    vault_launch_router_harness.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/LocalLaunch.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/LocalLaunch.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPageStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPageStore.h"
    "${COLOSSEUM_NATIVE_DIR}/player/MediaAdmissionProbe.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.h"
    "${COLOSSEUM_NATIVE_DIR}/third_party/miniz/miniz.c"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultRecent.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultRecent.h")
target_include_directories(vault_launch_router_harness PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" ${Libmpv_INCLUDE_DIRS})
target_compile_definitions(vault_launch_router_harness PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tankoban"
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(vault_launch_router_harness PRIVATE Qt6::Core ${Libmpv_LIBRARIES})
set_target_properties(vault_launch_router_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.vault_launch_router_harness
         COMMAND vault_launch_router_harness)
set_tests_properties(colosseum.vault_launch_router_harness PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Vault forensics (Agent Visibility Phase 2, Slice F1-Core) ────────────────
# VaultForensics — one bounded, read-only projection composing a REAL VaultLibrary (F0's named
# safe seam, docs/visibility/vault-forensic-owner-thread.md §10 — VaultLibrary, never VaultIndex
# directly): revision()/itemCount()/rootsDetail()/recentArrivals()/browseAt()/browseDetail() only.
# No SQLite connection of its own, no write, no mutation, no publish() call. Every fixture
# constructs the full graph VaultLibrary.cpp's own translation unit needs to link (its
# unconditional VaultWatcher child, plus VaultIdentifier/VaultDownloadsRoot/VaultBrowseAway/
# VaultBrowseDetail/VaultBrowseEmpty/VaultKit/ComicCoverId and their own transitive deps down to
# the three read-only catalogue databases) — the same cost tst_vault_scanner.cpp already pays for
# a smaller slice of this graph, paid here in full because F0 names VaultLibrary itself, not the
# lower helper modules, as the seam VaultForensics must compose. Qt6::Concurrent for VaultScanner
# (linked, never exercised — no scan/publish path is driven by any test here). GUILESS;
# QTemporaryDir index + small real fixture trees per run, same idiom as tst_vault_index.cpp.
# Browse-artwork execution plan, Slice 3 part 2 (2026-08-13) additive note: VaultLibrary.cpp now
# constructs VaultThumbnailer/VaultPosterFetcher/VaultArtworkResolver directly (same as
# lanista_harness, native/CMakeLists.txt), so this fixture needs the same three organs — and
# Qt6::Network below (VaultPosterFetcher's QNetworkAccessManager), which this target did not
# previously need.
#
# Ground-truthed live (2026-08-14): a clean build of this closure fails without MpvQt —
# VaultThumbnailer.cpp calls MpvItem::findFfmpeg() (native/player/mpvitem.cpp), so the real
# MpvItem TU and MpvQt::MpvQt must both be linked in, same shape as tst_window_set_state's
# identical fix (aa8630e) and lanista_harness's identical fix (1778f71) for the same
# VaultThumbnailer/MpvQt dependency — mpvitem.cpp + mpvproperties.h + http_header_fields.h,
# MpvQt::MpvQt. No MpvItem is ever constructed by any case here — findFfmpeg() is a static PATH
# lookup nothing in this suite calls — but the SYMBOL still has to resolve at link time.
add_executable(tst_vault_forensics
    auto/vault/tst_vault_forensics.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvproperties.h"
    "${COLOSSEUM_NATIVE_DIR}/player/http_header_fields.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultForensics.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultForensics.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultLibrary.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultLibrary.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultCacheKey.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultStoreIo.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.cpp"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.h")
target_include_directories(tst_vault_forensics PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/player")
target_link_libraries(tst_vault_forensics PRIVATE
    Qt6::Core Qt6::Sql Qt6::Concurrent Qt6::Network Qt6::Test MpvQt::MpvQt)
set_target_properties(tst_vault_forensics PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_vault_forensics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.vault_forensics
         COMMAND tst_vault_forensics)
set_tests_properties(colosseum.qttest.vault_forensics PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Slice 1 WIRE proof: drives a REAL MpvItem at a loopback QTcpServer and asserts the Referer/Origin
# reaches ffmpeg's request, a comma value stays one header, ytdl is off, and loadFile clears the
# field (leak guard). Compiles mpvitem.cpp (nearly standalone — only mpvproperties.h) and links the
# app's MpvQt/libmpv, whose DLLs already sit in build-msvc/ beside this exe. Loopback only, no live
# network — safe under the `unit` gate. AUTOMOC (set ON above) processes MpvItem's Q_OBJECT.
add_executable(http_header_channel_harness
    http_header_channel_harness.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvproperties.h"
    "${COLOSSEUM_NATIVE_DIR}/player/http_header_fields.h")
target_include_directories(http_header_channel_harness PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/player")
target_link_libraries(http_header_channel_harness
    PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network MpvQt::MpvQt)
set_target_properties(http_header_channel_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.http_header_channel_harness
         COMMAND http_header_channel_harness)
set_tests_properties(colosseum.http_header_channel_harness PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Journey: play video to decoded-frame readiness (Agent Visibility Phase 2,
#    Slice J1-Video-Seam, 2026-08-13) ────────────────────────────────────────
# Drives the REAL production MpvItem over the same real ffmpeg-generated fixtures the Vault
# admission gate uses (tests/fixtures/vault/media/), proving decodedWidth/decodedHeight report
# the same dwidth/dheight decode truth MediaAdmissionProbe reads on its own headless handle —
# never FILE_LOADED alone. Compiles mpvitem.cpp (same as http_header_channel_harness, just above)
# and links the app's MpvQt/libmpv, whose DLLs already sit in build-msvc/ beside this exe.
# AUTOMOC (set ON above) processes MpvItem's Q_OBJECT; real Qt Test idiom (per-case reporting).
add_executable(tst_journey_play_video
    auto/player/tst_journey_play_video.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvproperties.h"
    "${COLOSSEUM_NATIVE_DIR}/player/http_header_fields.h")
target_include_directories(tst_journey_play_video PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/player")
target_compile_definitions(tst_journey_play_video PRIVATE
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_journey_play_video
    PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test MpvQt::MpvQt)
set_target_properties(tst_journey_play_video PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_journey_play_video POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.journey_play_video
         COMMAND tst_journey_play_video)
set_tests_properties(colosseum.qttest.journey_play_video PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Window-set-state bridge command (Agent Visibility Phase 2, Slice J1-Tray-Bridge,
#    2026-08-14) ─────────────────────────────────────────────────────────────────────
# Drives the REAL LanistaServer::dispatch() path — including its central Drive gate — over a
# real QLocalSocket, exactly like tests/lanista_harness.cpp's own client, against one or two
# inline QML `Window {}` roots (no scene file needed). Compiles LanistaServer.cpp itself, which
# needs the full VaultForensics/VaultLibrary link closure (same reasoning + same list as
# tst_vault_forensics above and native/CMakeLists.txt's lanista_harness target: F0 names
# VaultLibrary itself, not a lower helper module, as the seam VaultForensics composes) even
# though no case here ever calls vault-forensics — m_vaultForensics stays nullptr, exactly the
# "no-Vault context" shape lanista_harness's own bare QML-scene fixture already exercises.
# Needs Qt6::Qml/Quick beyond that closure (LanistaServer's own QQmlApplicationEngine/
# QQuickWindow/QQuickItem surface, which tst_vault_forensics never touches).
#
# Ground-truthed live (2026-08-14): a clean build of this closure fails without MpvQt —
# VaultThumbnailer.cpp calls MpvItem::findFfmpeg() (native/player/mpvitem.cpp), so the real
# MpvItem TU and MpvQt::MpvQt must both be linked in, same shape as tst_journey_play_video
# above (mpvitem.cpp + mpvproperties.h + http_header_fields.h, MpvQt::MpvQt). No MpvItem is
# ever constructed here — findFfmpeg() is a static PATH lookup, never called by any case —
# but the SYMBOL still has to resolve at link time. Needs MpvQt.dll/libmpv-2.dll deployed
# beside the exe at runtime (same two DLLs the app target's own deploy step needs).
add_executable(tst_window_set_state
    auto/lanista/tst_window_set_state.cpp
    "${COLOSSEUM_NATIVE_DIR}/devtools/LanistaServer.cpp"
    "${COLOSSEUM_NATIVE_DIR}/devtools/LanistaServer.h"
    "${COLOSSEUM_NATIVE_DIR}/devtools/LanistaEventLog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/devtools/LanistaEventLog.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvitem.h"
    "${COLOSSEUM_NATIVE_DIR}/player/mpvproperties.h"
    "${COLOSSEUM_NATIVE_DIR}/player/http_header_fields.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultForensics.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultForensics.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultLibrary.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultLibrary.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultCacheKey.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultThumbnailer.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultPosterFetcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultArtworkResolver.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultScanner.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultConfig.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultWatcher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultDownloadsRoot.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIdentifier.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicCoverId.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseAway.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseDetail.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBrowseEmpty.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultBookStateMigrator.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultStoreIo.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ComicsCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/ImdbCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/MalCatalog.h"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.cpp"
    "${COLOSSEUM_NATIVE_DIR}/reader/BookStores.h")
target_include_directories(tst_window_set_state PRIVATE
    "${COLOSSEUM_NATIVE_DIR}" "${COLOSSEUM_NATIVE_DIR}/player")
# House trap (native/CMakeLists.txt's lanista_harness target; tests/window_shell_gui_harness.cpp):
# the windeployqt'd platforms/ dir beside the exe carries ONLY qwindows.dll and shadows the real
# Qt install's plugin dir, so bare QT_QPA_PLATFORM=offscreen fail-fasts silently. Bake the real
# platforms path in at configure time; the test applies it only when
# QT_QPA_PLATFORM_PLUGIN_PATH is unset (same conditional, same reasoning).
get_target_property(_wss_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_wss_qtbin "${_wss_qmake}" DIRECTORY)
get_filename_component(_wss_qtprefix "${_wss_qtbin}" DIRECTORY)
target_compile_definitions(tst_window_set_state PRIVATE
    WINDOW_SET_STATE_QT_PLATFORMS_DIR="${_wss_qtprefix}/plugins/platforms")
target_link_libraries(tst_window_set_state PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Sql Qt6::Concurrent Qt6::Test
    MpvQt::MpvQt)
set_target_properties(tst_window_set_state PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_window_set_state POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.window_set_state
         COMMAND tst_window_set_state)
set_tests_properties(colosseum.qttest.window_set_state PROPERTIES
    LABELS "unit;windows;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Qt Quick Test runner (slice 4) ───────────────────────────────────────────
# One repo-built runner over tests/qml/tst_*.qml, replacing the Qt-install
# qmltestrunner.exe path hardcoded in the legacy .ps1 gates. -input points at
# the SOURCE tests/qml dir so the tests' file-relative production imports
# ("../../qml/...") resolve against the real tree. These tests open REAL
# windows (physical hit-testing is their point): label `qml`, never an
# offscreen gate.
find_package(Qt6 REQUIRED COMPONENTS QuickTest Qml Quick)
add_executable(colosseum_qml_tests qml/quicktest_main.cpp)
target_link_libraries(colosseum_qml_tests
    PRIVATE Qt6::QuickTest Qt6::Qml Qt6::Quick Qt6::Gui)
set_target_properties(colosseum_qml_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET colosseum_qml_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QuickTest>" "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qml
         COMMAND colosseum_qml_tests -input "${CMAKE_CURRENT_SOURCE_DIR}/qml")
set_tests_properties(colosseum.qml PROPERTIES
    LABELS "qml;windows"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 300)

# Deterministic Reading Room contract: qml.exe injects fake service, progress,
# and download seams, so this gate is safe under the unit label and does not
# require the live app or network.
add_test(NAME colosseum.manga_reading_room
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_manga_reading_room.ps1")
set_tests_properties(colosseum.manga_reading_room PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Installed chronicle (installed-release chronicle plan, Slice 2, 2026-08-09)
# InstalledChronicle loads + verifies the bundled signed manifest that seeds the
# Update gallery with the installed release's chapters at rest. Pure QtCore +
# the existing UpdateTrust Ed25519 path; GUILESS. The test fixture under
# tests/lanista_fixtures/installed-chronicle/ is signed with the test private
# key (counterpart to kUpdateTestPublicKey, selected under COLOSSEUM_UPDATE_TESTING).
# Same deploy pattern as tst_window_state_policy: land beside the app's Qt DLLs
# and stage Qt6Test.dll, or the exe dies 0xc0000135 before main().
add_executable(tst_installed_chronicle
    auto/update/tst_installed_chronicle.cpp
    "${COLOSSEUM_NATIVE_DIR}/update/InstalledChronicle.cpp"
    "${COLOSSEUM_NATIVE_DIR}/update/InstalledChronicle.h"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateManifest.cpp"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateManifest.h"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateVersion.cpp"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateVersion.h"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateTrust.cpp"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateTrust.h"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdatePublicKey.h"
    "${COLOSSEUM_NATIVE_DIR}/update/UpdateTestPublicKey.h")
target_include_directories(tst_installed_chronicle PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_installed_chronicle PRIVATE
    COLOSSEUM_UPDATE_TESTING=1
    INSTALLED_CHRONICLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lanista_fixtures/installed-chronicle")
target_link_libraries(tst_installed_chronicle
    PRIVATE Qt6::Core Qt6::Test colosseum_update_crypto)
set_target_properties(tst_installed_chronicle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_installed_chronicle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.installed_chronicle
         COMMAND tst_installed_chronicle)
set_tests_properties(colosseum.qttest.installed_chronicle PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Store isolation (critical fix, 2026-08-14) ────────────────────────────────
# Proves ProgressStore/CollectionStore no longer leak a COLOSSEUM_APPDATA_TAG-tagged
# (isolated Lanista test) session's writes into the real Windows registry key
# (HKCU\Software\Brotherhood\Colosseum) every real user's Continue/Collection history
# lives in. Header-only stores (no separate .cpp), so this target just includes the
# headers directly — CMAKE_AUTOMOC handles their in-header Q_OBJECT classes the same
# way it already does for tst_vault_stores' VaultConfig.h/VaultIdentity.h. Pure
# Qt6::Core, GUILESS; QStandardPaths::setTestModeEnabled(true) sandboxes every tagged
# file this test creates — it never opens the real registry key (see the file's own
# top-of-file safety-contract comment for why that's true even for the untagged case).
add_executable(tst_store_isolation
    auto/store_isolation/tst_store_isolation.cpp
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h")
target_include_directories(tst_store_isolation PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_store_isolation PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_store_isolation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_store_isolation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.store_isolation
         COMMAND tst_store_isolation)
set_tests_properties(colosseum.qttest.store_isolation PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Red canary — the permanent negative control ──────────────────────────────
# Proves ctest actually surfaces a failing command as red (WILL_FAIL inverts
# it, so the suite stays green while the detection path is exercised on every
# run). If this test ever FAILS, ctest's failure detection itself is broken.
add_test(NAME colosseum.selftest.red_canary
         COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(colosseum.selftest.red_canary PROPERTIES
    WILL_FAIL TRUE
    LABELS "selftest")
