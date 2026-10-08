# QtWebEngine source/API audit: original Feria categories 16–20

Exact QtWebEngine revision: `eb0793cc4b76e93cf669f586fd68c76019f40ec9` (v6.11.1). Authoritative source: https://code.qt.io/cgit/qt/qtwebengine.git.

Source inspection only. No builds, tests, runtime harnesses or implementation were executed. All tests below were read, not run. This is the embedding-layer column, not independent corroboration of shared Chromium internals.

## Receiving version and authority

Qt metadata declares Chromium 140.0.7339.264 with security backports through 148.0.7778.96. Exact bundled gitlink is `58c11ad487f8a237cf0ac71cc3e818b52db150df`; its VERSION/import baseline remains 140.0.7339.225. Use the vendor pin for bundled behavior. Security-patch labels do not confer Chromium 148 APIs. See `../pins.json` for independently resolved ancestry. Windows target is Qt 6.11.1; cloud Qt executable version is unverified/not used.

## Decision summary

- Keep existing Colosseum playback policy and profile-level download/history owners. Add narrow correctness checks in those owners, not five rewrites.
- Use Qt public request controls and native save/context actions; exact-frame targeting, arbitrary QML download(url), restart-resume and private saved-file erasure are not supported claims.
- Explicit-path save autoaccepts in C++ and QML. Use SavePage action for deferred admission, or obtain destination consent before explicit-path save.
- The documented immediate-deletion claim for unanswered requests disagrees with pinned implementation and C++ delayed-answer test. Keep direct signal delivery, explicit request ownership and a QML delayed-consent/teardown acceptance fixture.
- Frozen, audible and context media toggles are not selective pause provenance.

## 16. Pause on app minimize/world-away; resume only media paused by Colosseum

Evidence status: Source/API path and relevant lifecycle/script tests inspected; exact media pause provenance is application-owned, not supplied by Qt. Cross-origin media/autoplay behavior unqualified.

### Recommendation

Keep existing activitySuspended/presentationMedia and callback/lifecycle blockers. Harden scoped per-document/per-frame/per-element owned-pause acknowledgements as needed; optionally use public isolated scripts and Qt 6.8 frame API. Do not replace policy with freeze, audible, mute or context toggle.

Classifications: Keep existing Colosseum implementation; Use a supported public engine API; Adapt donor behavior into an existing Colosseum owner; Internal reference only / unsupported / unresolved.

### API/ownership trace

Keep space/document/element pause records in existing Colosseum playback ownership. App visibility/world-away policy must remain distinct from normal tab selection and Qt optimization. Resume only the exact living element/document that an app command observed playing and successfully paused; revoke on explicit user pause, source replacement, document/frame retirement, user override, and app epoch change. Per-frame injection can extend coverage without cross-origin DOM traversal from the parent. Qt frame handle alone is not a document token.

- [src/webenginequick/api/qquickwebengineview_p.h:83–97](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview_p.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n83) · `lifecycleState/recommendedState; mainFrame/findFrameByName; runJavaScript` (private C++ declaration of public QML API): Public QML properties for muting/audibility/lifecycle and frame traversal, declared in a private C++ implementation header. This does not authorize public C++ access to QQuickWebEngineView internals. No per-element pause-owner API.
- [src/core/web_contents_adapter.cpp:2066–2215](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n2066) · `setLifecycleState/determineRecommendedState/freeze/unfreeze` (internal reference): Visible/DevTools pages cannot freeze; recommendation stays Active while loading, audible, capturing, crashed or related active contents. Freeze calls SetPageFrozen, not an application-owned media pause receipt. Not an automatic browser-tab policy.
- [src/core/api/qwebengineframe.cpp:22–183](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebengineframe.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n22) · `QWebEngineFrame::isValid/children/runJavaScript` (public API and internal implementation): Public since 6.8; weak adapter/frame handles, invalid frames, plain-data callbacks only; Promise results unsupported. Frame validity does not establish document identity.
- [src/core/web_contents_adapter.cpp:1934–2007](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1934) · `mainFrameId/frameChildren/hasFrame` (internal reference): Underlying frame identity is FrameTreeNodeId and current_frame_host; pair with a document nonce/generation for navigation-safe media commands.
- [src/core/api/qwebenginescript.cpp:43–65](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginescript.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n43) · `ApplicationWorld/runsOnSubFrames` (public API and internal implementation): Isolated application world is public; runsOnSubFrames definition at 237–249 allows injection into each frame.
- [src/core/renderer/user_resource_controller.cpp:153–246](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/renderer/user_resource_controller.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n153) · `runScripts/DidCommitProvisionalLoad/WillDetach` (internal reference): Scripts filtered by injection point/subframe option and URL; each frame executes in its isolated world; per-navigation Runner weak callbacks retired on detach. This is renderer implementation, not a callable application component.
- [src/core/web_engine_settings.cpp:397–401](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_engine_settings.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n397) · `PlaybackRequiresUserGesture mapping` (internal reference): Autoplay policy may require a gesture; do not turn it off to promise resume or treat calling play() as confirmed success.
- [src/webenginequick/api/qquickwebengineview.cpp:2161–2170](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n2161) · `ToggleMediaPlayPause` (public API and internal implementation): Context-position toggle depends on current context snapshot, not stable media identity or pause provenance; unsuitable as app-wide owned-pause mechanism.

