# Colosseum Guardian Loop — Implementation Plan (v0, autonomy level B)

## Status

**Planned — written on Fable per standing model routing (plan on Fable, execute on Opus), pending
cross-model pressure-test + Hemanth's ratification of the three Open Rulings below.**

- **Author:** Agent 0 Assistant 1 (Claude, Fable 5) · 2026-08-14, from live session ground truth.
- **Spec source:** Hemanth's Guardian Loop design message (2026-08-13) — the loop, the incident
  packet, triage-before-repair, the repair handcuffs, the independent Verifier, autonomy ladder
  A/B/C with **B (draft PR, human promotion gate) chosen and C architected as a policy change**.
  That message IS the approved spec; this plan sequences it. Where this plan deviates from the
  spec's sketches, the deviation is named inline with its reason.
- **Recon base:** Colosseum `master` at `63c70ea` (tree fully green: `ctest -L unit` 43/43,
  six J1 journeys Runtime-validated, store isolation fixed, Slice-3 build regression closed).
- **Execution:** Opus main seat as review gate, Sonnet 5 subagents per slice, one slice per
  session, per `brotherhood-executing-plans`. Status vocabulary and layer matrix as house law.
- **Both capability ledgers read fresh this session** (this author landed their latest entries):
  `docs/colosseum-test-verification.md`, `docs/colosseum-lanista-verification.md`.

## Objective

When the machine finds a failure, the machine reproduces it, explains it, repairs it in a
disposable laboratory, proves the repair adversarially, and hands Hemanth a draft PR with the
whole dossier — **never merging on its own, never touching the daily app or live data, never able
to weaken the instruments that judge it.**

```
failure found → Incident Packet → Triage (reproduce or dismiss) → Diagnosis (why)
→ Repair (sandboxed, handcuffed, ≤3 attempts) → Verify (pristine second sandbox,
independent judge) → Promotion (draft PR + dossier) → Hemanth
```

## Program rulings (hard law, every slice)

1. **The orchestrator owns the laws, not the model.** All policy/budget/forbidden-path files live
   in the MAIN repo and are read by the orchestrator from OUTSIDE the sandbox. The sandbox's own
   copies are never consulted. A model can argue; it cannot re-legislate.
2. **The patient never rewrites the thermometer.** A repair patch may ADD test files; it may not
   MODIFY or DELETE any existing file under `tests/`, the warning allowlist, `scripts/autorepair/`,
   `docs/autorepair/`, or Night Watch files. Enforced mechanically by diff classification (G6),
   not by agent goodwill.
3. **Triage before repair.** No code change is possible until the failure reproduces in a clean
   disposable copy. FLAKY/INFRA verdicts end the incident with a report, never an edit.
4. **Adversarial verification on a different mind.** The Verifier never sees the repair transcript
   or the diagnosis — only incident + base commit + patch + acceptance criteria — runs in a
   pristine SECOND sandbox built from base+patch alone, and runs on a different model than the
   repairer. It is allowed and expected to reject.
5. **The bug must be proven to have existed.** The orchestrator splits every patch into
   test-additions and production-changes: test-only applied → bug test MUST fail (red);
   production applied → MUST pass (green). A repair whose test cannot go red is rejected —
   the house negative-control law, mechanized.
6. **Autonomy ceiling is B.** Success ends at a draft PR on branch `autorepair/AR-<id>` plus
   dossier. No merge, no push to master. `policy.json` carries `autonomyLevel` so C is literally
   a one-line policy change later, and A (`patch-only`, no PR) is available for shakedown.
7. **Containment triad.** (a) REFUSE: headless agents run with cwd pinned to the sandbox,
   whitelisted tools, and a PreToolUse guard hook rejecting path escapes; (b) DETECT: the
   orchestrator snapshots the MAIN repo (`git status --porcelain` + untracked listing) before and
   after every agent stage — any drift aborts the incident as VIOLATION; (c) NEVER-TRUST: the only
   thing that ever leaves a sandbox is a git diff, and the only thing that ever enters the main
   repo is that diff applied by the orchestrator onto an `autorepair/*` branch. Honest threat
   model: v0 defends against an *erring* agent, not an adversarial one — the human PR gate is the
   backstop, which is exactly why the ceiling is B.
8. **Isolated sessions only.** Every app run inside any sandbox is a tagged `lanista session run`
   (the store-isolation fix makes tagged sessions genuinely safe for user data — landed and
   Runtime-validated `833ef21`). Naked `colosseum.exe` launches are refused by the guard hook.
9. **Budgets, not faith.** `policy.json` caps: max 3 repair attempts, per-stage wall-clock,
   per-incident total. Budget exhausted → escalate to Hemanth with the full dossier, stop.
10. **Rule 28 carve-out, ratified by the spec itself.** Hemanth's design message explicitly rules
    that master-only "cannot remain absolute for autonomous repair" and orders the ephemeral
    repair clone. This plan implements exactly that: per-incident local clone, origin removed,
    destroyed after; plus the `autorepair/*` PR branch as the single sanctioned branch class.
11. **No product source changes anywhere in this plan.** Every G-slice is runner-side (Python,
    policy JSON, scripts, tests). The app is never edited by the *program's construction* — only
    ever by a sandboxed repair, and that lands only via a human-gated PR.

## Ground-truth pins (verified live this session, not assumed)

- **The sensory estate the loop consumes exists and is green:** six J1 journey scenarios
  (`tests/lanista_scenarios/journey_*.json`) all Runtime-validated; L1 structural dump
  (`dump-ui`/`ui-query` with clip chains); L2 `layout-verdict`; W0 warning gate
  (`tests/warning_gate.ps1`, allowlist owner+reason); F1 `vault-forensics`; `window-set-state`;
  `get-state` isolation markers; session artifacts under `artifacts/lanista-sessions/<id>/`.
