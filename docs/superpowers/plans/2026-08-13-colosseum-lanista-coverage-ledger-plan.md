# Colosseum Lanista Coverage Ledger + Drift Block — Implementation Plan

## Status

**Planned — pending Agent 0 live-code gate.**

- **Recon base:** `kingoftheseas56/Colosseum` `master` at `81b92be52cff72eef2f5bc3e5b90b9eaef1dea55`, observed 2026-08-13.
- **Execution:** master only; no worktrees/branches.
- **Target:** **no meaningful user-visible state is opaque to Lanista**.
- **Boundary:** semantic command → stable surface → pixel observation; no generic QObject/property reflection.
- **Shape:** three slices; no app-wide audit.
- **Preflight status:** no Colosseum files changed, no product compile/run, no runtime verification.

If `master` advances before execution, Agent 0 re-pins and re-checks the load-bearing assumptions below before Slice 1.

## Objective

Make “is this surface drivable by an agent?” a durable machine-readable fact that cannot silently decay.

Port Colosseum’s encyclopedia law rather than inventing a new enforcement model:

1. accepted state is distinct from current state;
2. watched evidence drift becomes `DRIFTED`;
3. drift blocks the existing local pre-commit path;
4. clear by updating the claim + accepting, or re-accepting an unchanged reviewed claim;
5. `git commit --no-verify` remains the emergency door;
6. gaps stay visible; no coverage percentage.

Day-one seed: **Vault Browse only**. Later coverage enters when a family is touched or through findings already owned by Phase 2 `L1-Discovery`.

---

## Decisions

### D1 — What one ledger record means

A record describes one **stable automation contract**, not one arbitrary QML object.

- Unique meaningful surface → one record.
- Repeated delegate → one pattern record (for example `vaultBrowseCard_*`) only when every instance shares the same addressability/capabilities.
- Decorative descendants are not named or ledgered just to increase counts.

### D2 — Closed state vocabulary

Exactly:

- `covered`
- `structurally unreachable`
- `intentionally visual-only`
- `requires OS bridge`
- `blocked`

Rules:

- `covered`: required action/observation is reachable through the approved current Lanista contract and evidence is named.
- `structurally unreachable`: current target/tree/window structure prevents reaching the surface; structural evidence required.
- `intentionally visual-only`: no semantic drive action is required; rationale required.
- `requires OS bridge`: behavior lives outside in-process Lanista; `missingCapability` + evidence required.
- `blocked`: in-process Lanista should own it but lacks a capability; `missingCapability` + evidence required.

### D3 — Ledger and acceptance artifacts

**Planned locations:**

- `docs/lanista-coverage/ledger.json` — canonical classifications.
- `docs/lanista-coverage/<family>.paths` — exact evidence-dependency manifests.
- `docs/lanista-coverage/accepted-state.json` — generated, integrity-protected accepted blobs/digests.
- `scripts/lanista_coverage.py` — schema + accepted/current checker, modeled on `scripts/code_encyclopedia.py`.
- existing `scripts/precommit-encyclopedia-check.sh` — remains the **single local pre-commit entrypoint**; extend it rather than add another hook.

Minimum record:

```json
{
  "id": "vaultBrowseSheetPlay",
  "family": "vault-browse",
  "state": "covered",
  "target": {"kind": "objectName", "value": "vaultBrowseSheetPlay"},
  "capabilities": {"actions": ["ui-click"], "observations": ["qml-get"]},
  "evidence": [
    "docs/colosseum-lanista-verification.md",
    "tests/lanista_scenarios/vault_browse_smoke.json"
  ],
  "missingCapability": null,
  "rationale": "User-operable Play control.",
  "provenance": {
    "acceptedBy": "<agent/person>",
    "acceptedAt": "<UTC ISO-8601>",
    "acceptedAgainstCommit": "<40-char SHA>"
  }
}
```

`target.kind` may be `objectName`, deterministic `pattern`, or `none`. Exact accepted content identity lives in state-file blob hashes; `acceptedAgainstCommit` is provenance context.

