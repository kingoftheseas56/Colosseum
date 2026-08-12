# Colosseum Agent Visibility Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use `brotherhood-executing-plans`. Execute one slice per fresh Sonnet 5 subagent, then return it to Agent 0 (Claude) for a live-code and evidence review before starting the next slice. This planning session does not implement, commit, or push.

**Goal:** Give agents structural sight, bounded Vault forensics, six isolated assembled-app journeys, and a nightly battery that leaves an honest report waiting for Hemanth without ever touching his daily app or live data.

**Architecture:** Lanista remains the single automation stack. L1 enriches its read-only structural/geometry view, L2 adds runner-owned checkpoint verdicts, and F1 exposes one bounded read projection owned by the live Vault object and its authoritative thread. J1 composes those senses into six isolated journeys. N0/N1 run the same build, CTest, journeys, warning gate, and report pipeline from a dedicated per-run build tree, then expose the long run as an MCP Task and schedule it under the local Windows user.

**Tech stack:** C++20, Qt 6 / Qt Quick, Qt Test and Qt Quick Test, SQLite through the existing Vault owner only, the existing `lanista` C++ bridge/CLI, Python 3 for the MCP adapter and small contract tests, PowerShell 5.1 for Windows orchestration, CMake/CTest/Ninja, and Windows Task Scheduler.

**Program source:** `Brotherhood/agents/preflight-handoff-agent-visibility-workstream.md`, ratified 2026-08-12. Phase 1 is already landed: J0 (`da01de6`, `3199607`), W0 (`24119d0`), and facade F (`c5145d8`). Do not re-plan or rewrite them.

**Ledgers consulted fresh:** `docs/colosseum-test-verification.md` and `docs/colosseum-lanista-verification.md` on 2026-08-12. If either ledger changes before a slice begins, the executor must re-read it and let live code win.

---

## Program rulings and stop laws

1. **Vault Browse Slice 5 is a hard dependency for J1-Ceremony and J1-Vault; Slice 6 is additionally required for J1-Identify.** Slice 5 retires the shelves face and re-points `vault_shelves.json`, `vault_door.json`, and `vault_identify.json`; Slice 6 wires live uncertain-state and identify-in-place transitions. These journeys must target the new carousel, collapsible root rail, breadcrumb, media-faced grid, and poster-versus-16:9 cards. They may not be authored against the retiring face.
2. **F0 is discovery, not a license to improvise.** F1 stops immediately if a safe projection would require a second SQLite connection, store access from a foreign thread, a writer, a mutation, or widening `VaultIndex::publish()` identity-carry. Report `Plan contradicted`; do not design around the stop.
3. **`VaultIndex::publish()` identity-carry stays untouched.** In particular, do not carry canonical identity per stable file tuple. F1 is a new bounded read surface, not a publish-path change.
4. **GammaRay is a reference and optional external developer tool only.** Its GPL-2.0-or-later/dual-license and DLL-injection model rule out linking or shipping its probe. L1 first decides whether a narrow Lanista extension closes the actual gap; adoption is not presumed.
5. **The protocol upgrade belongs to Night Watch.** Facade v0 stays backward compatible, but N1 upgrades the MCP server to the 2026-07-28 protocol and formal Tasks because an overnight build-plus-journey run is the durable-handle use case. The preferred implementation uses the official MCP Python SDK pinned to a version proven to support the required Tasks surface; it does not hand-roll a second task protocol. Cost: a dev-tool dependency, adapter migration, host-compatibility tests, and durable task-state recovery. If the official SDK available to this repo does not support the ratified Tasks contract, N1 stops `Plan contradicted`; N0's standalone Night Watch still ships and schedules.
6. **Screenshots are exhibits, never verdicts.** Layout passes only from geometry; journey passes only from authoritative state/property signals; appearance remains Hemanth's eyes.
7. **No sleeps.** Every wait is a strict property equality, process completion, file/lock state change, CTest completion, or cooperative task phase transition. If none exists, record `Bridge blocked` and add the missing seam before proceeding.
8. **No live profile and no daily pipe.** Every driven run uses a unique pipe, tagged AppData/cache roots, versioned seeds, and an isolated process. Read-only diagnosis of the daily app is outside this plan.
9. **Rule 28:** master only. No worktree, side branch, or second installed build.

## Current assumptions Agent 0 must verify at the live-code gate

- **ASSUMPTION — Claude to verify:** the current authoritative Vault ownership still runs through `VaultLibrary`, `VaultIndex`, `VaultEnricher`, and `VaultIdentifier`, but their exact construction/thread pins have drifted since the older recon. F0 resolves this before F1 names an implementation seam.
- **ASSUMPTION — Claude to verify:** the active Vault Browse plan's Slice 5 lands the object names `vaultBrowseCarousel`, `vaultBrowseRail`, `vaultBrowseCrumb`, `vaultBrowseGrid`, and `vaultBrowseCard_<nodeKey>` as planned. If names differ, the J1 scenarios use the landed names and update both ledgers in the same commit.
- **ASSUMPTION — Claude to verify:** the current reader and player roots can expose completion by binding existing authoritative state; no timer-derived or test-only shadow state is acceptable.
- **ASSUMPTION — Claude to verify:** the current production binary loads the working tree's `qml/Main.qml` and QML-only slices therefore need no relink. If false, those slices compile through the same serialized build gate.
- **ASSUMPTION — Claude to verify:** `VaultIdentityCeremonyDialog` still exposes the planned choice object names (`vaultSameMedia`, `vaultNewMedia`, `vaultUseExistingState`, `vaultSeparateCopy`) after Vault Browse Slice 5. If false, use the landed names and update the ledger/scenario together.
- **ASSUMPTION — Claude to verify:** the official MCP Python SDK version selected for N1 negotiates `2026-07-28` and implements the Tasks lifecycle required below. If false, N1 does not fall back to a hand-rolled approximation.
- **ASSUMPTION — Claude to verify:** 03:00 local time is acceptable for the registered daily schedule. If false, only the installer parameter/default changes; the runner and evidence contract do not.
- **ASSUMPTION — Claude to verify (soak track):** the existing local HTTP fixture machinery (as used by the header-channel harness) can serve as S2's loopback download source. If false, v0 ships without the download action mix and says so in its manifest; no new server is improvised.
- **ASSUMPTION — Claude to verify (soak track):** `LanistaEventLog::append` tolerates S0's capped emission rate without measurable app-thread cost. If false, S0 buffers writes off the GUI thread before landing.

**Ledger-pinned, not inferred:** Lanista's request line ceiling is 1 MiB, and the fresh 2026-08-12 ledger inventories 18 scenario JSON files. Re-check both numbers if the ledger changes before execution.

## Build and execution serialization

- Before each compiled slice, Agent 0 checks that no other build owns `native/build-msvc` and no `colosseum.exe` launched from that directory is running. A compile slice owns the directory until its review gate closes.
- QML-only slices do not rebuild; they run the existing reviewed binary with the current `qml/Main.qml` only between other workstreams' build gates.
- App-booting verification is short-lived: launch, prove isolation, drive, collect warnings/grabs, stop. Never park a session across another slice.
- Night Watch never uses `native/build-msvc`. Each run configures a fresh `artifacts/night-watch/build/<runId>/` and runs only from the binary it just built.
- A slice touching either verification ledger declares that shared-file edit in `Brotherhood/agents/chat.md` before editing. A slice touching `native/CMakeLists.txt`, `tests/CMakeLists.txt`, `native/main.cpp`, or `qml/Main.qml` follows the repo's shared-file declaration rule.

## Dependency order

```text
F0 ───────────────► F1-Core ─► F1-Bridge ──────────────┐
                                                       │
L1-Discovery ─► L1-Bridge ─► L2-Verdict ──────────────┼─► six J1 journeys ─► N0-Runner ─► N0-Battery
                                                       │                         │
Vault Browse Slice 5 ─► Ceremony/Vault ────────────────┤                         └─► N1-Protocol ─► N1-Tasks ─► N1-Register ─► N1-First-Run
Vault Browse Slice 6 ─► Identify ──────────────────────┘
```

F0 and L1-Discovery may be investigated in parallel because they touch different subsystems. Everything else is reviewed and landed in the order written below. The six journeys are independent after their stated dependencies, but execute serially because they share one binary and common scenario inventory.

**Soak track (S0–S3, added 2026-08-12):** `J1-Manga-Seam + J1-Video-Seam ─► S0-Pulse ─► S1-Digest ─► S2-Driver ─► S3-Watch`, with S2 additionally consuming N0-Runner's lock module and S3 extending N0-Battery. S1 may be built in parallel with anything (fixture-only). S0 compiles and therefore serializes through the shared build gate like any compiled slice.

---

### Slice F0: Re-pin Vault ownership and thread law

**Purpose:** Establish exactly which live app object may answer a forensic read so the later tool observes the Vault through its own eyes without creating a rival database path.
**Dependencies:** Phase 1 only.
**Implementation guidance:** Perform a read-only trace from `native/main.cpp` construction through `VaultLibrary`, `VaultIndex`, `VaultEnricher`, and `VaultIdentifier`: object ownership, `QObject::thread()` at construction and use, SQLite connection creation/open site, publish/revision signals, and every current read projection (`recentGroups()`, `browseAt()`, `rootsDetail()`, `recentArrivals()`). Record exact file:line pins and a call/thread diagram in `docs/visibility/vault-forensic-owner-thread.md`. Add `tests/contracts/vault-forensic-owner-thread.json` with fields `dbOwner`, `connectionFactory`, `ownerThread`, `publishThread`, `safeProjectionOwner`, `safeInvocation`, and booleans for the five forbidden conditions. Add `tests/test_vault_forensic_owner_thread.py` to reject missing citations or any forbidden condition marked allowed. Do not change production source.
**Behavior to preserve:** all Vault scan, enrichment, identification, browse, and publish behavior; especially `VaultIndex::publish()` identity-carry.
**Baseline:** record current HEAD, the exact schema version, connection name/path, owning thread, and one current call path for each listed projection before writing the conclusion.
**Focused tests:**
  - Qt Test: existing `colosseum.qttest.vault_index`, `colosseum.qttest.vault_scanner`, `colosseum.qttest.vault_enricher`, and `colosseum.qttest.vault_stores` stay green if run; F0 itself changes no binary.
  - Qt Quick Test: not applicable — no QML change.
  - Existing harnesses: `python tests/test_vault_forensic_owner_thread.py` validates the discovery artifact.
  - Negative control: see the named mutation below.
**Test seam status:** available for report completeness; production safety remains a human/code-review conclusion from the cited pins.
**Lanista actions:** none; this is source/ownership discovery.
**Completion signal:** the contract test exits 0 and Agent 0 independently verifies every pin against current HEAD.
**State / events / probes:** source citations, thread-affinity connections, SQLite ownership, and allowed invocation mode.
**Visual evidence:** the Mermaid ownership/thread diagram embedded in the report; no app grab.
**Regression paths:** no runtime path is changed.
**Evidence artifacts:** `docs/visibility/vault-forensic-owner-thread.md`, `tests/contracts/vault-forensic-owner-thread.json`, and `artifacts/visibility-phase2/f0/{owner-thread-test.log,source-pins.json,thread-diagram.mmd}`.
**Bridge status:** not applicable.
**Negative control:** in a temporary copy of the JSON contract set exactly `secondConnectionAllowed` from `false` to `true`; `test_rejects_second_connection` must be the only red case, then restore and rerun green.
**Compilation:** none.
**Completion criterion:** **Test-reported** discovery with Agent 0 approval. If any forbidden condition is required, F1-Core and F1-Bridge become **Plan contradicted** and stop; L1/L2/J1 work may continue.

### Slice L1-Discovery: Structural gap and GammaRay decision