- **Store isolation:** tagged sessions divert Progress/Collection/SearchHistory to files under the
  tagged AppData root; untagged byte-identical (`833ef21`, test `colosseum.qttest.store_isolation`).
- **Fresh cold build is currently BROKEN** for any clone/export: `tests/CMakeLists.txt` references
  two files never committed — `native/installed_chronicle.qrc` (+ `resources/installed-chronicle/`,
  updater lane; the app target genuinely needs it) and `tests/auto/comick/tst_comick_db_url.cpp`
  (untracked AND does not compile against current `ComickCatalogClient.h`; dead work). This gates
  every sandbox build AND Night Watch. G0 fixes it.
- **Cold build cost measured:** ~24 min on this machine (1440 s observed); incremental rebuilds
  minutes. One-build-at-a-time machine law (a running exe locks its binary; two builds in one
  output dir corrupt each other; builds serialize via the tasklist gate).
- **Headless agent surface exists (probed 2026-08-14):** `claude` v2.1.220 supports `-p/--print`,
  `--allowedTools`/`--disallowedTools`, `--add-dir`, `--model`, `--fallback-model`. PreToolUse
  hooks blocking commands pre-execution are an established Claude Code capability (house
  precedent: git-guardrails hooks; encyclopedia pre-commit).
- **`gh` 2.88.1 authenticated** (kingoftheseas56) — draft-PR creation is real.
- **Delegates:** `glm` / `deepseek` MCP tools (single-shot, thinking dial) registered in this
  folder's `.mcp.json`; also drivable over stdio (`node runtime/mcp/brotherhood-delegates/
  server.mjs`) — proven live. Single-shot only: reviewers, never repairers.
- **N1-SDK-Gate verdict stands (`0dbeac5`):** the official MCP Python SDK ships NO Tasks
  extension. The Guardian Loop therefore carries state as FILES under `artifacts/autorepair/<id>/`
  (resumable stage JSONs), no MCP-Tasks emulation.
- **`lanista session run` always passes the qml arg** → the app's self-update `git pull` never
  fires inside a session; sandbox runs are hermetic in that respect too.
- **Fixture fragility law (learned in J1-Ceremony):** git does not preserve mtimes; any fixture
  hard-coding an mtime documents a recovery step. Incident packets embed `reproduce.ps1` with
  exact commands instead of relying on ambient state.

## Decisions

- **D1 — Decoupled from Night Watch.** The incident builder consumes *any failed run directory* —
  Night Watch's, a journey run's, or a hand-made one. So the loop is built and proven on manual +
  synthetic incidents FIRST; the Night Watch trigger is the last, thin slice (G10), blocked only
  on N0 landing. Guardian Loop construction does not wait for Night Watch. G0 unblocks both.
- **D2 — Orchestrator is Python 3 stdlib**, house pattern (`soak-digest.py`, `lanista_coverage.py`
  precedents): `scripts/autorepair/{orchestrator,incident,sandbox,triage,diagnosis,repair_contract,
  verify,promotion,policy}.py`. A per-incident state machine over stage files; resumable; no
  daemon; deterministic control flow in code, model calls only at named points.
- **D3 — Agent invocation = headless `claude -p`**, cwd pinned inside the sandbox, `--add-dir`
  scoped, `--allowedTools` whitelist per stage, PreToolUse guard hook from settings. Model routing
  per house doctrine: **Diagnosis = Opus** (hard reasoning), **Repair = Sonnet** (mechanical,
  diagnosis in hand), **Verifier = Opus** (different seat/context than the repairer) **+ one GLM
  high-thinking single-shot refutation, advisory in v0**. No web/network tools for any stage in v0.
- **D4 — Triage is CODE, not a model** (deviation from the spec's "Triage Agent", named plainly):
  reproduce k times, count, classify. Rerunning and counting is a script's job; a mind enters at
  Diagnosis. Policy default: 3 runs; CONFIRMED = ≥2 failures at the same step; FLAKY = mixed;
  INFRA = boot/session failure before the asserted step. This is the reduction reflex applied —
  the v0 triage *is* "run it again and count," so that is what gets built.
- **D5 — Sandbox = local git clone at the failing SHA** (`git clone --local --no-hardlinks`, then
  `git remote remove origin`, checkout `<sha>`): real .git for trivial diff extraction, zero shared
  refs with the main repo, fast. Provisioned by script: build inputs + runtime DLL deploy
  (windeployqt step + `libmpv-2.dll`/`MpvQt.dll` copy — the exact recipe proven three times in
  worktree builds this session). Destroyed after promotion or escalation.
- **D6 — Patch mechanics.** After repair: `git -C sandbox add -A && git diff --cached` = the patch
  candidate. Classification: paths ADDED under `tests/` = bug tests (≥1 required); paths
  MODIFIED/DELETED under `tests/` or matching `forbidden-paths.json` = mechanical REJECT;
  everything else = production change. Bug test declared by the repair as
  `bugtest.json {cmd, args, expectRedWithoutFix: true}` — orchestrator runs it, exit code is the
  verdict, so a ctest target and a lanista scenario are equally valid bug tests.
- **D7 — Verification builds a SECOND pristine sandbox from base+patch only** — which mechanically
  proves the patch is self-contained (nothing "works only with the repairer's stray file"). Cost
  honesty: two cold builds per incident (repair-base + verify-pristine), ~50 min machine time
  before tests; red/green check needs only an incremental rebuild between test-only and full
  patch. Total incident wall-clock budget defaults to 8 h; caps in `policy.json`, Hemanth tunes.
- **D8 — Evidence layout is the spec's, verbatim:** `artifacts/autorepair/AR-YYYY-MM-DD-NNNN/`
  holding `incident.json, failure.log, journey.json, screen.png, ui-tree.json, warnings.json,
  vault-forensics.json, environment.json, reproduce.ps1` + per-stage outputs
  (`triage.json, diagnosis.json, attempt-N/, verdict.json, report.md`). `artifacts/` is already
  gitignored; the durable record is the PR dossier. Laws live at `docs/autorepair/{policy.json,
  forbidden-paths.json, risk-classes.json}` (spec's layout). Tests follow the repo's FLAT
  convention `tests/test_autorepair_*.py` (deviation from the spec's `tests/autorepair/` sketch —
  repo convention wins, deviation named).
- **D9 — The PR body is the spec's 12-item dossier** (Problem, Root cause, Reproduction, Files
  changed, Why this fix, Negative control, Focused tests, Journey verification, Full regression,
  Warnings, Before/after screenshots, Risk assessment), written in Hemanth-language, no color, no
  emoji, no taglines.
- **D10 — Founding end-to-end bug is PLANTED and KNOWN:** J1-Manga-Seam's own documented negative
  control (bind `readerReady` to reader visibility instead of the page-render signal in
  `ComicReaderShell.qml`) — a real product-code regression with a deterministic journey failure
  (`journey_open_manga`'s `readerReady` wait) and a known correct fix to compare the machine's
  repair against. It lives on a LOCAL throwaway branch used only as the sandbox base SHA, never
  pushed, deleted after (Open Ruling 2).

## Assumptions — Claude to verify at the execution gate

- PreToolUse hooks fire for headless `-p` runs exactly as for interactive sessions, and
  `--allowedTools` denies un-whitelisted tools rather than prompting (G4 probes this FIRST, and
  the slice adapts within its own fence if flags behave differently).
- `git clone --local` + `remote remove origin` behaves as pinned on this Windows/git version
  (G2's hermetic tests prove it).
- `gh pr create --draft` works non-interactively with the keyring auth (G8; fallback if not:
  push the branch + write the PR body to the dossier for Hemanth's one click — promotion then
  reports `Bridge blocked` on the PR step only, honestly).
- Windows `os.utime`/mtime behavior for the planted-bug fixture branch (G9 verifies live).

## Dependency graph

```
G0 (fresh-build health, cross-lane) ─► G2 ─► G4 ─► G5 ─► G6 ─► G7 ─► G8 ─► G9 ─► G10 (blocked on N0)
G1 (laws) ────────────────────────────► G4..G8
G3 (incident builder) ────────────────► G4
```

G1 and G3 are pure-Python and may run before/parallel to G0. Everything agentic (G4+) needs
G1+G2+G3. Builds serialize machine-wide. One executor session per slice.

---

### Slice G0: Fresh-build health — a clean clone builds cold, unblocking every sandbox AND Night Watch
Purpose: make `git clone` + cold configure/build succeed with `BUILD_TESTING=ON`, so disposable laboratories (and N0's nightly export) are possible at all.
Dependencies: **Open Ruling 1 (Hemanth's cross-lane go)** — the two files belong to other lanes.
Implementation guidance: commit `native/installed_chronicle.qrc` + `resources/installed-chronicle/` (build-load-bearing, additive; updater lane's content, landed verbatim); for `tests/auto/comick/tst_comick_db_url.cpp` — it is untracked AND does not compile — either its owner repairs and commits it, or the dead `tst_comick_db_url` target block is removed from `tests/CMakeLists.txt` (code wins; a target referencing a nonexistent, non-compiling file is dead weight). Declare-first on `agents/chat.md`; explicit pathspec; `git show --stat HEAD` sweep-proof.
Behavior to preserve: the existing warm `native/build-msvc` keeps building exactly as today; no target other than the dead one changes; the updater lane's uncommitted `native/CMakeLists.txt` hunk stays untouched.
Baseline: reproduce today's failure — `git clone --local` to a temp dir, cold configure with `BUILD_TESTING=ON` → `Cannot find source file` on both paths; preserve the log.
Focused tests:
  - Qt Test: not applicable — build-system health, no C++ contract changes.
  - Qt Quick Test: not applicable.
  - Existing harnesses: after the fix, `ctest --test-dir <fresh-build> -L unit --output-on-failure` on the FRESH clone's build = 43/43 (or 42/42 if the dead target's registration is removed — count pinned in the report).
  - Negative control: temporarily delete the newly committed `.qrc` from the fresh clone → cold configure fails again on exactly that file; restore → clean. Proves the fix is the load-bearing element.
Test seam status: available.
Lanista actions: none — no app behavior in scope.
Completion signal: cold configure+build from a pristine local clone exits clean with zero `error C[0-9]|error LNK|ninja: build stopped` matches (grep-verified, exit codes lie).
State / events / probes: build log grep result; fresh-clone `ctest -N` inventory count.
Visual evidence: not applicable.
Regression paths: main-tree incremental build still clean; `test_lanista.ps1` still green.
Evidence artifacts: `artifacts/autorepair/g0/{clone-coldbuild-before.log, clone-coldbuild-after.log, fresh-ctest.log}`.
Bridge status: not applicable.
Completion criterion: internal slice done — pristine-clone cold build + fresh-clone unit gate green, both logs preserved. This simultaneously discharges Night Watch's standing blocker (note it on chat.md).

### Slice G1: The laws — policy, forbidden paths, risk classes, and a loader that fails closed
Purpose: give the orchestrator its constitution as data, so every later stage can refuse mechanically instead of arguing with a model.
Dependencies: none.
Implementation guidance: `docs/autorepair/policy.json` (schema v1: `autonomyLevel: "draft-pr"`, `maxRepairAttempts: 3`, triage `{runs: 3, confirmThreshold: 2}`, per-stage + per-incident wall-clock caps, model routing per D3), `docs/autorepair/forbidden-paths.json` (MODIFY/DELETE guards: `tests/**`, `docs/autorepair/**`, `scripts/autorepair/**`, `scripts/night-watch*`, `tests/lanista-warning-allowlist.json`, both verification ledgers, `.github/**`, encyclopedia `*-state.json`; ADD-under-`tests/` exempt — that is the bug-test door), `docs/autorepair/risk-classes.json` (failing-area → required verify set; v0 default: full `-L unit` + all six journeys + warning gate), `scripts/autorepair/policy.py` — closed-schema loader: unknown key → refuse; missing required → refuse; **the forbidden list must include its own three files** (self-protection asserted by a test).
Behavior to preserve: nothing existing — all files are new.
Baseline: none exists; record that absence.
Focused tests:
  - Qt Test: not applicable — Python/JSON only.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_policy.py` (stdlib unittest, house flat convention): schema green on shipped files; unknown-key refusal; missing-field refusal; self-protection presence.
  - Negative control: corrupt a temp copy of `policy.json` (`autonomyLevel: "merge"`, not in the enum) → exactly the enum-validation case red; restore → green.
Test seam status: available.
Lanista actions: none.
Completion signal: `python tests/test_autorepair_policy.py` exit 0.
State / events / probes: loader outputs on good/bad fixtures.
Visual evidence: not applicable.
Regression paths: none — additive.
Evidence artifacts: test log under `artifacts/autorepair/g1/`.
Bridge status: not applicable.
Completion criterion: Test-reported, all cases + negative control green.

### Slice G2: The laboratory — sandbox create/build/diff/destroy + main-repo drift tripwire
Purpose: give every later stage a disposable Colosseum that cannot leak back into the real one.
Dependencies: G0 (cold build must work), G1 (paths/policy for the drift + diff rules).
Implementation guidance: `scripts/autorepair/sandbox.py` — `create(sha)`: `git clone --local` into `artifacts/autorepair/<id>/sandbox/`, `git remote remove origin`, checkout sha, provision (runtime DLL deploy recipe as pinned); `build()`: serialized behind the machine-wide build gate (tasklist check), grep-verified log; `extract_patch()`: `add -A` + `diff --cached` + D6 classification into `{testAdds, forbidden, production}`; `main_drift_snapshot()/check()`: MAIN repo `git status --porcelain` + untracked listing before/after any agent stage, mismatch → VIOLATION; `destroy()`.
Behavior to preserve: the main repo — the entire point. Also: never `clean`/`stash`/`reset` anything in MAIN.
Baseline: none (new module); record cold-build duration for the budget table.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_sandbox.py` — hermetic mini-git-repo fixtures (pattern proven by `test_precommit_coverage_dispatch.py`): create→no-origin asserted; patch classification (add-under-tests vs modify-test vs forbidden vs production) on canned diffs; drift detector catches an injected main-side file.
  - Negative control: hand `create()` a sandbox where origin removal is skipped → the origin-present assertion refuses to proceed; restore → green.
Test seam status: available.
Lanista actions: none in the module tests. One LIVE proof: create a real sandbox at `master`, build it, boot ONE tagged session from it (`lanista session run` `self_smoke.json` equivalent from the sandbox exe), stop, destroy — proves the laboratory produces a runnable app.
Completion signal: hermetic suite exit 0 + the live sandbox's session `session.json` shows tagged `appDataRoot` and graceful exit.
State / events / probes: `get-state` isolation markers on the live proof; drift snapshots byte-identical.
Visual evidence: not applicable.
Regression paths: main repo `git status` before/after the whole slice — identical.
Evidence artifacts: `artifacts/autorepair/g2/{sandbox-selftest.log, live-proof-session/, coldbuild-duration.txt}`.
Bridge status: available (session run is ledgered).
Completion criterion: Runtime-validated for the live sandbox proof; the drift tripwire demonstrated firing on an injected change.

### Slice G3: The incident packet — from a failed run directory to an agent-grade bug report
Purpose: turn "Vault broken" into "on commit X, step 37 failed: expected A, got B — here is the tree, the logs, the state, and the exact command that reproduces it."
Dependencies: G1 (schema home). Independent of G0/G2 — consumes run dirs from the CURRENT tree.
Implementation guidance: `scripts/autorepair/incident.py --from-run <artifacts/lanista-sessions/id>` → `artifacts/autorepair/AR-<date>-<nnnn>/` with the D8 file set: `incident.json` schema v1 (id, baseSha, scenario path, seed, tag, failing step index/label, expected vs got, exit code), copied `failure.log`/`stdout`/`stderr`/`colosseum.log`, the scenario JSON, grabs found in the run dir, warning-gate verdict (invoke W0 on the captured logs), `environment.json` (HEAD, dirty-file list, toolchain versions), and a generated `reproduce.ps1` (the exact `lanista session run … --seed … --tag … --exe … --qml …` line). `vault-forensics.json` only when the scenario touches Vault surfaces (risk-classes mapping). `ui-tree.json`: best-effort from run artifacts; live `dump-ui` requires a live session — record honestly as absent for post-mortem packets.
Behavior to preserve: run directories are read-only inputs — never mutated.
Baseline + golden fixture: produce a REAL failed run dir cheaply by re-running J1-Manga's documented negative control (corrupt the seed's archive entry → `readerReady` wait times out — proven red this session), then build the packet from it; commit the packet-shape fixture (paths/mtimes normalized) as the golden contract.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_incident.py` — golden-fixture equivalence; missing-reproduce-command refusal; malformed run dir → clean error, never a half-packet.
  - Negative control: delete `session.json` from a temp copy of the run dir → the builder refuses with a named error; restore → green.
Test seam status: available.
Lanista actions: the one seeded NC run to mint the golden fixture (isolated tagged session, restore the seed after — both directions preserved, exactly as J1-Manga's NC did).
Completion signal: builder exit 0 on the golden run dir; packet validates against schema.
State / events / probes: packet field-by-field vs golden.
Visual evidence: the failing step's grab copied into the packet (exhibit, not gate).
Regression paths: builder on a PASSING run dir → refuses ("no failure to report"), by design.
Evidence artifacts: `tests/fixtures/autorepair/golden-incident/` (durable), `artifacts/autorepair/g3/` (ephemeral).
Bridge status: available.
Completion criterion: Test-reported + the golden packet minted from a genuinely red run.

### Slice G4: Triage — reproduce or dismiss, and the headless-agent probe
Purpose: no repair ever starts from a ghost — the failure must reproduce in a clean laboratory, and this slice also proves the headless-agent containment mechanics the later stages depend on.
Dependencies: G1, G2, G3.
Implementation guidance: `scripts/autorepair/triage.py` — build (or reuse) the incident's sandbox at `baseSha`, run `reproduce.ps1` k=3 times (policy), classify per D4 into `triage.json {verdict: CONFIRMED|FLAKY|INFRA, runs: [...], failingStepConsistency}`. **No model call in triage (D4).** SECOND deliverable — the headless probe, because this is the first slice that needs it proven: a scripted `claude -p` invocation with cwd=sandbox, `--allowedTools` limited to Read/Grep/Glob, a PreToolUse guard hook, and a canned prompt ("read file X inside sandbox; then attempt to read <main-repo path>") — assert the in-sandbox read succeeds, the escape is refused by the hook, and the drift tripwire stays silent. If the flags/hook behave differently than assumed, adapt INSIDE this slice and record the true mechanics in the plan's assumption ledger.
Behavior to preserve: PID hygiene — never touch a colosseum.exe the stage didn't launch.
Baseline: the golden incident (G3) — a CONFIRMED classification is the expected outcome.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_triage.py` — verdict math on canned run-result sets (3/3 fail = CONFIRMED, 1/3 = FLAKY, boot-failure = INFRA); guard-hook script unit cases (escape path → deny, sandbox path → allow).
  - Negative control: feed triage a run set where the failing STEP differs across runs → FLAKY, not CONFIRMED (consistency matters, not just failure count); restore → CONFIRMED.
Test seam status: available.
Lanista actions: the three live reproduce runs (tagged sessions from the sandbox exe); an INFRA demonstration by removing a runtime DLL from a scratch sandbox copy → INFRA verdict, restore.
Completion signal: `triage.json` written with CONFIRMED on the golden incident; probe transcript shows allow/deny exactly as designed.
State / events / probes: per-run failing step labels; probe hook decisions.
Visual evidence: not applicable.
Regression paths: a PASSING scenario run through triage → verdict "NOT-REPRODUCIBLE-AS-FILED", incident closed without repair.
Evidence artifacts: `artifacts/autorepair/g4/{triage-golden.json, probe-transcript/, infra-demo.json}`.
Bridge status: available.
Completion criterion: Runtime-validated (live reproduces + live probe), both negative controls performed.

### Slice G5: Diagnosis — why, with citations, before any edit
Purpose: separate "what happened" from "how to change the code," in writing, so the repair is aimed and the verifier can later judge intent against outcome.
Dependencies: G4 (CONFIRMED incidents only).
Implementation guidance: `scripts/autorepair/diagnosis.py` — headless Opus, read-only tools (Read/Grep/Glob), `--add-dir` = sandbox + the incident dir + `docs/encyclopedia/` (read the subsystem guide first — house law), NO Bash, NO web. Output contract `diagnosis.json {observed, expected, rootCause: {file, line, claim}, seam, confidence: high|medium|low, proposedRepair, wouldNeedForbiddenChange: bool}` — schema-gated by the orchestrator; **every cited file:line must exist in the sandbox** (citation check, the F0-contract pattern); `wouldNeedForbiddenChange: true` → incident escalates to Hemanth instead of proceeding (the stop-law reflex, mechanized).
Behavior to preserve: sandbox is read-only in this stage — drift tripwire on the SANDBOX too (no edits before Repair).
Baseline: the golden incident's known root cause (the corrupted seed / planted binding) — diagnosis must land on the real seam to be judged useful.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_diagnosis.py` — schema gate; citation-check refusal on a diagnosis naming a nonexistent file; forbidden-escalation path on `wouldNeedForbiddenChange`.
  - Negative control: canned diagnosis JSON with `confidence: "certain"` (not in enum) → refused; corrected → accepted.
Test seam status: available.
Lanista actions: none (evidence-based stage; the packet already carries runtime evidence).
Completion signal: schema-valid, citation-clean `diagnosis.json` for the golden incident.
State / events / probes: citation check results; sandbox drift snapshot unchanged.
Visual evidence: not applicable.
Regression paths: low-confidence diagnosis → orchestrator records it and STILL proceeds to repair only if policy allows (`minConfidenceToRepair: medium` default) — the refusal path exercised once.
Evidence artifacts: `artifacts/autorepair/g5/{diagnosis-golden.json, citation-check.log}`.
Bridge status: not applicable.
Completion criterion: Test-reported + one live Opus diagnosis run on the golden incident whose citations all resolve.

### Slice G6: Repair — handcuffed edits, mandatory bug test, mechanical red/green
Purpose: produce a patch that fixes the cause and carries its own proof — under handcuffs that make thermometer-tampering mechanically impossible.
Dependencies: G5.
Implementation guidance: `scripts/autorepair/repair_contract.py` (pure: D6 classification + bugtest contract validation — unit-testable without any agent) and the repair stage in the orchestrator: headless Sonnet, cwd=sandbox, tools Read/Grep/Glob/Edit/Write + Bash-under-guard-hook (needed for builds/test runs inside the sandbox), prompt = incident + diagnosis + the contract ("you must ADD a test that fails without your fix; you may not modify existing tests; forbidden paths listed"). Attempt loop ≤ `maxRepairAttempts`; each retry receives the prior verifier/contract rejection verbatim. After each attempt: extract patch → classify → REJECT on any forbidden/modified-test path → mechanical red/green (D6/ruling 5): pristine scratch export + testAdds only → bugtest cmd MUST exit nonzero; + production changes → incremental rebuild → MUST exit 0.
Behavior to preserve: MAIN repo untouched (tripwire around every attempt); the sandbox's own git history is the attempt ledger (commit per attempt).
Baseline: the golden incident + its diagnosis.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_contract.py` — canned malicious patches, no agent needed: modifies an existing test's expected value → REJECT; touches `docs/autorepair/policy.json` → REJECT; no test added → REJECT; clean patch shape → ACCEPT.
  - Negative control: canned patch whose added bug test PASSES even without the production fix (a vacuous test) → the red-check rejects it — the exact class of fake proof this program exists to kill; restore a genuine fixture → accepted.
Test seam status: available.
Lanista actions: whatever the bug test commands are (ctest target and/or scenario run) — executed by the orchestrator, in the sandbox, tagged.
Completion signal: an ACCEPTED patch for the golden incident with red-then-green proven mechanically, within ≤3 attempts.
State / events / probes: classification tables per attempt; red/green exit codes.
Visual evidence: not applicable.
Regression paths: budget exhaustion path exercised once with an impossible canned task (attempts=0 policy override) → clean escalation report, no patch.
Evidence artifacts: `artifacts/autorepair/g6/{attempt-*/patch.diff, contract.log, redgreen.log}`.
Bridge status: available.
Completion criterion: Runtime-validated for the golden repair (real agent, real red/green); all canned-contract cases green.

### Slice G7: Verify — the independent judge in a pristine second laboratory
Purpose: the repairer never grades its own work — a different mind, in a clean room, with only the incident, the patch, and the bar to clear.
Dependencies: G6.
Implementation guidance: `scripts/autorepair/verify.py` — orchestrator: SECOND sandbox from `baseSha`, `git apply` the patch (apply-failure = automatic reject: not self-contained), build, then the mechanical gates: bug test red/green re-proven, the ORIGINAL failing reproduce.ps1 now exits 0, `ctest -L unit` full, warning gate on every session's logs, journey set per risk-classes. Then the Verifier agent: headless Opus, read-only tools on {incident, patch, verify-sandbox, gate results} — explicitly NOT given diagnosis.json or any repair transcript (ruling 4) — writes `verdict.json {approve: bool, reasons[], riskAssessment}` judging cause-vs-symptom, adjacent-behavior risk, and bug-test meaningfulness. Plus one GLM single-shot refutation (thinking=high) of the patch summary — advisory in v0, recorded in the dossier. Reject → G6 retry loop with reasons; approve → G8.
Behavior to preserve: verifier independence — build the invocation so the repair transcript physically isn't in its context.
Baseline: G6's accepted golden patch.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_verify.py` — gate aggregation math on canned results; apply-failure → reject; the verifier-input builder provably excludes diagnosis/transcript paths (asserted on the constructed context manifest).
  - Negative control: canned patch that passes its bug test but reds one unit test → overall REJECT with the failing target named; clean patch → approve path.
