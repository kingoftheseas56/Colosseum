# Bounded decisions: capabilities 16–20

Status: source-based audit recommendations, not implementation or runtime qualification. All decisions are against Colosseum `62057bfbdaa2305a05d2d3fb02b870bc9e826638` and the exact donor pins in [PINS.md](donors/PINS.md). The proposed engine contract supplies requirements, not proof that an interface exists.

## Selection

| Category | Selected approach | Rejected substitution | Decisive boundary |
|---|---|---|---|
| 16 Owned pause/resume | **Keep existing Colosseum implementation**, narrowly adapt identity/reason/result reporting; use supported Qt frame/script APIs only where qualification justifies extending coverage | Chromium MediaSession system suspension, CEF mute/WasHidden, or Qt freeze as an owned-pause replacement | Existing element/document/source ledger already protects ordinary user pauses. System reasons are too coarse; mute/freeze are not receipts. Cross-frame and autoplay outcomes remain bounded. |
| 17 Download admission | **Keep existing profile-level owner and metadata grants; use supported public Qt APIs.** Extend captured request binding and explicit destination/admission policy | Per-window listeners, selected-tab attribution, treating metadata grant as file consent; replacing owner with CEF per-browser callback routing | Capture at first native request event. Qt request/view lifetime before that event still needs a fixture. CEF loses client updates after source-browser destruction. |
| 18 Progress/actions/Open | **Keep native Qt state/actions; adapt safe Open and truthful action availability into existing owners** | Importing Chromium DownloadItem, equating CanOpenDownload with safe completed-file Open, or optimistic completion on cancel | Completion is native state, not byte ratio/finished signal. Resume is an attempt, not a guarantee. Open resolves an admitted completed identity and rechecks current file availability. |
| 19 Persistence/private cleanup | **Keep FeriaAccountStore history/grants/QSaveFile; adapt transfer-dependent profile lifetime and typed metadata outcomes** | Chrome HistoryService/CEF history as another canonical store; treating serialized rows as native restart checkpoints; deleting saved private files by implication | Accepted resource transfers outlive source tabs; page serialization can remain document-dependent. Last private-view close currently cancels transfers. Account retirement is a different explicit cancellation boundary. History, live transfer and saved file are separate facts. |
| 20 Native Save | **Use supported public Qt native Save/context actions through existing profile pipeline; adapt narrow context retirement/admission** | Scraped/interpolated JavaScript URLs, an ad hoc downloader, private Qt adapter access advertised as public API, or adding CEF as another engine | Explicit-path save autoaccepts; SavePage action waits for admission. Public QML lacks download(url). Native context is mutable and exact original-frame identity is unavailable through inspected public APIs. |

No donor component is selected for source copying. Each donor report records file-specific notices/dependencies for inspected candidates. Using public APIs does not make an internal class portable, and sharing Chromium internals is not three independent confirmations.

## What actually needs changing later

1. **Preserve working policy before extending it.** Category16 is not missing. Preserve inactive spaces' playback within the displayed app. Add explicit result/coverage reporting and distinguish overlapping suspension reasons without creating a second media-progress store.
2. **Separate three authorizations.** Native transfer admission/destination policy, a one-use metadata-writing grant, and a later Open/reveal decision are distinct. Existing default auto-save is an implemented policy; the product must decide whether to retain it, add an ask preference, or require another explicit destination rule. Lack of a chooser alone is not proof of a bug.
3. **Fix the Open boundary.** Current live/history controls open stored paths without action-time availability/authorization checks. Route by owner-resolved download/history identity, not a caller-supplied path. No arbitrary-path exploit was demonstrated in this audit.
4. **Retain private transfer dependencies.** Do not retire a private profile merely because its view count reaches zero while a valid transfer/native request still depends on it. Account switch/sign-out follows its explicit cancellation/retirement policy and must reject late metadata into another account.
5. **Type history failures.** A transient QSaveFile failure may retain the same grant for retry. Revoked grants, inadmissible URLs and rejected descriptors need terminal invalidated/rejected outcomes, not endless 'Retry saving history'. A saved blob/data file need not be eligible for ordinary URL history.
6. **Commit a settled completion snapshot.** Qt emits Completed before updating final byte totals; current Feria can commit prior counts on that signal and never refresh its recorded row. Coalesce/reconcile the complete native snapshot before the one-use write. This is source-inferred and needs a native-order fixture.
7. **Add only missing Save integration.** Qt already has native default context-menu save behavior, including menu entries whose enabled state does not enforce MediaCanSave. An explicit app-owned interface must capture/retire context correctly and preserve native ownership. Print-to-PDF is a separate existing path, not proof of download-pipeline page saving.

