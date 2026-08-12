# Agent Visibility — J0 + W0 + FACADE Implementation Plan (gap-riders)

**Status:** awaiting Hemanth's approval. Execute under `brotherhood-executing-plans`.
**Program:** `Brotherhood/agents/preflight-handoff-agent-visibility-workstream.md` (`43a5a63`,
ratified 2026-08-12). This plan covers its first two approved slices — J0 (fixture zoo /
journey contract) and W0 (warning gate) — **plus one addition Hemanth ordered 2026-08-12: the
Lanista MCP facade (Slice F), pulled forward from the ledger's Planned section.** Approving this
plan IS the ruling that reorders it ahead of L1/J1. F0/L1/F1/L2/J1/N0/N1 remain later plans.
**Sequencing law (ratified):** these slices RIDE BETWEEN Vault Browse slices. See "Serialization"
below — it is a hard rule, not a preference.
**Ledgers consulted (fresh, 2026-08-12):** `docs/colosseum-test-verification.md` ·
`docs/colosseum-lanista-verification.md`.

---

## 0. Ground-truth pins (verified 2026-08-12, this session)

- **FIRST ACTION discharge.** The program handoff ordered independent verification of capability
  pins + the F0 ownership premise before planning consumed the scope:
  - F0 premise (Vault owner-thread/DB law): confirmed via the Vault recon Gate 9 pins
    (`VaultIndex.cpp:111-128, 366-459`; `VaultEnricher.cpp:518-536`) and this morning's drift
    re-pin — only the expected `e08424b` set landed since `3c55300`; the law holds at HEAD.
  - `native/engine/AppLog.cpp` (167 lines) + `AppLog.h` (38): **chained** message handler — it
    forwards to the previously-installed handler, so the program's "no second
    qInstallMessageHandler" reject is satisfied by construction. Writes
    `<AppDataLocation>/logs/colosseum.log`, rotating `.1/.2/.3` then dropped. `install()` after
    app identity is set. **Consequence pinned:** under `COLOSSEUM_APPDATA_TAG`, AppDataLocation
    re-roots → every isolated session already produces its OWN warning log. W0 needs zero app
    changes.
  - Seed zoo foundation: `tests/lanista-slice17-seed/vault/` = `config.json` + `identity.json` +
    `index-v1.sqlite` — a complete real-bug seed (the stale-index fusion, 2026-08-11 dossier:
    `Brotherhood/agents/handoff-vault-boot-rederivation-luna.md`).
  - Scenario inventory ON DISK (17): `self_smoke`, `self_visual`, `app_home`, 6 × `update_*`,
    `vault_launch_smoke`, `vault_launch_baseline`, `vault_open_recent`, `vault_door`,
    `vault_identify`, `vault_shelves`, `biblio_covers_pilot`, `biblio_library_empty`.
    **Ledger drift:** the Lanista ledger's scenario list predates the vault/biblio additions —
    J0 updates the ledger as part of landing (its own maintenance law).
- **Cross-workstream collision, named:** `vault_shelves.json` / `vault_door.json` /
  `vault_identify.json` assert the CURRENT Vault shelves face. Vault Browse **Slice 5 retires
  that face** and re-points affected runners (already in that plan). Therefore: J0 does NOT
  author any journey against the current Vault face; Vault journeys for J1 wait until Browse
  Slice 5 lands. J0's own session verification uses boot + door + non-Vault surfaces only.
