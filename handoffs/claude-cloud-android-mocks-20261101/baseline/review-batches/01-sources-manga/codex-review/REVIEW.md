# Codex receiving review: Batch 1

Reviewed 2026-11-01. This is independent receiving review of Pro's HTML deliverable, not app implementation or approval of a new UI.

## Outcome

The handoff is useful and complete as a set of nine mock artifacts. I do not accept all nine as the final phone-port reference yet. Five source-picker/confirmation pages are suitable for Hemanth's visual review. Hemanth has now selected the right-hand rebuilt content/layout direction for both manga series and chapters; the actual title bar still needs completing. The cover-flow allocation remains a separate proposal.

| Scope | Receiving disposition |
|---|---|
| Film and episode source pickers | Rich original source-row grammar retained: letter tile, quality, metadata, language/tag chips, round actions. Browser handoff checks passed. Suitable for visual review. |
| Comic sources and weak confirmation | Original confidence/evidence hierarchy and round download retained. Fresh filtered-cancel check preserves the query/result. Suitable for visual review. |
| Manga volume sources | Original tile/status-disc/evidence presentation retained; metadata correction is documented. Suitable for visual review. |
| Manga series and chapters | Hemanth selected the rebuilt content/layout direction after seeing both. Complete the real title bar, promote rebuilt to the main finished mock, and refresh its evidence before accepting the finished page. |
| Volume-flow pair | Keep as comparison evidence. The constrained original is not a usable accepted fit. The scrolling alternative remains a product proposal, and its surrounding header must follow the eventual manga baseline. |

## R1: title-page baseline is not established by the committed default

The frozen and current Main.qml both declare rebuiltTitlePages false by default (line 583) and select TankobanSeriesView.qml when that setting is true (line 4991). However, the actual desktop checkout's views.ini contains rebuiltTitlePages=true; the Android checkout's views.ini contains false.

Pro correctly disclosed the route it chose, but a source default does not establish which design Hemanth currently approves. Its finished series/chapter mocks remove the rebuilt genre/control composition and use MangaSeriesSharedHeader/MangaChapterSeriesView instead. Its optional rebuilt preview retains older reading-room chrome and does not reproduce TitlePageBar. Do not port these differences as an approved restoration until the baseline is resolved.

I asked Hemanth which version to preserve; he requested to see both. Fresh comparison images are now available in this directory for series and chapters, portrait and landscape. They clearly label the rebuilt side as an incomplete content preview. They must not be presented as two fully qualified native layouts.

**Decision received:** Hemanth selected the right-hand rebuilt versions for both pages: "Right is superior on both counts brother." See USER-DECISION-REBUILT-MANGA.json. This resolves the content/layout direction; the missing title-bar completion and refreshed final verification remain. It does not approve the separate constrained cover-flow proposal or qualify a native implementation.

Relevant source: reference/desktop/qml/Main.qml, TankobanSeriesView.qml, TitlePageBar.qml, MangaSeriesSharedHeader.qml and MangaTankobanLibrary.qml. The source TitlePageBar currently has Back, search and a system menu; its older comment about world pills is superseded by its implementation/comment explaining their removal. A complete rebuilt mock must follow that actual source.

The same Main.qml setting also selects the Theatre title-page route (line 5234). Recheck this assumption when receiving the detail-page batch; this review did not inspect that batch's outputs.

## R2: cover-flow proposal needs a decision after the header baseline

The real library source keeps a 190 px minimum and 1.10 focused scale (MangaTankobanLibrary.qml, lines 548–560). Pro's constrained comparison intentionally falls below that minimum in six cases; its readable alternative preserves the original emphasis through vertical scrolling. The existence of a failed constrained allocation does not prove that this is the only possible faithful fit. Evaluate its scrolling cost against the correct surrounding header, then retain Hemanth's selection explicitly.

## What I checked

- Personally inspected all nine portrait/landscape contact sheets and all three desktop comparison sheets, plus fresh older/rebuilt series and chapter comparisons in both orientations.
- Independently checked 801 supplied artifact/contact-sheet/source-image/receipt hashes; no mismatches.
- Freshly loaded all nine pages at portrait, landscape and compact landscape stress settings: 27 cases. No page script/HTTP errors, broken images or horizontal document overflow.
- Freshly checked provider Escape/focus return, outside-click dismissal without activating a source, and comic weak-match cancellation retaining its filtered query and result. All three passed.
- Read the source anchors for route selection, shared-header controls, cover sizing/scaling and rebuilt title-bar composition. The source header declares no genre-chip presentation in the older route; the rebuilt source does.

Unused declared font variants are not failed resources: the existing CSS/evidence identifies unused Segoe bold and Fraunces italic faces alongside loaded used faces. The receiving check retains those informational observations rather than treating them as failures.

The fresh evidence is receiving-checks.json. Its browser and loopback server were closed after capture. Original worker output, Pro's finished mocks/reports, frozen references, app source, device state and publication state were not changed.

## Practical limits

These are browser-rendered mock checks. They do not verify native QML metrics, the currently running desktop executable/configuration, Android input/safe areas, real accessibility, provider or acquisition behavior. Viewports remain provisional and some manga artwork is a disclosed stand-in. Native implementation and device testing remain separate stages.