## Comparison across required dimensions

| Dimension | Evidence-based choice |
|---|---|
| Correctness | Retain native state machines and existing one-use/reentrant metadata guards; add the precise missing boundaries above. Inspecting an engine test is not observing a product pass. |
| Origin/request targeting | Capture originating view/profile and available context at admission; selection never supplies identity. Preserve unavailable frame/gesture fields as unknown. Chromium source-frame attribution can retire while a transfer survives. |
| Async lifetime | Qt profile owns request objects. Keep direct signal delivery and validate delayed destination lifetime against pinned source/tests despite contradictory docs. CEF callback survival does not ensure continued notifications after browser retirement. |
| Privacy/account isolation | Reuse SessionStore/FeriaAccountStore scope. Private history exclusion is separate from user-saved file deletion. Revoked old-account grants cannot be retried into the current account. |
| Concurrency | One listener per profile, unique transfer/operation binding; no selected-view singleton. Serialize initial page-save admission where one native SavePageInfo is shared per WebContents. |
| Cancellation/retry | Distinguish public request state from underlying transfer/file cleanup: Qt cancel on Interrupted can finish the wrapper without calling native cancelDownload. Retry only a valid request/grant; no fabricated completion or cleanup acknowledgement. |
| Dependency footprint | Existing QtWebEngine dependency and narrow application-owner extensions. CEF or Chromium implementation transplants would import browser context, IPC, network, callback and version-maintenance cones with no evidence of need. |
| Resource cost | Bound/prune finished entry projections; history is already capped. Retaining a private profile for an active transfer is intentional cost. Per-frame observation adds IPC/DOM work and must stay bounded. No memory/CPU benchmark was run. |
| Portability | Qt6.11.1 public API is target authority. Qt vendor .225 lineage, declared .264 and CEF .214 are not identical. Chrome-only orchestration missing from the Qt vendor is reference-only. Windows behavior is unverified. |
| Maintainability | Extend the existing owners and publish typed adapter capabilities. Avoid a new store, shadow transfer scheduler, private Qt bridge or perpetual engine fork for unsupported precision. |

## Supported, conditional and unsupported are different

- Supported API use: native request state, accept/cancel/pause/resume, SavePage/context actions, page-save formats, documented frame/script mechanisms within their limits.
- Conditional/proposed: delayed QML destination UI, stronger origin capture, frame-specific media coverage, truthful asynchronous result acknowledgements, safe Open/reveal, private transfer leases. These need the later acceptance fixtures.
- Unsupported/unresolved through the inspected public surface: universal media pause/resume, exact originating-frame Save fidelity, arbitrary QML download(url), native download resurrection from metadata, private saved-file erasure, canvas/MSE/DRM/protected media export and any provider qualification.
- Reference-only: Chromium/Chrome/CEF internal algorithms and broader browser UX. No public Qt call is inferred from those classes.

## Coordination boundaries

The separate WebView2 parity audits own retained profiles, close/popups, activity/media and requests/downloads/recovery mapping. Their local results were not used or presumed accessible here. Reconcile captured identity, one-use response semantics, account teardown, activity outcomes and download lifetime with those reports before a later implementation assignment. This three-family source comparison does not establish WebView2 parity and does not authorize embedding CEF.

UI remains the existing app-only taskbar, app-local spaces and retractable app-specific sidebar. Reserve request controls outside the website viewport; History/Downloads remain app-style pages. No generic Browser Tools drawer, production UI rewrite or provider/DRM qualification is part of this audit.