**Purpose:** Pin the smallest additional sight agents need to explain a layout failure, instead of importing a broad debugger or inventing another automation stack.
**Dependencies:** Phase 1 only; may run parallel with F0.
**Implementation guidance:** Compare current `dump-ui`, `ui-query`, `ui-snapshot`, and `qml-get` output with the three actual gaps: unnamed items, binding provenance, and model contents. Use GammaRay only as the capability reference. Write `docs/visibility/lanista-structural-gap.md` with a required-now/deferred table. The default design is: include every `QQuickItem` in a structural dump with an ephemeral handle; expose type, nullable objectName, parent handle/name, child count, local rect, scene rect, z, effective visibility/opacity/enabled, root-window bounds, and clipping-ancestor chain; bound the request by root, depth, item count, and a reply byte ceiling safely below Lanista's 1 MiB line limit, returning `truncated`/continuation metadata rather than overflowing; keep arbitrary binding graphs and model enumeration deferred unless a named Phase 2 checkpoint cannot be explained without them. Never link or ship GammaRay.
**Behavior to preserve:** existing named-item lookup, DFS-first collision behavior, handle invalidation semantics, Drive/Read gates, first-root-window boundary, and current command reply fields.
**Baseline:** capture `dump-ui`, `ui-snapshot`, and `ui-query` for the existing Lanista harness scene; count named versus deliberately unnamed items and record which facts cannot be obtained today.
**Focused tests:**
  - Qt Test: not applicable — discovery only.
  - Qt Quick Test: not applicable — discovery only.
  - Existing harnesses: add `tests/test_lanista_structural_contract.py` to validate the decision document's machine-readable appendix and forbidden shipping modes.
  - Negative control: see below.
**Test seam status:** available for the contract artifact.
**Lanista actions:** existing `dump-ui`, `ui-snapshot`, `ui-query`, and `qml-get` against `lanista_harness_scene.qml`; no new command yet.
**Completion signal:** the contract validator exits 0 and the report names either the minimal L1-Bridge payload or a specific `Bridge blocked` prerequisite.
**State / events / probes:** baseline command replies and the gap matrix.
**Visual evidence:** one annotated harness grab showing the intentionally unnamed/clipped/nested items that correspond to the structural rows.
**Regression paths:** no runtime path changes.
**Evidence artifacts:** `docs/visibility/lanista-structural-gap.md`, `tests/test_lanista_structural_contract.py`, and `artifacts/visibility-phase2/l1-discovery/{dump-ui.json,ui-snapshot.json,ui-query.json,gap-matrix.json,structural-contract-test.log,harness.png}`.
**Bridge status:** available for discovery; implementation waits for this decision.
**Negative control:** flip the appendix field `gammaRayProbeShipped` to `true`; exactly `test_rejects_shipping_gammaray_probe` must turn red, then restore.
**Compilation:** none.
**Completion criterion:** **Test-reported** discovery approved by Agent 0, with binding/model inspection either explicitly required by a named checkpoint or explicitly deferred.

### Slice L1-Bridge: All-item structural dump and enriched geometry

**Purpose:** Let an agent see an unnamed or clipped item and identify its owner/geometry from one read-only structural snapshot.
**Dependencies:** L1-Discovery approved.
**Implementation guidance:** One-session production fence: `native/devtools/LanistaServer.h/.cpp`, `native/tools/lanista.cpp`, `tests/lanista_harness.cpp`, and `tests/lanista_harness_scene.qml`, plus the two required ledger updates; no binding graph, model enumeration, or unrelated bridge refactor. Preserve `dump-ui` compatibility while adding versioned structural fields decided by L1-Discovery; every item receives an opaque handle valid until the next structural/snapshot generation. Accept bounded optional `root`, `maxDepth`, and `maxItems` inputs; clamp them, enforce a reply budget below 1 MiB, and return `generation`, `truncated`, and continuation metadata. Extend `ui-query` to return the same geometry vocabulary for a named item or current-generation handle. Add deliberately unnamed, nested, clipped, transparent, disabled, zero-size, and over-budget fixtures. Update both ledgers in the same commit.
**Behavior to preserve:** old clients reading only existing fields; objectName targeting; stale handles fail `NO_SUCH_ITEM`; no visibility filter; first-root-window scope; Read gating.
**Baseline:** preserve the L1-Discovery replies and run `tests/test_lanista.ps1` before edits.
**Focused tests:**
  - Qt Test: extend `tests/lanista_harness.cpp` with named cases `structural_fields_are_versioned`, `structural_dump_includes_unnamed_items`, `parent_chain_is_exact`, `clipping_chain_is_exact`, `stale_structural_handle_is_rejected`, `requested_bounds_are_clamped`, `reply_budget_sets_truncated`, and `continuation_resumes_without_duplicates`.
  - Qt Quick Test: not applicable — the harness scene supplies fixture items; server truth is native.
  - Existing harnesses: `powershell -File tests/test_lanista.ps1`; `native/build-msvc/lanista_harness.exe` as invoked by that script; `tests/lanista_scenarios/self_smoke.json`, `self_visual.json`, and `app_home.json`.
  - Negative control: see below.
**Test seam status:** available.
**Lanista actions:** `dump-ui`; query the returned unnamed-item handle with `ui-query`; query its named viewport; take an item grab only as an exhibit.
**Completion signal:** replies contain the exact structural fields and the handle query returns matching scene geometry; all named tests exit 0.
**State / events / probes:** generation, item handle/type/name, parent chain, local/scene rect, root bounds, effective flags, z, clipping ancestors, item/byte totals, and truncation cursor.
**Visual evidence:** harness item grab paired with the structural JSON row.
**Regression paths:** existing `self_smoke.json`, `self_visual.json`, `app_home.json`, and all facade v0 tools remain green.
**Evidence artifacts:** `artifacts/visibility-phase2/l1-bridge/{dump-before.json,dump-after.json,unnamed-query.json,truncation-page-1.json,truncation-page-2.json,lanista-harness.log,test-lanista.log,unnamed-item.png}`.
**Bridge status:** available after this slice.
**Negative control:** omit the unnamed fixture row while keeping named rows; exactly `structural_dump_includes_unnamed_items` must fail, then restore.
**Compilation:** full app/lanista build required; serialize through Agent 0's build gate.
**Completion criterion:** **Runtime-validated** in the real isolated app plus harness tests; no GammaRay binary or library enters the repo/build.

### Slice F1-Core: Bounded app-owned Vault forensic projection

**Purpose:** Replace manual SQLite archaeology with one bounded, read-only answer produced by the same live Vault object Hemanth is looking at.
**Dependencies:** F0 approved with no stop law triggered.
**Implementation guidance:** Add a focused `VaultForensics` value/projection unit under the exact owner F0 names. The public call accepts a request map with `scope` (`summary`, `root`, `node`, `identity`), optional stable key/path, and `limit` clamped to 1..100. It returns `schema: "colosseum.vault.forensics.v1"`, index schema/revision, roots summary, browse/recent counts, bounded row summaries, identity state/candidate count, coverRef provenance, owner-thread name/id, `truncated`, and diagnostic errors. Clamp individual diagnostic strings and enforce a total serialized reply budget of 256 KiB with `truncated`/omitted-field metadata, staying safely below Lanista's 1 MiB transport line. It composes existing read APIs on the authoritative thread; it must not open SQLite, retain a query, write, mutate, scan, enrich, identify, or call `publish()`. A queued call from a foreign thread is marshalled to the owner with a deadline and returns an error on timeout; no cross-thread store access occurs.
**Behavior to preserve:** schema v5 data, `recentGroups()`, `browseAt()`, `rootsDetail()`, `recentArrivals()`, artwork `file://` refs, all identity semantics, and publish identity-carry exactly as found.
**Baseline:** using a temporary Vault fixture DB, record the equivalent answers from existing public projections before introducing `VaultForensics`; do not query SQLite directly in production code.
**Focused tests:**
  - Qt Test: create `tests/auto/vault/tst_vault_forensics.cpp`, registered as `colosseum.qttest.vault_forensics`, with named cases `schema_and_shape_v1`, `summary_scope_is_bounded`, `root_scope_is_bounded`, `node_scope_is_bounded`, `identity_scope_is_bounded`, `row_limit_sets_truncated`, `byte_budget_sets_truncated`, `executes_on_owner_thread`, `foreign_thread_marshals_to_owner`, `timeout_returns_error`, and `projection_does_not_mutate_files`.
  - Qt Quick Test: not applicable — no QML change.
  - Existing harnesses: `colosseum.qttest.vault_index`, `colosseum.qttest.vault_scanner`, `colosseum.qttest.vault_enricher`, `colosseum.qttest.vault_stores`, and the new `colosseum.qttest.vault_forensics` under `ctest --test-dir native/build-msvc -R "colosseum.qttest.vault_(index|scanner|enricher|stores|forensics)" --output-on-failure`.
  - Negative control: see below.
**Test seam status:** available after adding the registered Qt Test in this slice.
**Lanista actions:** none; bridge exposure is the next slice.
**Completion signal:** the projection call completes on the F0 owner thread and the test process exits 0; timeout is a returned error, never a hang.
**State / events / probes:** response schema, revision, bounded counts/rows, identity fields, thread identity, truncation/error flags.
**Visual evidence:** not applicable; internal read projection.
**Regression paths:** all existing Vault tests and browse projections remain byte/shape compatible.
**Evidence artifacts:** `artifacts/visibility-phase2/f1-core/{baseline-projections.json,summary-response.json,node-response.json,byte-budget-response.json,vault-forensics-ctest.log,file-hashes-before.json,file-hashes-after.json,publish-diff.txt}`.
**Bridge status:** not applicable until F1-Bridge.
**Negative control:** temporarily bypass the limit clamp so a request for 101 returns 101 rows; exactly `row_limit_sets_truncated` must fail, then restore. Separately review `git diff` to prove `VaultIndex::publish()` is untouched.
**Compilation:** required; serialize through the build gate.
**Completion criterion:** **Test-reported** internal slice. Any second connection, mutation, writer, foreign-thread store read, or publish change makes it **Plan contradicted**.

### Slice F1-Bridge: One Lanista/MCP Vault-forensics call

**Purpose:** Give an agent one typed call that explains the live Vault state through F1-Core, with a hard bound and a preserved evidence file.
**Dependencies:** F1-Core.
**Implementation guidance:** Add one Read-gated `vault-forensics` bridge command in `LanistaServer` that invokes F1-Core on its owner thread and returns its map unchanged. Add CLI support and one facade tool `vault_forensics(scope, key?, limit?, timeoutMs?)` that shells the same CLI, preserving v0's deadline/backstop and session ownership. Do not grow a generic reflection/write registry. Update both ledgers and facade tool inventory.
**Behavior to preserve:** all 11 facade v0 tools; `session_start` and every Drive journey unconditionally refuse the daily/default pipe; the three legacy tools retain their current environment-resolved target and Read behavior; one-live-session rule; CLI scenario runner.
**Baseline:** record F1-Core's response and the facade's current tool list before edits.
**Focused tests:**
  - Qt Test: extend `tests/lanista_harness.cpp` with named cases `vault_forensics_is_read_gated`, `vault_forensics_passes_response_unchanged`, `vault_forensics_rejects_bad_scope`, `vault_forensics_clamps_limit`, and `vault_forensics_deadline_is_bounded`.
  - Qt Quick Test: not applicable.
  - Existing harnesses: `powershell -File tests/test_lanista.ps1`; `ctest --test-dir native/build-msvc -R colosseum.qttest.vault_forensics --output-on-failure`; and `python tests/test_lanista_mcp_forensics.py` cases `tool_schema_is_exact`, `summary_round_trip`, `node_round_trip`, `deadline_is_bounded`, and `legacy_tools_unchanged`.
  - Negative control: see below.
**Test seam status:** available.
**Lanista actions:** isolated `session_start(seedName="vault-stale-index-v1")`; call `vault_forensics(scope="summary", limit=10)`; call `scope="node"` for a seeded key; run `warnings()`; stop.
**Completion signal:** each call returns before its explicit deadline with schema `colosseum.vault.forensics.v1`; session stop records final status.
**State / events / probes:** live revision/root/count/identity data and `truncated`; no typed events claimed.
**Visual evidence:** one Vault grab paired with the forensic JSON as non-atomic exhibits.
**Regression paths:** legacy 3 facade tools, 8 v0 typed tools, `vault_launch_smoke`, and `vault_open_recent` stay green.
**Evidence artifacts:** `artifacts/visibility-phase2/f1-bridge/{transcript.jsonl,summary-response.json,node-response.json,warning-verdict.txt,vault.png,test-lanista.log,mcp-forensics-test.log}`.
**Bridge status:** available after this slice.
**Negative control:** request `limit=101` and temporarily assert 101 rows; exactly the bound assertion must fail while the command remains responsive, then restore.
**Compilation:** app/lanista build required for the new bridge command; Python adapter itself is uncompiled.
**Completion criterion:** **Runtime-validated** in an isolated session; no direct SQLite or JSON archaeology exists in the shipped tool.

