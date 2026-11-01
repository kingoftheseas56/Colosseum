# Batch 3 receiving review

**Proposed disposition: RETURN_FOR_CORRECTION.** Fifteen pages are usable fitting/porting references with disclosed fixture/native gaps. The shared Search mock has a reproducible query-entry defect and submission-focus regression. This is a mock receiving decision, not native product acceptance.

## Required corrections

1. **P1 / SOL-B3-01 — slow typing erases the query.** In `finished-mocks/20-search-browse/app.js:147,328`, the 210 ms debounce changes one-character queries to the empty state, whose render blanks the input. At 412×900, Ctrl+A and type `Sherlock` at 260 ms/key: the final field is empty, focused, caret 0. Fast `Dune` works. Preserve input text independently of result-state rendering; test pauses after the first character and complete ordinary typing in all lanes. Evidence: [search-carousel-checks.json](search-carousel-checks.json), [screenshot](search-slow-typing.png). Frozen `BiblioSearch.qml:53,237-244` and `SearchSurface.qml:27,62` keep the input as query authority; zero characters defines empty.
2. **P2 / SOL-B3-02 — Enter loses focus.** In the same JS file at line 360, submit a populated `Sherlock` query after debounce. Focus moves from the input to BODY; typing ` Holmes` leaves the query unchanged. Restore focus/caret around submission. Evidence: [search-carousel-checks.json](search-carousel-checks.json), [screenshot](search-enter-focus.png). This is separate from the initial stale browser handle in the receiving harness.

No fixes were applied to Pro's mocks. Return Search for these bounded corrections; do not redesign the remaining pages.

## Independent evidence

- 64 fresh screenshots: all 16 pages at 412×900, 900×412, 1280×700, plus 792×354 / 1.3× / 24px inset stress. Every page's upright and sideways visuals was inspected, including corresponding desktop composition. [Portrait](412x900-sheet.jpg), [landscape](900x412-sheet.jpg), [desktop](1280x700-sheet.jpg), [stress](stress-sheet.jpg); originals are retained beside these sheets.
- Original worker assets and frozen input preservation: 847 snapshot entries checked with zero mismatches (845 unique preservation files as recorded). All 47 final artifacts and all 37 capture input files match their recorded current-resource hashes. All 37 unique directly referenced frozen desktop source files match Git `a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41`, exactly or UTF-8 CRLF-only. See [provenance.json](provenance.json).
- Fresh decisive checks cover all three catalogue final-index jumps, Biblio customization/index hiding, Discover popup bounds and source list scrolling, all three library select-all/confirmed membership removals, all three world carousel transitions, Explore keyboard reorder and retained See All return, genre navigation/view/membership, Continue keyboard context dismissal/focus and actual Search cadence/submission. Raw observations remain in [fresh-checks.json](fresh-checks.json), [focused-checks.json](focused-checks.json), [search-carousel-checks.json](search-carousel-checks.json).
- No page JavaScript errors or missing required resources observed. Implicit favicon requests return 404; this is not a required artwork/font failure. Initial missing bundled Chrome was resolved by selecting the existing installed Chrome. No install was performed.

## Fidelity and porting assessment

The pages retain gold/glass pills, source typography, book/poster/progress card families, world navigation and original index/sidebar/picker distinctions. Expanded row indexes reserve space; Discover retains its separate provider sidebar behavior. The 840px world-navigation gate is distinct from alpha's short-side phone-selection gate. Source inspection supports the porting notes on CarouselSlide poster/inset behavior, Biblio retained Discover/Explore, Theatre/Tankoban Next Up before Continue, library membership versus progress ownership, ranked book source reveal, and separate genre/result compositions. The original/finished comparison is HTML versus HTML, not a native screenshot; frozen QML is the authority.

