# Proposed shared-owner/QML interface

**Design proposal only. No declarations, implementation or prototype were added.** Source-verified interfaces are listed separately in [current implementation](colosseum/EXISTING-IMPLEMENTATION.md). Public engine mappings and exclusions are in the three donor reports. This proposal extends current owners; it does not create another session, transfer-history, file or media-progress database.

## Existing authority retained

| Concern | Existing authority / bounded extension |
|---|---|
| Browser identity | SessionStore.browserIdentity and its existing generation/host operations |
| Account history and one-use write grants | FeriaAccountStore; extend its result and admission descriptor without another writer |
| Native requests and transient entries | One FeriaBrowserDownloads engine shim per profile, owned by FeriaSessionDeck |
| Profile retention/retirement | FeriaSessionDeck plus existing FeriaBrowserPolicy runtime profile/view ownership |
| Navigation/admission and safe Open policy | Extend FeriaBrowserPolicy; resolve admitted metadata through FeriaAccountStore |
| Owned pause | Existing FeriaPlayback presentation ledger and FeriaQtWebEngine activity dispatch; observation remains FeriaMediaSession |
| Native Save context | Extend FeriaPageTools and engine shim; no ad hoc network downloader |
| Presentation | Existing QML app-specific sidebar/request region and app-style Downloads/history |

LocalDownloads is an aggregator, not an arbitrary browser-file authority. LocalLaunch validates known media routes but has Vault/recent side effects and no Feria grant scope. It is reusable only for an explicitly selected in-app media-opening action after Feria authorization. Existing Theatre download paths and portable download intents must not silently become a competing browser-transfer store. Exact source anchors are in the current implementation report.

## Identity envelopes and operation scope

Preserve the contract's names and meaning: `accountEpoch`, `profileId`, `spaceId`, `pageGeneration`, `documentGeneration`, `hostId`, `hostGeneration`. Include `engineId` and an opaque adapter profile binding; native pointers never cross into shared UI. Existing `accountSessionId`/`accountVisitToken` are mapped explicitly by the current owner, not silently renamed or replaced.

Each command carries a unique `operationId`. Native transfer identity is profile-generation-scoped plus native request ID and an app-stable `downloadId`; a numeric native ID alone is insufficient across destroyed/recreated profiles.

Three scopes prevent over-fencing:

1. **Document/context operation:** activity response, initial Save context and page request must still identify the intended document/frame/context. Navigation/replacement invalidates the operation. A native frame handle alone is not a document token.
2. **Presentation response:** a host move may legitimately rehost pending controls. The shared owner explicitly updates the presentation lease while preserving the same originating operation; replies from the old host cannot answer it. Moving a view is not a reason to recreate the page or transfer.
3. **Accepted transfer:** source identity becomes immutable historical attribution. Tab close/navigation/host movement does not by itself invalidate progress or an already-issued metadata grant. Current account/profile/transfer validity still gates callbacks. A native page-serialization SavePackage is a documented exception: it can still depend on the source page/resources. Carry transferKind and an explicit document-dependent phase; retain that dependency or truthfully cancel/fail when the source retires, rather than promising ordinary network-transfer survival. Account switching uses explicit retirement/cancellation; no grant or late completion can be redirected to another account.

This distinction preserves the known lesson that a presentation lease can survive navigation, while document-bound operations capture fresh identity. Do not apply one generic 'all generations must still equal selected page' test to an accepted transfer.

## Proposed commands and projections

Names below are semantic proposals, not existing public APIs.

### 16: activity and owned playback

`setActivity(binding, operationId, reasonSet)`

`reasonSet` is a bounded set of current app/world/minimize reasons. Ordinary inactive spaces in the displayed app do not acquire a suspension reason. The existing document-owned ledger records only media observed playing and actually paused by the app. Adding/removing overlapping reasons must not create two pause owners or resume while any applicable reason remains.

`activityResult(operationId, terminal, coverage, observations)`

