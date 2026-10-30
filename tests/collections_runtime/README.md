# Local Collections application qualification

These are Windows acceptance tools for the real Colosseum executable, not a replacement store or a mock production entry point. The authoritative delivery report for this task lives in the primary checkout at `docs/superpowers/handoffs/2026-10-29-collections-gpt6-pro/INTEGRATION-RESULT.md`; logs and screenshots are beside it under `integration-evidence/completion-20261030`.

## Safety and build

Normal builds leave `COLOSSEUM_ENABLE_COLLECTIONS_TEST_FIXTURES=OFF`. The isolated qualification build enables it. Its native profile command additionally requires the marked root, an exact matching `COLOSSEUM_APPDATA_TAG` beginning with `collections-fixture-`, `COLOSSEUM_COLLECTIONS_TEST_FIXTURES=1`, and the Lanista drive gate. It accepts only local/A/B test aliases and never signs in, creates an account, reads credentials, or routes an arbitrary profile identifier.

Build `native` with the existing project toolchain and build `colosseum_runtime_ready`. Do not launch new QML on an old native executable. The launch script uses normal `qml/Main.qml` discovery with the app's QML fingerprint verification; it never passes a QML override or enables hot reload. Native and QML contract suites also have standalone CMake projects under `tests/named_collections`, `tests/collections_identity`, `tests/collections_qml`, and this directory.

In the delivered worktree, the existing build scripts are in the evidence directory: `build-application.cmd`, `build-focused-checks.cmd`, and `build-qml-checks.cmd`. They initialize the installed MSVC/Qt environment. Paths in the task's launcher intentionally identify this machine's isolated candidate rather than guessing a different toolchain.

## Fixture setup and identity

Use a new empty disposable AppData sibling, never the daily application directory. Its basename must be `Colosseum-dltest-` plus the exact environment tag; `.collections-synthetic-fixture` must contain `collections-synthetic-fixture-v1` followed by a newline. Fixture producers reject mismatched roots, symlinks/reparse boundaries, and overwriting existing data.

`generate_fixture_media.py` creates two seconds of silent PCM and a tiny generated AVI. `seed_vinyl.cpp` imports the local album through VinylLocalSource/VaultIdentity, checks full JSON snapshots and read-only reopening, and exposes actual album/edition/song/artist references. `seed_vault.cpp` registers the fixture video folder through VaultConfig and indexes the exact local video through the normal native owners. Its adopted IMDb label is an identity/UI test, not a claim that the generated video contains that film. `seed_comic_pack.py` verifies and copies two tiny repository PNG/ZIP fixtures and records their provenance. `seed_collections.cpp` admits references through ColosseumTitleIdentityRegistry and saves them through NamedCollectionsStore. No fake persistence component participates in the app acceptance.

The separately guarded `repair_vault_fixture` helper documents and repairs the original fixture omission of a registered Vault root. It only permits the previously known synthetic AVI, preserves prior index rows and identity bytes, and does not repair or alter production application state.

Normal first-use guest startup currently owns `profiles/legacy/named-collections-v2.json`. Explicit native `activate-local` owns `profiles/local/named-collections-v2.json`. They are different lifecycle states: specify the actual file with `--store`. The guest creation/restart proof is separate from explicit-local/A/B profile-switch acceptance.

## Running the delivered candidate

From the isolated source directory, run:

```powershell
& 'C:\Users\Suprabha\AppData\Local\Programs\Python\Python311\python.exe' -B tests/collections_runtime/launch_candidate.py --root 'C:/Users/Suprabha/AppData/Roaming/Brotherhood/Colosseum-dltest-collections-fixture-20261030-final' --evidence 'C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/docs/superpowers/handoffs/2026-10-29-collections-gpt6-pro/integration-evidence/completion-20261030' --phase review --pipe CollectionsIntegration-20261030
```

The script records the executable hash, QML manifest, PID, environment isolation and exit code. Use a distinct phase name for another evidence run. Normal guest startup opens the independently verified guest profile. The gated `collections-test-profile` action `activate-local` selects the prepared cross-world verification profile. Do not launch a second instance on the same pipe/root while one is running.

`drive_app.py` sends observed input through the real Lanista bridge. It records every request/reply and captures the actual QQuickWindow. `candidate_window.py` verifies the exact candidate process image before resizing or gracefully closing only its window. Native DPI is retained; the tested monitor reported 144 DPI (150%). Restore a minimized candidate before input. Do not interact with the user's other windows.

The driver allows 30 seconds for transport, extends that allowance for longer explicit server waits, and records command latency. It sends each mutating request once. Use `verify_collections_offline.py --launch-receipt <receipt.json>` to bind the offline check to the current launch's exact executable and PID.

## Acceptance tools

- `verify_collections_app.py --mode core`: actual taskbar, validation, create, staged Save/Cancel/Escape, atomic create-and-add, note/description/name editing, reorder, removal/Undo, guarded Back and delete. It checks persisted native JSON and protected Library/progress hashes. `--mode routes` enumerates the stored references, verifies exact canonical/source/edition identity on the real owner, and exercises its Add picker and Cancel. `--route` scopes a qualification probe.
- `verify_collections_lifecycle.py`: forced atomic-save failure using a temporary handle on only the fixture file; visible error, draft retention, successful retry; actual local/A/B owner swaps; exact notes/order/identity persistence in a separate process after graceful exit. The after phase needs the saved restart receipt. Before rerunning the before phase, remove its named test collections through the UI rather than overwriting them.
- `verify_collections_recovery.py`: corrupt and unsupported schema bytes in the inactive marked A fixture, visible unavailable/error states, retained unknown route, and exact restoration in `finally`. It does not edit the daily profile.
- `verify_collections_viewports.py`: all three integrated surfaces at 1280x672 and 680x640 logical pixels, actual native-DPI resize receipts, and real narrow-window Cancel interactions.
- `verify_collections_offline.py`: verifies the candidate PID/path, creates one temporary outbound-block firewall rule for that executable only, runs real local management, and removes that exact rule in `finally`. Requires administrative Windows access and enabled firewall profiles. Its companion UI script requires the active-rule receipt. This is not a computer-wide network outage test or a provider playback test.

`complete_interrupted_core.py` is an explicitly recorded continuation of a failed acceptance run, not a way to relabel that run as passing. It checks the original saved temporary identities and exact before bytes. Historical failures and corrected reruns remain in evidence. `test_acceptance_navigation.py` tests the observation driver itself: dock toggles, immersive Back, stale clip handles and recreated delegates. It is not counted as production feature verification.

## Interpretation boundaries

The Windows app tests exercise the real shell, real native store, actual input and separate-process restarts. Provider catalogue metadata and generated media are clearly distinguished. Offline local management does not prove fresh online metadata retrieval, downloads, playback or DRM. Android/Linux, installers, updater qualification, shared-branch integration, publishing/sharing and deployment are outside this delivery. See `ROUTE-LIMITS.md` for admitted and unqualified identity cases.

One canonical media/edition membership is idempotent across owning-world aliases. When the native registry intentionally equates Theatre and Vault film identities, the native collection store preserves the first saved reopening reference rather than silently replacing it on a repeated Add. Editions retain separate membership keys.

Two existing initialization effects were measured separately: first profile teardown materializes an empty ProgressStore file, and the old startup download backfill can insert the separately seeded comic into Library. Qualification baselines are taken after those operations; membership changes themselves preserve Library/progress bytes. The original music row was also independently byte-verified across that one-time fixture backfill.
