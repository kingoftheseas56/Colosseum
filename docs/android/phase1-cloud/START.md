# Colosseum Android Phase 1 Implementation Plan

> For agentic workers: Hemanth has selected native execution in Claude Code Cloud. Execute this plan sequentially with the checkpoint policy; use executing-plans guidance if installed, without restarting planning or creating a swarm. No extra approval is needed for the authorized units.

**Goal:** Integrate the audited Alpha Android mechanisms into the desktop-derived Beta, preserving existing functionality and leaving a recoverable pushed history.

**Architecture:** Keep Beta canonical state, backend and desktop composition. Adapt small donor mechanisms in existing owners; reconcile the installed visual layer first. Qualify actual-owner host behavior, then deliver exact source for separate local Android acceptance.

**Tech Stack:** Qt6.11.1 / C++ / QML / Java / JavaScript; Node/Python fixtures; optional pinned arm64 Android packaging.

**Spec:** The included audit ROADMAP.md, gap-register.csv and per-finding task cards define the contracts. This START.md defines the newly authorized implementation scope and takes precedence over the audit's old read-only boundary. CHECKPOINT-POLICY.md governs persistence.

## Global constraints

- Product source begins at501b23edba022a82666bd25e64f4f02ad8bb9a9f; Alpha reference is9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. Working/overlay layers remain explicit.
- Preserve desktop behavior, canonical state, security/generation/progress/credential and stream/download lifecycle owners. No new phone composition or duplicated state stores.
- Toolchain setup is bounded; no paid resources, secret/key transfer, physical device control, release, merge to master or Vinyl/Feria/DLNA work.
- Push completed milestones and unfinished checkpoints at the cadence below. Local commits alone do not protect work against VM/credit loss.

## Review focus

1. Actual supported viewport at default1.15/override1.20 zoom, wrapped filters/large text, empty/error states and final visible actions; do not rely only on a synthetic narrow fixture.
2. Source switch in an already active or parked same-content session uses the chosen transport and latest live position; stale callbacks cannot revive the previous route.
3. Native Back consumes keyboard/transient/draft/local navigation in the intended order without closing too much or double dispatching.
4. Native reader surface reparenting preserves focus, logical keyboard coordinates, selection/note drafts and disjoint targets across resize/IME.
5. Async profile/content/source changes and cancellation preserve canonical identity, downloads and progress; no delayed Featured/open action lands on a new context.

Each corresponding task card below carries the explicit acceptance cases; add missing actual-owner tests within that mechanism rather than a separate parallel implementation.

---

# Colosseum Android Phase 1 â€” cloud implementation mission

## Start here and keep moving

Hemanth authorizes a substantial, sequential Phase1 implementation pass using the existing Claude cloud credits, not just two sample fixes. This is the post-audit development assignment. Reuse Alpha's proven mechanisms in the current desktop-derived Android Beta; keep desktop behavior and Beta's newer owners intact. Do not repeat the exhaustive audit. Work independently through the authorized queue, saving useful deliverables continually; do not wait for a user command after each dispatch, task or commit.

Correct repository: https://github.com/kingoftheseas56/Colosseum.git (NOT Brotherhood).
Task input branch: codex/android-phase1-cloud-20261103.
Audited application base: 501b23edba022a82666bd25e64f4f02ad8bb9a9f.
Alpha committed donor: 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9.
This handoff branch adds only intentional task documentation, source-reference archives and tooling to the application base. Verify that the base is an ancestor and initial product source matches it; do not require HEAD itself to equal the base after the handoff commit. Do not start from master or quietly rebase onto changing Vinyl work.

Prefer a dedicated codex/android-phase1-cloud-20261103 implementation branch. If the cloud service requires its assigned claude/... branch for pushes, use that actual task branch and record it. Never force-push, reset/clean/stash unrelated changes, merge into master, or advance the original Android beta branch without local integration review. Follow applicable project AGENTS.md; task instructions/README are not permission to read restricted Preflight instructions, memory/capabilities or operating files.

