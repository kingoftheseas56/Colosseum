# Independent evidence review: Feria capabilities 16–20

Review date: 2026-10-08 UTC. Scope: challenge consequential claims in the assignment, pin manifest, current Colosseum findings, and all three donor reports/JSON cells. This is a bounded source-text review of cited critical regions, not another implementation audit. No production edits, product execution, tests, builds, browser actions or external writes were performed.

## Verdict

The evidence is sufficient for the bounded **source-only audit**, provided the corrections/qualifications below survive the synthesis. All 15 family/category cells exist, have immutable revisions and named source/test paths, distinguish inspected from executed tests, and retain receiving-API limits. The main direction is supported: keep existing Colosseum policy and owners; use supported Qt APIs for narrow additions; adapt behavior instead of copying engine internals or adding CEF/a second downloader/history store.

This is not Windows Qt 6.11.1 qualification, proof of every asynchronous edge, or three independent corroborations of shared Chromium behavior. No reviewed finding justifies five rewrites. The largest additional cross-source finding is final-byte metadata ordering (R1).

## Corrections and additional qualifications

### R1. Final-state notification can commit stale final byte metadata

**Action:** Add a source-inferred integration gap and an explicit prototype fixture to the Colosseum findings/synthesis. Do not describe completion metadata as necessarily final/accurate merely because the state is `DownloadCompleted`.

Qt updates `downloadState` and emits `stateChanged` **before** assigning/emitting `receivedBytes`, `totalBytes`, and `isFinished`. Colosseum responds to `stateChanged` immediately, reads those byte getters, commits history, and sets `recorded`; its later `isFinishedChanged` update does not refresh an already recorded row. Thus a final update that changes both state and counters can persist the preceding counters, including a preceding unknown total coerced to zero. This is a source-permitted sequence, not an observed runtime failure.

Exact evidence:
- Qt `eb0793cc4b76e93cf669f586fd68c76019f40ec9`, [QWebEngineDownloadRequestPrivate::update, lines 145–159](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n145).
- Colosseum `62057bfbdaa2305a05d2d3fb02b870bc9e826638`, [FeriaBrowserDownloads.update, lines 108–126](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L108-L126), [observer, lines 140–144](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L140-L144).
- Qt's own page-save fixture checks final counter equality in `isFinishedChanged`, [lines 670–674](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n670). It does not prove Colosseum's earlier commit is safe.

Required fixture: deliver one native-equivalent update changing InProgress to Completed and changing received/total, preserving Qt's signal order. Exactly one history row must contain final counters. Negative control: commit once on `stateChanged` before remaining fields update. Keep atomic grant consumption/retry semantics when designing the later fix.

### R2. “No-view downloads still transfer” is a QML fallback, not qualified native behavior

**Action:** Qualify the category 17 implemented bullet in the Colosseum Markdown and JSON. Suggested wording: “The QML logic omits history when a safely obtained view is absent; safe native originless/stale-source handling is unqualified at this Qt pin.”

Colosseum immediately reads `request.view`, then has a null-view fallback. Qt can set the request's internal `adapterClient` to null, but the QML getter dereferences it without a null guard. Therefore the application-level null branch does not establish that a real originless native request reaches it safely. Likewise it cannot repair a pointer that became stale before first public delivery. This does not prove every ordinary download crashes or establish a runtime exploit.

Exact evidence:
- Colosseum [FeriaBrowserDownloads.receive, lines 39–45](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L39-L45).
- Qt [QQuickWebEngineProfilePrivate::downloadRequested, lines 279–282](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n279).
- Qt [QQuickWebEngineDownloadRequest::view, lines 22–32](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n22).
- Qt [queued native request delivery, lines 193–217](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n193).

The Qt report already records most of this limitation. Preserve it when reconciling current implementation with donor evidence. The decisive fixture destroys/navigates the original source before public signal delivery and never substitutes selected tab identity.

### R3. Qt interrupted cancellation needs a wrapper-versus-engine qualification

