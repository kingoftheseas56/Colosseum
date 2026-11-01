# Batch 3 — finished browsing and library mocks

## Outcome

**All 16 assigned pages have corrected, runnable HTML versions under `finished-mocks/`.** The selected layouts preserve the existing Colosseum design: local Fraunces/Segoe typography, gold and glass controls, project artwork, world navigation, rails, cards, menus and editorial copy. The finished set includes the previously incomplete Theatre world context above Discover.

Start with [index.html](index.html). It links every finished page, its preserved worker original, a live comparison, and saved comparison/stress sheets. [finished-mocks/index.html](finished-mocks/index.html) is the direct mock entry point. The pages run from local files or a static HTTP server rooted at the existing project folder; [README.md](README.md) gives the exact launch command and URL.

This pass implements the user’s expanded mock ownership instruction. It does not change the application, repository, frozen references or original worker outputs. No application build, installation, device operation, provider call or publication was performed. The temporary verification browser and loopback static-artifact server were owned by this pass.

## Completion and coverage

| Group | Finished pages | Completed work |
|---|---|---|
| 07 Biblio | `biblio-home.html` | Reading Desk scene, Biblio world, native poster hero, Discover, retained Explore, row index, filters, customization and owned navigation. |
| 08 Theatre Discover | `theatre-discover.html` | Original browser/sidebar/pickers, all five world tabs, and complete carousel/Next Up/Continue context. |
| 09 Catalogue | `theatre-catalogue.html`, `manga-catalogue.html`, `comics-catalogue.html` | Rail geometry/state, row customization, world routes, source Collection control, catalogue and genre doors. |
| 15 World homes | `theatre-home.html`, `tankoban-home.html`, `universes-home.html` | Hero fitting, carousel actions, progress rows, local filters, Universe facets and original action distinctions. |
| 16 Libraries | `theatre-library.html`, `tankoban-library.html`, `biblio-library.html` | Search, menus, local mutations, long press, selection/invert/all, confirmation, empty states and reserved selection space. |
| 20 Search/Browse | `search.html`, `biblio-explore.html`, `genre-index.html`, `genre-results.html`, `continue-see-all.html` | Queries, keyboard fitting, shelf drag/keyboard reorder, retained return state, lane-aware genre directories/results, sorts/views and scoped progress actions. |

[COVERAGE.csv](COVERAGE.csv) records each page’s final status, focused checks, matrix captures and visual evidence. The per-page `evidence/qa-*.json` files contain the actual assertions and screenshots. [CHECKPOINT.md](CHECKPOINT.md) records page completion and retests as work progressed.

The selected mock versions are verified in the browser. The remaining items below are a product decision, an asset fixture limitation and missing historical worker documentation. They are not silently treated as completed product acceptance. No selected page is handed off as fixed-but-unverified; the machine-readable final counts and any exceptions are recorded in [REPORT.json](REPORT.json).

## What changed and why

### Shell, source fidelity and desktop presentation

The workers mixed two different responsive gates. PhoneShellHost switches world navigation at **840px window width**: bottom navigation below it and a 92px left rail above it. Library phone selection separately uses **short side < 600**. The finished pages preserve both rules. Consequently, 900×412 retains phone library selection while using a left world rail; 800×1280 keeps bottom world navigation while omitting phone selection.

World margins and hero dimensions now use the width remaining after navigation. The original desktop world capsule, disabled SOON entries, account silhouette, native icons and hero typography remain recognizable. Desktop was checked at 1280×700 alongside the frozen QML; tablet presentation was checked in both orientations. The comparison sheets show worker HTML against finished HTML, not a native application screenshot.

The source poster/banner distinction is restored. Biblio’s poster hero uses one gradient-backed inset at native geometry on wide slides. Tankoban poster slides follow the same branch. The available Dune image is landscape saga artwork, so a native poster inset letterboxes it rather than pretending it is a portrait book cover. P1 below isolates the narrow-phone artwork choice.

### Browsing, indexes and missing context

The Biblio expanded-index toggle was falling into normal flow and could collide with navigation. It remains absolutely anchored to the rail edge. Expanded row indexes reserve space in place, keep their current-row highlight, reflect customized order and hidden rows, and stay open after a row jump. The original Discover provider sidebar retains its own existing overlay behavior; these two sidebar types were not conflated.