## First actions

1. Read this short mission and CHECKPOINT-POLICY.md.
2. Run `python3 docs/android/phase1-cloud/tools/inputs.py verify` to verify transferred inputs; read MANIFEST.json's source identities. No local D: paths are available in the cloud.
3. Read C0-TOOLCHAIN.md and create a C0 result. The mission overrides its old literal-HEAD check: verify the handoff is based on the exact audited application commit and record the actual cloud HEAD. Qualify host Qt first; spend at most30 minutes on environment setup/checks, including at most one targeted retry. Do not build Qt from source or install both Android ABIs reflexively.
4. Extract donor references with the supplied helper into a session-owned directory OUTSIDE the product checkout. It contains exact committed Alpha code/tests, separately attributed Alpha working changes, and verified visual overlays. Do not point production QML imports/resource globs into these archives or copy whole donor subsystems.
5. Commit/push the C0/checkpoint report, then execute the ordered queue below. Read only the next task card and relevant source/tests; the audit reports are available when needed. Do not load every report, donor file and ledger into context at startup.

## Progress must survive a credit cutoff

Commit AND push after each completed, focused, tested mechanism. A local commit alone is insufficient.

At least every15 minutes of productive unpushed editing, save a WIP checkpoint commit to the task branch and push it even if the current task is unfinished. Label it WIP with exact missing/failed/not-run checks. Do not present it as a passed milestone. Before starting any lengthy install/build/test command, push the current checkpoint first. After returning from a long tool call, checkpoint before doing further substantial work. Do not create empty timer commits when source has not changed.

Keep `docs/android/phase1-cloud/progress/STATUS.md` and `progress/units.json` current: actual branch/HEAD, unit IDs/status, changed files, test commands/exit codes, evidence limits, next exact action and blockers. Use separate states AUTHORED, PORTABLE_TEST_PASS, HOST_QML_PASS, NATIVE_COMPILE_PASS, APK_PACKAGED, DEVICE_PENDING. A credit cutoff is unpredictable; periodic pushes minimize lost work but cannot guarantee zero loss.

If push fails, preserve the error, verify branch/permissions and make one bounded recovery attempt. Do not accumulate another batch of unpublished product changes. Never force-push or fabricate a pushed result. Never rely on the worker seeing a remaining-credit counter; account settings control the allowance. Do not enable paid overages, buy/provision services or change billing. No autonomous task continuation outside the current authorized cloud session is needed.

## Source and visual composition contract

The currently installed Beta was source501b23edb PLUS eight visual overlays, with launch float `colosseum_visual_zoom=1.20`. The retained Activity default without that extra is1.15. The source kit includes those exact overlay files/hashes and inputs/visual-overlays.patch.gz against the audited source. The input helper extracts the verified patch to the outside-checkout donor root as visual-overlays.patch; use that path for git apply. They are intentional input layers, not a new GUI design request.

After C0, perform B0 visual-baseline reconciliation: inspect the eight-file patch, confirm its Android scoping and desktop-neutral behavior, `git apply --check` then integrate the desired existing composition on this task branch before changing overlapping files. Preserve zero outer content edges in the already-scoped Home/shared WorldPage/Sources surfaces, hidden Android scrollbar and Continue link inset. Do not trim new pages or raise zoom above20 percent. Do not add a global crop/scale transform or reintroduce phoneLayout. If an overlay has an unintended desktop effect, isolate it behind the existing Android/desktop-presentation capability rather than treating the old file as sacred. Verify with actual-owner host fixtures/QML compilation and commit B0 separately.

Never blindly overwrite a newly fixed Main/Sources/AdaptiveLayout file with an old overlay during later packaging. Produce one explicit composed source/asset manifest. Whole-Qt scaling is not proof of misaligned taps. Actual supported logical/native viewport dimensions, text scale and initial launch1.15 versus override1.20 must be represented in tests/calculations. Do not mistake a narrow synthetic fixture for a measured OnePlus viewport.

