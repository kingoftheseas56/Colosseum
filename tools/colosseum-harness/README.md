# Colosseum Workbench foundation

This directory is a thin, CLI-first Colosseum engineering harness.

Its job is to answer four practical questions from live repository evidence:

- what checkout/state am I working against?
- what domain or owner is relevant to this change?
- which existing tests actually exist and are registered?
- what verification can be inferred safely from the changed files?

It does not replace Git, CMake, CTest, Lanista, Preflight, a coding agent, or Colosseum's own repository instructions.

It deliberately does not provide an editor, debugger, terminal emulator, Git client, GUI, AI agent, generic plugin system, daemon, telemetry, database, or network service.

The local CLI remains the authority. The MCP bridge stays bounded to the seven semantic operations; run-receipt binding remains host-local CLI work and is not exposed as another MCP tool.

## Internal layout

`colosseum_cli.py` is the compatibility facade and command dispatcher. The implementation is split under `harness/` without changing the CLI or MCP contract:

- `repo.py` — repository/root/process primitives and response envelopes.
- `intelligence.py` — map loading, routing, semantic freshness, and inspection.
- `verification.py` — test discovery, execution, and verification planning.
- `journeys.py` — Lanista scenario discovery and argv construction.
- `receipts.py` — run-receipt creation, persistence, and session binding.
- `evidence.py` — bounded execution evidence and completion gating.
- `context.py` — task-context and Preflight authority collection.
- `cli.py` — parser and output-format helpers.

The split is intentionally internal. Existing callers may continue importing `colosseum_cli`; the facade re-exports the established helper surface used by current tests and integrations.

## Commands

The CLI exposes:

- `status`
- `inspect <domain-or-path>`
- `context-for-task <task>`
- `test <selector>`
- `journeys`
- `journey <name>`
- `verify`

Host-local receipt maintenance additionally exposes:

- `bind-session --run-id <runId> --session <session.json>`

Machine-readable output is available with `--json`. JSON output is deterministic and ASCII-safe.

Tests, journeys, and verification are dry-run by default. Explicit execution requires `--run`.

The intelligence map lives at:

`intelligence\colosseum-map.json`

It is a high-signal routing index, not a complete compiler or QML AST.
## Examples

From `tools\colosseum-harness`:

```powershell
$root = (Resolve-Path "..\..").Path
$map = ".\intelligence\colosseum-map.json"
```

Repository truth:

```powershell
python run.py --root $root status
python run.py --root $root status --json
```

Domain and path discovery:

```powershell
python run.py --root $root --map $map inspect ratings
python run.py --root $root --map $map inspect qml/Main.qml
python run.py --root $root --map $map inspect native/account/SyncEngine.cpp
```
Shared files such as `qml/Main.qml` return ranked candidate domains with routing evidence instead of being assigned to one unexplained owner.

Task context requires a fresh semantic working-tree fingerprint:

```powershell
python run.py --root $root --map $map context-for-task "Fix ratings opening" --path native/account/RatingsReviewsController.cpp
python run.py --root $root --map $map context-for-task "Fix ratings opening" --path native/account/RatingsReviewsController.cpp --record-run
```

A legacy map without semantic freshness metadata remains usable for the original inspection surfaces, but `context-for-task` fails closed with `MAP_STALE` / freshness `UNKNOWN` rather than presenting old architecture as current truth. Semantic freshness is watch-scope based: moving Git HEAD by itself does not stale a semantic map, while a watched file add/change/delete/rename still does. Existing semantic-v1 maps replay their recorded basis HEAD inside the fingerprint so they gain the head-neutral behavior without a map rewrite; semantic-v2 is content-only and remains available for future map refreshes.

`--record-run` requires at least one explicit file `--path`. It writes one small
`artifacts\harness-runs\<runId>\run.json` receipt containing the task, exact
repo/file state and selected existing checks. `bind-session` fills the existing
runtime slot from Lanista. A desktop claim made with that `run_id` appends the
existing observation PNG paths and action-receipt JSON paths to `desktopEvidence`;
it does not copy or redesign those evidence files. `artifacts/` remains ignored by Git.

Run the journey frozen into a recorded run against its already-bound Lanista session:

```powershell
python run.py --root $root journey --run-id RUN_ID --dry-run
python run.py --root $root journey --run-id RUN_ID --run
```

`journey --run-id` uses only the journey frozen when the run receipt was created and the receipt's bound Lanista pipe. It forces attached mode, rejects fresh selector or session overrides, and stores bounded output under `run.json.result.journey` without replacing `result.verification`. Dry-run does not mutate the receipt. Real execution re-evaluates the existing completion gate from receipt evidence.

CTest resolution:

```powershell
python run.py --root $root test colosseum.qttest.sync_engine --dry-run
python run.py --root $root test colosseum.qml --dry-run
```

A CTest dry-run is only successful when the selector exists in source CMake and in the generated `CTestTestfile.cmake` registry. Every generated CTest command includes `--no-tests=error`.

The response shows the selected test, exact argv, registration evidence, and why it was selected.

Verification inference:

```powershell
python run.py --root $root --map $map verify --dry-run
python run.py --root $root --map $map verify --path native/account/RatingsReviewsController.cpp --dry-run
python run.py --root $root verify --run-id <runId> --dry-run
python run.py --root $root verify --run-id <runId> --run
```

Without `--path`, verification keeps the original broad dirty-tree behavior for compatibility. Repeated `--path` arguments create a task-scoped verification boundary so unrelated concurrent dirt is excluded. Scoped map routing requires freshness `FRESH`; stale or legacy map intelligence is ignored instead of borrowed.

When checks are inferred, the result lists each selected check and the changed path/domain reason that selected it. Machine output also exposes `completionReady`; dry-run planning never sets it true.

`verify --run-id` does not infer a new scope. It executes only the selectors frozen
when that run receipt was created, and rejects an additional `--path`. A dry-run
previews those checks without mutating the receipt. `--run` stores the verification
result under `run.json.result.verification` and evaluates the same small completion
gate used by run-bound journey results.

`completionReady=true` requires a valid task/source receipt, a complete bound Lanista
runtime, at least one existing desktop evidence item with no pending, uncertain,
rejected, failed, missing, or changed action evidence, every frozen verification check
passing, and exactly one frozen Lanista journey passing against that bound session/pipe.
Otherwise the receipt remains
false and `completionBlockers` records explicit `RUN_*` blocker codes. Attaching new
desktop evidence invalidates a previously-ready receipt until the existing run-bound
gate is evaluated again. No `--finish`, teardown lifecycle, second verifier, or new MCP
tool is involved. The live response keeps full stdout/stderr; persisted execution output
remains bounded to exit codes, byte counts, SHA-256s, and 4,000-character tails.

## Honest failure states

Unknown or moved path:

```text
inspect: INSPECT_NOT_FOUND
```

The error includes nearby mapped suggestions. For example, the current checkout has:

`native/account/ProfilePreferencesStore.cpp`

not:

`native/profile/ProfilePreferencesStore.cpp`

Future/unimplemented test:

```text
test: TEST_NOT_FOUND
```

For example, `colosseum.qttest.ratings_reviews_delivery` is not reported as runnable until it is actually registered.
No safe verification surface:

```text
verify: NO_VERIFICATION_SURFACE
```

An empty inferred plan is a failure, not a successful-looking green result. The error includes next actions.

Generated registry missing/stale:

- `TEST_REGISTRY_UNAVAILABLE`: source CMake names the test but no generated registry exists.
- `TEST_NOT_REGISTERED_IN_BUILD`: source CMake names the test but the current build does not register it.
- `RUNNER_MISSING`: the selector is registered, but the CTest executable cannot be resolved for real execution.

A stale intelligence-map basis is surfaced explicitly. `context-for-task` and scoped verification fail closed unless the map is `FRESH`; a v0 map with no semantic fingerprint is `UNKNOWN`, not authoritative. Current source remains the authority.
## Ratings & Reviews discovery

`inspect ratings` reflects the current read-only target checkout, including dirty working-tree implementation when it exists.

After the Arc 49 Slices 1–2 map refresh (2026-09-24), the domain maps the uncommitted working-tree implementation:

- owners: `native/engine/ColosseumTitleIdentityRegistry.cpp` (pair admission, seed pin, deterministic v5 ct1 derivation, comic rejection), `native/account/RatingsReviewsController.cpp`, `native/account/RatingsReviewsStore.cpp`, `qml/ratingsreviews/RatingsReviewsHost.qml` (approved page with the inline private editor), `qml/ratingsreviews/RatingsReviewsAction.qml` (title-detail entry row)
- focused CTests: `colosseum.qttest.ratings_reviews_{journey,store,conversion,delivery,sync}` and `colosseum.qml.ratings_reviews_{journey,delivery,conversion}`
- Lanista journeys: `ratings-reviews-frieren-production` (runtime-validated path) plus the two test-build delivery fixture journeys

Supporting integration seams remain:

- `native/account/SyncEngine.cpp`
- `native/account/FirstAccountProfileCoordinator.cpp`
- `native/account/ProfilePreferencesStore.cpp`
- `qml/ShellBackPolicy.js`
- shared composition through `qml/Main.qml`

The harness distinguishes this dirty working-tree state from committed baseline through `status`. Unregistered selectors still fail with `TEST_NOT_FOUND`; `colosseum.qttest.ratings_reviews_delivery` is now registered and mapped because it actually exists. Category entry journeys (anime pivot, book, manga, vault film, comic absence) and the public community service (Slices 3–4) are not mapped because they do not exist yet, and RatingsWrapper stays deliberately unmapped until its API contract lands.
## MCP bridge

Run:

```powershell
.\start_bridge.ps1
```

The bridge exposes the same seven semantic operations through the official Python MCP SDK.

It does not expose arbitrary shell access, filesystem mutation, model execution, or a second copy of Colosseum intelligence.

## Runtime boundary

SWE-smith remains a donor, not a runtime dependency.

Windows/Lanista remains the authoritative runtime proof surface for assembled Colosseum UI behavior.

The harness itself must remain fast, deterministic, local, and bounded.