### Slice L2: Runner-owned layout verdicts at named checkpoints

**Purpose:** Turn “this control is cut off or sitting on top of its peer” into a deterministic red result without making screenshots or global overlap heuristics the gate.
**Dependencies:** L1-Bridge.
**Implementation guidance:** Extract a pure `LanistaLayoutVerdict` evaluator used by `native/tools/lanista.cpp`. Add a runner-local scenario step `layout-verdict` whose payload names a checkpoint and explicit rules: `actionableNonzero` for named controls; `contained` target-within-viewport with tolerance in logical pixels; and `noPeerOverlap` for an explicit peer pair/list. The runner takes one bounded L1 structural dump generation and resolves every rule against rows from that same generation, so moving delegates cannot create a verdict from mismatched moments. It computes in logical scene units, emits each rule's measured rectangles/intersection, and fails only the named checkpoint. No server command, global sweep, device-pixel math, or image comparison. Store checkpoint definitions under `tests/lanista_layout/` and begin with harness-only fixtures plus one stable app-home checkpoint.
**Behavior to preserve:** ordinary scenario steps/replies, `grab_on_fail`, exit-code contract, and existing geometry fields.
**Baseline:** run the harness scene and app-home without layout verdicts; preserve current rectangles and demonstrate that the existing runner does not reject the clipped/overlap fixture.
**Focused tests:**
  - Qt Test: create `tests/auto/lanista/tst_layout_verdict.cpp`, registered as `colosseum.qttest.layout_verdict`, with named cases `actionable_zero_size_fails`, `hidden_actionable_fails`, `disabled_actionable_fails`, `contained_inside_passes`, `contained_outside_tolerance_fails`, `touching_edges_do_not_overlap`, `named_peer_overlap_fails`, `unnamed_peers_are_not_global_rules`, `one_generation_is_required`, and `logical_units_ignore_device_pixel_ratio`.
  - Qt Quick Test: not applicable — geometry fixture is in the existing harness scene.
  - Existing harnesses: `powershell -File tests/test_lanista.ps1`; `ctest --test-dir native/build-msvc -R colosseum.qttest.layout_verdict --output-on-failure`; `tests/lanista_scenarios/self_smoke.json`; and `tests/lanista_scenarios/app_home.json` with `tests/lanista_layout/app_home.json`.
  - Negative control: see below.
**Test seam status:** available after the registered Qt Test lands.
**Lanista actions:** run `layout-verdict` for the harness checkpoint and app-home checkpoint; request a grab on failure only as diagnosis.
**Completion signal:** scenario step returns one machine-readable verdict per named rule and exit 0 only when all named rules pass.
**State / events / probes:** structural generation, queried geometry, viewport, enabled/visible/opacity flags, intersection rectangle, tolerance, and rule name.
**Visual evidence:** failing-fixture grab beside its red geometry JSON; never the pass gate.
**Regression paths:** run a scenario with no layout step and prove identical output/exit; run existing 18-scenario inventory unchanged.
**Evidence artifacts:** `tests/lanista_layout/harness.json`, `tests/lanista_layout/app_home.json`, and `artifacts/visibility-phase2/l2/{baseline-rects.json,harness-verdict.json,app-home-verdict.json,negative-containment.json,negative-overlap.json,layout-ctest.log,test-lanista.log,containment-failure.png,overlap-failure.png}`.
**Bridge status:** available after L1; no Planned capability used.
**Negative control:** move one harness button one logical pixel beyond its named viewport; exactly that checkpoint's containment rule turns red, then restore. A separate peer-overlap fixture changes only its named overlap rule red.
**Compilation:** lanista/test build required; no QML production change.
**Completion criterion:** **Runtime-validated** on the harness and app-home checkpoint, with no global no-overlap rule anywhere.

### Slice J1-Manga-Seam: Expose authoritative reader readiness

**Purpose:** Give the manga journey one real completion signal sourced from the loaded archive/page model rather than navigation or time.
**Dependencies:** J0/W0 and the existing reader test estate.
**Implementation guidance:** Agent 0 first pins the production owner (`qml/comicreader/ComicReaderShell.qml` or its landed successor). Expose read-only `readerReady`, `readerSourceId`, `readerPageCount`, and `readerPageIndex` on that production root, bound directly to the authoritative archive/page model. `readerReady` is false until source identity is non-empty, page count is positive, and the current page is render-ready. Add no timer, sleep, route shadow, or test-only object. Add `tests/qml/tst_journey_open_manga.qml`; add native `tests/auto/comicreader/tst_journey_open_manga.cpp` only if the authoritative fact must be aggregated in C++.
**Behavior to preserve:** reader routing, resume writes, page order, layout mode, and rendering behavior; this slice is observability-only.
**Baseline:** record current reader root properties and the earliest point at which the first real page becomes ready.
**Focused tests:**
  - Qt Test: optional new `colosseum.qttest.journey_open_manga` only if C++ aggregation is required; otherwise not applicable.
  - Qt Quick Test: `tests/qml/tst_journey_open_manga.qml` cases `ready_false_before_source`, `ready_false_before_page_model`, `ready_true_after_first_page`, and `source_count_index_match_model` under `colosseum.qml`.
  - Existing harnesses: `colosseum.comicreader_cache_harness` and `colosseum.cbz_archive_harness`.
  - Negative control: see below.
**Test seam status:** available after this slice; if authoritative readiness cannot be surfaced without test-only state, report **test blocked** and stop J1-Manga.
**Lanista actions:** none; assembled-app proof belongs to J1-Manga.
**Completion signal:** the Quick Test observes the real false→true transition and exits 0.
**State / events / probes:** source id, page count/index, page-model readiness, readerReady.
**Visual evidence:** not applicable; internal observability seam.
**Regression paths:** reader creation, restore/resume, manual page turn, reader close/reopen tests stay green.
**Evidence artifacts:** `tests/qml/tst_journey_open_manga.qml`, optional `tests/auto/comicreader/tst_journey_open_manga.cpp`, and `artifacts/visibility-phase2/j1-manga-seam/{baseline.json,qml-test.log,native-test.log}`.
**Bridge status:** not applicable until the journey consumes the QML properties.
**Negative control:** temporarily bind `readerReady` to reader visibility; exactly `ready_false_before_page_model` must fail, then restore.
**Compilation:** QML-only unless the optional native aggregation is required; any native edit uses the serialized build gate.
**Completion criterion:** **Test-reported** authoritative seam with no user behavior change.

### Slice J1-Manga: Open a downloaded manga into a ready reader

**Purpose:** Prove a user can open local manga from Tankoban and reach a rendered, page-ready reader—not merely a routed page.
**Dependencies:** J1-Manga-Seam and L2. F1 is not required.
**Implementation guidance:** Place the deterministic archive/library fixture under `tests/lanista_fixtures/journeys/open-manga-v1/` with `fixture.json` explicitly marked `seedZooAdmitted: false`; do not promote it into the real-bug seed zoo in this slice. Add only missing stable object names, then create `tests/lanista_scenarios/journey_open_manga.json` and `tests/lanista_layout/journey_open_manga.json`; do not change reader readiness logic in this slice.
**Behavior to preserve:** resume position, manga direction/layout, offline-only reading, and no progress writes outside the tagged root.
**Baseline:** boot the seed and record the current library row, open action, reader properties, warning verdict, and layout rectangles before scenario edits.
**Focused tests:**
  - Qt Test: `colosseum.comicreader_cache_harness`, `colosseum.cbz_archive_harness`, `colosseum.vault_launch_router_harness`, and `colosseum.qttest.journey_open_manga` only if J1-Manga-Seam registered it.
  - Qt Quick Test: `tests/qml/tst_journey_open_manga.qml` through `colosseum.qml`.
  - Existing harnesses: `colosseum.qml`, `colosseum.manga_reading_room`, `tests/test_manga_reading_room.ps1`, `tests/lanista_scenarios/journey_open_manga.json`, and W0 `tests/warning_gate.ps1`.
  - Negative control: see below.
**Test seam status:** available from J1-Manga-Seam; otherwise this journey is **test blocked**.
**Lanista actions:** isolated session with the manga seed; wait boot; click the seeded library item; wait production reader visible; wait `readerReady == true`; read source/page properties; run the reader layout checkpoint; grab the reader item; warnings.
**Completion signal:** strict equality on real `readerReady` plus page count/index properties; no sleep.
**State / events / probes:** source ID/path, page count, current index, reading mode/layout, tagged progress root.
**Visual evidence:** reader item grab showing a page and chrome state.
**Regression paths:** navigate back and reopen; scroll/turn one page and reopen; verify only tagged progress changes.
**Evidence artifacts:** `tests/lanista_fixtures/journeys/open-manga-v1/fixture.json`, `tests/lanista_scenarios/journey_open_manga.json`, `tests/lanista_layout/journey_open_manga.json`, and `artifacts/visibility-phase2/j1-open-manga/<sessionId>/{session.json,state.json,layout.json,warnings.txt,journey-open-manga.png,stdout.log,stderr.log}`.
**Bridge status:** available after the real readiness seam lands.
**Negative control:** corrupt the seed's archive entry while keeping the library row; exactly the `readerReady == true` wait must go red/timeout, then restore the seed and green.
**Compilation:** none for scenario/fixture/layout files; rebuild only if missing object names require QML changes under the standing QML-load assumption.
**Completion criterion:** **Runtime-validated** only after reader state, layout, warnings, and reopen regression all pass.

### Slice J1-Video-Seam: Expose decoded-frame truth from the production player

**Purpose:** Give the video journey real decoded-frame dimensions/state so route success or `FILE_LOADED` cannot masquerade as playback.
**Dependencies:** J0/W0 and the existing player/Vault admission tests.
**Implementation guidance:** Agent 0 pins the active production owner (`qml/PlayerPage.qml`, `qml/player2host/Player2Page.qml`, or its landed successor). Expose read-only `decodedWidth`, `decodedHeight`, `playerReady`, and stable source identity from the actual player/session. `playerReady` becomes true only when both decoded dimensions are positive and the authoritative session is in a playable state. Add `tests/auto/player/tst_journey_play_video.cpp` and `tests/qml/tst_journey_play_video.qml`. Do not use `FILE_LOADED`, a route counter, log text, or the separate admission probe as the production readiness value.
**Behavior to preserve:** playback, pause/seek, subtitle policy, volume, controls, and routing; observability only.
**Baseline:** record current route state and prove it can be true while decoded dimensions are still zero.
**Focused tests:**
  - Qt Test: new `colosseum.qttest.journey_play_video` cases `route_is_not_ready`, `audio_only_never_ready`, `decoded_fixture_reports_exact_dimensions`, and `source_identity_matches`.
  - Qt Quick Test: `tests/qml/tst_journey_play_video.qml` cases `ready_false_at_attach`, `ready_true_after_dimensions`, and `ready_resets_on_unload` under `colosseum.qml`.
  - Existing harnesses: `colosseum.vault_admission_probe_harness`, `colosseum.vault_launch_router_harness`, and `player2_state_machine_test`.
  - Negative control: see below.
**Test seam status:** available after this slice; if the active player cannot expose decoded truth safely, report **test blocked** and stop J1-Video.
**Lanista actions:** none; assembled-app proof belongs to J1-Video.
**Completion signal:** exact fixture dimensions and playerReady transition pass both named tests.
**State / events / probes:** source identity, decoded width/height, player state, playerReady.
**Visual evidence:** not applicable; internal observability seam.
**Regression paths:** load, unload, audio-only reject, pause/resume, and second-file load tests.
**Evidence artifacts:** `tests/auto/player/tst_journey_play_video.cpp`, `tests/qml/tst_journey_play_video.qml`, and `artifacts/visibility-phase2/j1-video-seam/{baseline.json,native-test.log,qml-test.log}`.
**Bridge status:** not applicable until the journey consumes the QML properties.
**Negative control:** temporarily derive `playerReady` from `FILE_LOADED`; exactly `audio_only_never_ready` must fail, then restore.
**Compilation:** expected native/player build plus QML wiring; serialize through the build gate.
**Completion criterion:** **Test-reported** decoded-frame seam with no playback behavior change.