- `terminal`: succeeded, cancelled, failed, blocked, unsupported or invalidated; never invent success from a timeout.
- `coverage`: explicit inspected roots/media and inaccessible/unobservable scope, not a claim of universal control.
- Observation receipts identify exact document/frame/element/source and app-owned pause reason; user commands, replacement and retirement revoke the receipt.
- Resume is reported from actual observed outcome. A play() call or rejected Promise is not success; Qt JavaScript return serialization cannot simply return a Promise.
- Per-frame scripts/Qt frame APIs may be evaluated later as supported mechanisms, with document nonces and bounded resource cost. Cross-origin/closed-shadow/provider/EME/WebAudio control remains capability-qualified.
- Freeze remains optional. Combine known app request/close/consent blockers with engine recommendation. If safe suspension around an unobservable licence/request lifetime cannot be established, retain an active page rather than promise safe freeze. No DRM qualification follows.

### 17: admission versus history permission

`downloadOffered(requestToken, originSnapshot, nativeFacts, destinationPolicy)`

The Qt profile creates and parents the native request before emission. The engine shim receives/retains that object internally at the first direct profile event and creates only the app token/entry. Capture the actual available source view, canonical binding, initiating origin/frame **only when exposed**, native ID, ordinary/private status, transfer URL and suggested filename. Preserve unavailable fields as unavailable. Native request URL is not the initiating-page origin. Capture cannot solve an unqualified source-pointer lifetime before public signal delivery; the pre-delivery close fixture is a release gate.

`decideDownload(requestToken, decisionToken, destinationDecision)`

- The policy determines whether configured default auto-save already authorizes a destination or whether a user destination/overwrite decision is required. A chooser is not universally mandatory.
- A distinct one-use decision binds that native request and approved destination. Invalid/expired decisions cannot be applied to a newer request or selected space.
- Qt request connection remains direct. Pinned source/test support delayed answers, despite conflicting documentation; retaining QML/native lifetime and page/profile teardown must be qualified before replacing immediate acceptance.
- Filename override is a validated leaf under an admitted destination. Preserve engine-selected/collision-adjusted defaults subject to admission; do not infer blanket sanitation or a demonstrated default-path traversal flaw. Reject or explicitly resolve separator/reserved-name/collision/overwrite cases.
- Ordinary metadata authorization remains an independent FeriaAccountStore grant. Private or unowned attribution does not magically become ordinary history. Product policy decides whether an unattributable native transfer is allowed without history or blocked; never retarget it.

### 18: live rows and actions

`downloadRow(downloadId)` projects native `state`, `receivedBytes`, `totalBytes`, `isPaused`, `isFinished`, interruption detail where public, originating app/privacy and `metadataState`.

`requestDownloadAction(downloadId, operationId, action)` where action is pause, resume or cancel.

- Unknown total and zero total differ. Native Completed is success; Interrupted, Cancelled, downloadFinished or bytes==total are not substitute success predicates.
- Guard public Qt pause/resume by native state and `!isFinished`. No public CanResume guarantee exists; represent a supported resume attempt separately from confirmed resumability and observed completion.
- Cancellation requested is not automatically underlying transfer/file cleanup completed. Qt can locally set an Interrupted request to Cancelled/isFinished without calling native cancelDownload. Preserve that distinction and reconcile late native updates/partial files; completion may also win a race. Repeated signals/actions remain idempotent.
- Snapshot completion after related native properties have settled: Qt stateChanged(Completed) precedes received/total updates. Do not consume the metadata grant with stale byte counts merely because the state signal arrived first.
- Retire native-action capability when the request disappears. Bound/prune finished projections without deleting canonical history or the user's file.

`openDownloadedFile(downloadId)` / `revealDownloadedFile(downloadId)`

