# Feria shared browser layer: GLM navigation and close slice

> **For GLM:** implement this bounded assignment natively, task by task. Use `superpowers:executing-plans` if available; the brief is self-contained. No additional workers or approval round is required for the authorized branch work. Return the implementation for Codex receiving review; do not merge or release it.

**Goal:** connect QtWebEngine and Windows WebView2 to one Colosseum navigation/consented-close contract, with real engine evidence through the existing owners.

**Architecture:** SessionStore, FeriaAccountStore and FeriaBrowserPolicy remain canonical. A small shared operation/request seam captures identities and exactly-once outcomes. Qt and WebView2 adapters retain their respective native mechanisms and lifetimes; the existing deck and presentation consume normalized behavior.

**Tech stack:** existing C++/Qt Quick application, QtWebEngine 6.11.x and installed Windows WebView2 SDK/runtime. Prefer C++ for durable shared rules and typed operation state; keep required Qt-specific QML glue. Do not rebuild Chromium/Qt or add a CEF runtime.

**Spec:** `docs/feria-browser-engine-contract-v1.md`, including the new consented-close adapter semantics. This assignment implements only navigation, per-space consented close and the minimum binding needed to prove those paths. Other contract groups remain targets for later slices.

## Authority and starting point

- Hemanth explicitly assigned this first slice to GLM. Codex owns receiving review and accepted main integration.
- Work in a clean isolated Git worktree/branch. Start from the committed revision containing this brief and contract update; record its full SHA. The production source baseline is `62057bfbdaa2305a05d2d3fb02b870bc9e826638`; this handoff changes documentation only.
- Bring in the standalone probe commit `1197508b0ecf8199afb42131e5aeea4eeeb1662d` from `codex/feria-webview2-close-probe` if it is not already present. Its files are under `tests/feria_webview2_close_probe/`. Record that test-only import separately from implementation commits.
- Read applicable repository-local AGENTS.md and only the live code/tests necessary for this scope. Do not bootstrap Preflight documents, its authority/memory/plans/handoffs, or Hemanth Memory Bridge. Existing unrelated changes must stay untouched.
- Use existing reviewed native source/API evidence and working code. This is implementation plus targeted mapping, not another audit of all 52 categories. Use supported Qt/WebView2 mechanisms; Chromium/CEF source may inform a specific lifetime decision but does not justify replacing the host stack.

## Global constraints

- Preserve identity field names/meaning: `accountEpoch`, `profileId`, `spaceId`, `pageGeneration`, `documentGeneration`, `hostId`, `hostGeneration`.
- No duplicate session, account, history, restore, download or preference store. Native handles and pending native deferrals belong inside adapters, not canonical storage.
- Exactly one terminal outcome per admitted operation: `succeeded`, `cancelled`, `failed`, `blocked`, `unsupported` or `invalidated`. Immediate dispatch failures also settle once. Capability unavailable/unsupported is a visible gap, not a passing test.
- Keep normal Qt functionality and public naming/UI direction. No top tab strip, generic tools drawer, automatic engine fallback, cookie migration or new browser entry.
- Controls/consent must be outside the website viewport. A focused test host may reserve a chrome region; do not introduce an overlay over the page or a settings/sidebar redesign.
- Retained background spaces keep their own identity and native context. Selection does not retarget navigation, a consent answer or controller disposal.
- Header X scope is still unresolved. Preserve its existing binding. Implement per-space semantics here; close-app aggregation and its product binding are outside this slice.
- Dots is separately implementing categories 16-20 (playback/downloads/save). Do not take over or merge that work. Playback, private-profile factory/lifetime redesign, popup routing, presentation transfer, permission/download bridges, engine-source patches, codecs and DRM/provider qualification are outside this assignment.

## Review focus

Each item below must have a regression/negative scenario in the owning task:

1. Stay before any native navigation ID exists; immediate close retry still targets the same document.
2. A consent answer after selection/account/profile/document/host identity changes; it cannot act on a replacement or sibling view.
3. A competing ordinary navigation while close/unload is pending; it cannot be mistaken for terminal close proof.
4. Disposal, timeout or renderer loss while a native consent deferral is retained; native ownership drains safely and the shared result settles once.
5. Internal about:blank unload corrupting visible URL, history, restore/reopen descriptors or document generation; it must stay part of the close transaction.

## Source map and ownership

Inspect these live owners first:

