# Vinyl A2 Linux qualification — PASS

## Identity and source

- Repository: isolated local `vinyl-a2-work/source`; branch `task/vinyl-a2-linux-qualification`.
- Imported baseline: `3c421e062c6bb54c9973a63d5b23a00f63c0c9ca`.
- Exact approved patch integration: `82828ed797bf628bcfb64a3461fc1bc7914dc6a0`.
- Supplemental regression tests: `eb052bc93659aa11c2fdf7c09025f861e2e8ba97` (final source).
- All 58 baseline files, all 66 packet entries and explicit patch SHA-256 verified before editing; imported Git blobs independently verified afterward. Final manifest pins all source bytes. Six sibling directories preserved.
- Applied original patch unchanged with `git apply --check` then `git apply` from composed directory. It changes only duration serialization and two `SourceBatch` aggregate initializations. No additional Linux implementation repair required.

## Environment

Debian GNU/Linux 13 (trixie), x86_64; GCC/G++ 14.2.0; CMake 4.4.3; Ninja 1.13.2.git.kitware.jobserver-pipe-1; Qt Core/Test 6.11.1. C++17, extensions off, Debug configuration. Qt SDK itself reports GCC 11.5.0/RHEL build; our prototype was compiled with Debian GCC 14.2.0. Existing SDK/tools reused read-only; no installation needed. Cached official Qt base archive matches its stored SHA-256. Exact SDK paths, complete versions, linked libraries, tool hashes, CMake cache and compile commands are retained.

## Results

| Stage | Result | Evidence |
|---|---|---|
| Unmodified baseline configure | PASS, exit 0 | baseline-configure.log |
| Unmodified baseline build | Expected failure, exit 1 | baseline-build.log; baseline-build-all.log |
| Approved patch configure/build | PASS, exit 0 | configure.log; build.log |
| Approved patch CTest | PASS, 6/6; exit 0 | ctest.log; ctest-verbose.log |
| Approved patch direct composed | PASS, 10/0/0; exit 0 | composed-direct.log |
| Supplemental regression build and suites | PASS, 6/6; composed 15/0/0 | regression-*.log |
| Final clean rebuild | PASS, exit 0; zero warning lines | final-clean-build.log |
| Final CTest | PASS, 6/6; exit 0 | final-ctest.log; final-ctest-verbose.log |
| Final direct composed | PASS, 15 passed/0 failed/0 skipped | final-composed-direct.log |

Baseline failure reproduced both plain-int optional misuse in `ArtistReadAdapter.cpp` and deleted default construction of strongly typed `SourceBatch` in `FakeArtistSource.cpp`. A keep-going baseline build exposed both; no baseline tests were runnable.

Final suite counts are heterogeneous and should not be summed as a single assertion count:
- Composed QtTest: 15 passed (original 8 behaviors + 5 regression data rows + init/cleanup), 0 failed, 0 skipped.
- Catalogue: 78 checks, RESULT PASS.
- Artwork: 38 PASS / 0 FAIL.
- Personal state: 5 PASS / 0 FAIL.
- Media: 15 total / 0 failed.
- Bridge QtTest: 14 passed / 0 failed / 0 skipped (includes init/cleanup).

The original eight composed cases cover entry/all lanes, redirects, invalid cursor, profile isolation, late-reply generation fence, saved-state reload, playback/edition mismatch and deterministic missing-artwork fallback. Supplemental cases verify duration positive/zero/negative through actual service serialization (positive stored as `QMetaType::LongLong`; nonpositive invalid QVariant), and direct fake-source unavailable/missing-batch artist identity, status, source label, snapshot ID, empty rows and exactly one callback. Tests are a separate commit; original cases and five sibling harness files are unchanged.

## Reproduction and evidence

`commands.json` records exact commands and exit codes, with matching `.command`, `.exit`, and raw `.log` files. `final.diff` is the complete baseline-to-final source diff; `approved-integration.diff` isolates approved repair; `regression-tests.diff` isolates tests. `FINAL-SOURCE-MANIFEST.json` pins final source. `approved-executables.sha256` and `final-executables.sha256` pin both qualification stages. `../vinyl-a2-source.bundle` preserves exact three local commits; no remote required to inspect it. Working tree is clean.

Configure with CMake from the recorded PATH, pointing `CMAKE_PREFIX_PATH` at any supported Qt >=6.5 Core/Test installation. Source is `source/artist-service-composed-20261015`; build is a separate directory. `cmake --build build --parallel 2`, `ctest --test-dir build --output-on-failure`, then `build/test_artist_service_composed` reproduce the workflow.

## Scope and disposition

- Authored: integration of supplied approved repair; five focused test data rows. Original repair authored upstream.
- Reviewed: patch scope/contracts and exact-byte baseline integrity checked; approved receiving review supplied in input packet. This is not a new external code-review approval.
- Built/tested: independently on Linux as above.
- Committed: exact baseline, approved integration, separate regression commit.
- Pushed/delivery entry: NOT RUN by this worker; parent handles publication.
- Windows: NOT RUN here; supplied Windows evidence remains separate.
- Live provider/network, full application playback, production registration, QML GC crash work, production adoption/release: NOT RUN/out of scope. Fake-source harness qualification only.
