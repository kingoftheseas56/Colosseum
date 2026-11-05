# Verification at the pause checkpoint

Product-code checkpoint: `5a0fdac784a372103bdf8421219968fa49e33de5`. The compiled native code and source QML bytes correspond to this checkpoint; the receiving build was made before its final commit, with the same four modifications present. This archive adds documentation only. No builds or feature tests were restarted for online archival.

## Completed checks

| Surface | Actual evidence |
| --- | --- |
| Windows application | Actual Release `colosseum_runtime_ready` build passed with Qt 6.11.1 MSVC2022_64, MSVC 14.44; Collections test fixtures enabled for this test candidate. Shipping default remains off. |
| Publication controller | 8 passed after closing Windows QFile readers before QSaveFile replacement; independently rerun offscreen by the receiving parent. |
| Sharing persistence | 71 checks with the cross-language mixed-gift fixture passed; parent independently reran that fixture executable, exit 0. |
| Native focused targets | Five CTest targets passed: sharing store, publication controller, named collections store, profile preservation and identity. |
| Sharing QML | 10 passed offscreen; independently rerun by the parent. |
| Full native QML fixture | Direct executable: 24 passed, 0 failed, 0 skipped in 286.315 seconds. An earlier 180-second CTest timeout is retained as a failed attempt; the CTest target did not pass within that timeout. |
| JavaScript | Viewer adapter and Collections route tests: 9 passed; independently rerun by the parent. |
| Windows service/database | Real disposable PostgreSQL 17.6 with Go 1.26.9: narrow collection lifecycle, concurrency/rollback, passphrase/rate, quota/revocation, HTTP projection/error/assembled journey and empty-database migration checks passed. No Windows race result is claimed. |
| Browser | Actual Chromium exercised anonymous desktop/mobile, passphrases, immutable gifts, owner rejection, revocation and exact native publication snapshots using a disposable service. |
| Dots Linux backend | Supplied affected-package PostgreSQL race results passed at backend pin `857c6333302a741f91a8bbe7012a4e5ede761a64`; that backend source matches the delivered implementation. This is supplied Linux evidence, distinct from local Windows results. |
| Wallpaper UI | Parent visually inspected overview/detail/sharing with only the wallpaper behind Collections and the foreground dock retained. Home visibility was observed before, during and after closing Collections. |
| User state | Protected original recording and comparison collection files retained their baseline hashes. No existing test profiles were cleared. |

Narrow backend command used with an explicitly disposable database:

```sh
go test -p 1 ./internal/account ./internal/httpserver ./internal/database -run 'TestCollection|TestRunMigrationsFromEmptyDatabase' -count=1 -v
go test ./internal/httpserver/collectionviewer -count=1 -v
```

The existing fixture resets schemas: never point it at production or an existing runtime/profile database.

## Material gaps and failed attempts

- The **full** `internal/database` package exited 1 in a separate disposable receiving test database, including migration-context timeout failures. This remains unresolved. Its failure does not invalidate the earlier narrow passing command, nor may the narrow pass be described as a full-package pass.
- Hemanth rejected the current sharing popup. The proposed simpler UI is documentation only.
- Incoming master window-control changes have not been combined with this code checkpoint.
- Functional native owner/receiver/Keep/restart/offline/copy/revocation/profile-isolation journeys ran during receiving. Final acceptance of the whole combined source was interrupted by the pause and must be completed when resumed.
- Some earlier corrected-source screenshots still showed BootSplash. They were excluded. The accepted visible-owner captures were gated on `bootSplash.visible=false`; do not substitute the earlier covered captures as visible UI evidence.
- Synthetic HTTP sign-in used the build-gated volatile credential seam. This does not qualify production OS-vault persistence. Tagged credential namespace derivation was inspected for the planned socket-only stay-open launch, but that final launch was not completed before pause.
- The final intermediate test candidate self-exited through its existing ten-minute watchdog. No app-close action was performed for the archive. Existing unrelated app windows were left untouched.
- Android, a shipping/default-fixture-off installer, live-provider playback, and production hosted qualification were not completed by this lane.

## Artifact fingerprints

| Artifact | SHA256 |
| --- | --- |
| Windows colosseum.exe | `34751dc6fd95af8f46bdf08979cd77449d2d6dfc743311ebfd7a3ae46c206329` |
| qml-build.manifest | `2240a741dd012233f58b3f42d3b55928404c5946c72c1c735e69881e296e86b5` |
| Source QML tree fingerprint | `a800c1cdfe20edfd8ba303ef3f6b2ed4299a62ab43207682e4db4e5426513ccb` |
| Canonical normalized migration 0011 | `f4276dd8012496a877b6ce65793f0c6c51c34a03ceddd575498e6567a8cf11eb` |

These identify retained local evidence; binaries and raw logs are not added to Git. Recalculate against any future source change.

## Migration rehearsal

An isolated clone rehearsed the guarded migration transaction. All 24 existing table column-schema fingerprints and ten prior migration checksums were unchanged; the new empty tables, seven PK/FK/check constraints, owner index and owner/ACL pattern matched expectations. An intentional failed transaction rolled back its probe table.

The provider's preset migration helper split dollar-quoted SQL incorrectly. The successful rehearsal used a transaction statement array keeping the DO guard as one statement. Do not naively split the archived SQL on semicolons. Recheck the complete baseline and live targets before any later approved production operation. Production was not migrated or deployed by this task.