### Meaningful tests inspected, not executed

- [setLifecycleState; setVisible:3842–4025](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginepage/tst_qwebenginepage.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n3842): Checks freeze/resume events and discard reload; refuses visible freeze/discard. Does not prove selective media resume.
- [discardAbortsPendingLoad:4240–4270](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginepage/tst_qwebenginepage.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n4240): Discard during pending navigation produces failed load and no restored committed page.
- [scriptsInNestedIframes:440–492](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginescript/tst_qwebenginescript.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n440): ApplicationWorld script with runsOnSubFrames modifies nested same-origin frame DOM.
- [test_mainFrame/test_findFrame/test_runJavaScript:28–93](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/quick/qmltests/data/tst_frames.qml?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n28): QML frame traversal and JavaScript execution; fixture uses about:blank child frames, not protected cross-origin media.

### Limitations / uncertainty

- No public Qt media-session resume provenance, stable media-element IDs, or user-versus-application pause reason. recentlyAudible is a page aggregate and mute is not pause.
- Top-frame document.querySelectorAll cannot control cross-origin child DOM. Per-frame injection/frame execution is a candidate, not a universal guarantee for sandboxed/opaque/remote/provider-controlled media, shadow roots, WebAudio or protected playback.
- A JavaScript callback may arrive at destruction; frame object can outlive document. Promise-returning play() requires explicit later observation/acknowledgement rather than unsupported Promise result serialization.
- recommendedState does not know Colosseum pending consent/licence/admission lifetimes. Preserve its stronger lifecycleBlocked and pending-command gates.
- No runtime or Windows Qt 6.11.1 qualification performed.

### Decisive prototype fixtures

- A playing element and a user-paused element in the same space; minimize then restore resumes only first.
- Same live app, switch inactive tab: established playback policy remains unchanged.
- Navigate same URL, replace media source/element, replace a cross-origin iframe, or deliver old pause/play ACK after an app epoch change: no stale resume.
- User pauses while away, autoplay rejects play(), or page script restarts media: ownership/result remains conservative.
- Pending consent/licence/request and JavaScript ACK when freeze timer fires: page stays active; discard never used as pause.

## 17. Download request ownership and admission

Evidence status: Public profile/request APIs, native callback maps, C++ delayed-answer/destruction tests and QML admission tests inspected. Document-origin precision and QML delayed UI lifetime remain bounded limitations.

### Recommendation

Retain FeriaBrowserDownloads profile-level listener and FeriaAccountStore metadata authority; harden captured provenance, destination consent and teardown, rather than adding a second transfer registry. Use direct profile signal and supported request methods; a C++ owner can retain QPointer with profile-owned lifetime. Refuse ambiguous attribution and expired consent.

Classifications: Keep existing Colosseum implementation; Use a supported public engine API; Adapt donor behavior into an existing Colosseum owner.

### API/ownership trace

Use exactly one authoritative listener per shared profile, with multiple originating spaces/windows mapped by the request view immediately and immutable app-generated transfer identity (profile generation + native ID + app identity). Profile owns native request; source tab is attribution, not transfer lifetime owner. Capture account/profile/private and origin provenance once; never substitute current selected tab or current account. A missing/stale origin is unresolved/rejected rather than silently reassigned. Native resource URL is not initiating-page origin. App metadata grant must be one-use and independent from file/destination acceptance.