### Slice J1-Video: Play a local video to decoded-frame readiness

**Purpose:** Prove a local video reaches real playback readiness, not just a route or `FILE_LOADED` event.
**Dependencies:** J1-Video-Seam and L2. F1 is optional unless the entry originates from Vault.
**Implementation guidance:** Place the existing deterministic decodable MP4 under `tests/lanista_fixtures/journeys/play-video-v1/` with `fixture.json` and `seedZooAdmitted: false`; it is a test fixture, not a real-bug seed. Create `tests/lanista_scenarios/journey_play_video.json` and `tests/lanista_layout/journey_play_video.json`; do not change player readiness logic in this slice.
**Behavior to preserve:** normal player routing, subtitle policy, volume, full-screen controls, and local-media launch ownership.
**Baseline:** record route state and show that current `localLaunchState.openCount` proves routing but not decoded-frame readiness.
**Focused tests:**
  - Qt Test: `colosseum.vault_admission_probe_harness`, `colosseum.vault_launch_router_harness`, `player2_state_machine_test`, and `colosseum.qttest.journey_play_video`.
  - Qt Quick Test: `tests/qml/tst_journey_play_video.qml` through `colosseum.qml`.
  - Existing harnesses: `colosseum.qml`, `tests/lanista_scenarios/journey_play_video.json`, and W0 `tests/warning_gate.ps1`.
  - Negative control: see below.
**Test seam status:** available from J1-Video-Seam; otherwise this journey is **test blocked**.
**Lanista actions:** isolated seeded session; activate local video; wait player visible; wait decoded width equals the fixture's known width and height equals known height; read playback/source state; run control-layout checkpoint; grab; warnings.
**Completion signal:** exact decoded dimensions and authoritative player state; never `FILE_LOADED` alone.
**State / events / probes:** route kind, media path hash/name, decoded dimensions, player state, subtitle/controls visibility.
**Visual evidence:** player item grab after decoded readiness.
**Regression paths:** pause/resume; navigate back and replay; reject an audio-only/non-video fixture.
**Evidence artifacts:** `tests/lanista_fixtures/journeys/play-video-v1/fixture.json`, `tests/lanista_scenarios/journey_play_video.json`, `tests/lanista_layout/journey_play_video.json`, and `artifacts/visibility-phase2/j1-play-video/<sessionId>/{session.json,state.json,layout.json,warnings.txt,journey-play-video.png,stdout.log,stderr.log}`.
**Bridge status:** available after the read-only player seam; typed media events remain unavailable and unused.
**Negative control:** replace the MP4 with the audio-only fixture; routing may occur but exactly the decoded-dimension wait must turn red, then restore.
**Compilation:** none for fixture/scenario/layout files; rebuild only if missing object names require QML changes under the standing QML-load assumption.
**Completion criterion:** **Runtime-validated** only on decoded-frame evidence; route-only success remains **Test-reported**.

### Slice J1-Identify: Identify an uncertain item on the new Vault Browse face

**Purpose:** Prove an uncertain Vault card can be identified in place and settles to the chosen identity on the rebuilt browsing surface.
**Dependencies:** F1-Bridge, L2, and **Vault Browse Slices 5 and 6 landed and reviewed**.
**Implementation guidance:** Re-point/extend `vault_identify.json` into `tests/lanista_scenarios/journey_identify.json` using the landed `vaultBrowse*` names. Put the deterministic two-candidate arrangement under `tests/lanista_fixtures/journeys/identify-uncertain-v1/fixture.json` with `seedZooAdmitted: false` unless Agent 0 cites a real dated bug dossier and admits it through J0. Drive the existing same-window identify dialog; confirm one candidate; wait the same card key to become `identified`. Pair the final UI state with one F1 forensic response for that node.
**Behavior to preserve:** no automatic adoption below certainty, Un-identify, Hide/Restore, and no widening of publish identity-carry.
**Baseline:** after Vault Slice 5, capture the uncertain card, dialog, current scenario behavior, and F1 node response before changes.
**Focused tests:**
  - Qt Test: `colosseum.qttest.vault_stores`, `colosseum.qttest.vault_index`, and `colosseum.qttest.vault_forensics`.
  - Qt Quick Test: `tests/qml/tst_vault_identify_dialog.qml` and `tests/qml/tst_vault_cards.qml` prove the chosen row emits the stable identity and the same delegate refaces without grid rebuild.
  - Existing harnesses: `colosseum.qml`, re-pointed `tests/lanista_scenarios/vault_identify.json`, new `journey_identify.json`, and W0 `tests/warning_gate.ps1`.
  - Negative control: see below.
**Test seam status:** available after Slice 5's landed object names are verified.
**Lanista actions:** start seeded session; open Vault; wait card `state == "uncertain"`; click its identify affordance; wait dialog; choose row; wait same card `state == "identified"`; call F1 node forensics; run card/grid layout checkpoint; grab; warnings.
**Completion signal:** exact card state transition and forensic identity state/candidate count; no sleep.
**State / events / probes:** card key/state/title/source, candidate count before, chosen identity after, index revision.
**Visual evidence:** before/after item grabs of the same card plus dialog grab.
**Regression paths:** Un-identify returns to uncertain; scroll away/back retains truth; leave/return to Vault.
**Evidence artifacts:** `tests/lanista_fixtures/journeys/identify-uncertain-v1/fixture.json`, `tests/lanista_scenarios/journey_identify.json`, `tests/lanista_layout/journey_identify.json`, and `artifacts/visibility-phase2/j1-identify/<sessionId>/{session.json,forensics-before.json,forensics-after.json,layout.json,warnings.txt,uncertain.png,identify-dialog.png,identified.png,stdout.log,stderr.log}`.
**Bridge status:** available only after Vault Browse Slices 5 and 6; before then this slice is **Bridge blocked** and must not target shelves or assume live identify-in-place.
**Negative control:** make the seed produce one certain candidate; exactly the initial `state == "uncertain"` wait must fail, then restore the two-candidate seed.
**Compilation:** normally QML/scenario only after Slice 5; no compile unless a missing real state property is added.
**Completion criterion:** **Runtime-validated** on the new Browse face with identity, geometry, warnings, and reversibility proven.

### Slice J1-Tray-Bridge: Minimal in-process window restore command

**Purpose:** Give an isolated journey a way to restore its own hidden/minimized root window without adding an OS-picker framework or a second UI stack.
**Dependencies:** L1-Bridge.
**Implementation guidance:** Add one Drive-gated `window-set-state` command to the existing Lanista server for the first root window only, accepting exactly `normal`, `minimized`, and `hidden`; reject daily/default-pipe Drive as the central gate already does. The server calls the real `QWindow`/application window path and replies with resulting visibility/window state. Add matching CLI/facade `act` routing only if needed; no FlaUI, pywinauto, tray-icon clicker, secondary-window enumeration, or OS taskbar automation.
**Behavior to preserve:** read-only commands always available, Drive opt-in, first-root-window scope, normal taskbar/tray production behavior.
**Baseline:** prove the current bridge cannot restore a minimized/hidden root and preserve that `Bridge blocked` transcript.
**Focused tests:**
  - Qt Test: create `tests/auto/lanista/tst_window_set_state.cpp`, registered as `colosseum.qttest.window_set_state`, with named cases `read_gate_refuses_window_set_state`, `drive_gate_accepts_normal`, `drive_gate_accepts_minimized`, `drive_gate_accepts_hidden`, `bad_state_is_rejected`, and `only_first_root_is_addressed`.
  - Qt Quick Test: not applicable; QWindow state is native/Windows truth.
  - Existing harnesses: `powershell -File tests/test_lanista.ps1`; `ctest --test-dir native/build-msvc -R "colosseum.qttest.(window_set_state|window_state_policy)" --output-on-failure`; and `tests/lanista_scenarios/app_home.json`.
  - Negative control: see below.
**Test seam status:** available after the new Qt Test lands.
**Lanista actions:** isolated app only: set minimized; query root state; set normal; wait visible/normal; set hidden; set normal.
**Completion signal:** exact `get-state` window visibility/state equality after each command.
**State / events / probes:** root window state, visibility, active flag, pid/pipe isolation.
**Visual evidence:** normal-state grab before and after; no claim about the OS tray icon.
**Regression paths:** ordinary close, app-home navigation, and session_stop remain green.
**Evidence artifacts:** `artifacts/visibility-phase2/j1-tray-bridge/{blocked-baseline.jsonl,minimized-state.json,hidden-state.json,restored-state.json,before.png,restored.png,window-set-state-ctest.log,test-lanista.log}`.
**Bridge status:** available after this slice; OS tray-icon clicking remains human-only and out of scope.
**Negative control:** temporarily misclassify `window-set-state` as Read-gated; exactly the gate-refusal test must fail, then restore Drive gating.
**Compilation:** required; serialize through the build gate.
**Completion criterion:** **Runtime-validated** for in-process hide/minimize/restore. No OS-picker framework is introduced.

### Slice J1-Tray: Minimize to tray and return to the same place

**Purpose:** Prove the app survives its real tray lifecycle and returns to the same user-visible page/state.
**Dependencies:** J1-Tray-Bridge, L2.
**Implementation guidance:** Create `journey_tray.json`. Start on a deterministic named page in an isolated profile, record a stable page/state property, click the production minimize-to-tray action, wait the authoritative window/tray-resident state, then use `window-set-state normal` to restore the same process and assert the page/state is unchanged. The bridge restores the window; it does not pretend to click the Windows tray icon.
**Behavior to preserve:** no process restart, no state reset, no taskbar semantics changes, and session_stop still graceful.
**Baseline:** record the chosen page state and the pre-bridge inability to restore.
**Focused tests:**
  - Qt Test: `colosseum.qttest.window_state_policy` and new `colosseum.qttest.window_set_state`.
  - Qt Quick Test: add `tests/qml/tst_journey_tray.qml` for page-state persistence across production minimize/restore signal handling.
  - Existing harnesses: `colosseum.qml`, `tests/lanista_scenarios/app_home.json`, new `journey_tray.json`, and W0 `tests/warning_gate.ps1`.
  - Negative control: see below.
**Test seam status:** available after the bridge and a real `trayResident`/window-state property are pinned.
**Lanista actions:** open named page; read state; click minimize-to-tray; wait hidden/minimized/tray state; invoke `window-set-state normal`; wait root visible/normal; re-read same page state; layout checkpoint; warnings.
**Completion signal:** exact window-state transitions and unchanged page/state property.
**State / events / probes:** pid, root visibility/state, tray-resident flag if production exposes it, page identity, selected item/scroll key.
**Visual evidence:** before/after window grabs at the same page; a one-time `human-witnessed:` note may confirm the actual Windows tray icon, but it is not the automated gate.
**Regression paths:** repeat twice in one session; session_stop from normal state.
**Evidence artifacts:** `tests/lanista_scenarios/journey_tray.json`, `tests/lanista_layout/journey_tray.json`, and `artifacts/visibility-phase2/j1-tray/<sessionId>/{session.json,state-before.json,state-hidden.json,state-restored.json,layout.json,warnings.txt,before.png,restored.png,human-tray-note.md,stdout.log,stderr.log}`.
**Bridge status:** available for lifecycle; OS tray icon remains human-only.
**Negative control:** deliberately navigate to a different page before restore; exactly the same-page assertion must fail, then restore the journey.
**Compilation:** QML-only unless the authoritative tray property is missing.
**Completion criterion:** **Runtime-validated** for the app lifecycle and preserved page; OS-icon appearance is separately human-witnessed.

### Slice J1-Ceremony: Resolve the copy/move identity ceremony

