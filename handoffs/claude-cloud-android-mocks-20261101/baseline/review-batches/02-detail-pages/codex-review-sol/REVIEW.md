# Batch 2 receiving review

Proposed disposition: **RETURN_FOR_CORRECTION**, limited to rebuilt Theatre platform chrome and Theatre baseline labeling/selection. The other six pages are usable mock handoffs with the declared artwork/native gaps. Primary retains final acceptance.

## Findings

1. **P2 — Rebuilt Theatre exposes desktop window actions on Android.** `finished-mocks/17-theatre-details/app.js:226-233` always emits the Window and app menu; `style.css:289` hides only legacy `.window-controls`. Actual clicks on BOTH film and series at 354×792, 1.3 text, 24px illustrative insets exposed Minimize, Exit fullscreen and Quit Colosseum. This contradicts retained Rule 27 (`context/ANDROID-PORT-CLASSIFICATION.md:55`). Frozen `TitlePageBar.qml` contains that desktop menu too; the platform-fit rule still applies. Preserve search/desktop controls, gate the Android window actions, and recheck phones/tablets. Evidence: [film](captures/film-rebuilt-android-window-menu.png), [series](captures/series-rebuilt-android-window-menu.png), `interaction-results.json`.

2. **P2 — Theatre source selection remains unresolved.** `QML-PORTING-NOTES.md:9,62` treats Main.qml default=false as authoritative. Frozen `Main.qml:575-583` says persisted views settings choose the route; line 5234 branches. Independently read current desktop `views.ini=true`, Android `views.ini=false`. This is configured-source evidence, not an observed running executable. Both finished compositions exist and render upright/sideways; the manga choice does not approve Theatre. Choose explicitly and qualify the default label/notes. Matched evidence: [portrait](theatre-both-portrait.jpg), [landscape](theatre-both-landscape.jpg), `captures/series-{legacy,rebuilt}-ledger-actual.png`.

## Eight-page coverage

All eight pages were independently rendered and visually inspected at **354×792**, **792×354** and **1280×700**, with font 1.3 and illustrative top/bottom 24px. Film/series were additionally compared in BOTH renderers upright/sideways. [Portrait sheet](portrait-sheet.jpg), [landscape sheet](landscape-sheet.jpg), [desktop sheet](desktop-sheet.jpg).

| Page | Independent check and source fidelity | Receiving result |
|---|---|---|
| Biblio book | BiblioBook jacket/copy, full synopsis and source sections retained; source 268×402 jacket and original Library/Collections owners checked. Read chooser End/Escape and Collections Escape returned focus to their invokers. | Accept with art/native gaps |
| Film | Legacy and rebuilt compositions retained separately. Library remove opens the source-style confirmation. | Return rebuilt Android chrome; baseline choice pending |
| Series | Both distinct episode ledgers present. Legacy season End/Enter selects Specials. Rebuilt phone synopsis matches source complete-text intent (`TheatreSeriesView.qml:1189-1229`). | Return rebuilt Android chrome; baseline choice pending |
| Comic series | Active ComicSeries release shelf, collected editions, search/order/actions preserved. Zero-match search works; clear restores eight rows. | Accept with art/native gaps |
| Comic edition | Frozen ComicSeriesPage explicitly PARKED; ledger/alternate source flow kept separate. Actual alternate-source action → weak confirmation → cancel → Back returns exact original alternate-source button. | Accept as parked study; no route activation approved |
| Comic archive | Source GenreMosaic board and ComicArchiveIndex retained. Board → DC index → All releases retains DC identity; Back returns index. Bundled Batman release fixture limitation stays disclosed. | Accept with art/data/native gaps |
| Saga | Source inline grid, tabs and Continue variant retained; original/proposed tab geometry measured. Context Remove emits source intent and leaves card. | Accept with B2-018 choice pending |
| Shelves | Original medium panels/rails and Continue retained; original/proposed tab geometry measured. Context Remove emits source intent and leaves card. | Accept with B2-018 choice pending |

## B2-018 and original state ownership

The docking proposal is a concrete fitting change, not a wholly new tab design. Actual frozen Saga source at 1087–1133 docks the natural-width bar at gutter+118; Shelves does likewise at 707–716. At 354px portrait after the first tab action, original Saga right=627.25, Shelves right=448.6875; proposed bars end at330 and sit below Back. At 792px landscape, both modes retain identical source position. Proposal preserves the original labels/pills and supplies horizontal scrolling. **It remains unapproved.** Retain both modes for Hemanth/primary choice; I recommend the conditional fit on this evidence. `focused-results.json` has measured rectangles; `captures/{saga,shelves}-{portrait,landscape}-{docked,original}.png` shows the comparison.

Frozen Saga Continue delegate 519–531 and Shelves 462–474 wire Resume/Detail only. Finished `app.js:61-70` emits removal intent without deleting. Actual keyboard ContextMenu → End → Enter retained one card on both pages and logged that the parent has no removal binding. No destructive or service behavior was invented here.

## Integrity and bounds

Independent SHA-256 checks matched **25 finished files**, **80 source/asset files**, **68 existing input files against BOTH start/end manifests**, and **all 522 PNG capture pairs**. Seven absent entries have no hashes and are not counted as verified existing files. See `independent-hash-check.json`. These checks establish present-byte agreement with the manifests, not an external proof that the declared archive pin was originally produced correctly.

Fresh isolated headless Chrome 156 rendered only loopback/local resources with zero page/resource errors and zero blocked external requests in completed passes. One owned profile/server at a time; all closed in finally, receipts in `closed*.json`. No repository/Git mutation, native app launch, ADB, service/account operation or publication performed. Writes are confined to this review folder. Original Pro outputs remain byte-matched.

This was a focused receiving review, not a rerun of every Pro case. Reported large-suite passes remain Pro evidence. Exact art is still substituted with disclosed frozen-project art; cast uses initials. Missing original worker19 REPORT remains a provenance gap. Native fonts/text metrics, real insets, focus/routing/persistence, clipping, async source lifecycle and service calls remain unqualified. No QML implementation or device acceptance is claimed.

Own evidence corrections are transparent: initial Universe captures used an oversized outer capture viewport and were replaced by actual-size captures; `focused-results.json` supersedes those entries in `fresh-results.json`. Initial programmatic alt state lacked an invoker; `last-focused-results.json` proves return from an actual invocation. Initial `series-*-ledger.png` files show bottom recommendations; use the later `*-ledger-actual.png`. Helper initialization/shortcut errors were corrected and are not artifact failures.
