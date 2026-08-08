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
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultStoreIo.h")
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
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultKit.h")
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

# ── Vault enricher (execution plan Slice 5, 2026-08-08) ──────────────────────
# VaultEnricher — thin fact-filler reusing CbzArchive (comic pages + cover entry
# for image://comiccover/) and ffprobe (duration cache); writes facts back to the
# index. Compiles CbzArchive + miniz (its ZIP backend). Qt6::Sql for the index;
# real CBZ via TANKOBAN_FIXTURES_DIR, corrupt CBZ via VAULT_FIXTURES_DIR. GUILESS
# (cover DECODE is the existing image://comiccover/ provider's job, not tested here).
add_executable(tst_vault_enricher
    auto/vault/tst_vault_enricher.cpp
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultEnricher.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultEnricher.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/VaultIndex.h"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.cpp"
    "${COLOSSEUM_NATIVE_DIR}/engine/CbzArchive.h"
    "${COLOSSEUM_NATIVE_DIR}/third_party/miniz/miniz.c")
target_include_directories(tst_vault_enricher PRIVATE "${COLOSSEUM_NATIVE_DIR}")
target_compile_definitions(tst_vault_enricher PRIVATE
    TANKOBAN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tankoban"
    VAULT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vault")
target_link_libraries(tst_vault_enricher PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
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

# ── Red canary — the permanent negative control ──────────────────────────────
# Proves ctest actually surfaces a failing command as red (WILL_FAIL inverts
# it, so the suite stays green while the detection path is exercised on every
# run). If this test ever FAILS, ctest's failure detection itself is broken.
add_test(NAME colosseum.selftest.red_canary
         COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(colosseum.selftest.red_canary PROPERTIES
    WILL_FAIL TRUE
    LABELS "selftest")
