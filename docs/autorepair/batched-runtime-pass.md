# Guardian Loop — Batched Runtime Pass (the deferred live proofs)

> **What this is.** The Guardian Loop v0 CODE (slices G0–G9) is complete, committed, and proven by
> ~254 deterministic Python tests (`python tests/test_autorepair_*.py`, all green, each with a
> two-directional negative control). What every slice **deferred** is its *live* runtime proof —
> the parts that need a real sandbox cold build, a headless `claude -p`/glm call, a tagged app
> session, or `gh`. This document is the runbook for that one batched pass, to run in a **quiet,
> high-RAM window** (see Machine constraints). Until it runs, each slice's status is
> **Test-reported**, never Runtime-validated.
>
> Ordering ratified by Hemanth (2026-08-14): land verified code fast, batch the heavy live proofs.

## Machine constraints (read first — they shape the whole plan)

See memory `machine_ram_constrained_hours_long_builds`. In short:
- **RAM-bound, not disk-bound.** Measure `FreePhysicalMemory` (`Get-CimInstance Win32_OperatingSystem`),
  not `df`. Observed ~2.85 GB free at idle. A cold build at `-j1` is mandatory unless RAM is freed;
  higher `-j` OOMs (`C1060`/`LNK1102`).
- A `-j1` Qt/WebEngine cold build runs **2–4+ hours** and **exceeds the ~60-minute background-task
  cap** — no single background task finishes it. `ninja` incremental state survives a kill.
- **Two ways to make a build fit one window:** (a) free RAM (close the daily app + heavy apps) and
  build at `-j4`/`-j6` — likely ~20–40 min, fits a window; or (b) a self-resuming Scheduled Task that
  re-runs the resume script every ~55 min until `build()` reports clean, then runs the proofs. Prefer
  (a) in an attended quiet window; use (b) for an unattended overnight run.
- **Build ONE sandbox once and reuse it** across G2/G4/G5/G6/G7 — do not rebuild per proof.
- A **partially-built sandbox is preserved** at `C:\arsbx\g2-live-proof` (correct SHA `353b675`,
  origin removed, `.ninja_log` present). Resuming it (`cmake --build ... -- -j <N>`) continues from
  its compiled objects rather than starting cold.

## Prerequisites before the pass

1. Quiet machine: the daily app closable (to free RAM for `-j4`), no other build running, and no
   other brother mid-commit in the shared repo (a concurrent lane was active 2026-08-14 afternoon).
2. `claude` 2.1.220 headless (`-p`, `--allowedTools`, `--add-dir`, `--model`), `gh` 2.88.1
   authenticated, `glm`/`deepseek` delegates reachable — all probed present at plan time.
3. The guard hook wired into a `.claude/settings.json` PreToolUse entry pointing at
   `scripts/autorepair/hooks/guard.py --sandbox-root <clone>` (deferred by G4; wire it here).

## The deferred live proofs (the checklist)

Each item names the slice, the deferred seam(s), and the pass criterion. The pure logic behind
every one is already green in the deterministic suites — only the live half is owed.

- [ ] **G0 residual (folds into G2).** A fresh clone reaches `ctest -L unit` **42/42** ONLY after the
  runtime DLLs are deployed (mpv `MpvQt.dll`/`libmpv-2.dll` beside the exe; `ffmpeg.exe` + 7 `av*/sw*`
  DLLs into `build-msvc/tools/`) — exactly what `sandbox.provision()` copies. The CRLF signed-fixture
  blocker is already fixed (`109fe69`). Criterion: fresh-clone `-L unit` = 42/42 after `provision()`.
- [ ] **G2 — the sandbox live proof.** `sandbox.create()/build()/provision()`, boot ONE tagged session
  (`lanista session run app_home.json --exe <clone>/…/colosseum.exe --qml <clone>/qml/Main.qml --tag
  arsbxG2`), confirm `appDataRoot` AND `cacheRoot` carry `Colosseum-dltest-arsbxG2`, `destroy()`, then
  `main_drift_check` = MAIN byte-identical. Reuse `C:\arsbx\g2-live-proof`. Also demonstrate the drift
  tripwire firing (hermetic test already does; a live inject-and-detect is optional). → **This is the
  shared sandbox build the rest reuse.**