- [src/webenginequick/api/qquickwebengineprofile.cpp:230–295](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n230) · `downloadRequested/downloadDestroyed/cleanDownloads` (public API and internal implementation): One profile receives request; request parent is profile, tracked with QPointer; QML JavaScriptOwnership; no post-signal immediate delete in inspected code. Autoaccepted saves answered after emission.
- [src/core/api/qwebengineprofile.cpp:145–155](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n145) · `downloadRequested documentation and implementation` (public API and internal implementation): Docs prohibit queued connection and say immediate deletion if not accepted; actual implementation at224–264 lacks that immediate deletion. Keep documentation/source distinction explicit.
- [src/core/download_manager_delegate_qt.cpp:108–247](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n108) · `DetermineDownloadTarget/downloadTargetDetermined` (internal reference): Native item observer -> DownloadItemInfo -> profile-bound zero-delay callback, per-ID pending DownloadTargetCallback. First profile client receives event. Answer consumes pending callback once, rejects missing item, and cancels if directory creation fails.
- [src/core/api/qwebenginedownloadrequest_p.h:41–70](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest_p.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n41) · `profileAdapter/adapterClient/answered` (internal reference): QPointer profileAdapter, raw adapterClient and one-shot answered flag. Capture page/view identity at admission and avoid later dereference after page retirement.
- [src/webenginequick/api/qquickwebenginedownloadrequest.cpp:22–32](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n22) · `QQuickWebEngineDownloadRequest::view` (public API and internal implementation): QML public view property implemented by raw adapterClient dereference; it does not supply frame/document/initiator-origin snapshot.
- [src/core/api/qwebenginedownloadrequest.cpp:176–210](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n176) · `QWebEngineDownloadRequest::accept/answer` (public API and internal implementation): accept changes Requested to InProgress then consumes answer once; directory/fileName must be set before accept. Metadata authorization is not native acceptance or user destination consent.

### Meaningful tests inspected, not executed

- [downloadAnswerLater:787–839](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n787): Retains profile-parented QPointer after emission; accepts or cancels later, and ignore row survives until explicit deletion. C++ evidence contradicts immediate-delete documentation.
- [downloadDeleted/downloadDeletedByProfile:865–917](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n865): Request deleteLater causes finish and destruction; page+profile deleteLater retires download object. Does not isolate page-only closure behavior.
- [downloadTwoLinks:525–612](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n525): Two downloads from one page complete separately; no multiple-window/account isolation test.
- [onDownloadRequested/test_downloadRequest/test_downloadCancelled:53–187](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/quick/qmltests/data/tst_download.qml?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n53): QML explicit accept/cancel and native state propagation; no delayed destination fixture.

### Limitations / uncertainty

- Docs explicitly forbid queued signal delivery; receive directly even if later decision UI is asynchronous. Actual pinned C++ delayed-answer behavior is test-backed; equivalent QML delayed dialog/GC/profile teardown needs a dedicated receiving test.
- Normal request without receivers goes directly to native acceptDownload with accepted=false, without creating an item; saves carrying accepted=true bypass default rejection.
- Download request API exposes native ID, resource URL, view/page, not stable initiating frame/document ID or initiator origin. Native DownloadItemInfo including raw adapterClient is queued to profile; source destruction before the public signal is not proved safe by inspected tests. Capturing at first public delivery cannot repair an already stale native pointer. Exact origin captured only after navigation may already be stale.
- Public page()/view() is not a durable lease. Originless/non-page requests may have a null adapterClient (QML profile lines 279–282 explicitly assigns nullptr); C++ page() can return nullptr. QML view() documentation permits null but its implementation asserts and dereferences adapterClient without a null/liveness guard. Do not assume that merely evaluating request.view to test for null is a safe originless or post-close check. This remains a source-inspected API lifetime risk requiring a receiving fixture, not a reproduced failure.
- Profile-scoped IDs are not durable cross-process/account transfer keys; native delegate begins currentId at0. No evidence of globally unique/stable persisted IDs.
- Destination/source filenames and Qt URL isValid are not admission policy; constrain scheme, filename leaf, parent directory, overwrite and private/account ownership.

### Decisive prototype fixtures

- Two windows/spaces share profile and download concurrently; change selection during delayed consent: no reassignment or duplicate accept.
- Normal and private profiles emit equal numeric native IDs: entries/account grants stay distinct.
- Destroy the source page between native request determination and public signal delivery: receiving fixture must establish safe attribution or identify an engine-lifetime blocker.
- Hold destination consent, destroy tab then profile/account owner, answer afterward: no dereference, transfer retarget, metadata write or stale accept.
- No listener: normal attachment rejected; explicit-path Save operation separately tested because it autoaccepts.
- Grant replay, canceled grant and wrong-account late completion: metadata write rejected while native destination choice remains a separate decision.

## 18. Download progress/pause/resume/cancel/open

Evidence status: Native states/actions and Qt update mapping inspected with relevant progress/pause/cancel/failure tests. OS Open/Show is not a QtWebEngine download API and requires Colosseum file authority.

### Recommendation

Keep existing native request controls; distinguish wrapper Interrupted cancellation from native cancellation, reconcile native cleanup/late callbacks, and commit metadata only from a settled final byte snapshot. Tighten resume/terminal guards and safe Open/Show in the canonical file owner. Do not replace engine download scheduling with an ad hoc downloader or import Chromium DownloadItem internals.