The checker must port the encyclopedia’s `--check`, accept-one, accept-all-drifted, integrity-digest, malformed-state fail-closed behavior. It does **not** copy encyclopedia prose harvesting.

### D4 — Drift trigger

For each accepted family, `<family>.paths` contains only exact files supporting its coverage claims: defining QML/component source, scenario/test evidence, and bridge/schema source only where a classification depends on that capability.

Run the coverage gate only when:

1. a staged path intersects an accepted family’s `.paths`; **or**
2. `ledger.json`, that family’s `.paths`, or `accepted-state.json` is staged.

Otherwise coverage checking is a no-op.

When triggered, compare current blobs plus that family’s ledger-record digest with accepted state. Any mismatch is `DRIFTED` and the existing pre-commit entrypoint exits non-zero.

Clear paths:

- claim changed → edit ledger → accept;
- claim unchanged after review → re-accept;
- emergency only → existing `git commit --no-verify`.

**Why this trigger:** it is neither a global `*.qml` gate nor a semantic-diff heuristic. A global gate will be bypassed; a heuristic can miss the unnamed/new-interaction cases this system exists to expose. This protects already-accepted facts with the same conservative blob-drift rule as the encyclopedia.

**False-positive cost:** a non-semantic edit inside an explicitly watched file still requires review/re-acceptance. Keep manifests exact; never use directory/glob coverage. If one multi-purpose QML file causes excessive unrelated drift, narrow/split evidence ownership where the repo permits rather than weakening the gate.

**Known residual gap:** a brand-new surface in an unledgered/unwatched family is not discovered by pre-commit. That is intentional. New facts enter through the naming law, touched-family maintenance, and L1-Discovery. Absence never means `covered`.

### D5 — Naming law lives in `AGENTS.md` only

Use `Colosseum/AGENTS.md`, not both Brotherhood workflow skills. It already owns repo-local “keep the durable map current” doctrine and is visible regardless of orchestration path.

Proposed rule:

> When adding or materially changing a meaningful user-visible interactive/state surface, either give the automation-facing surface a stable world/family-namespaced `objectName` (or deterministic repeated-surface pattern) and ledger it in the same change, or record why its ledger state is not `covered`. Do not name decorative descendants solely for automation.

### D6 — L1-Discovery is consumed, not duplicated

The commission says Phase 2 `L1-Discovery` already owns “unnamed items, binding provenance, model contents.”

- L1 finding → candidate ledger record/dependency.
- Owning family classifies it when that family is touched.
- Missing in-process capability → `blocked`.
- OS boundary → `requires OS bridge`.
- Structural boundary → `structurally unreachable` only with current evidence.
- No app-wide discovery pass in this program.

**ASSUMPTION — Claude to verify:** L1 produces a durable finding set.  
**Fallback:** consume Agent 0’s final L1 report/commit evidence; do not recreate discovery.

---

## Evidence / Uncertainty

### Confirmed at recon base

- `AGENTS.md` says the local encyclopedia hook blocks drift, supports update+re-accept or re-accept-only, and retains `git commit --no-verify`.
- `scripts/code_encyclopedia.py` implements ACCEPTED vs CURRENT, Git blob hashes, integrity state, `--check`, `--accept`, and `--accept-all-drifted`.
- `native/devtools/LanistaServer.cpp` resolves stable targets by object name or snapshot handle and synthesizes `ui-click` with left-button press/release only. The commission’s right-click/context-menu example is therefore a valid `blocked` worked example until separately approved bridge work changes that capability.

### Reported / must be re-verified

- `VaultIdentifyDialog` Popup itself cannot be resolved through the bridge item tree although children can.
  - **ASSUMPTION — Claude to verify.**
  - **Fallback:** classify from current source/runtime evidence; do not preserve the historical report if behavior changed.
- Exact Vault Browse QML/source membership was not reliably enumerated by Preflight.
  - **ASSUMPTION — Claude to verify.**
  - **Fallback:** trace each seed record to exact current source/test/bridge evidence before accepting `vault-browse.paths`; no broad `qml/` path.
