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
colosseum_register_harness(background_work_coordinator_harness unit)
colosseum_register_harness(background_activity_registry_harness unit)
colosseum_register_harness(background_work_composition_harness unit;composition)
colosseum_register_harness(progress_store_harness        unit)
colosseum_register_harness(collection_store_harness      unit)
colosseum_register_harness(cbz_archive_harness           unit)
colosseum_register_harness(poster_scoreboard_harness     unit)
colosseum_register_harness(loopback_pin_proxy_harness    unit;network)
colosseum_register_harness(pin_proxy_factory_harness     unit;network)
colosseum_register_harness(startup_layout_harness        unit;bootstrap)
colosseum_register_harness(app_data_migration_harness    unit;bootstrap)
colosseum_register_harness(ipv4_pin_store_harness        unit;bootstrap;network)
colosseum_register_harness(manga_image_host_resolver_harness unit;network)
colosseum_register_harness(comicreader_cache_harness     unit)
colosseum_register_harness(biblio_catalog_logic_harness  unit)
# Multi-volume pack demux (Slice 1): parser table + Entry round-trip. No net,
# QTemporaryDir/isolated-AppData — deterministic, sentinel+exit-code contract.
colosseum_register_harness(comic_downloader_pack_demux_harness unit)
colosseum_register_harness(update_version_harness unit)
colosseum_register_harness(update_manifest_trust_harness unit)
colosseum_register_harness(update_release_client_harness unit)
colosseum_register_harness(catalog_vault_client_harness unit)
# Arc 21 comics catalogue intelligence (curated_source/coverage layer, 2026-08-23):
# fixture-only SQLite, no net, stale-db degradation covered — house sentinel+exit contract.
colosseum_register_harness(comics_catalog_arc21_harness unit)
colosseum_register_harness(update_download_harness unit)
colosseum_register_harness(update_service_harness unit)
colosseum_register_harness(update_install_bridge_harness unit;windows)

add_test(NAME colosseum.catalogue_cancellation_contract
         COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                 -File "${CMAKE_CURRENT_SOURCE_DIR}/test_catalogue_cancellation_contract.ps1")