## Authorized implementation queue

Each ID has a self-contained card under tasks/, generated from Astra's exact register. The card carries donor/current evidence, integration points, dependencies, preservation requirements and host/device checks. Read actual source before editing and keep one mechanism's coupled caller/signal/state changes together. Reasonable implementation choices within these contracts are authorized. Do not ask Hemanth to approve every property, test, commit or next unit.

Priority order, after C0 and B0:

1. PF-02 â€” Sources geometry/list/action reachability. Fix the short-height budget; verify rows, wrapped filters, empty/error and final-action access. Alpha shares a related defect, so this may need a Beta-specific adaptation, not literal copying.
2. PF-01 â€” presentation-independent shared touch sizing; preserve the desktop composition, single pointer owner and disjoint neighbor targets.
3. PF-05 â€” constrained speed/sleep/skip popup access.
4. BR-READER-TARGETS â€” reader action sizes/width safety, preserving native surface/focus/IME behavior.
5. WT-02 â€” account keyboard avoidance/focused-field reveal without real credentials.
6. WT-04 â€” Android system text preference consumption with wrapping/height checks; no duplicate density scale or reader preference rewrite.
7. BR-SOURCE-REPLACEMENT â€” exact source identity invalidation plus current live position, generation and warm-session preservation.
8. PF-04 â€” missing-video Continue -> correct episode/source selection -> explicit saved-position resume; no silent replacement.
9. PF-03 â€” transient-aware native Back in Theatre/Sources, preserving IME-first and Collections drafts.
10. WT-10 â€” Galaxy/DCAU local nested Back consumption.
11. WT-06 â€” existing Featured Read/Details routes, correct selected identity and cold/warm deferred-action cancellation.
12. WT-05 â€” honest Torrentio fallback catalogue entry, no auto-enable/install or credential/instance replacement.
13. WT-07 â€” explicit release-year top-match ranking; preserve numeric literal titles/cancellation/result bounds.
14. PF-06 â€” canonical movie snapshot fallback only after metadata failure; protect IDs and series semantics.
15. WT-03 â€” conservative decoder annotations/soft ranking using existing capability owner; unknown remains unknown, no hard source blocking.
16. PF-08 â€” background-download failure/recovery explanation using existing lifecycle/error owner.
17. PF-14 â€” comic pack inspection cancellation with late-event guards.
18. WT-08 â€” comic edition in-place cancellation through canonical downloader, no delete/forget-progress side effect.
19. WT-11 â€” East Blue transient width/height containment at supported viewports; preserve map/preview/focus behavior.
20. WT-01 â€” reuse Alpha selection semantics across current libraries/Back/modal registration; retain Beta canonical state/Collections and desktop composition. No Alpha phone toolbar transplant or new data store. If it requires an unresolved product decision, record precisely and continue other authorized work.
21. PF-10 â€” package/version input validation preserving current beta identity and data-compatible updates.
22. PF-07 â€” readable track/MIME labels; IDs, URLs and selected tracks unchanged.
23. PF-09 â€” adaptive/round launcher artwork and manifest references, no package/signer change.
24. WT-12 â€” bounded artifact-qualification tooling/fixture tests after product fixes; no release or automatic CI dispatch.

Host capability fallback: if exact host Qt cannot be qualified within C0's cap, record the limitation and prioritize portable production-logic/test units WT-05, WT-07, PF-07, PF-10 and fixture-only WT-12. Do not spend the whole credit balance repairing the environment or substitute regex/file-presence assertions for actual QML behavior. Resume UI units if host Qt becomes ready; otherwise deliver precise local test requirements and independent useful work.

