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

A legacy map without semantic freshness metadata remains usable for the original inspection surfaces, but `context-for-task` fails closed with `MAP_STALE` / freshness `UNKNOWN` rather than presenting old architecture as current truth.

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

`journey --run-id` uses only the journey frozen when the run receipt was created and the receipt's bound Lanista pipe. It forces attached mode, rejects fresh selector or session overrides, stores bounded output under `run.json.result.journey` without replacing `result.verification`, and keeps `completionReady=false`. Dry-run does not mutate the receipt.

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
result under `run.json.result.verification` while keeping `completionReady=false`.
The live response keeps full stdout/stderr; the small receipt stores exit codes,
output byte counts and SHA-256s, plus bounded 4,000-character output tails.
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

At Workbench R1 verification time, Arc 49 has an uncommitted canonical owner:

- `native/account/RatingsReviewsStore.cpp`
- `native/account/RatingsReviewsStore.h`
- focused CTest `colosseum.qttest.ratings_reviews_store`

Supporting integration seams remain:

- `native/account/SyncEngine.cpp`
- `native/account/FirstAccountProfileCoordinator.cpp`
- `native/account/ProfilePreferencesStore.cpp`
- `qml/ShellBackPolicy.js`
- shared composition through `qml/Main.qml`

The harness distinguishes this dirty working-tree state from committed baseline through `status`. It also keeps the future `colosseum.qttest.ratings_reviews_delivery` honest: that selector remains NOT FOUND until it is actually registered.
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
