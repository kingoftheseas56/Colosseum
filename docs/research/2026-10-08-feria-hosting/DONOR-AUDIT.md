# Feria categories 6–10: donor selection and source boundaries

Scope: only categories 6–10. Baseline `cd4b7f6ce8b895239a1e1e02ecb4f0d377a4d413`. Five supplied research inputs were used as starting evidence: four donor reports and one Codex receiving review. Their old audit-only/52-category stopping rules are superseded by the current packet. This report describes source selection; runtime results belong in the delivery result matrix.

## Verified revisions

- QtWebEngine 6.11.1: `eb0793cc4b76e93cf669f586fd68c76019f40ec9`; tag peeled independently. CHROMIUM_VERSION reports 140.0.7339.264 with security patches through 148.0.7778.96; security backports do not imply feature parity with 148.
- Matching Chromium 140: `56ca847e9ea70a5c56fa3d634361da1002fb284b`, chrome/VERSION independently inspected.
- CEF: `2d4a38cec5a6d4604cc77b92d1a06902b1ead61d`; CHROMIUM_BUILD_COMPATIBILITY declares 156.0.8078.0.
- Brave: `9eb6dab3855ed6ba0950c37f7e3df0bd0fb5ef5a`; package.json declares 155.0.8059.40. No distinctive 6–10 override established that improves matching Qt/Chromium behavior; do not duplicate inherited Chromium work merely to fill a column.

## Selection matrix

| Category | Strongest useful machinery | Rejected alternatives and boundaries |
|---|---|---|
| 6 Canonical desktop windows | Public Qt Window/QQuick item presentation with permanent deck QObject/creation-context ownership. Chromium Browser::ActivateContents resolves supplied WebContents before raising its owner; CEF re-queries current native host after reparent. | No Chrome Browser/TabStripModel or CEF platform-delegate transplantation. Selected page is never a substitute for captured origin. Native window, QObject owner, QML context, visual parent and profile have separate lifetimes. |
| 7 Site dialogs and geometry | Synchronous same-profile Qt newWindowRequested/openIn preserves original WebContents. CEF popup-ID/opener-frame staged creation informs rollback. QScreen availableGeometry and actual host frame margins supply native clamp. | URL replay loses POST/opener semantics. openIn returns void, so no fabricated accepted boolean. Chromium clamp alone does not establish Qt outer-frame/DPI correctness; matching source explicitly carries an inner-vs-outer geometry TODO. |
| 8 Same-page Max/Restore | Same QQuickWebEngineView, stable creation context/QObject owner, explicit disposable visual hosts and saved embedded bounds/focus. Qt scenegraph reparent code reconnects windows; donor graphics test checks actual pixels. | Reload/new adapter is not same live page. Chrome's same-WebContents transfer test does not prove Qt creation-view safety. Adopted-child opener retirement has a source-supported potential invalid creation-view dereference, described below. |
| 9 Exact veto-capable close | CEF Pending→None on refusal, native close veto while pending; Chromium cancellation clears queues and weak callbacks; public Qt RequestClose and exact original view windowCloseRequested. Canonical native attempt identity plus pinned Qt rejection acknowledgment hook. | Dialog acceptance is not terminal close. No force-close promotion or timeout-as-success. A JavaScript round trip alone is not source-proven renderer rejection acknowledgment. Dynamic windowCloseRejected is private/undocumented Qt behavior, explicitly version-coupled. |
| 10 App-scoped reopen | Existing SessionStore gets one closed-descriptor writer and two-phase app/profile-filtered reopen. Chromium provides bounded descriptor/pruning and post-restoration removal reasoning; private profiles excluded at boundary. | Chromium RestoreMostRecentEntry is global and assumes context creation success. App scoping and retain-on-failed-runtime creation are new Colosseum glue, not donor-proven behavior. Never resurrect destroyed JS/POST state or import Chrome's session store. |

## Decisive traces and actual assertion bodies

### Qt integration

Pinned source root: https://github.com/qt/qtwebengine/tree/eb0793cc4b76e93cf669f586fd68c76019f40ec9