- Existing Python test convention for a new checker was not established.
  - **ASSUMPTION — Claude to verify.**
  - **Fallback:** stdlib `unittest` with hermetic fixture/temp-repo tests.
- No concurrent lane has already created this ledger/checker.
  - **ASSUMPTION — Claude to verify.**
  - **Fallback:** merge into the existing implementation; never create a second ledger/gate.

---

## Responsibility Map

| Responsibility | Authority |
|---|---|
| Surface classification | `docs/lanista-coverage/ledger.json` |
| Family evidence dependencies | `docs/lanista-coverage/<family>.paths` |
| Accepted blobs + ledger digest | `docs/lanista-coverage/accepted-state.json` |
| Validate / compare accepted-current | `scripts/lanista_coverage.py` |
| Commit-time enforcement | existing `scripts/precommit-encyclopedia-check.sh` |
| Bridge capability truth | Lanista source + `docs/colosseum-lanista-verification.md` |
| New-UI addressability law | `AGENTS.md` |
| Existing discovery work | Phase 2 `L1-Discovery` |

No product runtime state is owned by this feature.

## Dependency Graph

```text
Agent 0 live-code gate / re-pin
        |
        v
Slice 1 — ledger + accepted/current checker
        |
        v
Slice 2 — existing pre-commit drift block integration
        |
        v
Slice 3 — Vault Browse seed + naming law + L1 intake
```

No slice requires a product compile.

---

# Work Slices

## Slice 1 — Ledger schema + acceptance engine

**Purpose:**  
Create the machine-readable ledger contract and accepted/current checker without commit blocking yet.

**Dependencies:**  
Agent 0 confirms execution base and no concurrent ledger implementation; read current `scripts/code_encyclopedia.py`. If concurrent work exists, merge rather than duplicate.

**Implementation guidance:**  
Add `ledger.json`, `scripts/lanista_coverage.py`, and generated `accepted-state.json`. Enforce the five states and state-dependent required fields. State stores exact dependency blobs + per-family ledger digest with integrity protection. Port check / accept-one / accept-all-drifted semantics. Acceptance records who/when/HEAD provenance through one path. Do not inspect the running app.

**Behavior to preserve:**  
No encyclopedia behavior change; no Lanista protocol/Read/Drive change; no QML/C++ product change.

**Baseline:**  
Encyclopedia accepted/current behavior is the reference pattern; this coverage gate does not yet exist.

**Focused tests:**  
Unknown state fails; blocked/OS state without `missingCapability` fails; covered without evidence/capability fails; malformed integrity fails; changed watched blob drifts; changed ledger digest drifts; acceptance restores CURRENT.

**Test seam status:**  
Planned Python/CLI seam. **ASSUMPTION — Claude to verify** test convention; fallback stdlib `unittest`.

**Lanista actions:**  
None.

**Completion signal:**  
Synthetic family can be accepted → CURRENT, mutated → DRIFTED, re-accepted → CURRENT.

**State/probes:**  
CLI exit code plus explicit CURRENT / DRIFTED / schema-error output.

**Visual evidence:**  
None; pixel/aesthetic gating is out of scope.

**Regression paths:**  
Existing encyclopedia check remains unchanged; malformed state cannot be hand-edited green.

**Evidence artifacts:**  
Focused test transcript, one deliberate drift failure, restored green run.

**Bridge status:**  
Not applicable.

**Negative control:**  
**NC1 — invalid-state mutation:** change one fixture state from `covered` to `coveredd`; exactly the schema check turns red; restore and rerun green.

**Compilation:**  
**No product compilation.**

**Completion criterion:**  
A fresh agent can validate, accept, drift, and re-accept one synthetic family without product code, and invalid claims cannot enter accepted state.

**Rollback / containment:**  
Before Slice 2, removing the new files restores prior behavior because no hook invokes them.

---

## Slice 2 — Existing pre-commit drift integration

**Purpose:**  
Block silent drift of accepted coverage facts without turning every QML edit into a gate.