**Purpose:** Prove the Vault's identity ceremony presents the correct choice and applies it without corrupting the stable identity/progress relationship.
**Dependencies:** F1-Bridge, L2, and **Vault Browse Slice 5**.
**Implementation guidance:** Use `tests/lanista_fixtures/journeys/ceremony-use-existing-v1/fixture.json` unless Agent 0 cites and admits an exact real-bug seed through J0. The fixture declares `originalStableId`, `newTupleId`, `progressKey`, and `seedZooAdmitted: false`. Create `tests/lanista_scenarios/journey_ceremony.json` around `VaultIdentityCeremonyDialog`. Exercise exactly `vaultUseExistingState`: the original stable identity stays canonical, the new tuple becomes its alias, and the existing progress remains reachable under the canonical identity after restart. Wait the dialog owner's production completion signal, then confirm those exact manifest values through F1 and the settled Browse card. Do not add a new ceremony or identity carry.
**Behavior to preserve:** ambiguity never silently merges, progress follows only the explicit choice, cancel leaves state unchanged, and other choices remain available.
**Baseline:** capture pre-choice identity aliases/progress and current dialog state from the seed.
**Focused tests:**
  - Qt Test: `colosseum.qttest.vault_stores` and `colosseum.qttest.vault_forensics`; add the chosen result as a named row in `tst_vault_stores.cpp` if absent.
  - Qt Quick Test: `tests/qml/tst_vault_identity_dialogs.qml` clicks each choice and proves one signal/value each.
  - Existing harnesses: `colosseum.qml`, new `tests/lanista_scenarios/journey_ceremony.json`, and W0 `tests/warning_gate.ps1`.
  - Negative control: see below.
**Test seam status:** available if the seed and authoritative completion property exist; otherwise add that read-only property before the scenario.
**Lanista actions:** open new Browse face; trigger ceremony from seeded ambiguous card; wait dialog; click chosen action; wait dialog closed through an explicit owner property (not absence); wait card settled; call F1 identity scope; layout verdict; grabs; warnings.
**Completion signal:** explicit ceremony owner state `completed`/result plus forensic identity/alias values.
**State / events / probes:** old/new stable IDs, alias/progress binding, selected ceremony result, index revision.
**Visual evidence:** dialog and settled-card grabs.
**Regression paths:** cancel leg leaves F1 response unchanged; repeat boot preserves chosen result.
**Evidence artifacts:** `tests/lanista_fixtures/journeys/ceremony-use-existing-v1/fixture.json`, `tests/lanista_scenarios/journey_ceremony.json`, `tests/lanista_layout/journey_ceremony.json`, and `artifacts/visibility-phase2/j1-ceremony/<sessionId>/{session.json,forensics-before.json,forensics-after.json,layout.json,warnings.txt,dialog.png,settled-card.png,stdout.log,stderr.log}`.
**Bridge status:** available after Slice 5 and the completion property; absence assertions are not used.
**Negative control:** mutate the expected chosen stable ID to a different fixture ID; exactly the post-ceremony forensic assertion must fail, then restore.
**Compilation:** QML/scenario only unless completion state is missing.
**Completion criterion:** **Runtime-validated** with explicit user choice, forensic result, persistence, warning cleanliness, and layout proof.

### Slice J1-Vault: Browse the rebuilt Vault face

**Purpose:** Prove the new Vault Browse face works as assembled: carousel, collapsed/expanded root rail, breadcrumb drill, virtualized media grid, and correct poster versus 16:9 card shape.
**Dependencies:** F1-Bridge, L2, and **Vault Browse Slice 5 landed and its re-pointed scenarios green**.
**Implementation guidance:** Author `tests/lanista_scenarios/journey_vault_browse.json` only after inspecting the landed Slice 5 face. Place the five synthetic physical shapes under `tests/lanista_fixtures/journeys/vault-browse-five-shapes-v1/fixture.json` with `seedZooAdmitted: false`; admit them to `tests/lanista-seeds/` only if a real dated bug dossier exists. Drive the carousel/rail/breadcrumb/grid; use `tests/lanista_layout/journey_vault_browse.json` for carousel containment, rail controls, breadcrumb, poster cards, wide cards, and scoped peers. Query one film/show/folder node through F1. Do not resurrect `vaultMarquee` or `vaultShelf_*` assertions.
**Behavior to preserve:** Add storage, scan pill, confirm card, hidden surface, identify/ceremony entry points, reveal, open-media routing, and taskbar Vault door.
**Baseline:** preserve Slice 5's baseline/final artifacts and current re-pointed `vault_door`, `vault_identify`, and `vault_shelves` replacements.
**Focused tests:**
  - Qt Test: run `ctest --test-dir native/build-msvc -R "colosseum.qttest.vault_(index|forensics)" --output-on-failure`; no new native browse contract is introduced here.
  - Qt Quick Test: add exact `tests/qml/tst_journey_vault_browse.qml` cases `carousel_is_present_for_arrivals`, `rail_starts_collapsed`, `rail_toggle_expands`, `breadcrumb_tracks_drill`, `grid_count_matches_fixture`, `film_uses_poster_card`, `episode_and_clip_use_wide_card`, and `delegate_recycling_preserves_state`; run `ctest --test-dir native/build-msvc -R colosseum.qml --output-on-failure`.
  - Existing harnesses: run `native/build-msvc/lanista.exe session run tests/lanista_scenarios/vault_door.json --drive --ready-ms 60000`, then the same command for `vault_identify.json` and `vault_shelves.json`; run `native/build-msvc/lanista.exe session run tests/lanista_scenarios/journey_vault_browse.json --seed tests/lanista_fixtures/journeys/vault-browse-five-shapes-v1 --drive --ready-ms 60000`; then run `powershell -File tests/warning_gate.ps1 -LogPath <sessionAppData>/logs/colosseum.log,<sessionDir>/stderr.log` for each session.
  - Negative control: see below.
**Test seam status:** available after Slice 5's object names are ledgered and this slice's exact Quick Test lands.
**Lanista actions:** isolated seed; open Vault; wait Browse grid population; inspect carousel; expand rail; drill a plain folder and verify breadcrumb path; inspect poster and wide card geometry; scroll away/back; call F1 forensics; warnings; grabs.
**Completion signal:** strict grid count/path/rail-state/card-type equalities and L2 verdict exit 0.
**State / events / probes:** grid count, current path, rail expanded, card nodeType/aspect contract, selected F1 node/revision.
**Visual evidence:** item grabs of carousel, both rail states, breadcrumb, poster grid, and 16:9 episode/clip wall.
**Regression paths:** taskbar door toggle; leave/return; scroll recycling; restart into persisted folder/sort; re-pointed legacy scenarios.
**Evidence artifacts:** `tests/lanista_fixtures/journeys/vault-browse-five-shapes-v1/fixture.json`, `tests/lanista_scenarios/journey_vault_browse.json`, `tests/lanista_layout/journey_vault_browse.json`, and `artifacts/visibility-phase2/j1-vault-browse/<sessionId>/{session.json,forensics.json,layout.json,warnings.txt,carousel.png,rail-collapsed.png,rail-expanded.png,breadcrumb.png,poster-grid.png,wide-grid.png,stdout.log,stderr.log}`.
**Bridge status:** **Bridge blocked until Vault Browse Slice 5 lands**; available afterward using existing state/property actions plus L2.
**Negative control:** force one wide-card fixture to report the poster aspect contract; exactly the named card-aspect/layout rule must fail, then restore.
**Compilation:** normally QML/scenario only after Slice 5; no new native compile.
**Completion criterion:** **Runtime-validated** on the new face. Hemanth's eyes remain the aesthetic gate for spacing/taste; the machine gate covers behavior and geometry.

### Slice N0-Runner: Dedicated Night Watch process, build tree, and owner lock

**Purpose:** Create a safe overnight owner that either starts alone against a clean committed tree or leaves a clear BLOCKED report without killing anything Hemanth is using.
**Dependencies:** all six J1 journeys individually reviewed; a blocked journey remains named in the report and prevents a green battery.
**Implementation guidance:** Create `scripts/night-watch.ps1` and a small testable helper module under `scripts/night-watch/`. Every invocation creates `artifacts/night-watch/runs/<runId>/`, atomically acquires `artifacts/night-watch/owner.lock` with pid, owner executable path, process creation time, start time, and recorded HEAD; a lock is stale only when that exact pid/path/creation-time triple no longer names the owner. Preconditions: current branch `master`, tracked/untracked tree clean, no other Night Watch owner, no running `colosseum.exe`, and no repo-scoped compile/link process. If the daily app is open, write status `BLOCKED_DAILY_APP_OPEN`, preserve pid/path evidence, exit without terminate/kill. Freeze the recorded commit with `git archive --format=tar -o <runDir>/source.tar <headSha>`, verify its embedded commit via `git get-tar-commit-id`, hash the tar, and extract only into `artifacts/night-watch/runs/<runId>/source/`. All later configure/build/tests use that immutable export, never the mutable checkout. Build under `artifacts/night-watch/runs/<runId>/build/`; never call or modify `native/build-msvc`.
**Behavior to preserve:** daily app/process/data, other workstreams, source tree, and all existing build scripts.
**Baseline:** run preflight while the daily app is open and while the worktree is dirty; preserve both blocked reports before enabling build execution.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — orchestration only.
  - Existing harnesses: create `tests/test_night_watch_runner.ps1 -SelfTest` with individually reported cases `lock_allows_one_owner`, `live_lock_refuses_second`, `stale_lock_requires_pid_path_and_creation_time`, `dirty_tree_blocks`, `daily_app_open_blocks_without_kill`, `archive_matches_recorded_head`, and `path_confinement_rejects_escape`; update the test ledger.
  - Negative control: see below.
**Test seam status:** available through injected process/git probes in self-test mode; no real process is killed by tests.
**Lanista actions:** none yet; this slice stops after safe preflight and optional fresh configure/build smoke.
**Completion signal:** lock acquired atomically or a terminal BLOCKED report written; configured build process returns; lock released in `finally`.
**State / events / probes:** pid/path/creation-time list, git commit/tree SHA and clean flag, archive SHA-256/embedded commit, run/source/build dirs, lock owner, command exit codes.
**Visual evidence:** not applicable.
**Regression paths:** two simultaneous self-test invocations produce one owner and one blocked result; stale lock outside the exact Night Watch path is never touched.
**Evidence artifacts:** `artifacts/night-watch/runs/<runId>/{report.json,preflight.log,commands.jsonl,source.tar,source.sha256,source-commit.txt,owner-lock.json}` plus extracted `source/` and `build/` directories.
**Bridge status:** not applicable.
**Negative control:** temporarily weaken the runner's daily-app-open branch so it continues into the build phase; exactly `daily_app_open_blocks_without_kill` must fail, then restore the branch and rerun with the fake process still alive as no-kill proof.
**Compilation:** the runner may perform a fresh configure/build only after preflight; it never uses the shared build directory.
**Completion criterion:** **Test-reported** internal safety slice: safe ownership/preflight proven, including BLOCKED behavior and no-kill evidence.

### Slice N0-Battery: Fresh build, full CTest, journeys, warnings, and wake report

**Purpose:** Leave one report that tells Hemanth whether the whole app battery passed, failed, or was safely blocked—and links every artifact needed to understand why.
**Dependencies:** N0-Runner and all six J1 scenario files.
**Implementation guidance:** Extend the runner into ordered phases: preflight/export → call `vcvars64.bat` → configure exactly `C:\Qt\Tools\CMake_64\bin\cmake.exe -S <runSource>/native -B <runBuild> -G Ninja -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\Ninja\ninja.exe -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DBUILD_TESTING=ON -DCOLOSSEUM_BUILD_PLAYER2=OFF` → build exactly `cmake.exe --build <runBuild>` → inventory exactly `ctest.exe --test-dir <runBuild> -N` → compare every discovered name to committed `tests/night-watch-expected-tests.txt` → run the complete configured graph exactly `ctest.exe --test-dir <runBuild> --output-on-failure --no-tests=error` with no regex/label filter → each J1 scenario in its own isolated tagged session using `<runBuild>/lanista.exe`, `<runBuild>/colosseum.exe`, and `<runSource>/qml/Main.qml` → W0 per session → aggregate report. `COLOSSEUM_BUILD_PLAYER2=OFF` deliberately excludes the gated lab and its two ledgered environmental failures; “full CTest” means every test in this fresh active-app configuration. A missing toolchain is `BLOCKED_ENVIRONMENT`; inventory mismatch is `FAILED_CONFIGURATION`; any executed test red is `FAILED`. Copy no binary into the daily install. Stop on configure/build/CTest failure; continue through journey failures only while isolation is intact. Produce `report.json` plus Hemanth-language `report.md`; atomically update `artifacts/night-watch/latest.json` after report close. Measure wall-clock per phase; promise no threshold until three quiet-machine runs exist.
**Behavior to preserve:** scenario isolation/unique pipes, seed provenance, warning allowlist ownership, and one binary per run.
**Baseline:** record one dry-run plan (`-WhatIf`) and one foreground full run on a clean master while the daily app is closed.
**Focused tests:**
  - Qt Test / Qt Quick Test: the fresh run's full CTest is the deterministic battery.
  - Existing harnesses: extend `tests/test_night_watch_runner.ps1 -SelfTest` with `phases_are_ordered`, `configure_failure_stops`, `build_failure_stops`, `ctest_inventory_mismatch_fails_configuration`, `ctest_red_stops`, `journey_red_aggregates`, `warning_red_aggregates`, `report_schema_is_complete`, and `latest_pointer_is_atomic`; add committed `tests/night-watch-expected-tests.txt` containing the exact CTest names after all Phase 2 registrations and review it against `tests/CMakeLists.txt`.
  - Negative control: see below.
