# Colosseum Test Verification — the native/QML test ledger

> **What this is.** The honest inventory of Colosseum's native and QML test estate — the
> counterpart to `colosseum-lanista-verification.md` (which owns bridge/runtime capability).
> Planning consults BOTH before naming any test: this file controls what deterministic
> proof exists **today**; naming a test that isn't in here is inventing a capability.
>
> Ground truth as of Colosseum `236021a`, from a full read-only sweep of
> `native/CMakeLists.txt` and `tests/` (2026-08-06). If code and ledger disagree, the code
> wins — fix this file in the same commit. Maintained by whoever changes a test, a runner,
> or a registration.

## Headline shape (read this first)

| Fact | Count |
|---|---|
| Compiled harness/test targets in `native/CMakeLists.txt` | **71** (+ the app + the `lanista` CLI) |
| C++ harness sources in `tests/*.cpp` | 70 (zero orphan sources — every one is a target) |
| Hand-rolled QML harnesses in `tests/*.qml` | 88 (+ 1 fixture scene) |
| Real Qt Quick Test files (`tests/qml/tst_*.qml`) | **2** |
| PowerShell runners in `tests/*.ps1` | **150** (+ 18 in the gated-off player2 lab) |
| CTest / `add_test` / `Qt6::Test` / `Qt6::QuickTest` in the active build | **ZERO** |
| C++ harnesses invoked by NO runner or script | **39 of 69** |
| Runners that are pure static source-greps (no binary run) | **88 of 150** |
| Runners pointing at files that do not exist (broken) | 2 |

**The estate's real problem in one sentence:** the tests exist and mostly isolate
correctly, but there is no registration, no selection, no machine-readable output, and no
master gate — every runner is a standalone script a human must know to run, and more than
half the compiled harnesses are run by nobody.

## Build entry

- Active build root: `native/CMakeLists.txt`. All 70 harnesses are plain
  `add_executable` + `target_link_libraries`, built on every build, run by hand or by a
  `.ps1`.
- **No `include(CTest)`, no `enable_testing()`, no `add_test()`, no `Qt6::Test`, no
  `Qt6::QuickTest` anywhere in the active build.** The in-tree reason
  (`native/CMakeLists.txt:1274`): QVERIFY-style macros don't fit the house
  failure-collecting `main()` idiom.
- **Exception — the Player 2 lab** (`native/player2/CMakeLists.txt`): the repo's only real
  CTest suite (18 `add_test` entries, `Qt6::Test` linked), but gated behind
  `COLOSSEUM_BUILD_PLAYER2=OFF` and scoped by an `enable_testing()` call in a
  SUBDIRECTORY — top-level `ctest` will not see it even when built.
- Only two POST_BUILD deploy steps exist, both on the `colosseum` app target (Qt SQL
  driver + FFmpeg DLLs). **Trap:** the five SQL harnesses find `qsqlite.dll` only because
  they land in the same `build-msvc` dir the app deployed into — run them from elsewhere
  and they fail.

## Standard commands (the fuse box — slice 2, 2026-08-06)

- **Fast native gate:** `ctest --test-dir native/build-msvc -L unit --output-on-failure`
  — runs the registered pilot set (labels below). This is the default deterministic gate.
- **Everything discovered:** `ctest --test-dir native/build-msvc -N` (29 today: 12
  `colosseum.*` registrations + 17 Player 2 lab tests the seam surfaced — see gaps).
- Registration lives in `tests/CMakeLists.txt` (registers only; defines no targets),
  entered from the tail of `native/CMakeLists.txt` under `include(CTest)` +
  `if(BUILD_TESTING)`. OFF skips registration and changes no build output.
- **Toggle nuance (verified 2026-08-06):** configuring an EXISTING build dir with
  `-DBUILD_TESTING=OFF` leaves the previous run's `CTestTestfile.cmake` manifests on
  disk (CMake doesn't write them under OFF, so it doesn't remove them either) — `ctest`
  then reads stale manifests. A fresh generate under OFF writes none. Don't trust
  `ctest -N` on a dirty toggled dir.
- **Red detection is self-proven:** `colosseum.selftest.red_canary` (WILL_FAIL) exercises
  ctest's failure path on every run.