**Dependencies:**  
Slice 1 green; inspect current `scripts/precommit-encyclopedia-check.sh` and fresh-clone hook instructions.  
**ASSUMPTION — Claude to verify:** no concurrent hook rewrite conflicts.  
**Fallback:** Agent 0 selects the one shared entrypoint; do not independently add a second pre-commit hook.

**Implementation guidance:**  
Keep `scripts/precommit-encyclopedia-check.sh` as the single entrypoint. Add coverage dispatch using D4’s staged-intersection rule. No `*.qml`/directory globs. Any triggered family blob/digest drift blocks. Output names family/path and exact acceptance action. Preserve existing two clear paths and `--no-verify`. Deleted/renamed watched files fail closed until manifest/ledger are reconciled.

**Behavior to preserve:**  
Encyclopedia drift still blocks; unrelated files and QML outside manifests are not coverage-blocked; no app launch/build/runtime introspection; no new bypass.

**Baseline:**  
Existing hook blocks encyclopedia drift only.

**Focused tests:**  
No overlap → neutral; watched CURRENT → green; watched DRIFTED → red; staged ledger/manifest change without acceptance → red; re-accept → green; unrelated QML outside manifests → green; encyclopedia-only drift still red; both clean → green.

**Test seam status:**  
Hook-level seam. Prefer a hermetic temporary Git repo so staged-intersection behavior is exercised rather than mocked.

**Lanista actions:**  
None.

**Completion signal:**  
One test matrix proves unrelated edit green, watched CURRENT green, watched DRIFTED red.

**State/probes:**  
Staged path set; matched family; accepted/current blobs/digest; hook exit code.

**Visual evidence:**  
None.

**Regression paths:**  
Encyclopedia-only drift; coverage-only drift; clean combined gate; malformed coverage state; removed watched path; unrelated QML.

**Evidence artifacts:**  
Hook transcript with red drift output + green re-accept + encyclopedia regression.

**Bridge status:**  
Not applicable.

**Negative control:**  
**NC2 — watched-blob mutation:** mutate/stage exactly one watched fixture dependency without accepting; the coverage check turns red; restore/re-accept and rerun green.

**Compilation:**  
**No product compilation.**

**Completion criterion:**  
An already accepted family cannot silently drift through a relevant staged change, while unrelated QML remains unaffected.

**Rollback / containment:**  
If coverage dispatch blocks outside matched manifests, revert only that dispatch; never disable/weaken encyclopedia enforcement.

---

## Slice 3 — Vault Browse seed + naming law + L1 intake

**Purpose:**  
Prove the mechanism on one freshly evidenced family, make new UI responsible for addressability, and connect existing L1 findings without an audit program.

**Dependencies:**  
Slices 1–2 green. Agent 0 verifies exact Vault source/test/bridge dependencies, current Vault names, and the L1 durable output. Read current Vault sections of `docs/colosseum-lanista-verification.md` and existing Vault scenario evidence.

**Implementation guidance:**  
Create `vault-browse.paths` from exact load-bearing files only. Seed one record per proven Vault Browse automation contract, including the supplied/current vocabulary where still valid: `vaultBrowseGrid`, `vaultBrowseRail`, `vaultBrowseCrumb`, `vaultBrowseCard_*`, `vaultBrowseSheet`, sheet copy pattern, Play, Back, plus other currently evidenced Browse contracts. Classify each from current evidence. Right-click stays `blocked` only where a real seeded surface requires it. Popup structural status is not accepted until Agent 0 re-verifies it. Add D5’s naming law once to `AGENTS.md`. Add a short maintenance note: touched family updates ledger/manifest; L1 findings are candidate inputs; no audit, percentage, or mass objectName retrofit. Accept Vault only after review.

**Behavior to preserve:**  
No Vault product behavior change; no objectName retrofit in this slice; no bridge/gate change; no right-click implementation; no L1 rerun. Existing scenario behavior stays unchanged unless an evidence reference itself is factually wrong.

**Baseline:**  
Current commission/Lanista ledger reports fresh Vault Browse automation surfaces. Every non-Vault-Browse family is unseeded on day one.

**Focused tests:**  
All seeded records validate, have evidence/provenance, trace to current target/evidence, satisfy blocker rules, and belong to `vault-browse`; accepted Vault is CURRENT; mutating one accepted record creates DRIFTED; absence of other families does not fail merely for absence.