**Action:** Narrow any blanket statement that cancellation always projects the underlying Chromium item's terminal state; add an interrupted-cancel reconciliation fixture.

At this Qt pin, `QWebEngineDownloadRequest::cancel()` calls the profile's native cancellation only for `DownloadInProgress`. For `DownloadInterrupted`, it instead sets the Qt wrapper state to `DownloadCancelled`, emits state, and marks the wrapper finished. The one-shot `answer()` does not re-answer an already accepted transfer. Consequently, this path alone does not demonstrate that the underlying Chromium item was cancelled or its partial bytes cleaned. The normal InProgress/completion race explanation remains supported.

Exact evidence:
- Qt [cancel, lines 226–252](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n226), [answer one-shot, lines 176–187](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n176).
- Qt [underlying cancelDownload, lines 77–85](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/download_manager_delegate_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n77).
- Colosseum [cancel control and grant release, lines 108–110, 134–144](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L108-L144).

Required fixture: cancel a resumable Interrupted request, inspect subsequent native updates/partial-file outcome/profile retirement, and ensure it cannot regain active/completed UI or persist a cancelled-as-successful history row. Until tested, report the public wrapper transition and unresolved underlying retirement separately. Do not label this as an executed failure.

### R4. Default Qt media Save presence is not capability enforcement

**Action:** In the category 20 summary, say “default menu exposes Save actions, with incomplete capability gating.” Do not shorten the nuanced Qt report to “native default Save is safe/qualified” or imply `MediaCanSave` is enforced by the default menu.

The builder unconditionally adds `DownloadMediaToDisk`; its QML action is unconditionally enabled; execution checks only `mediaUrl.isValid()` before starting the main-frame-associated native download. The public media flag is available, but using it as a conservative admission gate is an application recommendation. Canvas also has a default Save entry despite its invalid media URL making this particular download action ineffective.

Exact evidence:
- Qt [appendMediaItems, lines 160–166](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/render_view_context_menu_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n160), [appendCanvasItems, lines 106–109](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/render_view_context_menu_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n106).
- Qt [isMenuItemEnabled, lines 2585–2596](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n2585).
- Qt [DownloadImageToDisk/DownloadMediaToDisk, lines 2110–2116](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n2110).

## Important conclusions independently retained