set_tests_properties(colosseum.catalogue_cancellation_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Qt Test targets (slice 3+) ───────────────────────────────────────────────
# Test-only Qt components: discovered HERE, under BUILD_TESTING, never linked
# into the shipping app (arc non-goal). Targets defined here are real Qt Tests:
# per-case reporting, data rows, selectable by function/tag.
find_package(Qt6 REQUIRED COMPONENTS Test Network WebSockets)
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

# Lanista presentation capture contract: pure command construction for the
# runner-local FFmpeg recorder used by deterministic Reddit/demo journeys.
add_executable(tst_capture_runner auto/lanista/tst_capture_runner.cpp)
target_include_directories(tst_capture_runner PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_capture_runner PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(tst_capture_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_capture_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.capture_runner COMMAND tst_capture_runner)
set_tests_properties(colosseum.qttest.capture_runner PROPERTIES
    LABELS "unit;qttest;lanista"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Startup qualification probe helpers: pure formatting/severity/context logic. The executable
# stays deterministic and does not boot the application or open a real window.
add_executable(tst_gui_responsiveness_probe auto/lanista/tst_gui_responsiveness_probe.cpp)
target_include_directories(tst_gui_responsiveness_probe PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_gui_responsiveness_probe PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(tst_gui_responsiveness_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_gui_responsiveness_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.gui_responsiveness_probe
         COMMAND tst_gui_responsiveness_probe)
set_tests_properties(colosseum.qttest.gui_responsiveness_probe PROPERTIES
    LABELS "unit;qttest;lanista;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)

# Lanista timing contract: schema/order checks for opt-in per-step and lifecycle timing.
# Pure QtCore/QtTest; it does not boot an app or require a bridge.
add_executable(tst_lanista_timing auto/lanista/tst_lanista_timing.cpp)
target_include_directories(tst_lanista_timing PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_lanista_timing PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_lanista_timing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_lanista_timing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.lanista_timing
         COMMAND tst_lanista_timing)
set_tests_properties(colosseum.qttest.lanista_timing PROPERTIES
    LABELS "unit;qttest;lanista"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Startup deferral contract: verifies the expensive but idempotent Collection/Biblio scans
# begin only after the native first-frame boundary and remain reachable from the timer.
add_test(NAME colosseum.startup_deferral_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_startup_deferral_contract.ps1")
set_tests_properties(colosseum.startup_deferral_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Startup responsiveness probe contract: keeps the native first-frame/Biblio ordering and
# deferred Home Continue rail wiring mechanically covered alongside the existing deferral gate.
add_test(NAME colosseum.startup_responsiveness_probe
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_startup_responsiveness_probe.ps1")
set_tests_properties(colosseum.startup_responsiveness_probe PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# World-warmer containment contract: automatic warming is opt-in, while explicit world
# navigation remains available for normal operation.
add_test(NAME colosseum.world_warmer_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_world_warmer_contract.ps1")
set_tests_properties(colosseum.world_warmer_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Home hidden-work contract: the retained Home surface must stop its carousel timer while a
# world is visible, without changing the explicit hide/show navigation boundary.
add_test(NAME colosseum.home_hidden_work_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_home_hidden_work_contract.ps1")
set_tests_properties(colosseum.home_hidden_work_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Retained world lifecycle contract: hidden pages preserve state but stand down timers,
# synchronous catalogue warmers, and Discover paging until activation.
add_test(NAME colosseum.world_lifecycle_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_world_lifecycle_contract.ps1")
set_tests_properties(colosseum.world_lifecycle_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Search backpressure contract: surface exits and superseded queries abort transport while
# generation guards reject any callback that races with cancellation.
add_test(NAME colosseum.search_cancellation_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_search_cancellation_contract.ps1")
set_tests_properties(colosseum.search_cancellation_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Tankoban volume-index recovery must leave construction quickly and publish a
# single readiness boundary before any index-backed operation is allowed.
add_test(NAME colosseum.tankoban_startup_recovery_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_tankoban_startup_recovery_contract.ps1")
set_tests_properties(colosseum.tankoban_startup_recovery_contract PROPERTIES
    LABELS "unit;responsiveness"
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

# ── Tankoban catalogue (catalogue-independence Slice 1, 2026-08-20) ──────────
# TankobanCatalog — the baked, provider-free volume-count/cover store: series
# count/count_basis round-trip, "1".."N" number synthesis from a known count,
# numeric-aware overlay ordering (2 before 10), and honest emptiness for a
# missing db or unknown malId. Qt6::Sql; same deploy-trap resolution as
# tst_vault_index (exe lands in build-msvc/ beside the app-deployed plugin).
# GUILESS; QTemporaryDir DB per run, no committed .sqlite.
add_executable(tst_tankoban_catalog
    auto/tankoban/tst_tankoban_catalog.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/TankobanCatalog.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/TankobanCatalog.h")
target_include_directories(tst_tankoban_catalog PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_tankoban_catalog PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_tankoban_catalog PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_tankoban_catalog POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.tankoban_catalog
         COMMAND tst_tankoban_catalog)
set_tests_properties(colosseum.qttest.tankoban_catalog PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Tankoban chapter migration (catalogue-independence Slice 5, 2026-08-20) ──
# TankobanChapterMigration — the one-time WC-era chapter purge: chapter tree
# deleted, manga-volumes/ intact, kind:"manga" progress purged, kind:"tankoban"/
# "comic" intact, idempotent second run, marker written. QTemporaryDir fixture
# (both the chapter tree and a ProgressStore backed by a temp ini), same
# GUILESS shape as tst_tankoban_catalog. ProgressStore.h is header-only (no
# matching .cpp on the app target), so only the migration's own .cpp joins it.
add_executable(tst_tankoban_chapter_migration
    auto/tankoban/tst_tankoban_chapter_migration.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/TankobanChapterMigration.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/TankobanChapterMigration.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h")
target_include_directories(tst_tankoban_chapter_migration PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_tankoban_chapter_migration PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_tankoban_chapter_migration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_tankoban_chapter_migration POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.tankoban_chapter_migration
         COMMAND tst_tankoban_chapter_migration)
set_tests_properties(colosseum.qttest.tankoban_chapter_migration PROPERTIES
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
target_link_libraries(vault_launch_router_harness PRIVATE Qt6::Core Qt6::Concurrent ${Libmpv_LIBRARIES})
set_target_properties(vault_launch_router_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.vault_launch_router_harness
         COMMAND vault_launch_router_harness)
set_tests_properties(colosseum.vault_launch_router_harness PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

add_test(NAME colosseum.local_launch_async_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_local_launch_async_contract.ps1")
set_tests_properties(colosseum.local_launch_async_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)

add_test(NAME colosseum.manga_downloader_responsiveness_contract
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_manga_downloader_responsiveness_contract.ps1")
set_tests_properties(colosseum.manga_downloader_responsiveness_contract PROPERTIES
    LABELS "unit;responsiveness"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30)

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

# Function 0006: imported Tankoban Progress must reach the real active ComicReaderShell.
add_test(NAME colosseum.comicreader_sync_resume
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_comicreader_sync_resume.ps1")
set_tests_properties(colosseum.comicreader_sync_resume PROPERTIES
    LABELS "unit;comicreader"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

add_test(NAME colosseum.comicreader_sync_resume_shell
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_comicreader_sync_resume_shell.ps1")
set_tests_properties(colosseum.comicreader_sync_resume_shell PROPERTIES
    LABELS "unit;comicreader"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

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

# MangaSeries.qml's catalogue-fed identity + masthead (catalogue-independence Slice 2,
# amended 2026-08-20) — fake MalCatalog/TankobanCatalog/TankobanVolumes seams, same
# qml.exe offscreen shape as colosseum.manga_reading_room above.
add_test(NAME colosseum.manga_series_catalogue
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_manga_series_catalogue.ps1")
set_tests_properties(colosseum.manga_series_catalogue PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Tankoban series volume-flow (arc-08 v2.3 adoption, 2026-08-21): the horizontal
# Pages/Flow continuum that replaced the vertical volume grid in MangaTankobanLibrary.qml.
# Same qml.exe offscreen shape as colosseum.manga_reading_room above; fake service/progress/
# downloader seams only, no live app or network.
add_test(NAME colosseum.manga_volume_flow
         COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_manga_volume_flow.ps1")
set_tests_properties(colosseum.manga_volume_flow PROPERTIES
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

# ── First-light harness: production account chain vs the live local service.
#    Not a ctest — built and run by hand for the world's first account.
add_executable(account_first_light
    auto/account/first_light.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountBootstrapStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountDeviceIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountHttpTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountRecoveryKeyPresenter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountRuntime.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountServiceEndpoint.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/CollectionSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/CoreStateSyncProjection.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistorySyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.cpp"
    # Slice D4: ProfileStoreRuntime/FirstAccountProfileCoordinator now own an
    # ActivityStore (CPP-PORT-CONTRACT §2/§17) — needs Qt6::Sql below too.
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProgressSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SharedPcProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountCredentialStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountSensitiveClipboard.cpp"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/SearchHistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/AudioPairingStore.h")
target_include_directories(account_first_light PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(account_first_light PRIVATE
    Qt6::Core Qt6::Network Qt6::Qml Qt6::Sql)
if(WIN32)
    target_link_libraries(account_first_light PRIVATE Advapi32 User32)
endif()
set_target_properties(account_first_light PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")

# ── Account + cloud sync foundation (Preflight Bundle 8C cumulative, adopted
#    2026-08-16 by Agent 0 (ZCode); hand-applied from tests-CMakeLists.bundle8c-cumulative.patch) ──

# ── Account + cloud sync foundation (cumulative Bundle 8C: Bundle 1) ─────
# Test-only deterministic account transport plus profile-path/adoption substrate.
add_executable(tst_account_core
    auto/account/tst_account_core.cpp
    support/account/AccountFixtureTransport.cpp
    support/account/AccountFixtureTransport.h
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/SearchHistoryStore.h")
target_include_directories(tst_account_core PRIVATE
    "${COLOSSEUM_NATIVE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/account")
target_link_libraries(tst_account_core PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_account_core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.account_core
         COMMAND tst_account_core)
set_tests_properties(colosseum.qttest.account_core PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

add_executable(tst_sync_core
    auto/sync/tst_sync_core.cpp
    support/account/AccountFixtureTransport.cpp
    support/account/AccountFixtureTransport.h
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h")
target_include_directories(tst_sync_core PRIVATE
    "${COLOSSEUM_NATIVE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/account")
target_link_libraries(tst_sync_core PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_sync_core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_sync_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.sync_core
         COMMAND tst_sync_core)
set_tests_properties(colosseum.qttest.sync_core PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Desktop account identity/session (cumulative Bundle 8C: Bundle 2B) ───
add_executable(tst_account_identity
    auto/account_identity/tst_account_identity.cpp
    support/account/AccountFixtureTransport.cpp
    support/account/AccountFixtureTransport.h
    support/account/MemoryAccountCredentialStore.h
    support/account/MemoryAccountOneTimeSecretSink.h
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountBootstrapStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountBootstrapStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountCredentialStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountCredentialStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountCredentialStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountDeviceIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountDeviceIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountHttpTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountHttpTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountOneTimeSecretSink.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountController.h")
target_include_directories(tst_account_identity PRIVATE
    "${COLOSSEUM_NATIVE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/account")
target_link_libraries(tst_account_identity PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test)
if(WIN32)
    target_link_libraries(tst_account_identity PRIVATE Advapi32)
endif()
set_target_properties(tst_account_identity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_identity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.account_identity
         COMMAND tst_account_identity)
set_tests_properties(colosseum.qttest.account_identity PROPERTIES
    LABELS "unit;qttest;windows"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Slice D1: native ActivityProjector, JS-oracle golden parity ─────────
# Pure-logic port of the Preflight-Architect activity-engine reference oracle
# (activity-reference.js) — no QObject, no filesystem/network/machine-clock
# dependency, so this target only needs Qt6::Core + Qt6::Test. Fixtures are
# compiled in as a source-tree path (ACTIVITY_FIXTURES_DIR), same pattern as
# VAULT_FIXTURES_DIR/TANKOBAN_FIXTURES_DIR above.
add_executable(tst_activity_projector
    auto/activity/tst_activity_projector.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h")
target_include_directories(tst_activity_projector PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_activity_projector PRIVATE
    ACTIVITY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/auto/activity/fixtures")
target_link_libraries(tst_activity_projector PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_activity_projector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_activity_projector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.activity_projector
         COMMAND tst_activity_projector)
set_tests_properties(colosseum.qttest.activity_projector PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Slice D2: profile-owned SQLite ActivityStore ─────────────────────────
# The durable ledger writer sitting in front of the golden-proven
# ActivityProjector above: eventId idempotency/conflict rejection, pre-
# mutation validation (reusing ActivityProjector::validateEvent — no second
# validator), revision()/changed() semantics, a projectMonth() smoke check
# against the same august-mixed fixture pair, earliestActivityMonth(),
# hasFixedCoverage(), clearAll(), close+reopen persistence/determinism, and
# unhealthy-database fail-closed behavior. Needs Qt6::Sql (SQLite storage) in
# addition to tst_activity_projector's Qt6::Core + Qt6::Test; reuses the same
# ACTIVITY_FIXTURES_DIR fixtures. Qt6Sql.dll is staged explicitly (same
# pattern as tst_vault_downloads_root/tst_vault_index) rather than relying on
# a prior `colosseum` app build having already deployed it.
add_executable(tst_activity_store
    auto/activity/tst_activity_store.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h")
target_include_directories(tst_activity_store PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_activity_store PRIVATE
    ACTIVITY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/auto/activity/fixtures")
target_link_libraries(tst_activity_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_activity_store PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_activity_store POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_activity_store POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.activity_store
         COMMAND tst_activity_store)
set_tests_properties(colosseum.qttest.activity_store PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Slice D3: transient ActivityPlaybackTracker ──────────────────────────
# Reusable playback sampler (Player 1, Player 2, audiobook) sitting in front
# of the same ActivityStore/ActivityProjector pair: the 10-second activation
# gate, <=30s-block coalescing, discontinuity/rate/UTC-offset-change baseline
# resets, and guarded-90% completion (movie/episode only). Deterministic via
# injected monotonic/wall/UTC-offset clocks — no system clock dependency.
# Persists through a real in-memory ActivityStore so every emitted fact is
# proven to pass the same validation/idempotency/projection path a live
# player would exercise. Needs Qt6::Sql for that reason, same as
# tst_activity_store; reuses the same ACTIVITY_FIXTURES_DIR definition even
# though this target does not load fixtures directly (kept for symmetry/
# possible future differential use, harmless if unused).
add_executable(tst_activity_playback_tracker
    auto/activity/tst_activity_playback_tracker.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityPlaybackTracker.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityPlaybackTracker.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h")
target_include_directories(tst_activity_playback_tracker PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_activity_playback_tracker PRIVATE
    ACTIVITY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/auto/activity/fixtures")
target_link_libraries(tst_activity_playback_tracker PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
set_target_properties(tst_activity_playback_tracker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_activity_playback_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_activity_playback_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.activity_playback_tracker
         COMMAND tst_activity_playback_tracker)
set_tests_properties(colosseum.qttest.activity_playback_tracker PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Account onboarding/recovery composition (Bundle 4B, Slices 9-10) ────
# Deterministic controller/presenter coverage. Clipboard writes use an in-memory
# test double; the production Windows implementation is compiled for link/API
# coverage but no system clipboard mutation is performed by these cases.
add_executable(tst_account_onboarding
    auto/account_onboarding/tst_account_onboarding.cpp
    support/account/AccountFixtureTransport.cpp
    support/account/AccountFixtureTransport.h
    support/account/MemoryAccountCredentialStore.h
    support/account/MemoryAccountOneTimeSecretSink.h
    support/account/MemoryAccountSensitiveClipboard.h
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountBootstrapStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountBootstrapStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountCredentialStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountDeviceIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountDeviceIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountOneTimeSecretSink.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountController.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountSensitiveClipboard.h"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountSensitiveClipboard.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/WindowsAccountSensitiveClipboard.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountRecoveryKeyPresenter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountRecoveryKeyPresenter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountServiceEndpoint.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountServiceEndpoint.h")
target_include_directories(tst_account_onboarding PRIVATE
    "${COLOSSEUM_NATIVE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/account")
target_link_libraries(tst_account_onboarding PRIVATE
    Qt6::Core
    Qt6::Test)
if(WIN32)
    target_link_libraries(tst_account_onboarding PRIVATE User32)
endif()
set_target_properties(tst_account_onboarding PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_onboarding POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.account_onboarding
         COMMAND tst_account_onboarding)
set_tests_properties(colosseum.qttest.account_onboarding PROPERTIES
    LABELS "unit;qttest;windows"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── First-account local-state adoption (Bundle 4B, Slice 11A) ─────────
add_executable(tst_account_adoption
    auto/account_adoption/tst_account_adoption.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.h"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.h"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.h"
    # Slice D4: ProfileStoreRuntime/FirstAccountProfileCoordinator now own an
    # ActivityStore (CPP-PORT-CONTRACT §2/§17) — needs Qt6::Sql below too.
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/SearchHistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/AudioPairingStore.h")
target_include_directories(tst_account_adoption PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_account_adoption PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Sql
    Qt6::Test)
set_target_properties(tst_account_adoption PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_adoption POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_adoption POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.account_adoption
         COMMAND tst_account_adoption)
set_tests_properties(colosseum.qttest.account_adoption PROPERTIES
    LABELS "unit;qttest;profile"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Shared-PC profile sealing / isolation (Bundle 4B, Slice 11B) ─────
add_executable(tst_account_shared_pc
    auto/account_shared_pc/tst_account_shared_pc.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.h"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.h"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SharedPcProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SharedPcProfileCoordinator.h"
    # Slice D4: ProfileStoreRuntime/FirstAccountProfileCoordinator now own an
    # ActivityStore (CPP-PORT-CONTRACT §2/§17) — needs Qt6::Sql below too.
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/SearchHistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/AudioPairingStore.h")
target_include_directories(tst_account_shared_pc PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_account_shared_pc PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Sql
    Qt6::Test)
set_target_properties(tst_account_shared_pc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_shared_pc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_account_shared_pc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.account_shared_pc
         COMMAND tst_account_shared_pc)
set_tests_properties(colosseum.qttest.account_shared_pc PROPERTIES
    LABELS "unit;qttest;profile;shared-pc"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Profile activity isolation (Slice D4, activity-engine port) ─────────────
# Proves ActivityStore joins ProfileStoreRuntime::StoreSet exactly like the
# other personal stores (CPP-PORT-CONTRACT §2/§17): a profile switch
# clears/rebinds the `ProfileActivity` QML context property in the same
# transaction as Progress/ProfileHistory, the previous profile's
# ActivityStore is destroyed (QPointer-proved, same pattern
# tst_account_shared_pc.cpp already uses for SearchHistoryStore), and no
# activity fact ever leaks across a profile switch. Same dependency graph as
# tst_account_shared_pc (SharedPcProfileCoordinator drives the switches) plus
# ActivityStore/ActivityProjector — a separate target rather than folding
# into tst_store_isolation.cpp, which guards a narrower, already-documented
# registry-vs-tagged-file contract with a much lighter (header-only) link set.
add_executable(tst_profile_activity_isolation
    auto/store_isolation/tst_profile_activity_isolation.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileContext.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileAdoption.h"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/LegacyPersonalStateStorage.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfileStoreRuntime.h"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/FirstAccountProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SharedPcProfileCoordinator.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SharedPcProfileCoordinator.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ActivityProjector.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/SearchHistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/AudioPairingStore.h")
target_include_directories(tst_profile_activity_isolation PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_profile_activity_isolation PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Sql
    Qt6::Test)
set_target_properties(tst_profile_activity_isolation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_profile_activity_isolation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_profile_activity_isolation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.profile_activity_isolation
         COMMAND tst_profile_activity_isolation)
set_tests_properties(colosseum.qttest.profile_activity_isolation PROPERTIES
    LABELS "unit;qttest;profile;activity"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Sync ownership inventory + exclusion firewall (Bundle 8C, Slice 12A) ──
add_executable(tst_sync_inventory
    auto/sync_inventory/tst_sync_inventory.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h")
target_include_directories(tst_sync_inventory PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_sync_inventory PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_sync_inventory PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_sync_inventory POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.sync_inventory
         COMMAND tst_sync_inventory)
set_tests_properties(colosseum.qttest.sync_inventory PROPERTIES
    LABELS "unit;qttest;sync-contract"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Sync adapter contract + typed registry (Bundle 8C, Slice 12B) ────
add_executable(tst_sync_adapter_registry
    auto/sync_adapter_registry/tst_sync_adapter_registry.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h")
target_include_directories(tst_sync_adapter_registry PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_sync_adapter_registry PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_sync_adapter_registry PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_sync_adapter_registry POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.sync_adapter_registry
         COMMAND tst_sync_adapter_registry)
set_tests_properties(colosseum.qttest.sync_adapter_registry PROPERTIES
    LABELS "unit;qttest;sync-contract"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Sync service wire protocol/client contract (Bundle 8C) ──────────
add_executable(tst_sync_protocol
    auto/sync_protocol/tst_sync_protocol.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h")
target_include_directories(tst_sync_protocol PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_sync_protocol PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_sync_protocol PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_sync_protocol POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.sync_protocol
         COMMAND tst_sync_protocol)
set_tests_properties(colosseum.qttest.sync_protocol PROPERTIES
    LABELS "unit;qttest;sync-contract;wire"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Bundle 8C AccountController sync lifecycle dependencies ──────────
target_sources(tst_account_onboarding PRIVATE
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h")

# ── Bundle 8C: AccountController links the sync engine (same dependency the
#    cumulative patch adds to tst_account_onboarding; the identity target
#    compiles AccountController.cpp too and needs the identical source set). ──
target_sources(tst_account_identity PRIVATE
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h")

# ── Generic offline SyncEngine / convergence (Bundle 8C, Slice 13) ──
add_executable(tst_sync_engine
    auto/sync_engine/tst_sync_engine.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h")
target_include_directories(tst_sync_engine PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_sync_engine PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_sync_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_sync_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.sync_engine
         COMMAND tst_sync_engine)
set_tests_properties(colosseum.qttest.sync_engine PROPERTIES
    LABELS "unit;qttest;sync;convergence"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 180)


# ── First production domain adapters: Collection + Continue/Progress
#    (Bundle 8C, Slice 14A) ───────────────────────────────────────────
add_executable(tst_core_sync_adapters
    auto/core_sync_adapters/tst_core_sync_adapters.cpp
    "${COLOSSEUM_NATIVE_DIR}/CollectionStore.h"
    "${COLOSSEUM_NATIVE_DIR}/ProgressStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h"
    "${COLOSSEUM_NATIVE_DIR}/account/CoreStateSyncProjection.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/CoreStateSyncProjection.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/CollectionSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/CollectionSyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProgressSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProgressSyncAdapter.h")
target_include_directories(tst_core_sync_adapters PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_core_sync_adapters PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_core_sync_adapters PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_core_sync_adapters POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.core_sync_adapters
         COMMAND tst_core_sync_adapters)
set_tests_properties(colosseum.qttest.core_sync_adapters PROPERTIES
    LABELS "unit;qttest;sync;domain;collection;progress"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 240)


# ── Dedicated full History authority + sync (Bundle 8C, Slice 14B) ──
add_executable(tst_history_sync
    auto/history_sync/tst_history_sync.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h"
    "${COLOSSEUM_NATIVE_DIR}/account/CoreStateSyncProjection.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/CoreStateSyncProjection.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistoryStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/HistorySyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/HistorySyncAdapter.h")
target_include_directories(tst_history_sync PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_history_sync PRIVATE
    Qt6::Core
    Qt6::Test)
set_target_properties(tst_history_sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_history_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.history_sync
         COMMAND tst_history_sync)
set_tests_properties(colosseum.qttest.history_sync PROPERTIES
    LABELS "unit;qttest;sync;domain;history"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 240)


# ── Profile-owned explicit-content preference sync (Bundle 8C, Slice 14C) ──
add_executable(tst_profile_preferences_sync
    auto/profile_preferences_sync/tst_profile_preferences_sync.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/AccountTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/AccountClient.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePaths.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/ProfilePreferencesSyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncHybridClock.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncStateStore.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncEngine.h")
target_include_directories(tst_profile_preferences_sync PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_profile_preferences_sync PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
    Qt6::Test)
set_target_properties(tst_profile_preferences_sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_profile_preferences_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.profile_preferences_sync
         COMMAND tst_profile_preferences_sync)
set_tests_properties(colosseum.qttest.profile_preferences_sync PROPERTIES
    LABELS "unit;qttest;sync;domain;preferences"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 180)


# ── Tankoban reader-preference codec/adapter contract (Bundle 8C) ──
# Production registration remains fail-closed until the concrete
# Tankoban-only durable owner/discriminator discovery gate closes.
add_executable(tst_tankoban_preferences_sync
    auto/tankoban_preferences_sync/tst_tankoban_preferences_sync.cpp
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncAdapterRegistry.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncOwnershipInventory.h"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/SyncPayloadFirewall.h"
    "${COLOSSEUM_NATIVE_DIR}/account/TankobanReaderPreferencesCodec.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/TankobanReaderPreferencesCodec.h"
    "${COLOSSEUM_NATIVE_DIR}/account/TankobanReaderPreferencesOwner.h"
    "${COLOSSEUM_NATIVE_DIR}/account/TankobanReaderPreferencesSyncAdapter.cpp"
    "${COLOSSEUM_NATIVE_DIR}/account/TankobanReaderPreferencesSyncAdapter.h"
    "${COLOSSEUM_NATIVE_DIR}/comicreader/ComicReaderRenderProfile.cpp"
    "${COLOSSEUM_NATIVE_DIR}/comicreader/ComicReaderRenderProfile.h")
target_include_directories(tst_tankoban_preferences_sync PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_tankoban_preferences_sync PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test)
set_target_properties(tst_tankoban_preferences_sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_tankoban_preferences_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.tankoban_preferences_sync
         COMMAND tst_tankoban_preferences_sync)
set_tests_properties(colosseum.qttest.tankoban_preferences_sync PROPERTIES
    LABELS "unit;qttest;sync;domain;tankoban;preferences"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 180)

# ── Watch Party room domain (approved plan Slice 1) ──────────────────────────
# Pure Qt6::Core room rules + protocol + deterministic fake transport. No QML,
# player, account, persistence, or live-network dependency enters this target.
add_executable(tst_watchparty_room
    auto/watchparty/tst_watchparty_room.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomController.h")
target_include_directories(tst_watchparty_room PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_room PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_watchparty_room PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_room POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_room
         COMMAND tst_watchparty_room)
set_tests_properties(colosseum.qttest.watchparty_room PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Watch Party authoritative source descriptor (approved plan Slice 2) ──────
# Pure Qt6::Core classifier + structured protocol shape. It mirrors Player 1's
# torrent/direct routing without constructing QML/mpv/network/provider state.
add_executable(tst_watchparty_source
    auto/watchparty/tst_watchparty_source.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyServiceEndpoint.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyServiceEndpoint.h")
target_include_directories(tst_watchparty_source PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_source PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_watchparty_source PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_source POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_source
         COMMAND tst_watchparty_source)
set_tests_properties(colosseum.qttest.watchparty_source PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Watch Party Player 1 sync policy (approved plan Slice 3) ─────────────────
# Pure Qt6::Core controller with explicit fake player observation/clock values.
# No mpv/QML/network/account dependency enters this deterministic policy target.
add_executable(tst_watchparty_sync
    auto/watchparty/tst_watchparty_sync.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.h")
target_include_directories(tst_watchparty_sync PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_sync PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_watchparty_sync PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_sync
         COMMAND tst_watchparty_sync)
set_tests_properties(colosseum.qttest.watchparty_sync PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Watch Party production client transport/service seam (plan Slice 4) ──────
# Deterministic: WebSocket behavior is exercised through an injected fake socket.
# No live-network endpoint is contacted by this target.
add_executable(tst_watchparty_transport
    auto/watchparty/tst_watchparty_transport.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.h")
target_include_directories(tst_watchparty_transport PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_transport PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test)
set_target_properties(tst_watchparty_transport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_transport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_transport
         COMMAND tst_watchparty_transport)
set_tests_properties(colosseum.qttest.watchparty_transport PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)


# ── Watch Party account-owned identity/invite seam (approved plan Slice 5) ───
# Deterministic interface/domain test. The fake account bridge exists only in
# the test TU: production gets no fabricated account database/client and this
# target performs no live account or room-service I/O.
add_executable(tst_watchparty_identity
    auto/watchparty/tst_watchparty_identity.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomController.h")
target_include_directories(tst_watchparty_identity PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_identity PRIVATE Qt6::Core Qt6::Test)
set_target_properties(tst_watchparty_identity PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_identity POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_identity
         COMMAND tst_watchparty_identity)
set_tests_properties(colosseum.qttest.watchparty_identity PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Watch Party QML-facing lifecycle/readiness seam (approved plan Slice 6) ──
# Deterministic native contract test. It uses fake room/account transport and the
# real PlayerSync policy; no live account service, room server, QML engine, or
# mpv instance is required.
add_executable(tst_watchparty_ui
    auto/watchparty/tst_watchparty_ui.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyUiController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyUiController.h")
target_include_directories(tst_watchparty_ui PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_ui PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test)
set_target_properties(tst_watchparty_ui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_ui
         COMMAND tst_watchparty_ui)
set_tests_properties(colosseum.qttest.watchparty_ui PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# ── Watch Party assembled lifecycle/failure matrix (approved plan Slice 7) ───
# Deterministic fake transport + real repo-local room/service/UI owners. No live
# service/account/QML/mpv dependency enters this target.
add_executable(tst_watchparty_lifecycle
    auto/watchparty/tst_watchparty_lifecycle.cpp
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTypes.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyProtocol.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/QtWatchPartySocket.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WebSocketWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/FakeWatchPartyTransport.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyRoomServiceClient.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyIdentity.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyPlayerSync.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartySource.h"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyUiController.cpp"
    "${COLOSSEUM_NATIVE_DIR}/watchparty/WatchPartyUiController.h")
target_include_directories(tst_watchparty_lifecycle PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_watchparty_lifecycle PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test)
set_target_properties(tst_watchparty_lifecycle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_watchparty_lifecycle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.watchparty_lifecycle
         COMMAND tst_watchparty_lifecycle)
set_tests_properties(colosseum.qttest.watchparty_lifecycle PROPERTIES
    LABELS "unit;qttest"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# LiveStore recorder lifecycle: the fake recorder is this same test executable,
# so start/stop/error paths are deterministic and never require mpv or network.
add_executable(tst_livestore
    auto/player/tst_livestore.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/livestore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/livestore.h")
target_include_directories(tst_livestore PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(tst_livestore PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
set_target_properties(tst_livestore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_custom_command(TARGET tst_livestore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.qttest.livestore COMMAND tst_livestore)
set_tests_properties(colosseum.qttest.livestore PROPERTIES
    LABELS "unit;qttest;player"
    ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 120)

# Function 0008: video source resolution, download acquisition, and playback handoff.
# Static contract is deterministic and catches Direct-vs-torrent routing plus HTTP
# request-header provenance across download and arriving-play seams.
add_test(NAME colosseum.video_source_handoff_p0
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/test_video_source_handoff_p0.ps1")
set_tests_properties(colosseum.video_source_handoff_p0 PROPERTIES
    LABELS "unit;contract"
    TIMEOUT 30)

# Function 0008 wire proof: DownloadStore must send addon-owned HTTP headers.
add_executable(downloadstore_source_headers_harness
    downloadstore_source_headers_harness.cpp
    "${COLOSSEUM_NATIVE_DIR}/player/downloadstore.cpp"
    "${COLOSSEUM_NATIVE_DIR}/player/downloadstore.h")
target_include_directories(downloadstore_source_headers_harness PRIVATE
    "${COLOSSEUM_NATIVE_DIR}")
target_link_libraries(downloadstore_source_headers_harness PRIVATE
    Qt6::Core Qt6::Network Qt6::Gui)
set_target_properties(downloadstore_source_headers_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
add_test(NAME colosseum.downloadstore_source_headers_harness
    COMMAND downloadstore_source_headers_harness)
set_tests_properties(colosseum.downloadstore_source_headers_harness PROPERTIES
    LABELS "unit;network;contract"
    TIMEOUT 15)

# Stage the Qt runtime DLLs required by the standalone Function 0008 harness.
foreach(_qt_target Qt6::Core Qt6::Network Qt6::Gui)
    add_custom_command(TARGET downloadstore_source_headers_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${_qt_target}>"
                "${CMAKE_CURRENT_BINARY_DIR}/..")
endforeach()