**Test seam status:**  
Static ledger + existing evidence seam. No new app test required. Optional existing-binary replay only if Agent 0 grants exclusive binary ownership.

**Lanista actions:**  
Default none; consume existing evidence. Optional replay uses the existing Vault scenario and signal/property waits only—no sleeps, no new drive capability.

**Completion signal:**  
Vault Browse accepted/CURRENT; naming law appears once in `AGENTS.md`; L1 intake documented; all other families explicitly unseeded.

**State/probes:**  
Coverage-family status, exact manifest/blob membership, ledger validation, existing Lanista evidence references.

**Visual evidence:**  
No new visual gate; existing artifacts may support evidence but pixel/AI aesthetic approval is not introduced.

**Regression paths:**  
Vault ledger CURRENT; context-menu blocker honest if still current; Popup claim not accepted without proof; encyclopedia hook unchanged; Lanista capability ledger not contradicted.

**Evidence artifacts:**  
Accepted Vault records, `vault-browse.paths`, acceptance state, checker/hook green transcript, record→source/evidence mapping; optional replay artifact only if run.

**Bridge status:**  
Mixed/explicit: name/handle targeting and left click are available; right-click is `blocked` until separately approved bridge work; Popup boundary remains **ASSUMPTION — Claude to verify** until confirmed.

**Negative control:**  
**NC3 — accepted-record mutation:** change exactly one accepted Vault record state/required capability without re-accepting; Vault turns DRIFTED/red; restore and confirm green.

**Compilation:**  
**No product compilation.** Optional existing-binary replay only with exclusive ownership.

**Completion criterion:**  
Vault Browse is the first honest accepted family; its claims cannot silently drift; new-UI doctrine and L1 intake make future coverage incremental rather than audit-based.

**Rollback / containment:**  
If exact Vault provenance cannot be established, keep Slices 1–2 and ship **no accepted Vault seed** rather than accepting guessed paths/states.

---

## Day-One Boundary

**Accepted seed: Vault Browse only.**

Everything else is **unledgered**—not a sixth state and not implied coverage. At minimum, do not claim day-one coverage for Tankoban, Biblio, Theatre, other Vault families, shell/window/global chrome, settings/update/download flows, native/OS dialogs/taskbar behavior, or any family not yet supplied by L1.

**ASSUMPTION — Claude to verify:** the example family list still matches live vocabulary.  
**Fallback:** retain the stronger rule: *all families except accepted Vault Browse are unledgered*.

---

## Acceptance / Traceability

| Commission requirement | Slice | Proof |
|---|---:|---|
| one machine-readable record per stable surface contract | 1 | schema tests |
| five states + blocker/OS capability citation | 1 | state-dependent validation + NC1 |
| who/when/commit provenance | 1 | acceptance-state/record test |
| port accepted/current + integrity | 1 | blob/digest drift tests |
| precise non-global trigger | 2 | watched vs unrelated staged-path tests + NC2 |
| block-not-warn + re-accept + existing bypass | 2 | hook matrix |
| Vault-only seed, no audit | 3 | accepted family boundary + NC3 |
| naming law in one place | 3 | `AGENTS.md` only |
| consume L1, do not duplicate | 3 | intake note; no discovery task |
| one build dir / binary constraint | all | zero required compile; optional replay gated |
| waits are signals | 3 | no sleeps |
| no reflection / second stack / % / mass retrofit / gate change | all | scope + diff review |

---

## Risks / Stop Conditions

Stop and return to Agent 0 rather than improvising if:

1. `master` drift or concurrent work invalidates this plan.
2. The existing pre-commit entrypoint cannot be extended without weakening/duplicating encyclopedia enforcement.
3. The trigger would require broad `qml/**` watching.
4. Classification would require unrestricted reflection or a Read/Drive gate change.
5. This program starts duplicating L1 discovery.
6. Vault seed provenance cannot be tied to exact source/evidence paths.
7. Right-click coverage would require implementing mouse-button support inside this program.
8. Required runtime verification would contend for the single binary/build directory without Agent 0 ownership.
9. A product decision is needed to decide whether a surface is intentionally visual-only or should be driveable.