- **Keep the implemented pause policy.** The existing per-element ledger records actual media objects/documents, source and owned pause status; it is not merely a URL guess. Resume rejection, frame retirement and aggregate app-reason limitations are appropriately disclosed. [FeriaPlayback.js:219–298](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaPlayback.js#L219-L298).
- **Do not invent missing download policy.** Existing immediate acceptance/default destination is source-backed; lack of a chooser is not proof of unauthorized files. The stronger destination policy remains a product/prototype decision, independent of metadata grants. [FeriaBrowserDownloads.qml:34–58](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L34-L58).
- **Public QML is not public C++ internals.** QML `save` is since 6.6, frames since 6.8; public QML declarations in the private header do not license direct C++ adapter access. No QML `download(url)` is present in the inspected complete slot list. [qquickwebengineview_p.h:480–510](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview_p.h?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n480).
- **Preserve the async contradiction.** Immediate-deletion documentation conflicts with actual profile ownership and the C++ accept/cancel/ignore-later fixture. It is reasonable to propose direct delivery plus explicit lifetime ownership, while requiring a QML delayed-dialog/GC/teardown fixture. [QWebEngineProfile docs:149–155](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/api/qwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n149), [downloadAnswerLater:787–839](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n787).
- **SavePage differs from explicit-path save.** Qt fixture directly asserts Requested versus InProgress and accepts only the action variant. Explicit-path save must follow destination consent rather than depend on later async UI. [downloadPage:647–666](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n647).
- **Exact original frame fidelity remains unresolved in Qt.** Context requests are copied to QML, whereas later actions use mutable native context; the original RenderFrameHost parameter is discarded and downloads are created against the main frame. A context serial can prevent known retargeting; it cannot invent an absent stable source-document/frame capability. [QML context copy:405–423](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineview.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n405), [ShowContextMenu:205–215](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_view_qt.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n205), [download:1181–1196](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/web_contents_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1181).
- **Private last-tab cancellation is real source behavior.** Once runtime views reach zero, Colosseum cancels private downloads and destroys that profile. Private history isolation does not imply deletion of files a user saved. [FeriaSessionDeck.qml:505–511](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaSessionDeck.qml#L505-L511).
- **Safe Open is a genuine boundary gap, not a demonstrated exploit.** Live controls gate on completed state but invoke a path directly; historic controls do not revalidate completion, availability or current file identity. No arbitrary renderer exploit was shown, and existence checks alone cannot eliminate filesystem TOCTOU. [FeriaBrowserDownloads.qml:194–219](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L194-L219).
- **CEF orphan transfer behavior is source-backed.** Its browser map is explicitly nulled without cancellation, and updates are sent only when a browser handler exists. Do not copy this observer topology into the profile-level Colosseum owner. [CEF OnBrowserDestroyed:423–433](https://github.com/chromiumembedded/cef/blob/49f5d095ac0c84b2da64d06fec866f22531b331d/libcef/browser/download_manager_delegate_impl.cc#L423-L433), [OnDownloadUpdated:297–312](https://github.com/chromiumembedded/cef/blob/49f5d095ac0c84b2da64d06fec866f22531b331d/libcef/browser/download_manager_delegate_impl.cc#L297-L312).
- **Test bounds are honest.** CEF Pending explicitly cancels during teardown; it does not demonstrate a surviving orphan completion. Qt read-only-directory coverage skips Windows. The Colosseum ordinary-tab-close fixture actually inspects final file bytes, but remains inspected, not passed. [CEF test:488–505](https://github.com/chromiumembedded/cef/blob/49f5d095ac0c84b2da64d06fec866f22531b331d/tests/ceftests/download_unittest.cc#L488-L505), [Qt Windows skip:1134–1138](https://code.qt.io/cgit/qt/qtwebengine.git/tree/tests/auto/widgets/qwebenginedownloadrequest/tst_qwebenginedownloadrequest.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n1134), [Colosseum tab-close fixture:231–244](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/tests/feria_session_probe/tst_RetainedSessions.qml#L231-L244).

## Provenance and review checks

- Re-read pinned metadata: Qt declares `.264` plus security backports; actual vendor VERSION is `.225`; CEF compatibility requests `.214`. These establish related source lineages, not byte/ABI equivalence or Chromium 148 API availability. The distinction in [PINS.md](../donors/PINS.md) is necessary and correctly preserved.
- Parsed all four findings/report JSON artifacts. Each donor contains exactly categories 16–20 and explicitly declares `tests_executed: false`.
- Recomputed SHA-256 for 19 selected critical cached source files across Colosseum, QtWebEngine and CEF; all matched their manifests. This is artifact integrity checking, not product testing.
- Source-read the relevant exact-vendor Chromium item/source-identity regions rather than treating upstream or CEF dependency code as automatic Qt proof.
- Reports generally distinguish shared Chromium internals, upstream Chrome orchestration, embedding APIs and application policy. The final matrix must retain those layer labels and may not count shared Chromium as three independent confirmations.
- Reviewed the CEF artifacts after they became available. No additional donor families were introduced. No claim is made to have validated every line citation, every cached file, or the final parent-authored interface/fixture synthesis at this review stage.

Only this review file was authored by the independent reviewer. Corrections to donor/current-state artifacts and the final synthesis belong to their respective owners.


## Final synthesis review (2026-10-08)

Reviewed `DECISIONS.md`, `PROPOSED-INTERFACE.md` and `ACCEPTANCE-FIXTURES.md` against the assignment and cited source. The synthesis correctly incorporates R1–R4, labels proposed methods as proposals, uses supported public Qt boundaries, preserves existing owners and inactive-tab behavior, keeps accepted network-transfer attribution independent of the currently displayed document/host, and separates explicit-path Save consent from normal Requested admission. No new canonical store, callable private Qt/Chromium API or executed-test claim is introduced.

**Final verdict: suitable for source-audit delivery after the following small synthesis corrections are incorporated.** These do not change the selected architecture:

1. The shim receives/retains the profile-created native request and creates only its app token/entry. It does not create the native request at signal delivery. Evidence: [Qt profile request construction:263–290](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/webenginequick/api/qquickwebengineprofile.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n263).
2. Replace “Qt-generated sanitized defaults” with “engine-selected/collision-adjusted defaults, still subject to admission.” The inspected path helper joins the suggestion and uses GetUniquePath; it is not blanket validation of every source suggestion. No default traversal flaw is established. Evidence: [ProfileAdapter::determineDownloadPath:920–942](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src/core/profile_adapter.cpp?id=eb0793cc4b76e93cf669f586fd68c76019f40ec9#n920).
3. Carve page serialization out of the ordinary accepted-network-transfer lifetime rule. SavePackage remains Page-bound and may retire/cancel when its source disappears. Add a fixture for navigation/source-close after page-save acceptance: retain required source lifetime or report actual cancellation/failure; profile retention alone cannot promise serialization completion. Evidence: [vendor SavePackage constructors/destruction:178–242](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/download/save_package.cc#L178-L242), [Init/InitWithDownloadItem:320–379](https://github.com/qt/qtwebengine-chromium/blob/58c11ad487f8a237cf0ac71cc3e818b52db150df/chromium/content/browser/download/save_package.cc#L320-L379).
4. Clarify safe Open for private/live/no-history transfers. Their admitted completed handles resolve through the existing live owner or an explicitly proposed transient extension of an existing native owner; ordinary historical handles resolve through FeriaAccountStore. Do not persist a private row or mint an ordinary private history grant to make Open work. Evidence: [private grant exclusion:257–261](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/native/feria/FeriaAccountStore.cpp#L257-L261), [private-capable live Open control:194–200](https://github.com/kingoftheseas56/Colosseum/blob/62057bfbdaa2305a05d2d3fb02b870bc9e826638/qml/feria/FeriaBrowserDownloads.qml#L194-L200).

The fixture set meaningfully covers the important negative controls, including source closure before first public delivery, delayed QML ownership, wrapper/native interrupted cancellation, final-byte signal ordering, account/profile retirement, grant replay/write retry, and mutable Save-context retargeting. The extra accepted-page-save lifetime case closes the remaining material distinction. All checks in this follow-up remained source/text review; no product tests, builds or implementation ran.


### Resolution and final verdict

Re-read the corrected synthesis and the matched F20.6 navigation/close negative control. All four synthesis corrections above are incorporated: profile-created native ownership, bounded filename claims, document-dependent page serialization with its own failing control, and ephemeral live/private Open authority separated from ordinary history. R1–R4 are also reflected in the current/donor reports and final interface/fixtures.

**Resolved: no remaining consequential evidence or synthesis blocker found within this review scope. Suitable for bounded source-audit delivery.** The proposal still requires the explicitly listed later runtime/Windows qualifications; this verdict does not upgrade inspected tests to executed tests, authorize implementation, or establish WebView2/provider parity.

Reviewed synthesis snapshot SHA-256:
- `DECISIONS.md`: `33ddbf4c0d2ad1308bebce462817f13ded22b5b2f431f20f5930ea06d7d97359`
- `PROPOSED-INTERFACE.md`: `ef651facdf67aef2d3ad9f75ece3a339a7da87a8c70e8640cd20a640f08b412c`
- `ACCEPTANCE-FIXTURES.md`: `7991a7c68321549c78e4b0d114d43a8e626978f7a0bc1d6c8bd911ea942702eb`