Test seam status: available.
Lanista actions: the original reproduce run + the risk-class journey set, all tagged, from the verify sandbox.
Completion signal: `verdict.json approve: true` for the golden patch with every mechanical gate green.
State / events / probes: gate matrix (unit count, journeys, warnings, red/green) in the verdict.
Visual evidence: before/after grabs of the failing step (from incident + verify run) — dossier exhibits.
Regression paths: the reject→retry loop exercised once end-to-end (canned bad patch → reject → a fixed patch → approve).
Evidence artifacts: `artifacts/autorepair/g7/{verdict-golden.json, gates.log, glm-refutation.txt}`.
Bridge status: available.
Completion criterion: Runtime-validated — a genuine independent approve on the golden patch, and a genuine reject demonstrated.

### Slice G8: Promotion — branch, draft PR, dossier; the human gate
Purpose: hand Hemanth a decision, not a diff — everything he needs to judge the repair in one page, and nothing lands without him.
Dependencies: G7 (approve verdicts only), G1 (autonomyLevel).
Implementation guidance: `scripts/autorepair/promotion.py` — in MAIN repo: `git fetch`, branch `autorepair/AR-<id>` from `baseSha`, apply patch, single commit (dossier trailer + Co-Authored-By), push branch, `gh pr create --draft` with the D9 12-item body assembled from the incident/diagnosis/verdict/gate artifacts. Guards: refuse unless `verdict.approve`; refuse any target branch other than `autorepair/*`; never merge; `autonomyLevel: "patch-only"` skips the PR and writes `PROMOTION-READY.md` instead. gh failure → push branch + body file, report the PR step Bridge blocked honestly.
Behavior to preserve: master untouched; the branch is additive; main working tree restored exactly (branch work via a temp worktree of the MAIN repo for the apply+commit, so the dirty main tree with other lanes' WIP is never checked out over — Rule 28 carve-out scope: `autorepair/*` only).
Baseline: the golden approved patch.
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_promotion.py` — guard cases as canned states: verdict.reject → refuse; branch name "master" → refuse; body assembly contains all 12 sections; patch-only mode writes the file and makes no git call (dry-run seam).
  - Negative control: verdict tampered to `approve: true` with a missing gates section → body assembler refuses (dossier must be complete to promote); restore → green.
Test seam status: available.
Lanista actions: none.
Completion signal: a real draft PR exists for the golden incident (URL captured), body carries all 12 items, base branch untouched.
State / events / probes: `gh pr view` fields; main `git status` unchanged.
Visual evidence: the PR's before/after grabs render (exhibit).
Regression paths: re-running promotion on the same incident → idempotent refuse ("already promoted").
Evidence artifacts: `artifacts/autorepair/g8/{pr-url.txt, pr-body.md}` + the PR itself.
Bridge status: available (gh probed).
Completion criterion: Runtime-validated — the golden draft PR live on GitHub, guards demonstrated.

### Slice G9: The orchestrator + the founding end-to-end run
Purpose: one command takes a failure from incident to draft PR with every law enforced — proven on a planted, known bug.
Dependencies: G4–G8.
Implementation guidance: `scripts/autorepair/orchestrator.py` — the state machine over stage files (resumable: rerun continues at the first incomplete stage), single-flight `owner.lock` (pid/path/creation-time triple, the N0-spec pattern), `--from-run <dir>` and `--incident <id> --resume` CLI, per-stage wall-clock enforcement, VIOLATION/BUDGET/ESCALATE terminal states each producing a Hemanth-language `report.md`. Founding e2e (D10): local throwaway branch planting the `readerReady`-binds-to-visibility regression in `ComicReaderShell.qml` (the seam's own documented negative control); run `journey_open_manga` against a sandbox of that SHA → red run dir → `--from-run` → the whole loop → draft PR (against the throwaway base; PR opened in patch-only mode OR as draft against the branch — pin: e2e uses `autonomyLevel: patch-only` so no PR pollutes the repo with a planted-bug fix; the PR path is already Runtime-validated in G8 on the golden incident). Compare the machine's fix to the known correct one in the report — agreement is not required, correctness is (verifier + gates decide).
Behavior to preserve: everything — this is the integration proof; main repo drift zero across the entire run.
Baseline: the planted-bug branch red run (preserved).
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: new `tests/test_autorepair_orchestrator.py` — state-machine transitions on canned stage files; resume-at-stage; lock exclusivity (second instance refused); terminal-state report shapes.
  - Negative control: delete `triage.json` mid-sequence in a canned incident → resume re-runs triage, does NOT skip to diagnosis; restore → continues.
Test seam status: available.
Lanista actions: the full founding run's sessions (all tagged, all from sandboxes).
Completion signal: terminal state `PROMOTION-READY` (patch-only) for the planted bug, with every stage artifact present and every gate green; wall-clock within budget; drift tripwire silent throughout.
State / events / probes: the complete stage-file chain; the budget ledger.
Visual evidence: before/after grabs in the founding dossier.
Regression paths: kill the orchestrator mid-repair (process kill) → `--resume` completes from the interrupted stage — the process-kill resilience this machine has demonstrably needed.
Evidence artifacts: `artifacts/autorepair/AR-<founding-id>/` complete; `report.md` is the deliverable Hemanth reads.
Bridge status: available.
Completion criterion: Runtime-validated — the founding incident traversed find→understand→repair→verify→promotion-ready autonomously, with the throwaway branch deleted after and main master byte-untouched.

### Slice G10: The Night Watch trigger — the last arrow in the circuit
Purpose: a FAILED nightly run opens an incident by itself; the loop becomes the ending Hemanth designed ("FAIL → open autonomous repair case").
Dependencies: **N0-Battery landed (its own plan; currently unbuilt — this slice is BLOCKED until then and stays thin by design)**, G9.
Implementation guidance: in N0's failure path: per failed journey/test, call `incident.py --from-run`, then (policy-gated: `nightWatchAutoRepair: false` initially) optionally launch the orchestrator; always list opened incident ids in the wake `report.md`. No other Night Watch change.
Behavior to preserve: N0's terminal statuses and report contract byte-identical when the policy flag is off.
Baseline: an N0 FAILED report (from N0's own negative control).
Focused tests:
  - Qt Test: not applicable.
  - Qt Quick Test: not applicable.
  - Existing harnesses: extend N0's self-test with `failed_run_opens_incident` and `flag_off_changes_nothing`.
  - Negative control: flag off → byte-identical N0 report vs control run; flag on → incident dir exists.
Test seam status: test blocked until N0 exists — named prerequisite: the Night Watch plan (refresh pending as its own document).
Lanista actions: none beyond N0's own.
Completion signal: one real nightly FAILED → incident opened autonomously.
State / events / probes: report.md incident listing; incident dir schema-valid.
Visual evidence: not applicable.
Regression paths: BLOCKED nightly (daily app open) opens no incident.
Evidence artifacts: the nightly run dir + the opened incident.
Bridge status: bridge blocked — prerequisite: N0-Runner + N0-Battery.
Completion criterion: Runtime-validated on a real scheduled failure, flag-off control proven identical.

---

## Final program gate — what "Guardian Loop v0 done" means

1. G0 discharged: a pristine clone cold-builds green (Night Watch's blocker dead as a side effect).
2. The founding end-to-end (G9): a planted real regression traversed the full loop autonomously to
   PROMOTION-READY, drift tripwire silent, budgets honored, throwaway branch gone.
3. The golden incident (G3→G8): a real draft PR on GitHub carrying the complete 12-item dossier.
4. Every law demonstrated failing closed: forbidden-path patch rejected, vacuous bug test rejected,
   tampered verdict refused, second orchestrator refused, escape attempt denied + detected.
5. Every negative control in G0–G9 performed both directions and preserved.
6. Ledgers updated (both), and `docs/autorepair/` carries the laws the orchestrator actually reads.
7. G10 explicitly outstanding until Night Watch lands — the program is honest about its missing arrow.

## Open Rulings for Hemanth (the plan proceeds on his word, not before)

1. **G0 cross-lane go:** commit the updater lane's `installed_chronicle.qrc` (+ resources) and
   fix-or-remove the dead comick test target. Both are other lanes' files; Agent 0 executes with
   declare-first only on your explicit yes.
2. **The planted-bug throwaway branch (G9):** a LOCAL-only branch carrying a deliberate regression
   as the founding e2e fixture, never pushed, deleted after. Rule 28 exception, narrow and named.
3. **Ratify autonomy level B as the shipped default** (`draft-pr`), A available for shakedown,
   C reachable later only by your explicit policy change — exactly your design message's ladder.

## Rejected / deferred (named, not silent)

Autonomous merge (level C) — deferred to a future policy decision after B has a track record.
MCP-Tasks emulation — rejected (SDK gap is real; files are the state). Network/web tools for any
repair-loop agent — rejected in v0. ccache or any new toolchain dependency to cheapen builds —
deferred until the two-cold-builds cost is proven painful in practice. Multi-incident parallelism —
rejected in v0 (one machine, one build at a time; the lock enforces it). Triage as an LLM —
rejected in v0 (D4; a script that counts is honest and free). Registry hand-edits — rejected
forever. Modifying existing tests by repair patches — rejected forever (ruling 2).

## Pressure-test amendments (GLM-5.2 adversarial pass, 2026-08-14) — BINDING

The plan was refuted by GLM at high thinking depth immediately after authoring; findings were
triaged by Agent 0 against the plan's actual text. The following are **binding amendments** —
where an amendment conflicts with a slice body above, the amendment wins:

- **A1 (D5/G2) — no shared object store, ever.** Sandbox clones use `--local --no-hardlinks`
  (a hardlinked pack rewritten by a sandbox `git gc`/`repack` could corrupt the MAIN repo's
  object DB — the working-tree drift tripwire cannot see that layer). The guard hook additionally
  denies `git gc|repack|prune` inside sandboxes.
- **A2 (G4) — path canonicalization before every guard decision.** The hook resolves
  `GetFullPathName` + realpath (junctions/reparse points/`.lnk`), strips `\\?\` prefixes, and
  case-folds before matching (NTFS is case-insensitive; `Tests\` must match `tests/`). G4's probe
  adds a junction-escape case. The hook also denies egress binaries (`curl`, `wget`,
  `Invoke-WebRequest`, `git fetch|push|pull`, `pip`, `npm`) and absolute reads under
  `%USERPROFILE%` — the no-network v0 ruling, mechanized.
- **A3 (G6) — patch classification on canonical case-folded paths** (same rule as A2).
- **A4 (G6) — bug-test command is template-constrained, not free-form:** it must be
  `ctest -R <a-test-the-patch-ADDED>` or `lanista session run <a-scenario-the-patch-ADDED>` —
  the declared command is validated against the patch's added paths, so the agent cannot point
  the proof at something it controls elsewhere. Red and green are each proven **2/2 runs** (kills
  timing-lucky proofs). `QML_DISABLE_DISK_CACHE=1` is set for red/green sessions (belt-and-braces;
  tagged sessions already isolate CacheLocation).
- **A5 (G7) — two added mechanical gates + a priming mitigation.** (1) `ctest -N` inventory count
  in the verify sandbox must MATCH the base inventory (± tests the patch added) — kills
  registration-tampering via build files. (2) The diagnosis-cited files and patch-touched files
  must intersect — kills drive-by "fixes" aimed somewhere other than the blamed seam. (3) The
  Verifier agent's primary exhibit is a comment-stripped production diff (raw patch remains in its
  sandbox) — reduces narrative priming; full identifier anonymization is DEFERRED (named, not
  silent). Acceptance criteria remain derived from the incident + policy only, never from
  diagnosis.json — now stated as an assertable property of the verifier-context builder.
- **A6 (G8) — HEAD drift handling.** Promotion attempts a rebase of the patch onto current
  `master`; on conflict it keeps the base-SHA branch and the PR body opens with a prominent
  NEEDS-REBASE flag — the human gate decides, never silent staleness.
- **A7 (G3/G9) — incident dedup.** `incident.json` carries a fingerprint (scenario + failing step
  + expected/got); opening a duplicate of an OPEN incident is refused with a pointer — no
  five-copies-of-one-flake mornings.
- **A8 (G1) — forbidden list widened; patch-size budget.** Forbidden MODIFY/DELETE additionally
  covers `.gitattributes`, `.gitignore`, `.claude/**`, and the guard-hook scripts themselves.
  `policy.json` gains `maxPatchLines` (default 400) — oversized patches escalate to Hemanth
  instead of promoting. `native/CMakeLists.txt` stays repairable (a legitimate fix may need it)
  but any build-file change is flagged HIGH-RISK in the dossier; the A5 inventory gate is the
  compensating control against target deletion.
- **A9 (G9) — Windows process hygiene.** Stage timeouts kill the full process tree
  (`taskkill /T /F`), never bare `.kill()` (orphaned `cl.exe` holds `.obj` locks for the next
  build). Operational notes: Defender exclusion for the sandbox root (snapshot races produce
  false VIOLATIONs otherwise); keep sandbox roots short (MAX_PATH).
- **Recorded misreads (so nobody re-litigates):** GLM's "no regression suite runs" and "green =
  build exit code" findings were artifacts of the compressed review prompt — the plan already
  orders the full `-L unit` gate, the risk-class journey set, the warning gate, and green =
  bug-test exit 0, plus the original failing repro as a hard gate (which also disposes of its
  entry-point-swallowing and temp-flag scenarios). Its "headless can't reproduce GUI bugs" does
  not apply — all repro sessions here are windowed by house law.
- **Deferred, named:** identifier-anonymized diffs for the verifier; post-merge auto-revert
  (compensating control: Night Watch retests every new HEAD nightly — the loop's own last arrow);
  FLAKY suppression lists; stage-file ACL hardening.

## Handoff

Execution begins only after: (a) the cross-model pressure-test of this plan (delegates/Opus), and
(b) Hemanth's yes on the three Open Rulings. Executor: fresh Opus session under
`brotherhood-executing-plans`, Sonnet subagents per slice, Agent 0 gate on every slice, statuses in
the exact house vocabulary. The plan stands alone: the executor gets this document and the two
ledgers, not this session's memory.