- `native/SessionStore.h`: `browserIdentity`, `beginBrowserClose`, `settleBrowserClose`, `advanceBrowserDocument`, `setBrowserHost`. Existing close state and generations remain authority.
- `native/feria/FeriaBrowserPolicy.*`, `FeriaAccountStore.*`: admission and persisted metadata. Read the needed paths; do not modify download/playback machinery for this slice.
- `qml/feria/FeriaSessionDeck.qml`, `FeriaQtWebEngine.qml`, `FeriaWebView2.qml`, `FeriaCloseController.qml`, `FeriaCloseConsent.qml`: retained binding, native wrapper/state, close and consent presentation.
- `native/feria/provider-host/FeriaProviderHost.*`, `FeriaHostItem.*`: existing Windows native controller, navigation, events and QQuickItem glue. Extend these rather than creating a second WebView2 owner.
- `native/feria/CMakeLists.txt`, `native/main.cpp`: minimal source registration/wiring when needed.
- `tests/feria_foundation_build/CMakeLists.txt`, `tests/feria_foundation_runtime.cpp`, `tests/feria_foundation_probe/tst_Navigation.qml`, `tests/feria_hosting_probe/tst_Hosting.qml`, `tests/feria_lifecycle_probe/tst_Lifecycle.qml`, and the standalone WebView2 close probe.

GLM owns only its branch edits in these navigation/close paths, any necessary small shared seam under `native/feria/`, and focused tests/build wiring. SessionStore changes must be minimal and preserve existing Qt caller compatibility. Do not refactor the whole deck or frontend. If another owner is genuinely necessary, record its exact purpose and keep changes inside the assignment.

## Task 1: shared operation seam and canonical ownership

**Files:** existing owners above; create a small typed seam under `native/feria/` only when those owners cannot hold the engine-neutral boundary cleanly. Add its focused native tests/build target without requiring a full application build.

**Interface requirements:** commands carry captured canonical identity, explicit `engineId`, unique `operationId` and operation kind. Requests additionally carry a one-use `requestId`, captured source metadata and purpose (app close versus external unload). Results carry the original identity/operation, typed terminal outcome, reason/evidence kind and optional native navigation ID. These field meanings are fixed; choose idiomatic concrete C++ declarations and document their exact names/signatures in the delivery mapping. Missing native IDs/provenance remain explicit, not invented.

- [ ] Map current command callers, close state and native callbacks to this minimal seam. List kept/extended/replaced code, supported APIs and exact file ownership before editing. Keep the mapping concise, then proceed with implementation.
- [ ] Add failing tests proving duplicate results settle once, old identities cannot close a current space, and selecting a sibling cannot retarget a captured request. Include invalidation of each identity dimension and distinguish an explicit Stay from timeout/unknown acknowledgement.
- [ ] Implement the minimum shared operation/request fencing through existing owners. SessionStore's close attempt can serve as the operation ID; do not maintain a second competing close ledger. Shared code must not receive raw COM/Qt request pointers.
- [ ] Run the focused native tests and existing relevant foundation/session/hosting tests. Record exact commands and results; commit this independently reviewable change.

## Task 2: native navigation/close adapters and retained binding

**Files:** existing Qt/WebView2 wrappers, close controller/consent, Windows provider host/item and minimum deck/build registration. Reuse the Task 1 ticket/result types and source of truth.

**Produced behavior:** native navigate/back/forward/stop/reload plus canGoBack/canGoForward and honest load outcomes; per-space requestClose and one-use respondToRequest; supported-capability state consumed by the existing presentation. Init, DOM-ready, main-document replacement and terminal load success remain distinct. Internal close navigation has explicit purpose; it is never ordinary restore success.