Classifications: Keep existing Colosseum implementation; Use a supported public engine API; Adapt donor behavior into an existing Colosseum owner.

### API/ownership trace

Keep per-entry request observation in existing profile-level owner. Treat public request state/pause/finished as the application control surface, while distinguishing native updates from wrapper-synthesized cancellation. An Interrupted request canceled in Qt is not proof that the underlying DownloadItem received Cancel or its temporary file was cleaned. Reconcile retirement and late native updates explicitly; never convert a canceled/retired transaction into completed history. Persist completion only from a settled, identity-valid final snapshot, not the first Completed stateChanged callback. On request loss retire actions and retain only historical metadata; do not invent completion. Guard Open/Show through trusted completed transfer identity, re-resolve canonical recorded path and verify authorized file/directory existence before native shell handoff. A renderer string must not become a path-opening authority.

- [src/core/api/qwebenginedownloadrequest.cpp:133–295](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n133) · `update/setFinished/accept/cancel/pause/resume` (public API and internal implementation): update maps native state but the wrapper also synthesizes cancellation: cancel226–252 calls native cancelDownload only while InProgress; Interrupted instead sets wrapper Cancelled/isFinished locally. Pause only InProgress; resume requires !isFinished and InProgress/Interrupted. Completed cancel is a no-op. isFinished is normally set from native info.done in update, but cancel can set it locally.
- [src/core/api/qwebenginedownloadrequest.cpp:401–543](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n401) · `totalBytes/isFinished` (public API and internal implementation): Unknown total=-1; zero is valid. isFinished includes Completed, Cancelled or non-resumable Interrupted. A progress ratio does not establish completion.
- [src/core/download_manager_delegate_qt.cpp:77–105](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n77) · `pauseDownload/resumeDownload/cancelDownload/OnDownloadUpdated` (internal reference): Native operations by profile ID; update 353–383 passes native bytes, paused and IsDone. No native CanResume property exported by request.
- [src/webenginequick/api/qquickwebengineprofile.cpp:297–315](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n297) · `downloadUpdated` (public API and internal implementation): QML profile downloadFinished emitted for every non-InProgress state, including Interrupted; this is not a completion-success signal.
- [src/core/api/qwebenginedownloadrequest.h:19–125](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n19) · `public properties/methods/slots` (public API and internal implementation): No open/show-in-folder, file existence/revalidation or restart-resume method. Public isFinished + state are necessary guards; explicit CanResume absent.

### Meaningful tests inspected, not executed

- [pauseDownload:1396–1443](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1396): 512MiB local HTTP fixture pauses on first byte, resumes on isPausedChanged and verifies bytes after finish; no restart/crash resume test.
- [downloadToReadOnlyDir:1134–1193](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1134): Expects Interrupted + FileAccessDenied + isFinished=false; no downloadFinished from isFinishedChanged; explicitly skipped on Windows.
- [downloadFileCancel/downloadDeleted:841–888](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n841): Requested cancel and deferred deletion terminal handling.
- [downloadLink data empty-file and result assertions:212–222](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n212): Empty file is a valid native download; result verification at455–522 includes zero total/received and actual bytes.
- [test_downloadSucceeded/test_downloadCancelled:160–187](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/quick/qmltests/data/tst_download.qml?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n160): Native Requested / InProgress / Completed and cancellation states; cancellation is not success.

### Limitations / uncertainty

- The public resume() method requests an attempt, not guaranteed resumability or eventual success. A resumable-looking Interrupted state is not a guarantee that server range/retry will succeed; no public Qt CanResume reason/detail property. Use resume only when !isFinished and supported native states; show failure honestly.
- Do not disable cancel by bytes==total or treat downloadFinished/isFinished as successful completion. Native completion may win an InProgress cancellation race. For Interrupted cancellation, Qt lines 226–252 synthesizes Cancelled/isFinished without calling the native cancel route; answered=true makes answer() a no-op. update() lines 133–165 has no terminal-state exclusion for later native notifications. Profile downloadDestroyed() lines 230–235 uses removeDownload, but wrapper Cancelled alone does not establish native retirement or partial-file cleanup.
- Exact update ordering is interruptReasonChanged at 141–143, stateChanged at 145–147, receivedBytesChanged at 149–151, totalBytesChanged at 153–155, then isFinishedChanged via lines 158–159. A synchronous Completed stateChanged handler can read previous byte totals and isFinished=false. Colosseum currently records metadata from this signal and may consume its one-use grant before the final bytes arrive; this is a source-derived ordering hazard, not an executed reproduction. Use one settled final snapshot after the update, gated by identity/retirement and completion, with idempotent persistence.
- No download-opening or trusted-file-identity API in QtWebEngine. Existing Colosseum file owner must verify existence/type/authorization; ordinary saved metadata can outlive the file.
- FileAccessDenied test excluded on Windows; no run here. Public-path collision/overwrite defense remains app-owned.