- [ ] **G4 — triage + the headless-agent probe.** (a) 3 live `reproduce.ps1` runs against the golden
  incident's sandbox → CONFIRMED. (b) INFRA demo: remove a runtime DLL from a scratch copy → INFRA.
  (c) The `claude -p` guard-hook probe: cwd=sandbox, `--allowedTools Read,Grep,Glob`, guard wired —
  assert an in-sandbox read succeeds, an out-of-sandbox read is DENIED by the hook, and the MAIN drift
  tripwire stays silent. **Also verify the plan's assumption** that PreToolUse hooks fire for `-p` and
  `--allowedTools` denies rather than prompts (G4's own "probe this FIRST" note).
- [ ] **G5 — diagnosis.** 1 headless-Opus `diagnose()` run on the golden incident; every cited
  `rootCause.file:line` resolves in the sandbox (citation check passes).
- [ ] **G6 — repair.** Headless-Sonnet `run_repair()` on the golden incident (+ diagnosis): a real
  ≤3-attempt loop producing an ACCEPTED patch with red-then-green proven on real scratch builds
  (test-adds-only → 2× nonzero; +production → 2× zero). Cost: two builds' worth.
- [ ] **G7 — verify.** SECOND pristine sandbox from base+patch; `git apply`; the mechanical gates run
  for real (red/green re-proof, original reproduce now-green, `ctest -L unit`, warning gate, risk-class
  journeys, both `ctest -N` inventory counts, diagnosis∩patch intersection); headless-Opus Verifier
  (context provably free of diagnosis/transcript) + one GLM refutation (advisory). Criterion: a genuine
  independent APPROVE on the golden patch, AND a genuine REJECT demonstrated once.
- [ ] **G8 — promotion.** A real `gh pr create --draft` for the golden incident: a live draft PR URL,
  body carrying all 12 dossier sections, master untouched, guards demonstrated (reject/branch/idempotence).
- [ ] **G9 — the founding end-to-end (D10).** **GATE FIRST (open risk):** G3 found `readerReady`
  resolves decode errors, so a corrupted-archive plant did NOT fail it. The D10 plant is a *code* change
  (`readerReady` bound to reader visibility instead of the page-render signal in `ComicReaderShell.qml`)
  — a different mechanism, but it **must be live-verified to actually produce a red in
  `journey_open_manga`** before burning a cold build on it. If it does not, choose an alternative
  planted regression that reliably reds. Then: local throwaway branch (never pushed, deleted after —
  Hemanth-approved Rule-28 exception), `journey_open_manga` → red run → `orchestrator --from-run` → the
  whole loop → terminal `PROMOTION-READY` (`autonomyLevel: patch-only` so no PR pollutes the repo),
  drift tripwire silent throughout, throwaway branch gone, MAIN byte-identical. Compare the machine's
  fix to the known-correct one in the report (agreement not required; correctness decided by the gates).
- [ ] **Every law failing closed, live:** forbidden-path patch rejected, vacuous bug test rejected,
  tampered verdict/dossier refused, second orchestrator instance refused, escape attempt denied +
  detected. (All proven hermetically; a live pass confirms end-to-end.)

## Final program gate ("Guardian Loop v0 done")

Per the plan: G0 discharged; the founding e2e traversed autonomously to PROMOTION-READY (drift silent,
budgets honored, throwaway branch gone); the golden incident produced a real 12-item draft PR; every
law demonstrated failing closed; every negative control performed both directions; **both ledgers
updated** (`docs/colosseum-test-verification.md` for the new autorepair Python suites,
`docs/colosseum-lanista-verification.md` for any bridge use); G10 (Night Watch trigger) explicitly
outstanding until N0 lands.

## Outstanding beyond the batch

- **G10 (Night Watch trigger)** — blocked on the Night Watch N0 battery (its own plan, unbuilt).
  The moment G0 landed it unblocked N0; G10 stays thin and deferred until N0 exists.
- **Model routing note:** the finished machine's brain is Opus for diagnosis + verify, Sonnet for
  repair (D3, Hemanth-confirmed 2026-08-14) — that is what `policy.json`'s `modelRouting` carries.
- **A candidate enhancement, not v0:** an outer repair↔verify retry loop (a Verifier REJECT currently
  maps to ESCALATE in the orchestrator, handing it to Hemanth rather than auto-retrying repair with the
  verifier's reasons). Decide at/after the founding e2e whether the retry composition earns its place.

## Adversarial audit (2026-08-14) — hardening applied + open limits

Two Sonnet adversaries (containment-escape, fraud-promotion) probed the v0 code. **Code fixes applied**
(each with a regression test): guard-hook git-egress patterns hardened against `git.exe`/path/flags;
Bash path-escape check extended to relative `..`/`cd`/`VAR=`/`--flag=` forms; `WebFetch`/`WebSearch`
denied; forbidden-paths widened to `**/.gitattributes`, `**/.gitignore`, `**/.claude/**` (a nested
`.gitattributes` could otherwise hide a repair's own diff from the PR reviewer — the design's backstop);
the three fail-OPEN Verify gates (`reproduce`/`warning`/`unitTests`) given the bool-not-int guard so
they fail closed; orchestrator resume validators (`_is_valid_verdict_file`/`_is_valid_repair_file`)
strengthened to require the full shape + internal consistency (a thin/tampered `{"decision":"APPROVE"}`
is no longer trusted on resume); `validate_patch_shape` now requires a non-empty production change;
self-protection extended to cover `scripts/autorepair/**`.

**Open items the batched runtime pass MUST handle (architectural, not quick fixes):**
- **ctest bug-tests are inert as a repair proof.** A repair patch cannot register a NEW ctest target
  (`tests/CMakeLists.txt` is forbidden-modify), so an added `tst_foo.cpp` creates no `ctest -R tst_foo`
  target — `ctest -R <stem>` would match pre-existing tests or nothing. **Live bug tests (G6/A4) must
  therefore be `lanista session run <scenario>` where the scenario is a file the patch ADDED under
  `tests/lanista_scenarios/`**, not ctest — or the test-registration policy is reconsidered. Resolve
  before the live repair stage.
- **`main_drift_check` is blind to `.git/` internals and gitignored paths** (working-tree porcelain +
  untracked only). Dormant today (no live stage calls it yet). When wiring the live drift check, also
  snapshot `.git/HEAD`+refs+hooks and consider `--ignored`, or rely on containment-first (the guard hook
  keeps the agent's cwd in the sandbox so it never targets MAIN's `.git/` in the first place).
- **The A5 drive-by-fix gate checks aim, not substance** (cited-file ∩ patch-touched paths; a cosmetic
  edit to the cited file satisfies it). The plan concedes this is the model Verifier's job, backstopped
  by `reproduceNowGreen` (independently-authored ground truth). Consider making the GLM refutation a
  veto (not `advisory`) if higher assurance is wanted before autonomy level C.
- **Dossier completeness is presence-only** (a `"TBD"` section passes) — acceptable, since the dossier
  IS the text Hemanth reads before approving.
- **guard.py stays an honest heuristic for an ERRING agent** (ruling 7's stated threat model), not an
  adversarial one; the human PR gate + the sandbox containment + drift tripwire are the real backstops.
