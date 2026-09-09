# Arc 41 repair 13 — Luna final qualification

Date: 2026-09-09
Branch: `codex/arc41-directional-scroll`

## Scope

This report records the final Luna implementation and qualification pass for the six assembled directional-navigation families. The work stayed in `C:\b\colosseum-arc41-scroll` and used local shell commands and Lanista sessions with unique tagged app-data roots.

## Source and cleanup

- Removed only accidental root probe artifacts and four abandoned runner `*.stdout.tmp` / `*.stderr.tmp` files.
- Preserved all `.superpowers` evidence, the unrelated deleted `docs/build/linux.md`, `docs/build/macos.md`, and `docs/build/windows.md`, and unrelated working-tree state.
- `git diff --check`: passed before staging.
- All six scenarios parsed successfully with PowerShell `ConvertFrom-Json`.

## Static and focused verification

- `C:\Qt\6.11.1\msvc2022_64\bin\qmllint.exe -I qml` over every changed QML and test-QML file: exit 0 for all files. Receipt: `repair13-final-qmllint-all.txt`.
- The requested 11-suite offscreen set was run with `QT_QPA_PLATFORM=offscreen` and `QML_DISABLE_DISK_CACHE=1`. Ten suites passed in the first receipt. `tst_main_book_return.qml` was rerun with absolute `QML_IMPORT_PATH` and `QML2_IMPORT_PATH` set to `tests/qml;qml`; it passed 14/14. Receipt: `repair13-offscreen-tst_main_book_return-imported.qml.txt`.
- The broader focused QML summary contains exit 0 for all 15 focused suites, including the 11 requested Arc 41 suites. Receipt: `repair13-focused-qml-summary.txt`.

## Fresh build

Fresh disposable build: `C:\b\colosseum-arc41-final-build-20260909-1700`.

Configure command:

```text
cmake -S C:\b\colosseum-arc41-scroll\native -B C:\b\colosseum-arc41-final-build-20260909-1700 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64 -DBUILD_TESTING=ON -DCOLOSSEUM_BUILD_PLAYER2=OFF -DCOLOSSEUM_PLAYER2_IN_APP=OFF -DCOLOSSEUM_UPDATE_TESTING=OFF
```

Configure exited 0. Build command targeted `colosseum lanista colosseum_qml_tests`; it exited 0. Final Ninja step was `[219/220] Linking CXX executable colosseum.exe`. Build log: `C:\b\colosseum-arc41-final-build-20260909-1700\build-final.log`. Error-pattern search found zero compiler/linker/Ninja errors. QML manifest: `qmlTreeSha256=30a59a01426b603129cbfdb8def71d4662e11bb6be42dc04cecbb9664ce5de8e`.

Final binary evidence:

- `colosseum.exe`: 14,440,448 bytes, mtime 2026-09-09 16:48:23
- `lanista.exe`: 231,936 bytes, mtime 2026-09-09 16:46:41
- `colosseum_qml_tests.exe`: 33,280 bytes, mtime 2026-09-09 16:46:57
- `tst_keyboard_key_events.exe`: 41,984 bytes, mtime 2026-09-09 17:08:53

The executable was deployed with repository `windeployqt --qmldir qml` procedure. The fresh directory also received the known-good repo-native `MpvQt.dll` and `libmpv-2.dll` runtime dependencies from the prior deployed build.

## Native keyboard gate

The native fixture now waits for real overflow (`contentHeight > height`) before dispatching Down. This removes the show/layout race while preserving held, repeat, and release behavior.

Command:

```text
ctest --test-dir C:\b\colosseum-arc41-final-build-20260909-1700 -R ^colosseum\.qttest\.keyboard_key_events$ --output-on-failure
```

Result: 3 passed, 0 failed. Receipt: `C:\b\colosseum-arc41-final-build-20260909-1700\ctest-keyboard-key-events-final-corrected2.log`.

## Aggregate QML limitation

The configured repository aggregate QML gate ran with explicit offscreen platform/plugin and QML import paths. Result: 644 passed, 13 failed, 3 skipped. The 13 failures are unrelated standing failures: five One Piece East Blue atlas evidence tests, six Player2Progress cascades caused by the unavailable real Player2 shell, and two TankoyomiConfigurationPage geometry tests. Receipt: `C:\b\colosseum-arc41-final-build-20260909-1700\ctest-colosseum-qml-final-corrected.log`.

## Six-family assembled runtime

Runner: `tests/test_arc41_directional_runtime.ps1`. Run start: `2026-09-09T17:22:44.4505341+05:30`. All scenario mtimes were earlier than this start time. The runner used unique tags, fresh Lanista pipes, the Settings HKCU onboarding helper and `QT_SCALE_FACTOR=1.5` only for Settings, and a bounded 180-second family timeout.

Aggregate receipt: `repair13-wave5-six-family-aggregate-20260909-172245-6ffea228.txt`.

- Family 1 World Featured → Continue: 29 steps, 0 failed
- Family 2 Reader long scroll: 33 steps, 0 failed
- Family 3 Biblio poster route return: 36 steps, 0 failed
- Family 4 Settings overflow: 18 steps, 0 failed
- Family 5 virtualized collection: 48 steps, 0 failed
- Family 6 overlay containment: 37 steps, 0 failed

Aggregate result: 201 steps, 0 failed; completed `2026-09-09T17:25:46.7469842+05:30`. No Colosseum or Lanista processes remained after the run.

## Limits

The aggregate QML gate remains 644/13/3 because of the unrelated failures listed above. This Arc 41 lane is qualified by the focused QML suites, the native keyboard CTest, and the fresh-build six-family Lanista aggregate.
