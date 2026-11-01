# Batch 2 — finished detail-page mock handoff

**Eight of eight assigned pages are finished and usable as local HTML mocks.** The consolidated final matrix verifies 56 page/size combinations. The completed interaction fixes are verified in HTML; one behavior proposal remains for Hemanth and two input/fixture limitations remain explicit. This is not native app acceptance.

Start with [finished-mocks/index.html](finished-mocks/index.html). It links every finished page, its preserved original and its comparison. Use [QML-PORTING-NOTES.md](QML-PORTING-NOTES.md) for original component ownership and concrete fitting changes.

## Scope and preservation

The user’s later scope update authorized repairing and completing mocks under finished-mocks, superseding review-only wording in the initial brief and unchanged README. Work stayed within this batch folder. Original workers, frozen references and other batches were preserved. No app/repository edit, build, install, device interaction, live service operation, upload or publication was performed. Check preservation-check.json for the start/end file comparison.

## Pages and useful finished behavior

| Page | Verified behavior | Evidence |
|---|---|---|
| Biblio book | Full synopsis; Read chooser; 7 expanded torrents; final audiobook rows; library; Collections save/cancel/create. | [Comparison](comparisons/biblio.html) |
| Theatre film | Full phone synopsis; opt-in More/Less; library/removal; Collections; Watch intent; 9-person expanded cast; recommendation rail. | [Comparison](comparisons/film.html) |
| Theatre series | 23 season options; Go to final episode; keyboard/context Play and Download; notification toggle; library/removal; Collections. | [Comparison](comparisons/series.html) |
| Comic series | Search/no-match/clear; sort; 8 release rows including final issue; whole-row read; queued intent; cancel; Collections. | [Comparison](comparisons/comic-series.html) |
| Comic editions | 5 editions; alternate-source keyboard action; search; weak confirmation; incomplete/combined/ambiguous pack outcomes; return to queued row. | [Comparison](comparisons/comic-edition.html) |
| Comic archives | 10 boxes; 8 index posters; archive-titled raw shelf; Back restoration; supported Batman route preserves viewport/insets. | [Comparison](comparisons/comic-archive.html) |
| Saga universe | 7 books/8 films; both Deathly Hallows parts; arrow-key grid; Continue/resume; branches; pending upcoming item; About and final section; tab alternatives. Continue context menu, focus return and Remove signal boundary. | [Comparison](comparisons/saga.html) |
| Shelves universe | 25 films; 14 books; final rail items; 40 magazine rows including last Now and Classics; actual 1.3 text growth; Continue; About; tab alternatives. Continue context menu, focus return and Remove signal boundary. | [Comparison](comparisons/shelves.html) |

## Principal fixes

- Restored the existing Collections component across six detail surfaces, with original cover/count/search/create controls, draft-versus-committed membership, Cancel/Escape and focus return.
- Restored Biblio chooser keyboard movement and corrected Android tablet window-control visibility. Full book information and source lists remain.
- Kept Theatre’s original default branch; fixed full handset synopsis in the opt-in branch, season menu fitting, final-episode jump coordinates, short-landscape sticky controls, nested keyboard actions and the original removal dialog.
- Corrected Comics Android chrome, long rows, alternate-source keyboard handling, archive identity/navigation and missing original source inspection states. The parked edition route remains explicitly parked.
- Corrected Universe Continue geometry/icons/badge/context controls, one-owner event handling, grid/rail keyboard navigation, Saga selected detail states, magazine text scaling/wrapping and source 72px tablet titles.
- Restored the approved ChromeScrim recipe and Theatre’s phone/pinned scroll shading after final visual comparison exposed the missing/approximate source treatment.

All eighteen grouped repair findings are fixed and verified in the relevant HTML evidence. FINDINGS.csv retains observed symptoms separately from inferred causes. No fixed-but-unverified repair is presented as verified. Source/provider routing and persistence remain local fixture boundaries, described below.

## Verification

Every page was checked at 412×900, 354×792, 900×412, 792×354, 1280×700 desktop, 800×1280 tablet and 1280×800 tablet. Final normal layouts have the expected inner frame dimensions, zero application-width overflow, no missing local images and desktop window controls hidden on the reviewed Android sizes. Native rendering is unavailable; the desktop column is approved QML inspection plus HTML before/after comparison.

