# Arc 41 runtime acceptance matrix

Candidate implementation: `1e18b813a26f4b4772fada24bac6c91b025f7454` on `codex/arc41-completion`.
Production executable: `native/build-arc41-msvc/colosseum.exe`, SHA-256 `f7a6a0688fa2c70255145dacd714e9e1f979593717789b7bba3eb39ae77d02ec`.
Production QML entry: `qml/Main.qml` from the same worktree. Colosseum loads QML from disk, so executable provenance and QML commit are recorded separately rather than treating the exe hash as QML proof.

## Fresh assembled-app evidence

| Area | Lanista session | Result |
|---|---|---:|
| Cold shell, onboarding, Vault, F11 | `20260904-011917-c93d79f3` | 16/16 |
| Keyboard Guide + global shortcuts | `20260904-011948-37050f4b` | 34/34 |
| K-06 Account/System + Update | `20260904-012027-9b73721a` | 27/27 |
| K-02 Tankoban catalogue/search | `20260904-012104-be3c66ac` | 23/23 |
| K-02 Theatre catalogue/search | `20260904-012142-fea0ea9f` | 20/20 |
| K-02 Biblio reorder/focus survival | `20260904-012245-142eeee0` | 20/20 |
| Real seeded ComicReader | `20260904-073212-e5d9d6fa` | 27/27 |
| Real decoded mpv Player | `20260904-071519-88c6e911` | 35/35 |
| Star Wars Universe + Settings/Extensions | `20260904-071715-e321573f` | 38/38 |

Total assembled-app assertions: **240/240 passed**.

## Lower-layer evidence

- Fresh action census: 278 QML files, 770 pointer-action candidates, 549 COVERED, 128 DELEGATED, 93 EXCEPTION, **0 BUG**.
- Fresh scroll census: 131 rows, 91 COVERED, 9 DELEGATED, 31 EXCEPTION, **0 BUG**.
- Eight Qt Quick Test files delivered real key events through registry, region, primitive, Player, Reader, K-06, Star Wars, and focus-containment production components. Result: all green.
- Arc 41 Python/PowerShell contract battery: registry, regions, Guide parity/integration, K-02, K-06, K-07, readers, TheatreSeries, runtime identities, accessibility sliders, utilities, Player hotkeys/nesting, Universes, Downloads, and Star Wars parity/native/load contracts all green.
- Relevant CTest slice: 7/7 passed (`reader2_logic`, `reader2_runtime_contract`, immersive-reader shell, download reveal, and three Player 1 recovery/error contracts).
- Production-target build: `cmake --build native/build-arc41-msvc --config RelWithDebInfo --target colosseum lanista` completed with `ninja: no work to do` after the final source tree had already produced the tested binaries.
- `git diff --check`: clean.

## Runtime notes

The Player acceptance seed was copied into a disposable `artifacts/arc41-final-player-seed` directory and its media path resolved to an absolute path on Device 1. The checked-in historical fixture was restored unchanged. This avoids encoding a machine-specific path into the repository while still proving a real decoded 64x64 frame, Space pause/resume, Escape, and warm replay.

Two historical tests are red on the current master baseline and were not rewritten to hide that fact: `tests/test_galaxy_universe_p0.ps1` still expects the retired `loadGalaxy` triptych implementation, and `tests/tankoban_library_api_harness.qml` still expects the retired chapter-lane row semantics. Their source/API inputs are identical to `origin/master`; they are not Arc 41 regressions.