**Test seam status:** available after J1; any Bridge-blocked journey prevents a green overall status and is named.
**Lanista actions:** execute all six journey scenarios via isolated `session run --exe <runBuild>/colosseum.exe`; run W0 against each session's app log/stderr.
**Completion signal:** each process exit code; each scenario's strict waits; final report closed with terminal status `PASSED`, `FAILED`, or `BLOCKED`.
**State / events / probes:** commit, tool versions, phase durations, CTest totals, journey status/evidence dirs, warning totals, blocked reason.
**Visual evidence:** journey grabs linked from report; no pixel-diff score.
**Regression paths:** inject build failure, CTest red canary misuse, journey red, warning red, and report-write failure in self-test mode.
**Evidence artifacts:** `tests/night-watch-expected-tests.txt`; `artifacts/night-watch/runs/<runId>/{report.md,report.json,configure.log,build.log,ctest-inventory.txt,ctest.log,journeys/,commands.jsonl,source.sha256,source-commit.txt}`; and `artifacts/night-watch/latest.json`.
**Bridge status:** available for completed J1 journeys; overall result is **Bridge blocked** if any required journey remains blocked.
**Negative control:** run a copied scenario with one authoritative expected value flipped; exactly that journey and overall battery turn red while other journeys still report their true results, then restore.
**Compilation:** always performs a fresh full build in its dedicated run directory.
**Completion criterion:** **Runtime-validated** battery only when fresh build, full CTest, all six journeys, and all warning gates pass from the same committed HEAD and report; otherwise terminal FAILED/BLOCKED is the correct completion.

### Slice N1-SDK-Gate: Prove the 2026 protocol and Tasks implementation route

**Purpose:** Prove the official Python route can implement the current stateless core and formal Tasks extension before changing the working facade.
**Dependencies:** N0-Battery proven standalone.
**Implementation guidance:** Read the official 2026-07-28 release notes (`https://blog.modelcontextprotocol.io/posts/2026-07-28/`) and Tasks extension overview (`https://modelcontextprotocol.io/extensions/tasks/overview`) fresh. In an isolated Python environment, pin and probe the official MCP Python SDK for stateless per-request protocol metadata, optional `server/discover`, extension capability negotiation, task-augmented tool calls, `tasks/get` (including terminal result/error), `tasks/update`, `tasks/cancel`, normative statuses, and stdio support. Write `docs/visibility/mcp-2026-sdk-gate.md` and machine-readable `tests/contracts/mcp-2026-sdk-gate.json`. Do not edit `server.py` in this slice. If any required feature is missing, stop N1 `Plan contradicted`; N0 remains the standalone path.
**Behavior to preserve:** the entire existing MCP facade and standalone Night Watch are untouched.
**Baseline:** record the current Python runtime/dependencies and v0 server protocol/tool transcript.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — dependency/protocol discovery.
  - Existing harnesses: create `tests/test_mcp_2026_sdk_gate.py` with named cases `stateless_core_supported`, `server_discover_supported`, `tasks_extension_negotiates`, `task_augmented_tool_supported`, `tasks_get_update_cancel_supported`, `tasks_get_carries_terminal_output`, `normative_status_enum_matches`, and `stdio_transport_supported`.
  - Negative control: see below.
**Test seam status:** available through the SDK probe; production migration is blocked until it passes.
**Lanista actions:** none; do not touch the app or live facade.
**Completion signal:** every named SDK-gate case exits 0 and Agent 0 verifies the official source links/version pin.
**State / events / probes:** Python/SDK versions, supported protocol versions, extension identifiers, method/status inventory, stdio capability.
**Visual evidence:** not applicable.
**Regression paths:** existing v0 transcript is rerun unchanged before and after the isolated SDK installation test.
**Evidence artifacts:** `docs/visibility/mcp-2026-sdk-gate.md`, `tests/contracts/mcp-2026-sdk-gate.json`, `tests/test_mcp_2026_sdk_gate.py`, and `artifacts/visibility-phase2/n1-sdk-gate/{probe.json,test.log,v0-transcript.jsonl}`.
**Bridge status:** not applicable; this is the protocol prerequisite.
**Negative control:** in a temporary contract copy set `statelessCore` to `false`; exactly `stateless_core_supported` must fail, then restore.
**Compilation:** none.
**Completion criterion:** **Test-reported** SDK route approved, or **Plan contradicted** with no facade migration attempted.

### Slice N1-Protocol: Upgrade MCP without regressing facade v0

**Purpose:** Move the dev-tool server onto the current MCP protocol and a maintained SDK while every existing interactive tool keeps working exactly as before.
**Dependencies:** N1-SDK-Gate.
**Implementation guidance:** File fence for one executor session: `native/tools/lanista-mcp/server.py`, the repo's one existing Python dependency/lock file, new `tests/test_lanista_mcp_protocol.py`, and `docs/colosseum-lanista-verification.md`. Migrate only transport/request dispatch/tool registration to the SDK; do not add Night Watch tools or refactor CLI/session helpers. The 2026 core is stateless: remove the protocol-level `initialize`/`initialized` handshake, accept protocol version/client identity/client capabilities on every request as the SDK specifies, and implement optional `server/discover` for up-front inspection. Application-level Lanista session handles remain explicit tool state, not MCP transport sessions. Preserve every existing tool name/input schema/deadline/isolation/coded error. Record dependency/startup cost and host compatibility. If the SDK route cannot satisfy the gate, stop/revert and record `Plan contradicted`.
**Behavior to preserve:** all facade v0 and F1-Bridge tools; default-pipe refusal remains scoped to interactive session creation and Drive operations while the legacy three keep their environment-resolved Read target; one interactive session; deadline backstops; CLI runner; standalone Night Watch.
**Baseline:** preserve current v0 JSON-RPC transcripts, tool inventory/schemas, startup time, and host negotiation before migration.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — Python dev-tool protocol.
  - Existing harnesses: create `tests/test_lanista_mcp_protocol.py` with named cases `no_initialize_handshake`, `server_discover_reports_2026_07_28`, `every_request_is_self_describing`, `tool_inventory_is_backward_compatible`, `tool_schemas_are_backward_compatible`, `deadlines_remain_bounded`, `coded_errors_survive`, `application_session_recovers`, and `legacy_transcript_replays`.
  - Negative control: see below.
**Test seam status:** available only after the SDK capability probe; otherwise **Plan contradicted**.
**Lanista actions:** through real stdio MCP, call optional `server/discover`; send self-describing `tools/list`/`tools/call` requests; call `session_start`, `snapshot`, `act`, `get`, `wait_for`, `grab`, `warnings`, `vault_forensics`, `session_stop`, `lanista_call`, `lanista_grab`, and `lanista_snapshot`; replay the legacy three-tool transcript through the compatibility path.
**Completion signal:** `server/discover` reports `2026-07-28`, each independent request carries/accepts required metadata, every call returns before its deadline, and tool inventory/schema equivalence exits 0.
**State / events / probes:** discovered protocol/capabilities, per-request metadata, SDK version, tool names/schema hashes, startup/call durations, application session pid/pipe/root isolation.
**Visual evidence:** one existing facade grab proves binary image transport survived; it is not a protocol gate.
**Regression paths:** all 11 v0 tools plus `vault_forensics`, server restart, stopped-session errors, and CLI scenario path.
**Evidence artifacts:** `tests/test_lanista_mcp_protocol.py` and `artifacts/visibility-phase2/n1-protocol/{server-discover.json,tool-inventory-before.json,tool-inventory-after.json,schema-diff.json,stateless-transcript.jsonl,legacy-transcript.jsonl,timings.json,test.log}`.
**Bridge status:** available only after the official SDK gate; N0 remains available if contradicted.
**Negative control:** remove one existing tool registration in a temporary test copy; exactly `tool_inventory_is_backward_compatible` must fail, then restore.
**Compilation:** no app compile; Python dependency/pin and adapter tests only.
**Completion criterion:** **Runtime-validated** protocol migration with zero facade tool/schema regression; otherwise revert the attempted migration and record **Plan contradicted**.

### Slice N1-Tasks: Expose Night Watch as a durable Task

**Purpose:** Let an agent start an overnight run, release the conversation, and later poll/cancel/update the same durable job instead of holding one fragile tool call open.
**Dependencies:** N1-Protocol and N0-Battery.
**Implementation guidance:** Register a task-augmented tool `night_watch_run` with Tasks support required. Only requests whose per-request capabilities opt into `io.modelcontextprotocol/tasks` may receive a `CreateTaskResult`; `server/discover` and tool metadata advertise the extension explicitly. Persist `artifacts/night-watch/tasks/<taskId>.json` atomically before replying. Use only normative statuses: `working`, `input_required`, `completed`, `failed`, `cancelled`; keep Night Watch phases/durations/annotations as namespaced application metadata or status messages, never protocol statuses. Implement `tasks/get` so terminal replies carry the final result/error, plus cooperative `tasks/cancel`. Cancellation acknowledgement is not proof of `cancelled`: continue polling because work may remain `working` or finish `completed`/`failed` if it wins the race. Implement `tasks/update` only for `inputResponses` keyed to outstanding `inputRequests`; production Night Watch normally creates none. Exercise update through an injected conformance task that enters `input_required`; do not repurpose it as a phase setter. Server restart reloads task files, validates child pid/path/creation time, and returns true state.
**Behavior to preserve:** facade v0 tools and deadlines, the exact legacy-versus-Drive pipe boundary from N1-Protocol, one interactive session, CLI runner, and standalone scheduled Night Watch.
**Baseline:** preserve N1-Protocol's green compatibility transcript and one standalone N0 run before adding task lifecycle.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — Python dev-tool protocol.
  - Existing harnesses: add `tests/test_lanista_mcp_tasks.py` with named cases `tasks_extension_is_explicitly_negotiated`, `night_watch_tool_requires_task_support`, `create_task_is_durable_before_reply`, `status_starts_working`, `tasks_get_polls`, `tasks_get_returns_terminal_result_or_error`, `tasks_update_answers_only_outstanding_input`, `tasks_cancel_is_cooperative`, `terminal_status_never_changes`, `restart_recovers_by_pid_path_creation_time`, `malformed_task_state_fails_closed`, and `existing_tools_unchanged`.
  - Negative control: see below.
**Test seam status:** available after N1-Protocol. If its SDK gate failed, this slice is **Plan contradicted**; do not emulate Tasks by name.
**Lanista actions:** through real stdio MCP: call `server/discover`; invoke `night_watch_run` once without extension support and verify refusal, then with Tasks support and receive a durable task; poll `tasks/get` through its terminal result/error; run the injected conformance task to reach `input_required` and answer its exact key with `tasks/update`; cancel another run with `tasks/cancel` and poll to its actual terminal outcome; restart the server and recover; replay existing facade tools.
**Completion signal:** valid transitions (`working` → `input_required` → `working` → terminal for the conformance task; `working` → terminal for Night Watch) persist atomically and every protocol call returns before its deadline.
**State / events / probes:** extension negotiation, task id/run id, normative status, inputRequests/inputResponses, Night Watch phase metadata, pid/path/creation time, progress totals, report path, terminal result/error.
**Visual evidence:** not applicable; task/report metadata is the evidence.
**Regression paths:** v0 tool transcript, server restart, malformed/stale task file, concurrent start refusal, cancel during each safe phase boundary.
**Evidence artifacts:** `tests/test_lanista_mcp_tasks.py`, durable `artifacts/night-watch/tasks/<taskId>.json`, and `artifacts/visibility-phase2/n1-tasks/{discover.json,no-extension-refusal.json,create-task.json,tasks-get.jsonl,tasks-get-terminal.json,input-required.json,tasks-update.json,cancel.json,restart-recovery.json,test.log}` plus the linked Night Watch report.
**Bridge status:** available after N1-Protocol; otherwise **Plan contradicted**, while N0 remains usable.
**Negative control:** disable atomic persistence before the initial CreateTaskResult; exactly `create_task_is_durable_before_reply` must fail, then restore. Separately confirm a cancel request never calls a daily-app kill path.
**Compilation:** no app compile; Python adapter/task tests only.
**Completion criterion:** **Runtime-validated** durable task lifecycle with backward compatibility. A custom hand-rolled Tasks look-alike is not completion.