### Decisive prototype fixtures

- Unknown total(-1), zero-length completed file, total changing mid-transfer and bytes reaching total before completion.
- Interrupted+!isFinished versus Interrupted+isFinished; refused server range; repeat resume and network retry.
- Interrupted+!isFinished -> cancel: assert wrapper Cancelled/isFinished, separately establish native request retirement and temporary-file cleanup, deliver later native Interrupted/Completed notifications, and prove no resume, reopen or completed history for the retired app transaction. Deletion/profile teardown must reconcile underlying item safely.
- Final native update from received=7,total=-1,isFinished=false to Completed,received=11,total=11,done=true: observe Completed stateChanged before byte updates, but persist exactly one completed history row with 11/11 only after settled terminal snapshot; duplicate notifications/retry consume one grant.
- Cancel just before/after completion, duplicate terminal notifications and request destruction while controls visible.
- Completed metadata pointing to missing/replaced/unauthorized file, renderer-supplied ../ path, UNC/path-scheme confusion: no arbitrary Open/Show.

## 19. Download persistence and private cleanup

Evidence status: Profile ownership/destruction and private configuration inspected; no public API to persist/reconstitute native downloads or delete user-saved private files. Application persistence/replay/write retry is outside Qt API.

### Recommendation

Keep FeriaAccountStore ordinary download metadata and FeriaBrowserDownloads active ownership. Gate every persistence attempt by immutable original account/profile/private binding and one-use grant; treat write failure/retry in that owner. Expose historical file availability separately. Do not claim restart-resume or private saved-file erasure.

Classifications: Keep existing Colosseum implementation; Use a supported public engine API; Adapt donor behavior into an existing Colosseum owner; Internal reference only / unsupported / unresolved.

### API/ownership trace

Active transfer, saved history and actual file availability are three different states. Native transfer is bound to profile lifetime, not source tab. Reuse existing ordinary-history owner; private rows stay ephemeral and cannot invoke ordinary metadata persistence. Account switch must not retarget a live request or reuse a prior one-use grant; either retain the original profile/account owner until resolution or retire it explicitly under existing policy. Late callbacks after retirement must fail closed. Private cleanup must establish native retirement/temporary-data handling separately from wrapper-synthesized Cancelled, and retire private metadata; it must not silently delete files the user explicitly saved.

- [src/webenginequick/api/qquickwebengineprofile.cpp:237–249](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n237) · `cleanDownloads/destructor` (public API and internal implementation): Profile destructor457–460 cleans every tracked unfinished request, removes native item and clears map. There is no per-tab cancellation owner here.
- [src/core/profile_adapter.cpp:146–162](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/profile_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n146) · `~ProfileAdapter` (internal reference): Profile teardown notifies destruction, releases webcontents clients, shuts down native DownloadManager and drops delegate. Keep profile alive or explicitly retire transfers before teardown.
- [src/webenginequick/api/qquickwebengineprofile.cpp:363–370](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n363) · `offTheRecord documentation` (public API and internal implementation): Off-record profile forces normal browser cookies/cache/persistent data into memory; this is not a user-chosen downloaded-file deletion promise.
- [src/core/download_manager_delegate_qt.cpp:137–247](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n137) · `DetermineDownloadTarget/downloadTargetDetermined` (internal reference): Actual output path and write pipeline do not branch on offTheRecord; saved bytes can be written under private profiles too.
- [src/core/api/qwebenginedownloadrequest.cpp:212–253](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n212) · `cancel/destructor` (public API and internal implementation): Cancel ignores completed downloads; Interrupted cancellation sets wrapper Cancelled/isFinished without native cancelDownload. Item destruction641–647 skips cancel if wrapper finished; profile downloadDestroyed/removeDownload handles item removal separately. Wrapper finished alone is not proof of native temporary-file cleanup, and no completed user-file deletion path is shown here.
- [src/core/api/qwebengineprofile.h:51–143](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebengineprofile.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n51) · `public profile API` (public API and internal implementation): No public persisted-download enumeration/import/restore API. Download request public slots are only accept/cancel/pause/resume; metadata is not an engine restart checkpoint.

### Meaningful tests inspected, not executed

- [downloadDeletedByProfile:890–917](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n890): Profile teardown destroys outstanding request. Test closes page and profile together, so it does not prove accepted network transfer survives page-only closure.
- [downloadRequestedWithoutHandler:760–785](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n760): A default-constructed off-record profile still writes a page.save explicit destination to disk without a listener.
- [downloadPage:614–706](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n614): Default test profile receives/completes all save formats and output file exists; no application private-history purge assertion.
- [downloadItem:855–863](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebengineprofile/tst_qwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n855): New profile can receive file download requests; narrow ownership coverage, not persistence.