The comic source page also passes 34 final state/size cases, including all original outcome states on desktop and the Android Back boundary. Possible/strong/weak actions and the source queued-state indicator were retested through real row entry, with focus and visibility restored.

Four phone stress sizes use actual 1.3 text scale with illustrative 24px top/bottom insets. Relevant checks include long synopsis, last source/episode/release/poster/magazine row, open Collections and Read controls, season/jump/context menus, removal confirmation, source inspection outcomes, scrolling, keyboard selection and return focus. All ten Continue context cases (both pages at four phone sizes plus desktop) fit and pass their action/dismissal checks. Two-column magazine order is verified at both tablets and desktop.

Use verified-layouts.json as the final matrix. It consolidates final-layout-matrix.json with the Biblio tablet, Universe Continue-control and Theatre/Comics ChromeScrim retests. Earlier failed attempts remain in their raw ledgers, with superseding evidence identified in ASSETS-AND-PROVENANCE.md. A temporary syntax error is retained in the browser’s cumulative history; final checks must not mistake that retained history for recurring errors.

## Hemanth decision — B2-018

The original source tab bar fits both phone landscapes but extends past both narrow portraits at 1.3 text. Saga reaches approximately x=627px; Shelves reaches x=449px. The verified proposal keeps the original beside-Back arrangement wherever it fits and moves the original-size scrollable bar below Back when it does not.

Both attempts are runnable and have matched action-based evidence. Actual tab landings clear their headings in all 24 final cases (Saga, Bond shelves and magazine shelves; both modes at four phone sizes). The proposed default is labeled “decision pending” outside the app. See [DECISION-B2-018.md](DECISION-B2-018.md) and [the eight matched comparisons](comparisons/decision-B2-018.html). Hemanth’s behavior choice is still required before a QML port uses that adaptation.

## Remaining findings and exact next work

1. **B2-018 — decision pending.** Choose the Universe narrow-phone tab docking. No additional unaffected mock implementation is waiting on this decision.
2. **B2-019 — unavailable original handoff.** outputs/19-generic-universes/REPORT.json is absent. Fresh evidence covers the available HTML/source; the absent worker report was not fabricated.
3. **B2-020 — actual item artwork/live data unavailable.** Existing frozen project artwork is retained as declared stand-ins, and provider/catalogue/progress data is fixture-only. Supply exact approved item assets to replace stand-ins later, then recheck image crop, contrast and title association.
4. **Native implementation and integration remain outside scope.** Apply the component-specific fitting in QML after the behavior decision, then verify actual native fonts, focus, clipping, device insets, routes and services. The frozen Saga/Shelves parents do not connect Continue removeRequested; the mock faithfully emits that intent and leaves the card, with the boundary visible in its external log. Connecting persistence requires separate app ownership.

There are no deliberately unfinished assigned HTML pages. Unavailable app/provider/device checks are not counted as passes. Archive routes without bundled detail data emit the selected intent rather than misleadingly opening Batman. The optional linked-account removal choice is only a local UI fixture. These limitations do not prevent running the finished layouts or using the evidence for the approved QML owners.

## Deliverables and evidence

- [Finished index](finished-mocks/index.html), eight workbenches, shared Collections runtime and local launch command.
- [Comparison pages](comparisons/biblio.html) and [contact sheets](contact-sheets/index.html), with original/finished labels and capture provenance.
- [COVERAGE.csv](COVERAGE.csv), [FINDINGS.csv](FINDINGS.csv), [QML-PORTING-NOTES.md](QML-PORTING-NOTES.md), [ASSETS-AND-PROVENANCE.md](ASSETS-AND-PROVENANCE.md) and [REPORT.json](REPORT.json).
- Per-page CHECKPOINT files, original/start/end manifests, final runtime hashes, raw browser ledgers, capture PNG/JSON pairs and the final preservation check.

REPORT.json is written last after final artifact validation and closure of the owned browser.

## Final handoff checks

Final [verification summary](verification-summary.json) and [visual review](VISUAL-REVIEW.md) are saved. The 75 start/end input records are unchanged. All 711 checked relative links/assets resolve, all 522 capture PNG hashes match their metadata, and the two Report links resolve to the final file written after these checks. The eight-card index works at desktop and 354px width; its finished-page link and comparison images were exercised. The owned Chrome session is closed; its process was confirmed absent.