### Slice N1-Register: Local-user Task Scheduler registration

**Purpose:** Register one safe local-user schedule for Night Watch without needing Hemanth's password or touching unrelated tasks.
**Dependencies:** N0-Battery; N1-Tasks for task-handle integration. If N1-Tasks is contradicted, the registered action may call N0 standalone but must pass `-McpTasksUnavailable` so every report states that limitation.
**Implementation guidance:** One-session file fence: create `scripts/install-night-watch-task.ps1`, `scripts/uninstall-night-watch-task.ps1`, and scheduler cases in `tests/test_night_watch_runner.ps1`; update only the Lanista/test ledgers they affect. Register a task named `Brotherhood-Colosseum-Night-Watch` for the current local user, **Run only when user is logged on** (Qt GUI journeys need an interactive desktop), default 03:00 local pending Agent 0 verification, working directory fixed to the Colosseum repo, single-instance policy `IgnoreNew`, and action invoking the checked-in runner with a bounded PowerShell command line. Do not store credentials, use SYSTEM, wake the machine, or auto-kill blockers. Installer is idempotent and exports the registered task XML to the run evidence. Uninstaller removes only the exact named task after confirming its action points to this repo.
**Behavior to preserve:** all unrelated scheduled tasks and daily app behavior.
**Baseline:** query and preserve whether the exact task name exists; do not alter a same-name task with a foreign action—stop and report conflict.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable.
  - Existing harnesses: extend `tests/test_night_watch_runner.ps1 -SelfTest` with named cases `task_xml_has_local_user_principal`, `task_runs_only_when_logged_on`, `task_action_and_working_dir_are_owned`, `task_trigger_matches_requested_local_time`, `task_ignores_concurrent_start`, `installer_is_idempotent`, and `uninstall_refuses_foreign_action`.
  - Negative control: see below.
**Test seam status:** available using exported XML and injected scheduler command wrapper; live registration is the final Windows integration proof.
**Lanista actions:** none; registration is Windows configuration, not an app drive.
**Completion signal:** scheduler query returns the exact owned task/principal/trigger/action/settings and exported XML matches the validator.
**State / events / probes:** registered principal, trigger time, action/working directory, multiple-instance policy, owned-path marker.
**Visual evidence:** not applicable.
**Regression paths:** installer twice is idempotent; foreign same-name task refused; uninstall removes only the exact owned task; reinstall restores it.
**Evidence artifacts:** `artifacts/visibility-phase2/n1-register/{Brotherhood-Colosseum-Night-Watch.xml,install.log,query.json,uninstall-refusal.log,reinstall.log,scheduler-selftest.log}`.
**Bridge status:** not applicable.
**Negative control:** temporarily weaken the uninstall ownership guard so it accepts a foreign action path; exactly `uninstall_refuses_foreign_action` must fail, then restore.
**Compilation:** none.
**Completion criterion:** **Test-reported** registration slice with exact ownership, idempotence, and uninstall safety proven.

### Slice N1-First-Run: First scheduled pass and daily-app block

**Purpose:** Prove the registered schedule leaves the promised wake report and safely blocks when Hemanth's daily app is open.
**Dependencies:** N1-Register and N0-Battery; use N1-Tasks when available.
**Implementation guidance:** No product edits. Trigger the owned scheduled task once with the daily app closed and once with it open. Poll through `tasks/get` when N1-Tasks passed, otherwise through the atomically written `latest.json`, always with a fixed overall deadline. The closed-app run must execute the full N0 battery. The open-app run must stop at preflight, report `BLOCKED_DAILY_APP_OPEN`, and preserve the exact same daily-app pid. If either run exposes a runner defect, return to the owning N0/N1 slice rather than patching behavior here.
**Behavior to preserve:** registered task configuration, daily app process/data, unrelated tasks, and all N0 battery gates.
**Baseline:** scheduler's last-run result and `latest.json` before either trigger.
**Focused tests:**
  - Qt Test / Qt Quick Test: the closed-app scheduled run's full CTest output names every executed test; no additional test target in this slice.
  - Existing harnesses: `tests/lanista_scenarios/journey_open_manga.json`, `journey_play_video.json`, `journey_identify.json`, `journey_tray.json`, `journey_ceremony.json`, and `journey_vault_browse.json`; W0 `tests/warning_gate.ps1` once per session; `tests/test_night_watch_runner.ps1 -SelfTest`; and `tests/test_lanista_mcp_tasks.py` when N1-Tasks is available.
  - Negative control: see below.
**Test seam status:** available after N1-Register.
**Lanista actions:** indirect through the scheduled N0 battery: all six isolated journeys; no daily/default-pipe drive.
**Completion signal:** Task Scheduler reaches a terminal instance state and the matching report closes `PASSED` for the closed-app run; the second report closes `BLOCKED_DAILY_APP_OPEN` before build.
**State / events / probes:** scheduler last result, run/task id, report status, daily-app pid before/after, full evidence directory.
**Visual evidence:** the six journey grabs linked from the first scheduled report; optional Task Scheduler screenshot is documentary only.
**Regression paths:** rerun the blocked case; confirm no build directory/process/session was created; confirm the next closed-app run can pass.
**Evidence artifacts:** `artifacts/visibility-phase2/n1-first-run/{closed-run-id.txt,closed-report.md,closed-report.json,blocked-run-id.txt,blocked-report.md,blocked-report.json,tasks-get.jsonl,latest.json,Brotherhood-Colosseum-Night-Watch.xml,daily-pid-before.json,daily-pid-after.json,scheduler-last-result.json}`.
**Bridge status:** available for N0 standalone; MCP polling available only if N1-Tasks passed.
**Negative control:** temporarily mutate the report aggregator to classify `BLOCKED_DAILY_APP_OPEN` as `PASSED`; exactly `blocked_run_never_reports_passed` must fail, then restore and rerun the live block control.
**Compilation:** the closed-app scheduled run performs its own fresh dedicated build; the blocked run performs none.
**Completion criterion:** **Runtime-validated** after one scheduled full pass and one scheduled daily-app-open BLOCKED pass, both leaving correct wake reports and never killing the daily app.

---

## Soak track (S0–S3) — added by Agent 0 on Hemanth's order, 2026-08-12

**What this is:** the overnight soak run Hemanth commissioned — the app driven for hours across
varied content while narrating its own behavior into a structured event stream, so the morning
report answers "where does it degrade?" rather than only "did the fixed battery pass?". Journeys
are deterministic and prove correctness; the soak is volumetric and hunts the class journeys
structurally cannot find — leaks, slow degradation, handle exhaustion, the failure that only
appears on the two-hundredth repetition. It is the machine equivalent of a hundred testers'
accidental volume.

**Ground truth this track stands on (verified in live code 2026-08-12):**
`native/devtools/LanistaEventLog` already writes a timestamped, tailable `events.jsonl` under the
session's AppData root — but exactly ONE emitter exists today (`log-mark`,
`LanistaServer.cpp:1130`). The pipe is built; the app says almost nothing into it. S0 fixes that.
Because the path is derived from AppDataLocation, a tagged session automatically gets its own
event file; no new isolation machinery is needed.

**Track rulings:**
1. **The daily app's narration budget stays zero.** Soak emission arms only under a tagged
   AppData root or an explicit `COLOSSEUM_PULSE=1`; an unflagged daily launch emits exactly what
   it emits today (marks on request, nothing else).
2. **No live network in v0.** Download exercise runs only against a loopback fixture source with
   a byte budget. Hammering real sources all night is a product/etiquette ruling reserved for
   Hemanth, and the no-default-acquisition-sources law applies. Theatre streaming soak is
   explicitly deferred (the video path carries known harness fragility; its journey is already
   ordered last for the same reason).
3. **The soak never flips the battery.** N0-Battery's PASSED/FAILED/BLOCKED is decided entirely
   by the deterministic phases. The soak digest attaches as intelligence; its findings route to
   Hemanth and Agent 0 for triage, and a confirmed finding becomes a J0 seed. This keeps the
   wake report's terminal status trustworthy while the soak is young.
4. **Seeded randomness only.** The driver's action mix is drawn from a recorded RNG seed; any
   run is reproducible from its manifest. Variety without unrepeatability.
5. **Isolated synthetic library.** The soak never sees Hemanth's real profile or his 8-item
   library. Its content is programmatically synthesized at scale from existing fixture
   primitives (CBZ synthesis is pure zip; video items are fixture copies under varied names —
   no new binary dependency assumed or permitted).

### Slice S0-Pulse: The app narrates its own behavior

**Purpose:** Give the soak (and every future agent session) a heartbeat: authoritative app
events — opened, ready, failed, memory, frame health — appended to the existing per-session
`events.jsonl`, so behavior over hours becomes data instead of vibes.
**Dependencies:** J1-Manga-Seam and J1-Video-Seam (their authoritative readiness signals are the
event sources for open/ready truth); Phase 1.
**Implementation guidance:** One new devtools-owned observer (`native/devtools/PulseNarrator.h/.cpp`),
constructed beside the Lanista server in the devtools wiring and armed per ruling 1. It CONNECTS
to existing authoritative signals only — the J1 seam properties, download lifecycle signals, and
`QQuickWindow::frameSwapped` for frame-delta health — plus a coarse timer sampling process
memory via the Windows process API. It owns no state machine and adds no signals to lane code:
observation by connection, zero edits inside Tankoban/Theatre/Vault lane logic. Event record
shape (versioned, documented in the ledger): `{type, at, subject, durationMs?, outcome?,
bytes?, memMb?, droppedFrames?}` with a small fixed type vocabulary (`open`, `ready`, `fail`,
`download`, `mem`, `frames`, `nav`). Bound the emission rate (coalesce frame/mem samples;
hard cap events/minute) so an eight-hour run cannot produce an unreadable or disk-eating file.
**Behavior to preserve:** daily-app emission stays exactly today's (ruling 1 proven by test);
`log-mark` unchanged; no lane file edited.
**Baseline:** capture today's `events.jsonl` after a scripted isolated session — expect only
marks; record the file's growth as ~zero. That emptiness is the before.
**Focused tests:**
  - Qt Test: new cases in the devtools test family — event record schema, rate cap, armed/unarmed
    gating (unarmed emits nothing beyond marks).
  - Qt Quick Test: not applicable.
  - Existing harnesses: `-L unit` green; W0 warning gate on the verification session.
  - Negative control: see below.
**Test seam status:** available (seams land in J1; the event log is shipped code).
**Lanista actions:** one isolated scripted session performing a manga open + a local video play +
a fixture download; `events-tail` then asserts the expected event types appeared with sane
values.
**Completion signal:** `events-tail` returns the expected typed records; strict property waits
throughout the driving scenario.
**State / events / probes:** the event stream itself is the probe under test.
**Visual evidence:** not applicable.
**Regression paths:** unarmed daily-style launch (no tag, no flag) → byte-identical emission
behavior to today; rate-cap flood test (rapid page flips) stays under the cap.
**Evidence artifacts:** `artifacts/visibility-phase2/s0/{events-sample.jsonl,rate-cap-test.log,unarmed-control.jsonl}`.
**Bridge status:** available (`events-tail` ships).
**Negative control:** temporarily arm the narrator in the unarmed configuration; exactly the
unarmed-gating case must go red (events appear where none are allowed), then restore and rerun
green.
**Compilation:** yes — serialized through the shared build gate.
**Completion criterion:** **Runtime-validated** — the typed events observed live in an isolated
session, the unarmed control clean, rate cap proven.

### Slice S1-Digest: The morning triage report

