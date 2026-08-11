# Vault tranche Slice 17B / 20 / 21 — evidence manifest

`[Agent 0 (Luna / Codex)]` · 2026-08-12 · workspace
`C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`

Base confirmed at/after `b5986bb` on nested `master`. Existing dirty updater/Reading Room WIP was
preserved. Shared declarations were recorded in `..\agents\chat.md` before touching
`qml\Main.qml`, `native\main.cpp`, and the earlier `qml\TheatreApi.js` tranche.

## Slice ledger

| Slice | Status | Evidence | Remaining gate |
|---|---|---|---|
| 17B — manual Identify type-and-pick | `Test-reported` | `tst_vault_mal_match`, `tst_vault_imdb_match`, `tst_vault_identifier` green; direct `tst_vault_identify_dialog.qml` 5/5; chosen-B/progress-preservation assertions pass; Cinemeta path fixture-injected. Commit `6f1be0e` pushed. | Hemanth eyes-on; changed production binary was not relaunched. |
| 20 — Next to Open tray | `Test-reported` | `vault_launch_router_harness` prints `VAULT_LAUNCH_ROUTER_OK`; direct tray QML 4/4 in the aggregate suite; staged-not-recent negative control failed as expected, then was restored. Commit `b1d10f1` pushed. | Hemanth eyes-on; daily `colosseum.exe` remained running and locked the old binary. |
| 21 — identity ceremonies | `Test-reported` | `tst_vault_stores` 22/22; changed-content, ±2s tolerance, likely-copy, persisted choice, silent rename, and alias/progress rows pass; deliberate remembered-decision flip failed, then was restored. Ceremony QML 4/4. Production C++ objects compiled cleanly. | Hemanth eyes-on/Lanista mutation journey; production relink was blocked by the daily process. |

## Deterministic gates

- `ctest --test-dir native/build-msvc -L unit --output-on-failure`: **33/33 passed**.
- Focused `tst_vault_stores.exe -v1`: **22 passed, 0 failed**.
- `tst_vault_identity_dialogs.qml`: **4 passed, 0 failed**.
- Existing `tst_vault_identify_dialog.qml`: **5 passed, 0 failed**.
- `tst_next_to_open.qml`: **4 passed, 0 failed**.
- `vault_launch_router_harness.exe`: **VAULT_LAUNCH_ROUTER_OK**.
- Changed production objects (`VaultIdentity`, `LocalLaunch`, `VaultLibrary`, `main`) compiled via
  direct Ninja object targets; exit 0. The final `colosseum.exe` relink timed out while PID 39452
  was running, and the old executable timestamp remained unchanged. The daily process was not
  killed or relaunched.
- Aggregate `colosseum.qml`: first run was 156 passed / 1 transient failure; immediate rerun was
  **1/1 CTest passed**. New ceremony tests and the existing Identify tests were green in both the
  focused run and the aggregate rerun.

## Negative controls

- Slice 17B: candidate-B/progress-preservation and offline-miss fixture assertions are active;
  the manual search path never contacts the network in tests.
- Slice 20: temporarily expecting two Open Recent rows produced the named failure
  `staged files must not pollute Open Recent`; assertion restored and harness returned green.
- Slice 21: temporarily expecting a second prompt after `same-media` produced the named failure
  (`Actual 0`, `Expected 1`); assertion restored and the full store test returned green.

## Review state

- Cross-substrate Sol advisor was invoked read-only but stopped without a verdict; no files or
  state were changed. Reported as unavailable rather than treated as approval.
- Agent 0 self-review against the locked Slice 21 DoD: `MET` detection, tolerance, copy choice,
  persisted relationship decisions, silent unique rename, shared LocalLaunch/VaultIdentity seam,
  shared ceremony surface, focused tests, negative control, and unit gate. `PARTIAL` runtime
  mutation/Lanista and human visual review because the daily app process blocked a safe relink.
- Final Slice 21 commit is intentionally surgical; only the declared identity files, ceremony
  surface/tests, one `main.cpp` wiring line, and the corrected Slice-20 tray placement are staged.

## Lanista / eyes-on

No new Lanista manifest or grab is claimed. The required isolated runtime journey is
`Bridge blocked`: the available `colosseum.exe` is the pre-change daily instance and was left
untouched. Hemanth's eyes remain the closing gate for both dialog surfaces and the tray.

`[Agent 0 (Luna / Codex), evidence manifest]`
