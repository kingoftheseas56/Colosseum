# Batch 5 receiving review

Proposed verdict: **ACCEPT_WITH_GAPS** for the mock handoff. All seven pages are usable fitting references. This is not native or device acceptance, and the claim that no behavioral repair remains is too broad. Primary retains final acceptance.

## Finding

**SOL-B5-001 / P2 — Restore the exact tracker dossier opener.** Open Connections at 354×792, click MyAnimeList, then press Escape. Focus lands on Preferences instead of the MyAnimeList card. Independently reproduced twice. See [tracker-focus-after-escape.png](tracker-focus-after-escape.png) and [checks.json](checks.json). The fallback is in finished-mocks/tracker.js:325; openPanel at :327 records only the action, and Escape at :386 renders without using the opener. Frozen TrackerSyncCenterPage.qml:1381–1401 instead retains dossierInvoker and restores that exact enabled visible item. Keep that existing source behavior during porting; repair all dossier close paths and separately verify nested dialogs. The visible design can proceed, but this keyboard regression must remain recorded until repaired.

## All-seven-page coverage

| Page | Fresh portrait / landscape / desktop | Decisive review |
|---|---|---|
| Connections | 354×792 / 792×354 / 1280×700 | Approved heading/action/relay/connected/catalogue/Stremio hierarchy, source icons and scroll-end; exact opener failure above |
| Dossier | same three | Body click stays open; slow title match keeps text and returns to a separate progress decision; MAL send/live/export gates preserved |
| Preferences | same three | All four source switches toggle; Escape closes; original panel retained |
| Wallpaper | same three | Six native canvases render; slow zero-result search; last KDE pick and local Apply; original shelves/picker retained |
| Extensions | same three | Slow URL typing and preview/install/Done; original text menu Select all/Cut restores editor; core toggle gated; artwork and horizontal House composition retained |
| Update | same three | Original artwork/gallery/status composition; unknown size has no fabricated percent; Pause/Resume works |
| Collections | same three | Remove→Cancel preserves six titles; realistic slow search; nested Create→Cancel returns; Escape closes per approved source |

The seven page contact sheets were visually inspected in both orientations and desktop context, alongside fresh runtime captures. The sheets compare unchanged original worker HTML with finished HTML; neither side is an approved native capture. Source checks included latest Connections typography/grid/action hierarchy and closeDossier, preferences, wallpaper identities, update unknown-size handling, collection overview/card/search/edit and AddToCollectionsPopup close/cancel contracts. No generic phone card/sheet replacement or lost major section was found in the reviewed samples. This bounded review does not certify every possible data/capability state.

## Independent evidence

- **21 fresh captures**, all seven pages in compact portrait, compact landscape and desktop. No horizontal document overflow, missing used image or page script failure. One initial /favicon.ico 404 belongs to the minimal reviewer server, not a failing product asset. No external requests were attempted in these captures.
- **11/12 focused scenarios passed**; the one failure is SOL-B5-001. Realistic typing used 100–200 ms per character. These are independent checks, not a replay or restatement of Pro's 44 scenarios.
- **63 viewport +108 state/scroll receipts =171**. Every nonempty loaded-code hash list matches current artifact bytes. All 171 recorded receipts have no script/HTTP error, missing image or external-request entry.
- **342 capture PNG/receipt files** and **30 finished files** match their final manifests byte-for-byte; **31 report-listed artifacts** also match. **29/29 intake files** remain byte-identical. Ten contact-sheet PNGs exist. These verify present bytes against supplied manifests; they do not establish an independent historical chain of custody before intake.
- Pro's 44 passing scenario receipts were inspected for method and boundaries, not fully rerun. Existing stress/state evidence remains source/hash-bound; physical geometry is provisional.

[checks-first.json](checks-first.json) preserves two initial harness problems: wrong shelf selector and an inert background New collection selector, plus an expected-count mistake (Worlds correctly matches two fixtures). Corrected checks pass. The initial expectation that nested-create Escape should return was rejected after source inspection: Popup.CloseOnEscape closes the whole popup; Cancel is the return action. No collection bug is reported from those checks.

## Acceptance boundaries

The missing original tracker worker REPORT.json is the known provenance gap. Current source and final evidence are sufficient to receive the mock handoff with that gap explicitly retained. No native Batch 5 reference, Qt shader/font parity, physical device geometry, OS keyboard/safe-area behavior, provider/authentication, persistence, shell routing or actual updater operation has been qualified. Preserve existing owners and real source behavior; local fixture outcomes are not service success.

No new phone UI exception or additional product behavior is requested. Keep latest approved Connections refinement and Android window-chrome hiding. The focus repair and the overbroad no-unfinished-repair statement should be routed back; no mock was repaired during this review. Browser/profile and owned loopback server were closed after each isolated session. Only codex-review-sol was written.