**Purpose:** Turn hours of events into the page Hemanth actually reads: counts, latency
percentiles per operation, failures grouped by subject, memory trend, frame-health summary, and
a top-N worst list — so "that's a bug right there" takes one look, not a log excavation.
**Dependencies:** S0's event schema (fixture-testable independently of any live run).
**Implementation guidance:** `scripts/soak-digest.py` (Python 3, stdlib only): input one session
root, output `soak-digest.md` (Hemanth-language) + `soak-digest.json` (machine). Sections:
operation counts; p50/p95/max duration per event type and per world; failure table grouped by
subject with first/last timestamps; memory start/end/peak and growth slope; dropped-frame
episodes; top-10 slowest subjects; W0 warning verdict folded in. The digest declares its own
coverage honestly — worlds visited, item counts, wall-clock — so an aborted run can never
masquerade as a full one.
**Behavior to preserve:** none (new, runner-side only; zero app code).
**Baseline:** run against a hand-authored fixture `events.jsonl` with known statistics; the
expected digest is committed beside it as the golden contract.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — Python.
  - Existing harnesses: new `tests/test_soak_digest.py` — golden-fixture equivalence, empty-input
    honesty (digest says "no data", never fabricates), malformed-line resilience (skip and
    count, never crash), percentile math against known values.
  - Negative control: see below.
**Test seam status:** available.
**Lanista actions:** none.
**Completion signal:** `test_soak_digest.py` exits 0.
**State / events / probes:** the golden fixtures.
**Visual evidence:** one rendered sample digest attached as the report format Hemanth approves —
the format itself is a product surface and gets his eyes before S3 wires it into the wake
report.
**Regression paths:** fixture with one poisoned line; fixture with zero failures (digest must
say so plainly, not render empty tables).
**Evidence artifacts:** `tests/fixtures/soak/{events-golden.jsonl,digest-golden.md}`,
`artifacts/visibility-phase2/s1/{sample-digest.md,test.log}`.
**Bridge status:** not applicable.
**Negative control:** corrupt one duration in the golden fixture; exactly the golden-equivalence
case must go red naming the differing statistic, then restore.
**Compilation:** none.
**Completion criterion:** **Test-reported**, plus Hemanth's approval of the digest format
(`Human aesthetic verdict` applies to the report layout — it is the product here).

### Slice S2-Driver: The soak driver

**Purpose:** Drive the app for hours, unattended, across a synthesized library — open, read,
scroll, navigate, play, download-from-fixture — with seeded variety, hard budgets, and honest
teardown, writing nothing but events, logs, and a manifest.
**Dependencies:** S0 (events), S1 (digest), J1-Manga-Seam + J1-Video-Seam (readiness waits),
N0-Runner's lock module (reused for exclusive ownership).
**Implementation guidance:** `scripts/soak-run.py` over the existing `lanista` CLI (the facade's
own pattern — subprocess with explicit `--timeout` per call, no naked pipe reads). Parameters:
`--minutes` (budget, hard), `--seed` (recorded), `--worlds` (default tankoban+vault+local-video),
`--content-root`. Provisioning step synthesizes the soak library per track ruling 5 into the
run's tagged root. Loop: pick world/action from the seeded distribution → act via CLI →
`ui-wait-for` the authoritative readiness signal (never a sleep) → next. Downloads: loopback
fixture source only, byte-budgeted (reuse the http fixture machinery the header-channel harness
already uses; if that machinery cannot serve as a local source, mark `ASSUMPTION failed` in the
report and ship v0 without the download mix rather than improvising a new server). Teardown at
budget: stop the session cleanly, run the W0 gate, run S1's digest, write
`manifest.json{seed,budget,mix,coverage,exitReason}`. Any wait timeout is recorded as a finding
and the loop continues with the next action — a soak survives individual failures and reports
them; it does not abort on the first.
**Behavior to preserve:** one session at a time (reuse the lock); the shared build directory is
never touched; the daily app never signaled.
**Baseline:** a 5-minute foreground smoke soak on the synthesized library, digest produced,
before any long run is attempted.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable — orchestration.
  - Existing harnesses: `tests/test_soak_driver.py -SelfTest` style — seeded-mix determinism (two
    dry-runs with one seed plan identical action lists), budget termination (a 30-second budget
    run ends within grace), provisioning idempotence, lock exclusivity (second driver refused).
  - Negative control: see below.
**Test seam status:** available after the seams; the driver adds none.
**Lanista actions:** the run itself: sessions on unique pipes, tagged roots, `session_start`-style
spawn per the facade pattern, item grabs only on findings (a grab per anomaly, not per action).
**Completion signal:** budget-reached teardown with manifest closed, or clean early exit with
`exitReason` — never a hang (every CLI call carries its own deadline).
**State / events / probes:** the event stream, the manifest, coverage counters.
**Visual evidence:** anomaly grabs only, linked from the digest.
**Regression paths:** kill the app process mid-soak externally — driver detects, records
`exitReason: app_died`, digests what it has, exits nonzero; the partial digest still renders.
**Evidence artifacts:** `artifacts/soak/runs/<runId>/{manifest.json,events.jsonl,soak-digest.md,soak-digest.json,logs/,anomaly-grabs/}`.
**Bridge status:** available.
**Negative control:** plant one deliberately corrupt CBZ in the synthesized library; the run must
complete, and the digest's failure table must name exactly that subject; then remove it and the
failure table is clean.
**Compilation:** none (Python + existing binaries; runs only between other workstreams' build
gates when foreground, or inside Night Watch's dedicated tree in S3).
**Completion criterion:** **Runtime-validated** — one foreground soak of ≥30 minutes on the
synthesized library: budget honored, digest produced, corrupt-fixture control demonstrated,
zero isolation breaches in the manifest.

### Slice S3-Watch: The soak rides the Night Watch

**Purpose:** The full circle Hemanth asked for: the app works through the night after the
deterministic battery, and the wake report carries both the battery verdict and the soak
digest — extensive data waiting in the morning.
**Dependencies:** S2 and N0-Battery (N1-Tasks optional; when present, soak phase progress rides
the task's application metadata).
**Implementation guidance:** Extend the Night Watch runner with an optional post-battery soak
phase: `-SoakMinutes <n>` (default 0 = off; the schedule Hemanth approves sets the real budget).
The phase runs S2's driver against the run's own fresh build (`<runBuild>/colosseum.exe`) and
tagged root, inside the existing owner lock, after the battery report is already closed — per
track ruling 3 the battery's terminal status is decided before the soak begins and is never
rewritten. The wake `report.md` gains a soak section: coverage, top findings, digest link.
Soak-phase wall-clock is bounded by budget + fixed teardown grace; a soak that will not die is
killed at grace and recorded `exitReason: killed_at_grace` (this kill targets only the soak's
own tagged session process — the no-kill law for the daily app is untouched).
**Behavior to preserve:** every N0-Battery gate, its terminal statuses, the lock discipline, the
blocked-daily-app behavior — all byte-identical when `-SoakMinutes 0`.
**Baseline:** one Night Watch run with soak off after S2 lands, proving byte-identical battery
behavior; preserve its report as the control.
**Focused tests:**
  - Qt Test / Qt Quick Test: not applicable.
  - Existing harnesses: extend `tests/test_night_watch_runner.ps1 -SelfTest` with
    `soak_off_changes_nothing`, `soak_runs_after_battery_close`, `soak_grace_kill_scoped_to_session`,
    `report_gains_soak_section_only_when_run`.
  - Negative control: see below.
**Test seam status:** available after S2 + N0-Battery.
**Lanista actions:** the embedded soak run, isolated as in S2.
**Completion signal:** Night Watch terminal report closed with the soak section present and the
digest linked; scheduler instance terminal.
**State / events / probes:** battery status (unchanged), soak manifest, digest, phase durations.
**Visual evidence:** anomaly grabs linked from the digest, as S2.
**Regression paths:** soak-off control run; blocked-daily-app run with soak configured (must
block before any soak, exactly as before).
**Evidence artifacts:** `artifacts/night-watch/runs/<runId>/soak/` (S2's layout) + the extended
`report.md`.
**Bridge status:** available; task metadata optional per N1-Tasks outcome.
**Negative control:** temporarily wire the soak phase before the battery report close; exactly
`soak_runs_after_battery_close` must fail, then restore.
**Compilation:** none beyond the battery's own fresh build.
**Completion criterion:** **Runtime-validated** — one real overnight scheduled run with a nonzero
soak budget, wake report carrying both verdicts, battery control run proven identical with soak
off.

---

## Final program gate

Phase 2 can close green only when Agent 0 can point to all of the following from one committed master HEAD. If F0 triggers a stop law or N1's official SDK gate is contradicted, the plan records that terminal outcome honestly; it does not call the full program complete.

1. F0's current owner/thread map approves the F1 seam. If it does not, F1 is absent, dependants stay blocked, and the overall program ends `Plan contradicted` rather than green.
2. L1 sees unnamed items and their structural/geometry ownership without shipping GammaRay; L2 produces scoped nonzero/containment/peer-overlap verdicts.
3. If F0 approves the safe seam, F1 returns one bounded app-owned Vault response through Lanista/MCP, never a second connection or mutation, and `VaultIndex::publish()` is unchanged. If F0 triggers a stop law, F1 remains absent, its dependent journeys stay `Bridge blocked`, and the overall Phase 2 program closes **Plan contradicted**, never complete/green.
4. Each of the six J1 journeys has its own J0-admitted real-bug seed or explicitly non-zoo deterministic fixture, exact scenario, authoritative completion signal, L2 checkpoint where relevant, W0 verdict, negative control, and isolated evidence directory. Vault Browse targets Slice 5; Identify also waits for Slice 6.
5. N0 exports one immutable committed HEAD into its run directory, builds that export fresh with the pinned configuration/inventory, runs full configured CTest plus all six journeys and warnings, and writes an honest terminal report tied to the exported commit/tree/archive hash.
6. N1 proves the official SDK route, migrates to the stateless 2026-07-28 core without tool/schema regression, and exposes the run through the formally negotiated Tasks extension with normative lifecycle/method semantics.
7. Windows Task Scheduler registration is separately proven, then a first scheduled run under the local user blocks rather than kills when the daily app is open and leaves the first real wake report.
8. Both capability ledgers describe the final actual estate, commands, scenario inventory, status, limitations, and evidence paths.
9. The soak track (S0–S3) has run one real overnight scheduled pass: typed app events emitted only under its armed configuration, the digest format carrying Hemanth's approval, the driver's seed/budget/coverage manifest honest, and the battery's terminal status provably unchanged by the soak's presence. Gate on the mechanics, not on discoveries — a soak that ran clean and found nothing still passes; a soak that cannot prove isolation or budget does not.

## Plan self-review performed

- Every slice carries purpose, dependencies, bounded guidance, preservation law, baseline, named deterministic tests, test-seam status, Lanista actions, a real completion signal, state/probes, visual evidence, regressions, exact artifact roots, bridge status, a named negative control, compilation impact, and completion/status vocabulary.
- No Planned/Unavailable capability is treated as present: L1 and the tray command are explicit prerequisites; typed events, absence assertions, secondary-window automation, semantic image verdicts, and OS tray picking are not claimed.
- Every user-visible journey uses state/property waits and warnings; no sleep, route-only proof, screenshot-only proof, live profile, default pipe, live network, or direct SQLite archaeology appears.
- F0 and L1-Discovery are the only parallel investigations. All build/app gates serialize, and Night Watch uses an immutable exported source plus a fresh dedicated per-run build tree.
- Vault Browse Slice 5 is explicit for Ceremony/Vault Browse and Slice 6 for Identify; the retiring shelves face is never targeted.
- The MCP upgrade decision and cost are explicit. N0 remains useful if the official Tasks SDK gate contradicts N1.
- No unfinished placeholder or unowned product decision remains. The only uncertainties are labeled `ASSUMPTION — Claude to verify` with a defined stop/fallback.
- **Amendment 2026-08-12 (Agent 0):** the soak track S0–S3 was added on Hemanth's order after this plan was delivered; the original author's integrity hash covers the pre-amendment revision and the repo commit is the amended truth. The soak reuses the delivered plan's own organs — J1 seams for readiness, N0-Runner's lock, N0-Battery's report — and holds three of its own rulings: zero daily-app narration budget, no live network in v0, and battery verdicts never rewritten by soak findings. Real-source download soak and streaming soak are named deferrals reserved for Hemanth's ruling, not silent omissions.
