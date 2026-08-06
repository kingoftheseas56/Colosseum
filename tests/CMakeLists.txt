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

# ── Red canary — the permanent negative control ──────────────────────────────
# Proves ctest actually surfaces a failing command as red (WILL_FAIL inverts
# it, so the suite stays green while the detection path is exercised on every
# run). If this test ever FAILS, ctest's failure detection itself is broken.
add_test(NAME colosseum.selftest.red_canary
         COMMAND ${CMAKE_COMMAND} -E false)
set_tests_properties(colosseum.selftest.red_canary PROPERTIES
    WILL_FAIL TRUE
    LABELS "selftest")