Ranked book rails retain source reveal while suppressing only the competing rating. Rank numerals sit behind covers rather than across captions. Standalone Explore now uses WorldPage margins and the original 130×182 book art size. Shelf drag handles perform actual pointer reordering, with Ctrl+Shift+Up/Down as the existing keyboard route. See All returns to the preserved Explore order, hidden rows, expanded index and scroll position.

The original 08 mock had only a rough representation of the world content above Discover. Its finished copy includes the TheatreWorld carousel, Next Up and Continue Watching, with the original row/card vocabulary, keyboard carousel controls, context menu, progress removal and scoped See All. The default still lands at Discover; `context=top` exposes the complete upper context directly.

Pickers retain their source grouping, appearance and clear affordance. Their widths and scroll heights are bounded by the available viewport, with above/below placement, keyboard navigation, Escape and focus return. Selected filters can reopen; clear resets only the relevant filter. Local fixture filtering now changes the displayed items and reaches meaningful empty states.

### Libraries and progress actions

The finished libraries perform their local demonstration actions. Confirmed single/batch removal changes membership and reaches the original empty state. Select All and Invert use visible item identities. Theatre watched/unwatched changes fixture appearance. Dismissing progress remains distinct from removing a library entry.

The source selection host keeps its 128px portrait and 72px short-landscape reservations plus the bottom inset, including the horizontal action scroller. Menus stay at the root rather than being clipped inside a poster delegate. The alpha source explicitly uses a 48px visible phone dots control; that design was retained.

Two translation defects were corrected: QML ARGB `#99000000` becomes CSS `rgba(0,0,0,.6)`, so confirmation actually dims its backdrop; and scaled library titles reserve exactly two lines instead of multiplying text scale twice. At compact 1.3× stress, long Theatre tab labels elide inside their existing pills. Their full labels and activation remain available.

Continue See All preserves all four scopes, the home-only media filters, original sorts and the distinction between “Nothing to continue.” and “Nothing here yet.” Keyboard context actions keep Open details / Resume / Remove, first-item focus, arrow navigation and focus return.

### Search and genre results

Search queries now filter deterministic local results and distinguish success from empty results. The empty audiobook section is omitted. A Books count with no “rest” cards is valid when the only book is already displayed as top match. Theatre browse genres use Theatre metadata rather than manga labels.

The existing labelled BackAction and search field share a row in short landscape with the illustrative keyboard reserve. Genre results retain their original standfirst, Detailed/Covers views and lane-specific sorting: Biblio Readers, manga Readers/Score, Theatre watcher wording. On phones, the genre hero grows with its measured text so the kicker cannot run beneath Back/Search. Desktop keeps its 300px hero.

BiblioWorld’s legacy genre pages are still individually reviewable because this batch explicitly assigns them. Owned BiblioSearch jump actions return to their owner and do not reintroduce a retired navigation route.

## Verification and evidence

The final matrix contains **160 captures: 16 pages × 10 combinations**:

| Class | Logical viewport | Text / inset configuration |
|---|---|---|
| Primary phone | 412×900 and 900×412 | 1×, zero additional inset |
| Compact phone | 354×792 and 792×354 | 1×, zero additional inset |
| Desktop | 1280×700 | Original desktop presentation |
| Tablet | 800×1280 and 1280×800 | 1×, zero additional inset |
| Stress | 354×792, 792×354 and 900×412 | 1.3× text, 24px top and bottom |

The capture checks inspect image decoding, font readiness, JavaScript errors, root dimensions and document overflow. Focused checks exercise long scrolling, open menus, keyboard/pointer actions, filters, empty states, selection, confirmation, reordering, carousel states and navigation. Enlarged-text control values are recorded in screenshot metadata; they are not inferred merely from a filename.

The original baseline contains **80 captures** across the 16 pages. Additional evidence covers Reading Desk, completed Theatre context, native/alternative poster heroes, library confirmation and keyboard fitting. Normal, stress/tablet and before/after contact sheets are supplied for every page. Intermediate screenshots preserve defects found late in the pass rather than erasing the investigation.

The final local HTTP check loads every finished page, verifies both entry indexes, checks original/finished stress controls in live comparison, exercises the six folder redirects and validates P1’s links/images. The read-only static server binds only to loopback and is closed after testing. Cross-page navigation is also checked separately.

Important evidence entry points:

- [Final verification](evidence/final-verification.json), [HTTP checks](evidence/http-qa.json), [navigation checks](evidence/navigation-qa.json).
- [Final matrix](evidence/finished-matrix.json) and [capture input hashes](evidence/FINAL-CAPTURE-INPUTS.json).
- [Original input snapshot](evidence/INPUT-VERSIONS-START.json) and [finished artifact versions](evidence/FINAL-ARTIFACT-VERSIONS.json).
- [Index-toggle comparison](contact-sheets/issue-biblio-index-toggle.jpg), [confirmation comparison](contact-sheets/issue-library-confirmation.jpg), [keyboard comparison](contact-sheets/issue-search-keyboard.jpg), [completed Theatre context](contact-sheets/issue-theatre-complete-context.jpg).
- [P1 comparison](decisions.html) and [QML porting guide](QML-PORTING.md).

[FINDINGS.csv](FINDINGS.csv) retains stable IDs for **27 grouped findings**. Twenty-five have implemented corrections and final verification; two remain disclosed input/fixture limitations. P1 is tracked separately as a product choice. Earlier exploratory failures remain in checkpoint/evidence history; the final per-page QA, HTTP and coverage records are authoritative. The preliminary HTTP run is retained separately as `evidence/http-qa-before-harness-correction.json`: it exposed lazy-image decoding assumptions in the verification harness. The corrected standalone run explicitly loaded all images after rendering and passed all 27 checks.

## Remaining decisions and exact follow-up

| Item | Current deliverable | Exact remaining work / owner |
|---|---|---|
| **P1 — artwork on narrow poster heroes** | Default follows frozen CarouselSlide: inset hidden below 600px, with no poster bleed. `posterPhone=bleed` is an isolated proposed alternative. Original attempt, source-faithful result, alternative and desktop evidence are in decisions.html. | **Hemanth** chooses whether the phone poster branch should show artwork. **Codex** changes the real source condition only after that choice. The alternative is not selected or approved by this handoff. |
| **B3-015 — representative artwork** | All artwork is from the actual frozen project bundle, but some title fixtures use other saga/universe art. Dune has no exact portrait book cover in the supplied assets. | Supply matching real catalogue covers and recheck crop/identity. Examples include Shogun/Ice and Fire, The Bear/Witcher, Fallout/Jurassic and some manga/comic fixtures. Existing geometry, controls and actions are usable now. |
| **B3-014 — historical worker handoffs absent** | Original REPORT files are absent for 08, 09, 15, 16 and 20; FIDELITY files are absent for 08 and 20. This review supplies fresh evidence without fabricating those worker records. | Recover the missing historical reports if provenance completeness is required. No mock fix depends on them. |
| **Native integration verification** | Working HTML fixtures and concrete porting notes. | In a separately authorized app pass, verify Qt text metrics, actual SafeArea/IME geometry, focus, providers, cancellation/errors, persistence and mutation outcomes. These were outside this assignment. |

The short-landscape keyboard example reserves 100px at 792×354 with 24px top/bottom insets. It proves a fitting arrangement for that fixture, not a measured Android keyboard. The requested viewport sizes and inset values are provisional. No physical-device or native-runtime acceptance is claimed.

## Porting handoff

[QML-PORTING.md](QML-PORTING.md) maps all 16 pages to their original components and records the source pins, breakpoint rules, card/rail dimensions, signals, reorder lifecycle, popup ownership, selection semantics and genre/search seams. It distinguishes changes already correct in QML from changes needed only in the HTML translation. It also gives a concrete intrinsic-height rule for the enlarged genre hero and a Text.ElideRight fitting treatment for narrow pills.

Keep the native models and owners. The HTML’s arrays, manual clocks, URL-carried return state and intent status messages are demonstration infrastructure. Details, players/readers, accounts, general Home and live provider operations stop at the batch boundary. Unowned Store/extension See All surfaces and other mock batches remain outside this handoff.

The source authority remains desktop **a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41** and alpha recovery **77844eef0bd3b7fee2c1f884be4a76db6d63321d**. The rejected Android snapshot **9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9** was not used as visual authority. The final hash verification reports exactly which tracked originals and source files were unchanged; it does not imply an audit of every untracked file on the machine.

The pass began at **2026-10-31 22:37:56 UTC**. REPORT.json records the final completion time, verification totals, artifact paths and remaining items, and is written after the deliverables and checks are complete.