### Limitations / uncertainty

- No public Qt API rehydrates native DownloadItem from history or resumes after app restart. Resume() operates the living request only.
- No evidence that off-record shutdown removes completed user-saved files. Native cancellation partial-file cleanup and history privacy are distinct.
- Qt owns neither Colosseum metadata grants nor account write retries; replay/late/write-failure correctness must stay in existing account store.
- Page-only network-download survival is supported by profile ownership path, but not directly covered by inspected Qt test. Save packages may depend on live source document/resources until finished; retain/retire explicitly.
- Public original page/view pointer must not be read to recalculate origin after source page closes.
- No cloud/Windows binary/runtime proof; tested-source assertions are not executed tests.

### Decisive prototype fixtures

- Source tab closes during accepted download while profile remains; network transfer continues and origin history remains original.
- Private download completes before/after private owner retirement: no ordinary row; user-saved file retained unless explicitly deleted by separate authorized action.
- Account switch or profile generation replacement before completion: no late row written into new account.
- Metadata write fails once then succeeds on authorized retry; replay and duplicate terminal callbacks create no duplicate rows. Completion metadata uses the settled final-byte snapshot, not the first Completed stateChanged event.
- Restart displays historical record with file-availability check and no false Resume control; removed file shows unavailable.

## 20. Save page/link/image/media through the native download pipeline

Evidence status: C++/QML save/context download paths and tests inspected. Public native pipeline supported with material admission/context/frame limitations; no universal arbitrary QML URL download or frame-exact Save API claimed.

### Recommendation

The default Qt context menu already appends Save page/link/image/media when its context allows; this is engine-owned behavior, not proof that Colosseum shared page-tool integration is implemented. Add only missing save actions to existing FeriaPageTools and route every admitted request to existing FeriaBrowserDownloads/FeriaAccountStore. Native Qt actions are preferred to extracted URLs, with snapshot retirement and capability checks. Keep arbitrary QML URL download, exact originating-frame guarantees, canvas/stream export, and PDF-via-download claims out of supported scope until resolved.

Classifications: Use a supported public engine API; Adapt donor behavior into an existing Colosseum owner; Internal reference only / unsupported / unresolved.

### API/ownership trace

Prefer native SavePage action to obtain a Requested item and perform destination consent through the canonical profile-level owner; call explicit-path save only after destination consent and trusted path selection. Existing context snapshot must be bound to original view, document generation and a monotonically retired context serial; new context/menu/navigation invalidates old operation. Dispatch action while that native context is still current, then keep the resulting request through destination UI. Use public MediaCanSave/mediaType/URL admission, not action.enabled alone. Serialize initial page-save admission per source because native SavePageInfo is one per WebContents. Actual transfer still belongs to profile. Do not scrape URL strings via interpolated JS or build a replacement downloader.