FeriaBrowserPolicy resolves a live/private completed binding through the existing FeriaBrowserDownloads owner and an ordinary historical binding through FeriaAccountStore. It then rechecks current account/private scope, admitted destination, normalized native path and current file/directory existence. Private/unowned transfers need no ordinary history grant or persisted row to enable a valid live Open; keep their file/admission capability ephemeral in the existing owners. UI supplies an ID, never an arbitrary path. Report unavailable/replaced/invalidated/unsupported rather than launching another file. Filesystem checks cannot promise an atomic race-free shell launch; state the platform limit and test path replacement. 'Open containing folder' and 'select file in shell' are distinct capabilities.

### 19: metadata and profile lifetime

Extend the existing metadata admission descriptor with stable transfer binding and admitted file identity, while retaining one-use grants and synchronous-reentrancy protection.

`recordBrowserDownloadOutcome(downloadToken, completedDescriptor)` →

- `saved`: one successful atomic commit, consume grant once.
- `retryableWriteFailure`: preserve valid grant and old state; explicit retry only.
- `invalidated`: stale account/profile/retired grant, no retry into a replacement account.
- `rejected`: inadmissible descriptor/source URL/identity, no endless disk-error retry.

This is a proposed typed companion/extension, not a claim the current boolean method already has these results. Preserve compatible existing callers while integrating the typed path. Do not automatically admit credential-bearing, blob or data URLs into ordinary history merely to eliminate a retry message.

Existing profile lifetime accounting gains dependent native requests/transfers, rather than another registry. Final private-space close waits for valid transfer/request dependencies under the chosen policy. Explicit account/profile retirement invalidates decisions/grants and cancels/disposes native transfers using supported engine lifecycle, with later callbacks fenced. Qt destruction paths provide cancellation mechanics; lack of an app-level terminal-drain event is an acceptance question, not proof of a native use-after-free.

Persist history only, unless a separately authorized native-resume design is later supported. Historical completion does not prove current file availability. Private/off-record excludes ordinary metadata/browser persistence; it does not delete files explicitly saved by the user.

### 20: context capability and Save

`captureSaveContext(binding, contextSerial, nativeSnapshot)` → `contextId`, supported kinds/formats, safe presentation facts, provenance availability and current capability flags.

`saveFromContext(contextId, operationId, kind, destinationDecision)`

- Bind to original view/document and current native context serial. New context, navigation, source retirement or stale presentation response invalidates the operation.
- Use Qt's native DownloadLink/Image/MediaToDisk or SavePage action in the original current context. Public QML has no arbitrary download(url); C++ QWebEnginePage.download is available only where an actual public page is owned, not through a private Quick adapter fiction.
- Qt default menu already exposes Save actions, but menu presence/action.enabled is not sufficient capability gating. Check public media type/MediaCanSave, URL admission and unsupported contexts explicitly. Native Quick menu does not enforce every such flag.
- Public Qt context APIs do not expose stable originating-frame/document identity. The native download path uses a main-frame request plus private context referrer information. Do not synthesize a frame ID or claim exact subframe fidelity; unsupported/ambiguous contexts must remain unavailable or explicitly qualified.
- SavePage action produces a requested download for normal admission. Explicit-path C++/QML save autoaccepts and may write without a listener: obtain destination consent first, or do not call that path. A later UI decision is too late.
- Serialize initial page-save admission where one SavePageInfo per WebContents can be overwritten. Accepted resource transfers retain independent request ownership; page serialization may retain a source-document dependency until its native phase completes.
- Page formats are SingleHTML, CompleteHTML and MHTML. Existing Print/PDF remains separate. Canvas, empty/opaque/MSE/protected media and build-excluded features are not promised.

## UI and integration limits

Request controls remain outside website content in the reserved chrome region or app-specific sidebar. Address unfolds vertically; sidebar may resize a stable embedded viewport. Preserve app-only taskbar, app-local watch/read/soundspaces and same-live-context Max/return. The proposal is an operation/state boundary, not styling or another Browser Tools drawer.

WebView2 is a separate mapping/qualification lane. Reconcile these shared semantics with its actual native deferrals and profile/request lifetimes before implementation. No CEF runtime, engine replacement, production change or prototype has been authorized or implemented by this audit.