False-positive safeguard: exact family manifests only.  
False-negative safeguard for new/unledgered surfaces: `AGENTS.md` naming law + touched-family maintenance + L1 intake.  
State-integrity safeguard: generated digest; malformed/hand-edited state fails closed.

---

## Verification Before Handoff

- **Request fidelity:** three slices; no audit; Vault-first; one naming-law owner; L1 consumed.
- **Evidence:** repo-confirmed claims are pinned to recon base; Popup, exact Vault paths, L1 artifact shape, and test convention remain labeled assumptions with fallbacks.
- **Consistency:** ledger = classification authority; family manifests = trigger scope; accepted state = exact snapshots; existing hook = enforcement.
- **Runtime:** **requires execution evidence.** Preflight did not edit Colosseum, stage files, run the hook, compile, launch, or replay Lanista.

---

## First Action

**Agent 0 live-code gate before assigning Slice 1:**

1. re-pin current `master` and check for concurrent ledger/hook work;
2. re-read current `scripts/precommit-encyclopedia-check.sh` + `scripts/code_encyclopedia.py`;
3. identify exact Vault Browse source/test/bridge dependencies for `vault-browse.paths`;
4. verify Phase 2 `L1-Discovery` durable output/ownership.

If those checks preserve the assumptions, execute Slice 1. If not, revise the affected decision before code.

---

# AGENT PACKET

**TASK**  
Implement the Lanista coverage ledger + drift block in three slices.

**OBJECTIVE**  
Make accepted “is this surface drivable?” facts fail closed on relevant evidence drift, seeded only with Vault Browse.

**READ FIRST**  
This plan; `AGENTS.md`; `scripts/precommit-encyclopedia-check.sh`; `scripts/code_encyclopedia.py`; `docs/colosseum-lanista-verification.md`; Phase 2 plan’s `L1-Discovery`; live Vault Browse source/scenario evidence.

**EVIDENCE**  
Recon base `81b92be52cff72eef2f5bc3e5b90b9eaef1dea55`. Lanista source confirms objectName/handle target resolution and left-button-only `ui-click`. Popup limitation remains reported pending live verification.

**DECISIONS**  
Five states; exact family dependency manifests; existing hook is the single enforcement entrypoint; Vault Browse only day one; naming law in `AGENTS.md`; L1 findings are inputs.

**NON-GOALS**  
App-wide audit; second automation stack; unrestricted reflection; pixel/AI aesthetic gate; coverage percentage; Read/Drive changes; mass objectName retrofit; right-click implementation.

**CONSTRAINTS**  
Master only. One build directory/one binary. No required compile. Negative control per slice. Signal/property waits only. One Sonnet 5 session per slice.

**CONFIRMED LOCATIONS**  
`AGENTS.md`; `scripts/precommit-encyclopedia-check.sh`; `scripts/code_encyclopedia.py`; `native/devtools/LanistaServer.cpp`; `docs/colosseum-lanista-verification.md`.

**LIKELY LOCATIONS TO VERIFY**  
Exact Vault Browse QML/source membership; exact L1 durable output.

**IMPLEMENTATION SLICES**  
1. ledger + acceptance engine  
2. existing pre-commit drift integration  
3. Vault seed + naming law + L1 intake

**ACCEPTANCE TESTS**  
Use each slice’s focused tests and NC1/NC2/NC3; each mutation must turn the intended check red and restore green.

**VERIFICATION**  
Planned only; Agent 0 gates live code and execution evidence.

**RISKS**  
Noisy watched files; omitted new families; stale blocker classification; concurrent binary ownership.

**OPEN QUESTIONS**  
Execution-time verification only: Vault path membership, L1 artifact shape, Popup boundary, Python test convention.

**FIRST ACTION**  
Run the four-point Agent 0 gate above.

**SUGGESTED SKILLS**  
`brotherhood-writing-plans` → `brotherhood-executing-plans`