## Legacy commands (the pre-seam estate — still authoritative for what it gates)

- No master gate exists. Each `.ps1` under `tests/` is standalone; plans and handoffs name
  the gate to run. Output is stdout sentinels (`<NAME>_OK` / `FAIL: ...`) + exit codes.
- QML harnesses run via the HARDCODED path `C:/Qt/6.11.1/msvc2022_64/bin/qml.exe`
  (49 runners; breaks in 49 places on any Qt bump), 42 with `-platform offscreen`.
- The two real Quick Test files run via the Qt-install `qmltestrunner.exe`
  (`test_comicreader_chrome.ps1`, `test_search_history_p0.ps1`).
- Lanista's gate: `tests/test_lanista.ps1` (greps + harness selfcheck + two scenarios on
  the `ColosseumLanistaTest` pipe, readiness-polled, never the daily pipe).

## House assertion idioms (no framework)

- **require idiom:** `require()` prints `FAIL: <msg>`, `exit(1)`; one `*_OK` on success.
  Chosen over `Q_ASSERT` because Q_ASSERT compiles out under NDEBUG.
- **CHECK-collecting idiom** (comicreader family): collect every failure, print each, emit
  `<NAME>_OK` iff zero — the pattern Qt Test migration must preserve (one failure must not
  hide the rest; today three harnesses still `qFatal` and DO hide the rest:
  `download_file_ops`, `window_shell_gui`, `window_state_policy`).
- **QML idiom:** hand-rolled checks + `Timer` + `Qt.exit(0|1)` + stdout sentinel. None of
  the 88 imports QtTest (one exception below).

## Registered CTest entries (slice 2 — existing harnesses, unconverted)