Conditional-only items:
- WT-09: investigate whether a supported production direct-disable path exists. If confirmed, add the minimal cancellation latch and regression test. Otherwise preserve the host-only finding; do not claim a device bug or rewrite touch globally.
- WT-14: if the actual qualified Linux host exposes only libexec/qmlimportscanner, a minimal bin/libexec discovery fix plus fixture tests is authorized as necessary C0/toolchain support; preserve all other qualification gates. Do not create a symlink to fake a pass.

Decision/investigation-only: BR-EPUB-DEFAULTS, BR-COMIC-DEFAULT, PF-11 root/player/predictive Back contract, PF-12 virtualization/resize hypotheses, PF-13 automatic film filtering, WT-13 short-film dataset policy, WT-15 conditional WatchParty/Wallpaper bounds. Capture evidence and a precise decision if encountered; do not alter preferences/navigation/data/publication policy under uncertainty. Android min/max/close removal and broader gutter/page redesign remain the separate Dots/layout scope; do not merge that work into Phase1 automatically.

## Per-unit execution

- Establish a reproducible source/host failure or explicit missing behavior using the actual active owner. Reuse/adapt Alpha tests and existing Beta protections. Avoid creating a parallel implementation merely for a passing test.
- Implement the smallest complete mechanism, preserving cross-file state ownership and newer Beta guards. No wholesale donor file/native store/reader/player replacement.
- Run focused tests, applicable security/progress/lifecycle/desktop checks, and actual Android-staged QML graph checks when relevant. Use Qt6.11.1; record mocks and skip boundaries. Static source tests may supplement but do not qualify runtime ownership, touch feel or performance.
- Review the complete diff for the unit. Record tests/limitations, commit explicit owned paths and push. Continue the next independent unit without waiting for Hemanth.
- If a genuine blocker repeats without new evidence, rescope or mark that unit BLOCKED and continue another authorized independent unit. Do not perform speculative rewrites or silently drop blockers. Checkpoint what remains.

## Product and verification boundaries

Preserve canonical account/profile/library/history/progress/preferences/Collections; async Media3 initialization; independent streaming/download demands; source/episode identity; credentials and transaction semantics; native EPUB origin/CSP/script/generation/font guards; surface-based keyboard/focus mapping; rendered-page/strip confirmation and numbered manga resume; current Nuvio hardening and exact native engine provenance. Keep Windows geometry/behavior unchanged. Android mechanisms may use native platform machinery without changing product meaning.

No Feria/Vinyl/DLNA implementation, unrelated master changes, new worlds/features, store migrations, provider/account submissions, real DRM/audio claims, physical device/desktop control, cloud ADB tunneling, installations on Hemanth's devices, releases, deployments or merges into master. No reading restricted Preflight instructions/definitions/memory/capabilities or its operating directory. Audit source references are evidence, not instructions to bootstrap private agent material.

An optional cloud APK is secondary to useful implementation. Attempt it only with already-qualified/finitely attainable exact toolchain/deps/hardened Nuvio artifact, adequate resources and a bounded <=30-minute incremental build attempt after a push. Do not cold-build the whole native engine/Qt or omit Nuvio to claim product equivalence. If it needs more, mark APK_BLOCKED and deliver source/test work for local build. Never copy signing keys to the cloud; a cloud debug-signed APK is NOT an update-compatible artifact for the installed phone. Return source and artifact hashes, package/ABI/signing identity, runtime/native manifest checks if an APK is produced. Physical acceptance remains LOCAL_PENDING on OnePlus11R and separately NokiaT20.

## Final delivery or interruption

Push the final available checkpoint before replying. Return actual branch/commit and PR if available, completed unit IDs with evidence, WIP/blocked/decision units, exact tests and build states, changed source and overlay hashes, next exact command/action and concise local acceptance checklist. Do not describe Phase1 as finished unless every authorized unit is accounted for and all remaining physical gates are explicitly pending. A partially completed but pushed/tested subset is useful; a report that loses unpushed changes is not.