- `src/core/web_contents_delegate_qt.cpp`: WebContentsCreated 580–587 supplies opener view client; adoption 224–241/776–788 transfers original adapter, null blocks; SetContentsBounds 714–725 subtracts frame margins; CloseContents 246–252 may destroy its own delegate; BeforeUnloadFired 855–862 routes refusal to originating client.
- `src/core/web_contents_view_qt.cpp`: setFactoryClient 37–71 creates delegate; setClient 49–59 changes factory/adapter client but does not replace an existing widget delegate.
- `src/webenginequick/api/qquickwebengineview.cpp`: synchronous same-profile adoption 951–970/1937–1956; delegate construction 379–384; Bind 269–292 does not update original m_parentView; bindViewAndDelegateItem 1061–1118 fixes visual/focus association only; close 765–769 emits public acknowledgment; windowCloseRejected 771–777 invokes application method dynamically.
- `src/core/render_widget_host_view_qt_delegate_item.cpp`: mousepress 246–257 consults ActiveFocusOnPress, which dereferences original creation view; itemChange 338–383 and releaseResources 408–445 handle actual QQuickWindow/scenegraph transitions.
- `src/core/web_contents_adapter.cpp`: public lifecycle dispatch 2066–2118 refuses Discarded if visible/inspected; discard 2217–2267 aborts loading, creates hidden kNoRendererProcess WebContents, disconnects WebChannel and destroys original contents/delegate.
- `src/webenginequick/api/qquickwebenginedialogrequests.cpp`: weak controller answer 381–405; use dialogAccept()/dialogReject(), not nonexistent generic accept/reject methods.
- Actual donor assertion bodies inspected: newViewRequest null/unhandled/dialog/background cases; JavaScriptDialogs accepted/rejected close after real mouse activation; graphics reparent green-pixel test 118–130; lifecycle discarded JavaScript-disabled/automatic-undiscard/DevTools cases; widget close→Discarded. None proves all required Colosseum POST/nested-input/host-retirement cases. Donor tests were not executed by this audit.

QtDeclarative creation-context source `a02bed441965ee1f18f856352c7d5ee5ba35d795`, `src/qml/qml/qqmlcomponent.cpp` lines 205–227, establishes that Component declaration context must outlive created objects even if createObject is supplied another parent.

### Matching Chromium and CEF

Pinned Chromium root: https://github.com/chromium/chromium/tree/56ca847e9ea70a5c56fa3d634361da1002fb284b

- `chrome/browser/ui/browser.cc`: exact source activation 2194–2203, CloseContents 2211–2215, BeforeUnloadFired 2287–2297.
- `chrome/browser/ui/unload_controller.cc`: BeforeUnloadFired 128–169, CancelWindowClose 278–295, renderer loss 301–308, detached content 349–363, weak callback invalidation and pending dispatch 365 onward.
- `chrome/browser/unload_browsertest.cc`: cancellation 345–360 waits for renderer title `cancelled` before retry; double close cancellation 471–504 asserts one abort/zero successes, waits for acknowledgment and then retries; double accept 444–466 asserts one terminal success. These are stronger than CEF's delayed forced cleanup. Two similarly named browser tests are DISABLED and are not counted as active evidence.
- `content/browser/web_contents/web_contents_impl.cc`: staged creation 5233–5249/5433–5487 preserves storage partition and POST, uses weak pointer across embedder call; original ShowCreatedWindow 5580–5659 moves ownership once; AdjustWindowRectForDisplay 553–579 clamps work area with permission boundary; source warns about inner/outer inaccuracies.
- `chrome/browser/ui/tabs/tab_strip_model_unittest.cc` lines 793–830: preserves exact model/WebContents during transfer, but detached host association and opener metadata are not automatically preserved.
- `components/sessions/core/tab_restore_service_helper.cc`: global most-recent 438–445; restore 594–638/770–783 removes by stable ID after callbacks; RestoreTab 1000–1069 assumes context creation; prune 839 onward. Tests WindowRestore 454–506 and PruneEntries 1165–1250 assert bounds/app name/order. Test named DontPersistPostData does not assert actual POST-byte absence and is not overclaimed.

Pinned CEF root: https://github.com/chromiumembedded/cef/tree/2d4a38cec5a6d4604cc77b92d1a06902b1ead61d

- `libcef/browser/alloy/alloy_browser_host_impl.cc`: CloseBrowser/TryCloseBrowser 266–316 gate duplicates and veto pending native close; BeforeUnloadFired 950–961 resets pending on refusal; CloseContents 879–925 and teardown 514–546 separate approval/native host/destruction.
- `libcef/browser/browser_info_manager.cc`: PendingPopup destructor 53–58 aborts once; CanCreateWindow 112–259 carries opener frame + popup ID and late identity resolution; CreateWindowResult 261–283 distinguishes abort from valid no-opener result.
- `tests/ceftests/life_span_unittest.cc`: refusal assertions include noDoClose/noOnBeforeClose/noUnload, but delayed cleanup is not normal reject-then-retry proof.

## Qt creation-view blocker and unapproved exception

Pinned source shows that an adopted child's widget delegate can retain its opener's creation-view reference after public `openIn`. Ordinary visual reparenting does not prove that reference is rebound. Deleting the opener while child input continues presents a potential invalid dereference through the traced input path. This is a strong source-supported lifetime hazard, **not a runtime-proven crash or proof of inevitable failure**. No supported public delegate-rebind route was found. Private pointer mutation and forced child reload are rejected.