| CTest name | Labels | Notes |
|---|---|---|
| `colosseum.window_state_policy_harness` | unit, windows | qFatal idiom — first failure hides the rest (slice-3 conversion pilot) |
| `colosseum.search_history_store_harness` | unit | |
| `colosseum.progress_store_harness` | unit | |
| `colosseum.collection_store_harness` | unit | |
| `colosseum.cbz_archive_harness` | unit | |
| `colosseum.poster_scoreboard_harness` | unit | |
| `colosseum.comicreader_cache_harness` | unit | |
| `colosseum.biblio_catalog_logic_harness` | unit | fixture dir baked at compile time |
| `colosseum.update_version_harness` | unit | strict three-component version parsing, canonical release-tag/display formatting, and comparison ordering; no network or filesystem writes |
| `colosseum.update_manifest_trust_harness` | unit | RFC 8032 Ed25519 verification (valid, mutated, short-key, and short-signature cases) plus strict signed-manifest schema rejection; production public key only, no private key material in the repository |
| `colosseum.update_release_client_harness` | unit | loopback GitHub Releases API fixture: stable-release filtering, ETag/304, exact asset/digest matching, signature-before-parse, bounded metadata, redirect/error/timeout handling, and cancellation/destruction safety |
| `colosseum.update_download_harness` | unit | 2 MiB loopback installer stream: bounded cache paths/metadata, cancel-and-resume Range/If-Range, ignored-range and changed-ETag restarts, truncation/length/hash rejection, space/path preflight, atomic promotion, and root-scoped superseded cleanup |
| `colosseum.update_service_harness` | unit | injected-clock/release/downloader/launcher lifecycle: six-hour policy and manual bypass, chronicle-preserving failures, unseen/seen state, pause/resume/verify/ready/install transitions, failed-target suppression, minimum-updater manual path, signed offline restart, verified-artwork fallback, and the two Lanista cache seeds with mutated-signature rejection |
| `colosseum.update_install_bridge_harness` | unit, windows | installed-layout eligibility (source/dev/registry mismatch suppression), verified-cache installer launch arguments, detached restart contract, success/rollback parsing, and exact sibling backup cleanup with unsafe-path refusal |
| `colosseum.comic_downloader_pack_demux_harness` | unit | pack-demux Slice 1: volume label parser table (the 12 real Chew filenames) + index Entry round-trip of `packRole`/`packOrder`; legacy rows load unchanged. **Slice 2 added:** the demux happy-path scenario — a 3-volume pack (2 CBZ + 1 CBR nested in a top-folder ZIP) ingests into 3 readable child volumes sharing seriesId, the parent retires via `removed()` (no `failed()`), the pack archive + extractTmp are reclaimed after all children index, the manifest clears. **Slice 3 added:** crash-resume (hand-authored active manifest + preserved pack + subset of children indexed → construct re-enqueues the missing children, completes, reclaims), staged-reuse offline completion (a pack at the canonical `dl_<hash>.archive` path completes via `downloadIssue()` with an unreachable postUrl — no network touch), cancel-mid-pack (cancel before the deferred resume fires → no children enqueue, pack file kept on disk, sticky inactive manifest). **Slice 4 added:** the ordered-volumes read API `packVolumes(seriesId)` — hand-authored index of 2 mains (v1, v2), 1 extra (v1-Bonus, em-dash label), and 1 ordinary issue sharing the seriesId → `packVolumes()` returns `{mains:[v1,v2], extras:[v1-Bonus]}` sorted by packOrder ASCENDING (v1 first, natural reading order), the ordinary issue excluded from both lists, rows shape-identical to `downloadedIssues()` (shared delegate contract for Slice 5), and a no-pack-rows seriesId returns two empty lists. Negative control: a flipped descending comparator makes the two ordering assertions fail RED, then restored to GREEN. 108 checks total. **Slice 5 (paint-only QML, 2026-08-07):** the shelf + reader wiring consumes this C++ API — `routeDownloadItem` routes pack-role rows to a new `openPackSeries()` (Main.qml) that injects `packVolumes()` rows into `ComicSeries.qml` as a baked release list with an explicit `packSeriesId` identity (df003eb identity-ordering law preserved). The page renders VOLUMES + EXTRAS sections (by packRole) and feeds the reader a mains-only DESCENDING chapters array (crossing convention: v8→v1); extras open solo (single-entry chapters). No new harness checks (paint-only — the ordering contract is Slice 4's 108-check gate); full app builds clean, `unit` gate stays 10/10. **Arc closed Runtime-validated 2026-08-07:** Slice 6's human-witnessed live Chew journey completed — the failed pair self-healed via boot resume on the fixed build, all 12 volumes landed under Hemanth's eyes ("done, it worked"), pack + extract tree reclaimed, manifest cleared (read-only disk checks recorded in chat.md). **Slice 7 added (2026-08-07):** the accent scenario — the live Chew "file stat failed" failure mode: CbzArchive fed miniz ANSI (`QFile::encodeName`) while the vendored miniz on MSVC decodes paths as UTF-8 (`mz_utf8z_to_widechar` → wide CRT APIs), so any non-ASCII path component mangled; fixed by `nativePath()` → `.toUtf8()`. Accent-named CBZ (probe fast path) + CBR (extract-repack path) via direct `ingestLocalArchive`, both land readable; the fixture carries U+00B4 in the archive FILE name only (Qt-written) because Windows bsdtar's ZIP writer transliterates ´→' in entry names (proven — a zip-entry accent fixture is silently vacuous), self-guarded by `accent-fixture-name-faithful`. RED recorded pre-fix (both ingests fail, 3 named reds); negative control (flipped landed-count → exactly one red, restored). 117 checks total. Mirrors `comic_downloader_ingest_harness` isolation (dedicated org/app, QTemporaryDir, path mirrors) |
| `colosseum.http_header_channel_harness` | unit | Theatre House HTTP Source slice 1 WIRE proof: drives a REAL MpvItem at a loopback QTcpServer and records the request headers mpv/ffmpeg actually send. Asserts the addon Referer reaches the wire (+ the forced VLC user-agent, proving production config), a comma value stays ONE header (node-array, not comma-joined), `ytdl` is off (else ytdl_hook clobbers our headers), and `loadFile` CLEARS `http-header-fields` so the next plain load carries no leftover header (leak guard + plain-path-no-header negative control in one). Loopback only, no live network; event-driven waits, no sleeps. Compiles `mpvitem.cpp` + links the app's MpvQt/libmpv. Green 2026-08-07; **negative control performed live** — removing the clear in `loadFile` turned it red (`leak: /plain.bin carried a leftover X-Thing`), restored to green |
| `colosseum.selftest.red_canary` | selftest | WILL_FAIL negative control |

The 18-test updater-inclusive `unit` gate (19 tests in this worktree because the unrelated Vault
Kit Qt Test is also registered, including all five updater harnesses) ran green under `ctest`
2026-08-08.
Registration ≠ conversion: these still speak the
house sentinel/exit-code contract; CTest consumes the exit code.

**Surfaced, not registered by us:** the top-level `include(CTest)` made the Player 2
lab's 17 `add_test` entries visible from the top build dir for the first time (15 pass;
2 fail environmentally in this build dir — `player2_state_machine_test` exits 0xc0000135
= a DLL missing beside the test exe, `player2_seek_generation_test` fails — Player lane's
to triage; excluded from the fast gate by having no `unit` label).