- [src/core/api/qwebenginepage.cpp:1117–1139](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginepage.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1117) · `QWebEnginePage::save/download` (public API and internal implementation): C++ save(path,format) since 5.8 delegates to adapter; C++ download(url,suggestedName) at1912–1930 since 5.10. The latter is not a public WebEngineView QML method.
- [src/webenginequick/api/qquickwebengineview_p.h:480–510](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview_p.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n480) · `WebEngineView::save/triggerWebAction` (private C++ declaration of public QML API): QML save(path,format) since 6.6; mainFrame since 6.8. This private C++ header declares those public QML APIs; it is not a supported C++ adapter API. Public QML slot list has no download(url).
- [src/webenginequick/api/qquickwebengineview.cpp:2076–2117](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n2076) · `DownloadLinkToDisk/DownloadImageToDisk/DownloadMediaToDisk/SavePage/save` (public API and internal implementation): Context native actions use current private filtered link/media URL, filename and sanitized referrer pipeline; SavePage2191–2193 has no path, while save2633–2637 forwards explicit path.
- [src/core/web_contents_adapter.cpp:773–779](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n773) · `save/download` (internal reference): save stores one SavePageInfo and calls WebContents::OnSavePage. download1150–1197 calls profile BrowserContext DownloadManager using CreateDownloadForWebContentsMainFrame and sanitized referrer; no independent network client.
- [src/core/download_manager_delegate_qt.cpp:137–161](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n137) · `DetermineDownloadTarget/ChooseSavePath/savePathDetermined` (internal reference): Explicit path sets acceptedByDefault; ChooseSavePath258–345 defaults MHTML, starts with total=-1, ignores can_save_as_complete/default_extension arguments, maps per-ID save callback. Empty-path SavePage action stays requested.
- [src/webenginequick/api/qquickwebengineprofile.cpp:252–295](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n252) · `downloadRequested autoaccept answer` (public API and internal implementation): Explicit-path save delivered already InProgress; after directly connected listeners run, answer() starts write unless canceled. With no listener accepted=true passes directly to native.
- [src/core/web_contents_view_qt.cpp:167–216](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_view_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n167) · `ShowContextMenu/update` (internal reference): Native RenderFrameHost input is ignored by ShowContextMenu. Private context keeps page/frame URL but no stable source frame/document handle.
- [src/core/render_view_context_menu_qt.cpp:144–175](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/render_view_context_menu_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n144) · `appendImageItems/appendLinkItems/appendMediaItems/appendPageItems` (public API and internal implementation): Built-in native menu already adds Save image/link/media/page. With QML request not accepted, view405–434 builds this default menu; custom Colosseum save integration must not be confused with absent engine capability.
- [src/webenginequick/api/qquickwebengineview.cpp:405–434](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n405) · `contextMenuRequested/isMenuItemEnabled` (public API and internal implementation): QML emitted request is a copied snapshot, whereas triggerWebAction uses current mutable internal request. isMenuItemEnabled2559–2596 returns true for Save actions; not a capability/permission guarantee.
- [src/core/api/qwebenginecontextmenurequest.h:56–122](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginecontextmenurequest.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n56) · `public and private context fields` (public API and internal implementation): Public mediaFlags/MediaCanSave and URLs; filteredLinkUrl, referrerUrl/policy, hasImageContent, suggested filename private. Public linkUrl is unfiltered (cpp118–126).
- [src/core/api/qwebenginedownloadrequest.cpp:338–348](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n338) · `SavePageFormat/setDownloadFileName` (public API and internal implementation): SingleHTML, CompleteHTML, MHTML only; fileName setter505–517 accepts nonempty string without path-leaf sanitation. App cannot treat renderer filename as safe. PDF printing is separate from this pipeline.

### Meaningful tests inspected, not executed

- [downloadPage:614–706](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n614): All three formats; action produces Requested and requires accept; explicit path produces InProgress automatically. Checks output existence.
- [downloadRequestedWithoutHandler:760–785](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n760): Explicit-path save writes even without listener.
- [test_savePage/test_saveImage/test_saveWebAction:88–183](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/quick/qmltests/data/tst_save.qml?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n88): QML all formats roundtrip; explicit image save autoaccept/cancel; empty-path SavePage action stays Requested.
- [downloadLink:396–523](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n396): Native Save link with attachment/download attribute variants, exact page pointer, referrer and file bytes including empty file.
- [test_contextMenuLinkAndSelectedText/test_contextMenuMediaType:70–140](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/quick/qmltests/data/tst_contextMenu.qml?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n70): Snapshot link/media data and native action availability; no stale-menu, iframe-origin, authenticated/blob media or protected-provider save coverage.

### Limitations / uncertainty

- Explicit-path C++ and QML save is autoaccepted, not an unanswered normal download. First direct request handler may cancel, but path setters reject InProgress; later asynchronous consent is too late.
- Public context request cannot recover stable originating RenderFrameHost/document. Native context download uses main frame plus context referrer; exact original-frame fidelity remains unsupported/unresolved for sandbox, opaque/blob or authenticated subframe contexts.
- Keeping a QML snapshot does not preserve underlying native action target; subsequent context updates can retarget triggerWebAction. Retire stale snapshot and fail closed.
- QML arbitrary download(url) is absent. Do not invent browser.download or reach through private QQuick adapter as public API; C++ page.download is only directly supported when the application actually owns a public QWebEnginePage.
- Canvas context has invalid mediaUrl; native download image action checks valid mediaUrl. Do not promise canvas saving from menu presence. MediaCanSave false, empty/unsupported URL, MSE/DRM/protected-stream export remain unsupported or require explicit qualification.
- Only SingleHTML/CompleteHTML/MHTML belong to page-save formats. printToPdf is a separate printing path with webengine_printing_and_pdf build guard; no native downloadRequested ownership is established for PDF printing.
- Normal save/download public paths inspected have no printing/PDF feature guard; PDF viewer requires extensions and printing/PDF configuration. Proprietary codecs build option defaults off; media format/provider playback support is not established here.
- No default-filename path traversal bug is established by this audit. Qt derives native suggestions and collision-adjusts its proposed path; the concrete unsanitized setter boundary is caller-supplied setDownloadFileName override. Native target disposition is overwrite; profile GetUniquePath is filesystem-name selection, not an atomic privacy/overwrite/destination grant. Sanitize leaf, check collisions/overwrite intent, canonicalize destination, and avoid claiming suggested names are safe.
- Single SavePageInfo per WebContents means back-to-back explicit saves before target choice require a concurrency fixture/serialization; not proved safe by separate download maps.