- **Warning baseline does not exist** (the handoff's "verification required" item). W0's first
  task is to MEASURE it, not assume it.
- **Parallel-executor protocol (this plan runs beside the Vault Browse execution):** the two
  workstreams share one repo and one exe. Fences: this plan's executor touches ONLY
  `tests/lanista-seeds/`, `tests/lanista_scenarios/seed_zoo_smoke.json`,
  `tests/warning_gate.ps1`, `tests/lanista-warning-allowlist.json`,
  `native/tools/lanista-mcp/server.py` (Python, uncompiled), `artifacts/`, the two ledger docs
  (DECLARE on `agents/chat.md` first — the Vault workstream edits them at its Slice 10), and
  `.mcp.json` (shared — declare). NEVER: `native/**` C++, `qml/**`, `tests/auto/**`,
  `tests/fixtures/vault/**`, `tests/CMakeLists.txt`. Before ANY session boot:
  `tasklist | findstr /i "ninja cl.exe link.exe"` must be EMPTY (a running exe locks the binary
  a concurrent Vault build must relink); sessions are short — boot, assert, stop, never parked.
  Git: pull-rebase before every commit, commit+push together, explicit pathspec always.

## Serialization (hard rule for every slice here)

A running `colosseum.exe` LOCKS the exe; a concurrent build in `native/build-msvc` then fails at
link. The Vault Browse workstream owns `native/build-msvc` while its slices execute. Therefore:
- J0/W0 contain **zero native compilation** (fixtures, scenario JSON, PowerShell, docs only).
- Any step that BOOTS a session (J0-V, W0 baseline/negative control) runs **only between Vault
  slice review gates**, when no build is in flight — coordinated by Agent 0, who runs both gates.
- Night Watch's dedicated-output-dir requirement is N0's business, not ours; nothing here touches
  build configuration.

---

### Slice J0: The fixture zoo and the journey contract

**Purpose:** Every real bug's setup becomes a permanent, versioned seed any agent can boot; and
"journey" gets one written contract so J1's six journeys are authored to a standard instead of
each inventing its own.
**Dependencies:** none (rides now).
**Implementation guidance:**
- Create `tests/lanista-seeds/` with one folder per seed: `vault-stale-index-v1/` (PROMOTED copy
  of `tests/lanista-slice17-seed/` — the original stays until its referencing runners re-point;
  no deletion in this slice). Each seed carries a `seed.json` manifest: `{name, version,
  provenance: {bug, date, dossier}, placement: [{content, destination: roaming|local, relPath}],
  expectedOnBoot: [{surface, property, value}]}`. The `placement` field exists because of the
  ledger-documented `--seed` limitation: `session run --seed` reaches only Local
  (GenericDataLocation); Roaming stores (`vault/`, and AppLog's `logs/`) must be pre-placed at
  `<Roaming>/Brotherhood/Colosseum-dltest-<tag>/…` — the manifest makes that mechanical.
- Write `tests/lanista-seeds/README.md` — the **journey contract** (one page, binding for J1):
  a journey = seed (versioned, never live AppData) + scenario JSON + one authoritative
  completion property per phase (strict-equality waitable) + at least one negative control +
  evidence into the session run dir. Admission rule: a new seed is admitted when a real bug's
  diagnosis produces one (the fusion seed is the founding example) — never invented complexity.
- Update the Lanista ledger: scenario inventory (the 17 on disk), the seeds directory, the
  placement mechanism. Same commit.
**Behavior to preserve:** existing runners keep working — `lanista-slice17-seed` untouched;
no scenario edited.
**Baseline:** `lanista session run` against the slice17 seed path today (proves the promoted
copy's `expectedOnBoot` values are the same truth, not new invention).
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — no native/QML change.
  - Existing harnesses: `tests/test_lanista.ps1` (the bridge's own gate) stays green.
  - Negative control: corrupt the promoted seed's manifest `expectedOnBoot` value (wrong count)
    → the J0-V scenario goes red; restore → green. Proves the seed verification can fail.
**Test seam status:** available.
**Lanista actions (J0-V, the seed-boot verification — gap-scheduled):** `session run` a new
scenario `tests/lanista_scenarios/seed_zoo_smoke.json` with the promoted seed pre-placed per its
manifest: wait `bootSplash.visible == false`; assert the manifest's `expectedOnBoot` properties
via `qml-get`/`ui-wait-for` (for the fusion seed: the healed post-`e08424b` state — the boot
republish heals the stale index, so the seed now proves THE FIX holds against the original bug's
data); whole-run manifest to `artifacts/lanista-sessions/<id>/`.
**Completion signal:** each `ui-wait-for` strict equality; scenario exit 0.
**State / events / probes:** the manifest-declared properties, `log-mark` at phase boundaries.
**Visual evidence:** one item-grab of the healed surface (exhibit, not gate).
**Regression paths:** `vault_launch_smoke` + `vault_open_recent` replayed green (shared seed
placement mechanics must not disturb them).
**Evidence artifacts:** session manifests; the seed manifest itself.
**Bridge status:** available (session run, pre-place, qml-get, ui-wait-for — all AVAILABLE).
**Completion criterion:** Runtime-validated — promoted seed boots in an isolated session with
every `expectedOnBoot` assertion green + negative control performed and restored + ledger
updated + regressions green.

### Slice W0: Warnings become verdicts

**Purpose:** A journey or session run FAILS on an unsuppressed Qt warning/critical/fatal and
passes on info — warnings stop being incidental noise only a human notices.
**Dependencies:** J0 (the contract it gates rides on; the baseline session uses J0's mechanics).
**Implementation guidance:**
- **Measure the baseline first (the handoff demands it):** boot a clean isolated session (no
  seed), navigate nowhere, collect `<sessionRoot>/logs/colosseum.log` + the runner's
  `stderr.log`; then one pass through each world tab. Classify every distinct warning line:
  legitimate red vs known-noise. Known-noise entries go into a versioned suppress list
  `tests/lanista-warning-allowlist.json` — each entry `{pattern, owner, reason, date}`; an
  entry without an owner and reason is invalid by schema. (Expected known-noise example: the
  harmless `Cannot load nvcuda.dll` probe seen at every launch.)
- **The gate:** a runner-side parser `tests/warning_gate.ps1` — input: a session's log paths;
  output: `WARNING_GATE_OK` or `FAIL:` with the offending lines; exit code accordingly. House
  sentinel contract, so it composes with every existing `.ps1` runner. No app change, no second
  handler, no live event stream (deferred by the program until demonstrated need).
- Wire it as an OPT-IN step: `test_lanista.ps1` and future journey runners call it on their
  session's logs. Do not retrofit every existing runner in this slice (that is J1's battery
  work); wire exactly one caller as the proof.
- Update BOTH ledgers: the warning gate's existence, command, and the allowlist location.
**Behavior to preserve:** every existing runner green unchanged (opt-in wiring only).
**Baseline:** the measured warning inventory itself — preserved as
`artifacts/warning-baseline/<date>/` (raw logs + classification table). This artifact IS the
handoff's required "current warning baseline."
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — no native/QML change.
  - Existing harnesses: the one wired caller green; `test_lanista.ps1` green.
  - Negative control (mandatory, two-sided): (a) feed the parser a captured log containing a
    real unsuppressed warning → `FAIL` with that line named; (b) add it to the allowlist with
    owner+reason → `WARNING_GATE_OK`. Both outputs preserved.
**Test seam status:** available (parser is new but self-contained; the log sources are pinned).
**Lanista actions (gap-scheduled):** the baseline session + one wired-caller session, both
isolated (`session run`); no Drive needed beyond existing scenario steps.
**Completion signal:** scenario exit codes; parser sentinel.
**State / events / probes:** not applicable beyond the logs (the log IS the probe).
**Visual evidence:** not applicable.
**Regression paths:** the wired caller runs twice (clean pass, then the negative-control pass).
**Evidence artifacts:** `artifacts/warning-baseline/<date>/`; the allowlist file; parser output
logs.
**Bridge status:** available.
**Completion criterion:** Runtime-validated — baseline measured and preserved; gate proves both
red and green honestly; one real runner wired; ledgers updated. **Explicitly NOT claimed:** any
statement about the warning behavior of surfaces the baseline session did not visit.

### Slice F: The Lanista MCP facade v0 — interactive hands

**Purpose:** An agent drives a live, isolated Colosseum the way the Claude Code browser pane is
driven — look, decide, act, look again — through typed MCP tools with real deadlines, instead of
only firing static scenarios. Scenarios remain the regression gates; the facade is for
exploration, diagnosis, and review-gate walking.
**Dependencies:** J0 (seed placement mechanics + something meaty to boot), W0 (the warning log
verdict the `warnings` tool reads). Soft dependencies — ordered after them in this plan.
**Implementation guidance:** pure Python, `native/tools/lanista-mcp/server.py` (already
registered in `.mcp.json`; it is NOT compiled — no build-dir contention). Today it has 3 tools
and **no deadlines anywhere** (ledger-documented flaw). v0 grows it to eight typed tools, every
one shelling the existing `lanista` CLI with an explicit `--timeout` — no naked pipe reads:
- `session_start(seedName?, tag?, drive?)` — spawns `colosseum.exe` from an explicit exe path
  with a UNIQUE `COLOSSEUM_LANISTA_PIPE` (generated; the daily default name is refused
  unconditionally), `COLOSSEUM_APPDATA_TAG`, optional `COLOSSEUM_LANISTA_DRIVE=1`; performs J0
  manifest seed placement (Roaming pre-place); readiness = `ping` until pid match; isolation
  asserted from the app's own `get-state` markers, killing the session on mismatch (same law as
  `session run`). One live session at a time in v0; a `session.json` on disk enables crash
  cleanup.
- `session_stop()` — graceful-then-kill, logs preserved.
- `act(action, target, …)` → ui-click / ui-keypress / ui-text-input / ui-scroll.
- `get(target, props)` → qml-get. `snapshot()` → ui-snapshot. `wait_for(target, prop, value,
  timeoutMs)` → ui-wait-for. `grab(target)` → PNG path + inline image.
- `warnings()` → the session's `logs/colosseum.log` + stderr through W0's allowlist verdict.
The existing 3 tool names keep working. `session run`, the CLI, and the bridge itself are
untouched — this wraps the ONE automation stack; it does not create a second.
**Behavior to preserve:** existing adapter consumers; every ledger law (Drive gating, default-
pipe refusal, isolation asserts, daily app untouchable).
**Baseline:** read and record today's `server.py` surface (3 tools, no deadlines, no session
ownership) before editing.
**Focused tests:**
  - Qt Test / Qt Quick Test / Existing harnesses: not applicable — no native/QML change; the
    facade's proof is its live drive below.
  - Negative control: three, listed under Lanista actions.
**Test seam status:** not applicable.
**Lanista actions (gap-scheduled):** the executing agent itself performs one full interactive
drive through the MCP tools against a seeded isolated session: `session_start` on the J0
founding seed → `snapshot` returns named items → `act` click `taskbarVaultDoor` → `wait_for`
`vaultPage.visible == true` → `get` state properties → `grab` → `warnings()` returns a verdict
→ `session_stop`. **Negative controls:** (a) `session_start` forced onto the default pipe name
→ refused; (b) `act` against a stopped session → clean deadline error, no hang (the fix the
ledger demands); (c) isolation-marker mismatch (tag stripped) → session self-kills.
**Completion signal:** every tool's explicit timeout; the `wait_for` equalities.
**State / events / probes:** as driven; the drive transcript is the record.
**Visual evidence:** the `grab` PNG (exhibit).
**Regression paths:** `lanista session run` on an existing scenario still green (CLI path
undisturbed); the three legacy MCP tool names still answer.
**Evidence artifacts:** drive transcript + grabs + `session.json` under
`artifacts/lanista-sessions/<id>/`; ledger MCP section rewritten (deadline flaw closed) in the
same commit; `.mcp.json`, if touched, is a SHARED file — declare on `agents/chat.md` first.
**Bridge status:** available (wraps only AVAILABLE commands; adds no bridge capability).
**Completion criterion:** Runtime-validated — the interactive drive performed end-to-end by the
executing agent with all three negative controls shown and both ledgers updated. **Note:** MCP
tools load from the Colosseum project config — the verifying chat must be opened at the
Colosseum root (or the server added to the Brotherhood-level MCP config with Hemanth's say-so);
if neither, the CLI-subprocess equivalent drive is the honest fallback, recorded as such.

---

## Plan self-review (performed)

- Both slices: zero native compilation; all Lanista actions in the ledger's AVAILABLE section;
  session-running steps explicitly gap-scheduled behind the Vault workstream's build ownership;
  no sleeps; negative controls on both; baselines defined; evidence locations named.
- Program rejects honored: no second message handler (AppLog chains), no live event stream
  (deferred), no pixel gating (grabs are exhibits), no live-profile fixtures (versioned seeds
  with provenance), no shared build dir (no builds at all).
- Cross-workstream collision with Vault Browse Slice 5 named and fenced (no Vault-face journeys
  authored here).
- Honest limits: W0's allowlist covers only what the baseline session visits; J0 promotes ONE
  seed (the founding fixture) — the zoo grows by the admission rule, not by bulk invention.