## Registered Qt Test targets

| CTest name | Source | Proves | Status |
|---|---|---|---|
| `colosseum.qttest.window_state_policy` | `tests/auto/window/tst_window_state_policy.cpp` | WindowStatePolicy geometry contracts (4 as named data rows) + WindowModeStore settings round-trip, isolated INI in QTemporaryDir, GUILESS | 11/11 green 2026-08-06; negative control performed (one deliberate break → exactly one named red, all other cases still ran); labels `unit;windows;qttest` |
| `colosseum.qttest.http_header_fields` | `tests/auto/player/tst_http_header_fields.cpp` | `httpHeaderFieldsList()` — the map→mpv `http-header-fields` formatter: comma/colon safety + the third-party-JSON injection guards (CRLF / colon-in-key / whitespace-in-key / empty-key dropped). Pure, APPLESS, no mpv. Theatre House HTTP Source slice 1 | green 2026-08-07; data rows are self-falsifying (flip any expected → one named red); labels `unit;qttest` |
| `colosseum.qttest.vault_kit` | `tests/auto/vault/tst_vault_kit.cpp` (compiles `native/engine/VaultKit.cpp`) | VaultKit pure logic — the Vault's shared brain (execution plan Slice 1): census classifier (per-subtree kind inference, mixed-leaf flag + honest leftover line, loose-file capture, user `scanIgnore` needle exclusion), the ported `cleanMediaFolderTitle`, the SxxExx grammar + anchored season-climb guard (bare `Season N` never masquerades as a show), and the walker's depth cap + pre-cancel. GUILESS, pure Qt6::Core (no app deps); fixture tree baked via `VAULT_FIXTURES_DIR` at `tests/fixtures/vault/` (structural stubs incl. a U+00B4 accent filename; real-byte corrupt-CBZ/decodable-MP4 deferred to Slices 5–6). Ported from Tankoban 2 ScannerUtils/BulkPackVerifier/VideosPage | 27/27 green 2026-08-08; negative control performed (two flipped expectations → exactly two named reds, `kind_for_file(epub_is_book)` + `title_cleaner(plain_unchanged)`, all other cases still ran; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_stores` | `tests/auto/vault/tst_vault_stores.cpp` (compiles `native/engine/VaultConfig.cpp` + `VaultIdentity.cpp`; shared `VaultStoreIo.h`) | The Vault's two stores (execution plan Slice 2): **VaultConfig** user-intent round-trip (roots + confirmed flag, per-subtree kind overrides, scanIgnore, hidden), atomic-write recovery from the last-known-good `.bak` (corrupt primary → restore; both corrupt → clean fresh), and path normalization (slash + Windows case); **VaultIdentity** content-addressed id (`vault:`+SHA-1 of `normPath::size::mtimeMs`) stability + normalization, the unique-(size,mtimeMs)-signature rename/move re-attachment (progress follows via alias, persisted), and the two-candidate ambiguity guard (parked, never silently merged — the copy ceremony is Slice 21). Pure Qt6::Core, GUILESS; QTemporaryDir isolation, no net | 11/11 green 2026-08-08; negative control performed (ambiguity expectation flipped to assert migration → exactly one named red, `reconcile_two_candidate_ambiguity_does_not_migrate`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_index` | `tests/auto/vault/tst_vault_index.cpp` (compiles `native/engine/VaultIndex.cpp`) | The Vault's rebuildable scan product (execution plan Slice 3): a SQLite index with transactional full-replace `publish()` (a cancelled or errored publish rolls back — previous contents intact, decision 4), incremental `upsert()` for live-shelf arrivals (Slice 15), and numeric-aware folder-order listing via a zero-padded sort-key column (SQLite default collation is lexicographic, not natural). Query surface: `itemCount` / `itemCountForKind` / `kinds` / `groupsForKind` / `filesInSubtree`. Qt6::Sql; the qsqlite driver resolves from `build-msvc/` beside the app-deployed plugin (ledger deploy note); QTemporaryDir DB per run, no committed `.sqlite`. GUILESS | 7/7 green 2026-08-08; negative control performed (dropped the sort-key numeric padding in production → the two ordering cases red, `folder_listing_is_natural_order` + `natural_sort_key_is_numeric_and_case_insensitive`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_scanner` | `tests/auto/vault/tst_vault_scanner.cpp` (compiles VaultScanner + VaultIndex + VaultIdentity + VaultKit) | The Vault's cancellable off-thread census (execution plan Slice 4): pure `buildScan()` over the fixture tree (kind-pure slice model + dominant-kind index rows, season-nested subfolders, leftovers excluded), the generation-guarded `applyResult()` (a stale result is dropped; a cancelled census never publishes — a real bug the test caught: a cancelled walk returns empty, and without the explicit cancelled flag applyResult would publish empty and WIPE the index), and — async — `scanRoot()` + the buffered-rescan-runs-after guarantee. QtConcurrent pool + QFutureWatcher marshalling; thread-affine index/identity work stays on the GUI thread. Qt6::Concurrent/Sql; GUILESS; QTemporaryDir index+identity per run. App registration + boot check deferred to Slice 10 (no consumer yet). | 9/9 green 2026-08-08; negative control performed (generation guard disabled → the stale-drop case red, `generation_guard_drops_stale_result`; restored); labels `unit;qttest` |
| `colosseum.qttest.vault_enricher` | `tests/auto/vault/tst_vault_enricher.cpp` (compiles VaultEnricher + VaultIndex + CbzArchive + miniz) | The Vault's fact-filler (execution plan Slice 5), deliberately THIN — reuses Colosseum's existing facilities instead of re-porting TB2 ArchiveReader: comic page count + cover-entry pick via `CbzArchive` (the cover is served on demand by the existing `image://comiccover/` provider — no new decoder, no thumbnail cache), a corrupt-archive honest error state (never a wedge/hang), the triple-keyed video duration cache (hit / miss-on-triple-change / persist), and `enrich()` writing facts back to the index. Added a `coverRef` column to VaultIndex. Deferred (gradient-fallback until their slices): epub cover ladder + author, video thumbnails, page dimensions (the Vault UI shows none of the last); the live ffprobe path is exercised at Slice 6. Pure Qt6::Core/Sql, GUILESS; real CBZ (`tiny-volume.cbz`) + corrupt-CBZ fixtures + QTemporaryDir | 5/5 green 2026-08-08; negative control performed (corrupt-CBZ flipped to expect success → one named red, `corrupt_cbz_is_error_not_wedge`; restored); labels `unit;qttest` |

Parity: the legacy `window_state_policy_harness` covers the identical contracts and
stays built + registered until a parity review retires it (migration policy). The
conversion's evidence gain, demonstrated live: the legacy `qFatal` idiom reports ONLY the
first failure; the Qt Test reports every case independently.

**Qt Test build facts (slice 3):** `Qt6::Test` is discovered in `tests/CMakeLists.txt`
under `BUILD_TESTING` only — never linked into the app. Two deploy traps solved there,
both verified live as 0xc0000135-before-main: Qt Test exes must land in `build-msvc/`
beside the app-deployed Qt DLLs (`RUNTIME_OUTPUT_DIRECTORY`), and `Qt6Test.dll` itself
is staged by a POST_BUILD copy (no app deploy step ever shipped it).

## Registered Qt Quick Test targets (slice 4–5, 2026-08-06)

**One runner, one CTest entry, four test files, 21 cases:**
`colosseum.qml` runs the repo-built `colosseum_qml_tests` (QUICK_TEST_MAIN_WITH_SETUP —
the setup object supplies a TEST application identity + INI settings in a per-run temp
dir, because production `Settings` blocks fail to initialize without one; verified live)
with `-input` pointed at the SOURCE `tests/qml/`, so file-relative production imports
resolve against the real tree. Labels `qml;windows` — these open REAL windows; never an
offscreen gate. Qt6::QuickTest discovered only under BUILD_TESTING; `Qt6QuickTest.dll`
staged by POST_BUILD beside the exe (same 0xc0000135 disease as Qt6Test.dll).

| File | Proves | Notes |
|---|---|---|
| `tst_comicreader_title_controls.qml` | REAL mouse hit-testing against production `ComicReaderHud` | pre-existing; legacy `qmltestrunner` gate still works |
| `tst_search_history_flow.qml` | search-history flow against production QML | pre-existing. **KNOWN FLAKE:** `test_biblioRecentChipBodyAndRemoveHaveIndependentClickTargets` fails ~1 run in 3 (real-window focus/timing); pre-dates the runner — reconciliation owed by its owner, not silently rerun-until-green |
| `tst_comicreader_resume_race.qml` | the four resume-race regressions (T1 mount-time page-1 cannot overwrite a restore · T2 manualActivity disarms · T3 give-up clears both arms · T4 goMinimize flushes synchronously before emitting once) as independent cases: tryVerify on the debounced write, SignalSpy, createTemporaryObject; T3 injects a `seriesRecords` record (`layout: long_strip`) because the fraction arms only at long_strip OPEN — the legacy harness got that from ambient runner prefs. Negative control performed (one flipped expectation → exactly one named red). Converted from `comicreader_resume_race_harness.qml`, which stays with its gate until parity review | slice-5 pilot |
| `window_behavior_harness.qml` | (still top-level, still orphaned) | adoption candidate |

**Conversion learning worth keeping:** QML `Settings` writes are batched/deferred — a
test that writes a preference through one component instance and expects another
instance to read it immediately is racing the batch timer. Inject the record layer
instead; never "fix" it with a wait.

## Existing bespoke estate — classification

Full per-target build facts (sources, links, compile defs, CMake lines) live in the sweep
this ledger was built from; the classes and gates below are the planning surface.

### C++ harnesses (69) by class

- **Deterministic unit (48):** pure contracts over temp dirs, no net. Families: comics
  torrent/edition stack (~15), comicreader engine (8), biblio catalog (3), manga/tankoban
  logic (5), catalogs over SQLite-in-tempdir (5 — no committed .sqlite fixtures; DBs are
  built per-run), stores (progress/collection/search-history/model-manifest), net policy
  units (poster scoreboard, pin proxy factory), window-state policy, reader2 stores/bridge,
  anime order index, archive (cbz) pair, download file ops, hosted player bridge, knaben
  indexer, comick pair.
- **Integration with fakes at the boundary (7):** `manga_tankoban_service`,
  `comic_torrent_pack_transport`, `manga_volume_torrent`, `comic_torrents_search`
  (fake torrent/nyaa engines), `reader2_autoattach`, `anime_order_service` (local
  QTcpServer), `loopback_pin_proxy` (loopback sockets + hang failsafe).
- **Live network — NEVER in a deterministic gate (3):** `knaben_probe` (real Cloudflare
  verdict), `audiobook_engine_probe` (self-declared triage tool), `torrent_engine_download`
  (live DHT, watchdog, exit 2 = timeout).
- **Infrastructure, not tests (3):** `comic_torrent_seed` + `comic_torrent_pack_seed`
  (loopback seeders that serve for 5 minutes), `torrent_engine_link` (link-only smoke).
- **GUI/offscreen-sensitive (3):** `comicreader_core` + `comicreader_provider` (need
  `QT_QPA_PLATFORM=offscreen`), `window_shell_gui` (needs offscreen AND
  `QT_QPA_PLATFORM_PLUGIN_PATH` to the Qt install — the windeployqt `platforms/` beside
  the exe ships only `qwindows.dll`, and the failure is a SILENT `0xC0000409`).

### QML harnesses (88) by class

- **~70 deterministic component harnesses** (offscreen qml.exe, sentinel + exit code),
  importing production QML/JS by relative path from `tests/`.
- **Probes, not tests (~9):** the Cloudflare/image probes (`batcave_guard`, `comichub_img`,
  `rco_cf`), reality/perf probes (`catalogue_residency`, `theatre_shelf_reality`,
  `comicreader_fullscreen_timing`), genre/world-search probes.
- **Live network (2):** `abb_live_probe`, `hosted_player_webengine_smoke` (WebEngine +
  live VidKing).
- **Two giants:** `comicreader_shell_harness.qml` (187 KB, 18 Timers) and
  `comicreader_surfaces_harness.qml` (160 KB, 8 Timers) — hundreds of hand-rolled checks,
  no isolation between them; the highest-flake, highest-value migration surface after the
  named pilots.

### PowerShell runners (150) by class

- **88 pure static source-grep gates** — assert a string exists in a source file. They
  regress on rename, not behavior; zero coverage signal. The single largest population.
- **49 qml.exe component gates** (hardcoded Qt path), some hybrid grep+behavior by design
  ("the 'no guided' assertion is a grep here, the behavior is the harness").
- **14 gates that run compiled C++ harnesses** (the real native gates):
  `test_biblio_discover_explore`, `test_collection_p0`, `test_comic_torrent_pack_dltest`,
  `test_comic_torrent_sources_v2`, `test_comicreader_chrome`, `test_comics_catalog_db`,
  `test_lanista`, `test_manga_tankoban_native`, `test_native_deploy_runtime` (grep-only),
  `test_search_history_p0`, `test_tankoban_discover`, `test_theatre_search_p0`,
  `test_theatre_shelf_reality`, `capture_catalogue_perf` (perf capture, no verdict).
- **6 launch the real app**; **2 are destructive-by-design real-download gates** made safe
  by `COLOSSEUM_APPDATA_TAG` isolation (`test_comic_torrent_pack_dltest`,
  `test_manga_tankoban_native`).

## Test labels (proposed vocabulary — nothing carries labels yet)

`unit` · `qml` · `integration` · `network` (explicit live-net probes only) · `slow` ·
`legacy` (registered bespoke harness) · `lanista` · `windows` · `visual` · `probe`
(no verdict; never a gate) · `destructive` (real side effects; opt-in env-gated only).

## Fixture and isolation rules (as practiced today)

- **Compile-def fixture dirs:** `TANKOBAN_FIXTURES_DIR`, `BIBLIO_FIXTURES_DIR`,
  `COMICS_PACK_FIXTURES_DIR` bake `tests/fixtures/<domain>/` paths in at build time.
  Fixture inventory: tankoban (6 files incl. `tiny-volume.cbz`), biblio (3 JSON), comics
  pack (3 CBZ), anime order (argv-passed), locg (4 JSON), abb (2 HTML, node-consumed),
  comicreader pages (2 PNG), lanista golden (1 PNG).
- **Isolation:** 23+ harnesses use `QTemporaryDir`; settings-touchers use
  `QStandardPaths::setTestModeEnabled(true)` so the live AppData is never touched. SQL
  harnesses build their DBs in temp dirs per run — no committed .sqlite.
- **Env flags that gate danger:** `COLOSSEUM_*_DLTEST` + `COLOSSEUM_APPDATA_TAG` for the
  real-download gates; `COLOSSEUM_LANISTA_SELFTEST/_PIPE/_DRIVE/_WRITE` for the bridge;
  `QT_FORCE_STDERR_LOGGING=1` needed by ~30 runners (GUI-subsystem binaries are otherwise
  silent).

## Machine-readable output

**None.** No JUnit, no XML, no manifest anywhere in the active estate (Lanista's `suite`
verb emits junit.xml + report.md for scenarios — the only machine-readable reporter in
the repo). Everything else is stdout sentinel + exit code.

## Known gaps (the honest list)

1. **39 of 69 C++ harnesses are invoked by nothing** — they compile on every build (so
   they can't rot at compile level) but nothing runs them: the entire `reader2_*` family,
   the whole `net/` family, both window-mode harnesses, `progress_store`, most of the
   comics edition stack. Unrun tests protect nothing.
2. **19 QML harnesses are referenced by no runner**, including the 48 KB
   `reader2_logic_harness.qml`, the only QtTest-importing top-level file
   (`window_behavior_harness.qml`), and the complete 3-file calendar cluster — a whole
   feature's test surface with no gate.
3. **2 broken runners** point at QML files that don't exist:
   `test_comics_catalog_v1.ps1` → `comics_catalog_logic_harness.qml`;
   `test_reader2_readalong.ps1` → `reader2_readalong_harness.qml`.
4. **88 grep-gates** prove strings, not behavior.
5. **Hardcoded Qt path in ~49 runners** — one Qt bump breaks the whole QML gate estate in
   49 places.
6. **Player2 CTest is invisible** from the top-level build even when enabled
   (subdirectory `enable_testing()`).
7. **Misfiled artifacts:** captured probe logs stored under `tests/fixtures/` with no
   consumer; ~350 KB of stray run logs/CSVs checked into `tests/`.
8. **No selection, no per-case reporting:** the two giant comicreader QML harnesses run
   hundreds of checks as one all-or-nothing process.

## Migration candidates (register first; convert only for named benefit)

**Pilot CTest registration set** (deterministic, isolated, sentinel+exit-code, no net —
lowest-risk first registrations): `window_state_policy_harness`,
`search_history_store_harness`, `progress_store_harness`, `collection_store_harness`,
`cbz_archive_harness`, `poster_scoreboard_harness`, `comicreader_cache_harness`,
`biblio_catalog_logic_harness` (fixture-dir baked, still deterministic).

**Qt Test conversion pilot:** `tests/window_state_policy_harness.cpp` — deterministic,
already QTemporaryDir + isolated QSettings, naturally splits into test functions, and its
`qFatal` idiom currently hides every later case on first failure (a named evidence
benefit).

**Qt Quick Test pilots:** register the two existing `tst_*.qml` under a repo-built runner
(they run today only via external qmltestrunner); adopt the orphaned
`window_behavior_harness.qml`; then migrate
`tests/comicreader_resume_race_harness.qml` (timer-chained today; the resume-to-page-one
race is a real shipped regression worth permanent per-case protection).

**Leave bespoke (do not convert):** the probes, the seeders, the live-network triage
tools, the grep-gates (retire or fold into behavior gates over time, don't convert), the
giant comicreader shells until the pilot pattern is proven.

## Reader-state vocabulary (slice 6, 2026-08-06)

The reader's authoritative session state is readable as stable properties on ONE named
surface: `qml-get comicReaderShell` (production `MangaReader.qml` names its shell) —
`seriesId`, `curChapterId`, `currentPage`, `pageCount`, `mode`, `_stripRestorePending`.
This is the shared state vocabulary for Qt Quick Test assertions AND Lanista replays. A
versioned C++ snapshot object (`colosseum.reader-state.v1`) is deliberately DEFERRED
until a consumer needs more than these properties answer — demand-driven, not built on
spec.

## Three-layer minimize/restore regression (slice 7 — layer status, 2026-08-06)

- **Qt Test:** pass — `colosseum.qttest.window_state_policy` (geometry + window-mode
  persistence round-trip).
- **Qt Quick Test:** pass — `tst_comicreader_resume_race.qml` (restored-state
  consumption, stale-write prevention, synchronous minimize flush).
- **Lanista (real OS minimize → taskbar restore → same page):** **Bridge blocked.** The
  bridge cannot restore a minimized window: `ui-click` needs an on-screen item, nothing
  can reach the Windows taskbar, and no window-state command exists. Smallest
  prerequisite, recorded in the Lanista ledger's Planned section: a Drive-gated
  `window-set-state` command (minimize/restore via QWindow showMinimized/showNormal —
  the real product path, not a simulation). Until it lands, the assembled-app
  minimize/restore proof remains human-witnessed only.

## Runtime boundary (unchanged by this arc)

Qt Test proves C++ contracts; Qt Quick Test proves QML component behavior (its window is
NOT the Windows shell — no taskbar/lifecycle claims); Lanista proves the assembled app in
isolated sessions per its own ledger; pixels are exhibits; aesthetic verdicts are
Hemanth's. A green suite here earns **Test-reported**, never **Runtime-validated**, for a
user-visible slice.