### Decisive prototype fixtures

- SavePage action pauses at Requested during destination UI; cancel creates no saved file. Explicit-path save is called only after consent; no-listener autoaccept negative control.
- Open context A then context B or navigate same URL before choosing Save from A: operation rejected, not B download. Switch selected space: original view remains target.
- Subframe authenticated resource/referrer, opaque blob, sandboxed frame and MediaCanSave=false: correctness proven individually or unavailable explicitly.
- Canvas/empty mediaUrl, malformed or disallowed URL scheme, filename ../absolute/separator/reserved name and destination collision: safe rejection/explicit overwrite path.
- Two page.save calls to distinct destinations before first request resolves: serialized or exact attribution proven.
- Save format roundtrip SingleHTML/CompleteHTML/MHTML; PDF print must not masquerade as same download transaction.

## Review clarification: wrapper cancellation and final completion snapshot

Qt request state is an application-facing wrapper, not an exact synonym for underlying DownloadItem state. At [qwebenginedownloadrequest.cpp226–252](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n226), native cancelDownload is called only for InProgress. An Interrupted request instead becomes wrapper Cancelled/isFinished locally. For an already accepted item, answer() has already been consumed. Later native notifications are not excluded by a terminal-state guard in update(). Application cancellation must retire app authority immediately but must separately reconcile native removal/temporary-file cleanup; wrapper finished is insufficient evidence.

At [update145–159](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n145), Qt emits Completed stateChanged before assigning receivedBytes and totalBytes, then marks finished. [Colosseum entry.update108–144](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L108-L144) records immediately from stateChanged and releases a successful one-use metadata grant. Therefore the initial completion handler can persist the previous bytes; the later finished notification cannot fix an already-recorded row. This is a source-derived ordering hazard, not a runtime-reproduced result. The acceptance fixture must start at 7/-1 and deliver final 11/11 with done=true, prove no pre-final metadata commit, and verify exactly one 11/11 row after a settled terminal snapshot. Retired/canceled transfers must remain ineligible even if a late native Completed update arrives.

## Dependency, licensing and cost boundary

No Qt donor file is recommended for copying. Keep the installed public QtWebEngine dependency. DownloadManagerDelegateQt, WebContentsAdapter, the renderer UserResourceController and their private structures are tightly coupled to Chromium content/download/Blink/Mojo and Qt profile/event-loop lifetimes. Copying them would create an engine/private-ABI fork and is not justified by this audit.

Exact file-level notices, byte counts and SHA256 hashes are recorded in [source-manifest.json](source-manifest.json). Runtime source typically carries the Qt commercial/LGPL3/GPL2/GPL3 choice; many test files carry commercial/GPL3 only. Do not relabel these as BSD or copy tests without resolving notices. The inspected tst_save.qml lacks a file-level SPDX header; this audit makes no copy-license inference.

Resource cost: retain only bounded identity/receipt/context metadata in existing owners; native download objects and live profile remain necessary for active transfers. Releasing finished native requests reduces long-lived profile retention. Per-frame scripts add IPC/observation cost proportional to live frames/media and need bounded sampling/retirement; no benchmark claim was made. Freeze may reduce CPU but cannot substitute for correct activity policy.

## Proposed shared-owner interface boundary (proposal, not implementation)

Keep the existing FeriaBrowserDownloads/FeriaAccountStore and FeriaPageTools ownership. Shared semantics should carry immutable transfer identity, profile/account/private binding, captured origin provenance, native state and allowed actions. A narrow admission method receives a native request plus resolved source binding; destination acceptance consumes a distinct one-use consent ticket. Metadata authorization remains its own one-use grant. A safe file-open method accepts a trusted completed transfer/history identity, never a renderer-supplied path.

For page tools, a context snapshot includes original view/space, document generation, context serial, link/media URL, mediaType/mediaFlags and availability. Dispatch checks the snapshot is still current and invokes the supported native action on that original view. It must not manufacture a frame ID that Qt does not expose. SavePage produces a request for the same profile-level admission owner; explicit-path save requires prior destination approval.

For media, existing presentation activity commands should carry app reason/epoch and document/frame identity. Record actual paused elements and confirmed resume results in the existing playback owner; a frame handle, URL, audible bit or freeze transition is not an ownership receipt.

## Checks and stopping state

Retrieved authoritative immutable sources, inspected implementations/declarations/tests/build guards, generated source manifest, and validated JSON structure. No tests executed. No UI, production implementation, engine rebuild, release or platform/provider qualification. Cross-family decisions are in the audit synthesis.