- [ ] Add focused failing runtime cases for actual adapter binding before changing behavior. Instantiate real Colosseum adapters connected to SessionStore in a focused host, with two retained spaces and local fixtures. A standalone raw controller or adapter double does not meet this integration test.
- [ ] Wrap the working Qt native history and RequestClose/close/rejection path. Preserve its actual cancellation barriers and opener guards. Do not globally reinterpret dialogReject as a Qt renderer acknowledgement.
- [ ] Extend the existing WebView2 host with captured ScriptDialogOpening requests and supported deferrals. For a current app-close Stay, complete without Accept and settle only that explicit decision after successful completion and identity validation. Do not require NavigationStarting/Completed for this cancelled attempt or synthesize a Qt rejection signal.
- [ ] Implement the qualified WebView2 native unload candidate, or prove a safer supported alternative. Leave remains pending until actual correlated departure is observed. Do not dispose on Accept, a timer, a script result or native dispatch S_OK. Preserve the original reopen descriptor; suppress internal unload from user URL/history/restore/document-generation pathways. If overlapping native events cannot be associated safely, report blocked/invalidated and preserve the replacement.
- [ ] Wire the minimum retained bindings and request controls through the shared owners. Existing callers should use normalized outcomes; do not leave the new seam unused beside the existing production path. An external navigation unload Stay/Leave is consent for that navigation, not permission to delete the app space.
- [ ] Fence stale/duplicate answers, account/document/host replacement and teardown; drain outstanding native requests even when the UI-facing operation has already been invalidated. Renderer loss/timeout may produce truthful failed/blocked outcomes; recovery redesign is not required here.
- [ ] Run both actual engines and relevant existing Qt regressions, inspect the diff and commit the adapter/integration change separately.

### Native evidence to reuse, with limits

The standalone probe passed 38 assertions twice on WebView2 SDK 1.0.3800.47 / runtime 154.0.4258.62, with a forced controller Close rejected by the negative control. It proved Stay retention, same-document retry, close after cancelling a trusted site navigation, sibling survival and no-handler/no-gesture departure. It did **not** instantiate SessionStore or the Colosseum deck/host wrappers.

Stay was observed before NavigationStarting, with no cancelled-navigation terminal callback. Qt's acknowledgement path therefore cannot simply be copied into WebView2. CDP input required native MoveFocus; check trusted input rather than assuming a dispatched click worked. ExecuteScript diagnostic reads observed activation on this runtime, so no-gesture fixtures use Runtime.evaluate with `userGesture:false` and no ExecuteScript before unload. Read the probe README and its event traces; carry these controls into the integrated harness.

## Task 3: parity evidence and delivery

**Files:** focused shared-owner/adapter integration tests and local delivery artifacts. Extend existing harnesses when practical; document build/run commands if a new focused host is required.

- [ ] Run matching Windows Qt/WebView2 scenarios: native history/back/forward; redirect and stopped/failed load; Stay preserving exact form/document/native URL/history; immediate retry; cancelled trusted site-navigation then app close; Leave and sibling survival; no-handler/no-gesture unload; stale/duplicate consent after selection and each identity change; competing navigation; pending-deferral teardown/renderer loss; and internal unload not consuming restore/reopen or changing descriptors. Classify every case pass/fail/blocked/not-run separately per engine.
- [ ] Run a meaningful RED/GREEN control that disables a production adapter fence or substitutes unsafe forced disposal and makes the corresponding integration assertion fail for the intended reason. A setup error, crash or timeout is not a successful negative control. Keep the standalone probe passing as supporting API evidence, separately from integration assertions.
- [ ] Run touched Qt foundation/hosting/lifecycle regressions. Linux Qt qualification is required for touched shared behavior: run it if the environment is available, otherwise explicitly report it not-run for receiving review. Do not fabricate Linux evidence or quietly waive the remaining gate.
- [ ] Record actual SDK/runtime/Qt/compiler versions, source and executable hashes, fixture identities, commands, exit codes, assertion counts and event traces. Separate source inspection, builds, doubles, real-engine integration and full-app/provider checks. No Netflix/Prime sign-in or DRM claims in this batch.
- [ ] Inspect final diff, preserve unrelated work and commit only the assignment. Push the isolated branch if access permits; if blocked, provide a verified Git bundle/patch and precise error. Do not merge main, release or deploy.
- [ ] Deliver a short `DELIVERY.md`, concrete shared-interface/current-source `MAPPING.md`, per-engine `RESULTS.json` and logs, final SHA, ordered commits, changed-file list and explicit remaining gaps. Supply actionable receiving commands so Codex can reproduce the results.

Save delivery artifacts in the local `delivery/` directory Hemanth specifies in the dispatch message, outside the repository. The implementation/test source belongs in the branch. Do not commit machine-specific build paths, profiles, credentials or generated logs.

## Completion standard

Return working, connected code and real-engine evidence for this slice, not just an abstract interface or renewed audit. A passing isolated probe is not assembled-host parity. Continue all independent in-scope work if one native API case is blocked, preserve the safe existing path and identify the exact remaining limitation. Codex will review and qualify the delivery before any production integration. This task does not claim full browser parity or completion of all 52 categories.