The optional poster bleed proposal is clearly marked unapproved and remains outside the selected default. Frozen `CarouselSlide.qml:19-22,46,63,81-84` explains the gradient-only narrow poster branch. Hemanth may decide that proposal separately; no decision is needed to retain the default. Cosmere's one-button tribute treatment is distinct from the other tribute-plus-standard universe actions. Universes remains the fourth world; the normal picker does not make Vault a sixth world.

Representative mismatched covers (B3-015) and missing historical worker handoffs (B3-014) are accurately disclosed. Fresh hashes/captures support current provenance but cannot recreate absent historical reports. Keep title/crop acceptance open until real covers are supplied. The HTML's arrays, URL return state and local mutations must not become replacement native owners.

## Per-page coverage

| Page | Result / decisive observation |
|---|---|
| biblio-home.html | ACCEPT_WITH_GAPS — Native poster default, book rails, Discover/Explore composition; customization hide/index checked. |
| theatre-discover.html | ACCEPT_WITH_GAPS — Theatre tabs, original source sidebar and category/filter vocabulary; popup fit and provider-list scrolling checked. Default is scrolled Discover, not missing upper context. |
| theatre-catalogue.html | ACCEPT_WITH_GAPS — Expanding rail and final-row jump checked; original shelf/index treatment retained. |
| manga-catalogue.html | ACCEPT_WITH_GAPS — Collection and ranked/standard rail vocabulary retained; final index jump checked. |
| comics-catalogue.html | ACCEPT_WITH_GAPS — Collection, catalogue rails and mosaic/index composition retained; final index jump checked. |
| theatre-home.html | ACCEPT_WITH_GAPS — Featured, Next Up, Continue composition retained; carousel changes Dune to Shogun. |
| tankoban-home.html | ACCEPT_WITH_GAPS — Featured, Next Up and shared progress retained; carousel changes One Piece to Saga. |
| universes-home.html | ACCEPT_WITH_GAPS — Tribute carousel, separate universe banners and medium facets retained; carousel changes One Piece to Star Wars. Source Cosmere has one action, other standard+tribute universes have two. |
| theatre-library.html | ACCEPT_WITH_GAPS — Original search/sort/ledger, cards and actions retained; select-all confirmed removal produces empty membership. |
| tankoban-library.html | ACCEPT_WITH_GAPS — Original reading filters/cards retained; select-all confirmed removal produces empty membership. |
| biblio-library.html | ACCEPT_WITH_GAPS — Book/author information retained; select-all confirmed removal produces empty membership. |
| search.html | RETURN_FOR_CORRECTION — Portrait/landscape/desktop styling fits; core slow query entry and Enter focus fail. |
| biblio-explore.html | ACCEPT_WITH_GAPS — Shelf/index composition retained; keyboard reorder and See All->Discover->Back retain reordered state. |
| genre-index.html | ACCEPT_WITH_GAPS — Grouped genre cards/editorial directory retained; Adventure activation reaches results. |
| genre-results.html | ACCEPT_WITH_GAPS — Standfirst/detail/covers and original genre-chip vocabulary retained; membership and same-count view switch checked. |
| continue-see-all.html | ACCEPT_WITH_GAPS — Scope/filter/sort and progress-card vocabulary retained; keyboard menu Escape restores tile focus. |

## Pro claim reconciliation and native gaps

The on-disk records contain 160 matrix captures, 220 focused checks, 11 navigation checks, 27 HTTP checks and 68 contact sheets, with no failures recorded by those final checks. These are verified record counts, not independently repeated check totals. Their “no failures” result does not cover the two fresh Search failures. Fresh tests were bounded, with no full matrix rerun or native qualification.

Still unverified: real Qt metrics/focus, Android SafeArea/IME and touch behavior, provider loading/error/cancellation, catalogue identity, persistence, partial mutation failures, player/reader endpoints. No app build, Git change, device run, account/provider operation or publication occurred. Owned headless browsers/contexts and loopback servers were closed; closure times/origins are in REVIEW.json. Requested reviewer route was Sol 6.1 Medium; effective runtime model/effort metadata was not exposed.