The isolated creation-view probe was safety-blocked and was never executed or published. It must not be reproduced or routed through another execution mechanism. The reviewed 1–5 generic new-window survival regression remains useful but does not establish adopted-child mouse/key input after opener retirement.

A public `Discarded` origin shell was considered as a possible exception: source suggests it could destroy the original document/WebContents while retaining the view/context needed by a child. It would need to prevent all reactivation, release nested dependencies and demonstrate final-leaf cleanup. Count/depth bounds would not provide a finite duration bound while a child remains open. **The user has not approved this indefinite-shell exception, and it is not implemented as an accepted solution.** Opener-first retirement remains an explicit acceptance gap. Defensive refusal to close an opener while children exist is a prototype limitation, not satisfaction of that requirement.

No shell resource-release, zero-hidden-playback or post-retirement input claim is supported by execution. A shared renderer PID would not by itself prove or disprove document release. Any future solution needs a supported, authorized route and separately reviewed lifetime/resource evidence.

## Compatibility and verification contract

Only behavioral patterns, public Qt API wiring and newly authored Colosseum glue are selected; no GPL donor test code is copied into production. The Qt 6.11.1 dynamic rejection callback is an explicitly private compatibility dependency. Repeat-rejection tests exist, but their status belongs to the exact-commit runtime report. Missing acknowledgment must fail diagnostically, never authorize close or convert a timeout into success; fail-closed behavior does not establish universal retry liveness. Replace it with a public terminal rejection API when available and verified.

Source coverage is distinct from native/model tests, actual QtWebEngine graphical component tests, full-app runtime and Windows qualification. Native additions require the matching newly compiled runtime. Multimonitor/DPI/GPU loss and full-app cases unavailable on the Linux runner stay NOT RUN with receiving instructions. The known baseline retained-download failure is not hidden or relabeled green.

## Exact embedded Chromium version and acknowledgment boundary

The top-level Qt `CHROMIUM_VERSION` says based-on 140.0.7339.264, while the runtime reports 140.0.7339.225. This is explained by the exact pinned build inputs: Qt's `src/3rdparty` gitlink is `58c11ad487f8a237cf0ac71cc3e818b52db150df`, whose [embedded chrome/VERSION](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/chrome/VERSION) has PATCH=225. [BUILD.root.gn.in lines 18–20 and 124](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/configure/BUILD.root.gn.in#L18-L20) generates the CHROMIUM_VERSION macro from that file; [qWebEngineChromiumVersion lines 974–981](https://github.com/qt/qtwebengine/blob/eb0793cc4b76e93cf669f586fd68c76019f40ec9/src/core/web_engine_context.cpp#L974-L981) returns the macro, while the security-patch accessor separately returns 148.0.7778.96. The differing patch strings alone do not show an artifact mismatch; package/build identity still requires its own evidence.

The decisive cancellation/acknowledgment paths were also checked in that **exact embedded** Chromium tree, not just upstream .264:

- [WebContentsImpl::OnDialogClosed lines 10703–10748](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/web_contents/web_contents_impl.cc#L10703-L10748) invokes navigation cancellation before the renderer response callback. FrameTreeNode::BeforeUnloadCanceled (759–783), ResetNavigationRequest (636–655) and DidStopLoading (701–733) have bodies identical to the inspected upstream reference.
- [RenderFrameHostImpl::ProcessBeforeUnloadCompletedFromFrame lines 6659–6797](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/renderer_host/render_frame_host_impl.cc#L6659-L6797) clears pending renderer state later. The only compared difference is a Qt guard disabling a timing DCHECK. DispatchBeforeUnload (11753–11848), including pending navigation/close coalescing, is body-identical.
- [Navigator::BeforeUnloadCompleted lines 1164–1215](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/renderer_host/navigator.cc#L1164-L1215) can return when the canceled navigation request is gone. Its only compared difference disables another DCHECK for Qt. The close route through RenderFrameHostManager::BeforeUnloadCompleted (839–870) and WebContentsImpl::BeforeUnloadFiredFromRenderManager (10387–10397) is body-identical and reaches Qt's private rejection callback.

Therefore navigation `LoadStopped` is **not** evidence of terminal renderer beforeunload acknowledgment. Qt's beforeunload dialog does not reliably distinguish navigation from site close. Same-document navigation-Stay followed by app-close remains a specific category 9 gap; an extra RequestClose or JavaScript round trip is not an established acknowledgment barrier. A defensive visible close/transfer quarantine may preserve safety, with recovery on a verified replacement main document or renderer termination, but it must be qualified and tested at the final commit. This audit does not claim that in-progress repairs or final runtime tests passed.
