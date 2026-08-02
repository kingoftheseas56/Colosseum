# Theatre Catalogue Poster & Shelf Polish — Eyes-On Gate (Task 8)

**Status:** Automated gates GREEN. Live pilot shown to Hemanth — visuals approved; he flagged slow
poster loading, now fixed (see "Post-pilot fix" below). Awaiting confirmation before Tasks 9–10.

## Post-pilot fix (2026-08-03) — poster load speed

Hemanth's eyes-on: visuals good, **posters too slow to load**. Root cause (measured, not guessed):
the arc had switched poster fetching to "medium first, small fallback". Ground truth from
`live.metahub.space`:

- Metahub **small = 300×450**, **medium = 500×750**. The gallery poster (148px) decodes at **≤296px**
  (2× cap), so both sizes downscale to ~296px — **medium adds no visible sharpness at this size**.
- Medium is **2–3× the bytes** (e.g. tt1375666: 42KB small vs 108KB medium).
- Metahub has **no medium for many long-tail titles** (tt2431250 → medium **404 after ~2.1s**), so
  those posters waited ~2s for a failed request *before* falling back to small.

**Fix:** resolve Metahub posters to **small** everywhere — in `PosterSourcePolicy.candidates()` (the
cards) and back in `TheatreApi.normalizeArtUrl()` (the genre mosaic and any direct cover consumer).
Small is fast, always present, and already sharp at the capped decode. All harnesses + the residency
probe stay green. This retires the arc's medium-first change; every other polish (rounded crop,
bounded decode, lazy shelves, gallery geometry, hover-only rating) is unchanged.

**Arc:** Make Theatre's poster catalogue sharper, calmer, and more expensive while staying
unmistakably Colosseum. In-place refinement, not a redesign.

- Spec: `docs/superpowers/specs/2026-08-02-catalogue-poster-shelf-polish-design.md`
- Plan: `docs/superpowers/plans/2026-08-02-catalogue-poster-shelf-polish.md`
- Approved mock: `docs/superpowers/specs/assets/2026-08-02-theatre-catalogue-polish-mock.html`

---

## What changed for you as the user

Theatre's shelves now use bigger, genuinely rounded posters that load promptly (small 300px art,
already sharp at this size), a calm two-line title, and quiet hover that
reveals the IMDb rating only when your pointer is over a card. Keyboard focus stays clearly visible
but never pops the hover rating. Under the hood, only the shelves near your screen stay "live," so a
long page stops getting heavier as you scroll, and a rail you scrolled sideways returns to exactly
where you left it. The wallpaper, top bar, gold, tabs, Fraunces headers, Top 10 numerals, Customize
rows, and Genre mosaic are untouched.

---

## Commits (all on `master`, task-scoped)

Current master HEAD: **`e7ceec6`**

| Task | Commit | Title |
|---|---|---|
| 1 | `d001ec3` | feat(catalogue): add poster source and metric policy |
| 2 | `02593e2` | feat(catalogue): render bounded rounded poster art |
| 3 | `d70995b` | feat(catalogue): add restrained gallery poster profile |
| 4 | `7c47ac3` | feat(theatre): add gallery rail geometry and focus |
| 5 | `bb6af7f` | feat(catalogue): mount poster shelves around viewport |
| 6 | `55edd86` | feat(theatre): compose lazy gallery catalogue shelves |
| 7 | `e7ceec6` | feat(theatre): extend gallery profile to see all |

(An unrelated Agent 4 plan commit, `bfd477e`, interleaved on master mid-arc; it touched none of
these files.)

### Files changed per task
- **Task 1:** `qml/CatalogueVisualMetrics.js`, `qml/PosterSourcePolicy.js`, `qml/TheatreApi.js`, `tests/poster_source_policy_harness.qml`, `tests/test_theatre_deep_catalogue.ps1`
- **Task 2:** `qml/RoundedPosterImage.qml`, `tests/rounded_poster_image_harness.qml`, `tests/test_theatre_deep_catalogue.ps1`
- **Task 3:** `assets/icons/rating-star.svg`, `qml/CataloguePosterCard.qml`, `tests/catalogue_poster_card_harness.qml`
- **Task 4:** `qml/PosterRail.qml`, `tests/poster_rail_gallery_harness.qml`, `tests/test_theatre_deep_catalogue.ps1`
- **Task 5:** `qml/LazyPosterShelf.qml`, `qml/WorldPage.qml`, `tests/lazy_poster_shelf_harness.qml`, `tests/test_theatre_deep_catalogue.ps1`
- **Task 6:** `qml/TheatreCatalogPage.qml`, `qml/TheatreWorld.qml`, `tests/theatre_catalog_page_harness.qml`
- **Task 7:** `qml/CataloguePosterGrid.qml`, `qml/DiscoverBrowser.qml`, `qml/TheatreSeeAllPage.qml`, `tests/catalogue_polish_scope_test.mjs`, `tests/discover_browser_harness.qml`, `tests/theatre_see_all_harness.qml`
- **Task 8 (this):** `tests/catalogue_residency_probe.qml`, `tests/capture_catalogue_perf.ps1`, `agents/eyes-on/theatre-catalogue-polish/*`

---

## Automated evidence (all GREEN)

### Focused + adjacent contract suite
`tests/test_theatre_deep_catalogue.ps1 -Stage All` → **`THEATRE_DEEP_CATALOGUE_OK`** (exit 0). Stages:
`POSTER_SOURCE_POLICY_OK`, `ROUNDED_POSTER_IMAGE_OK` + `ROUNDED_POSTER_RENDER_CHAIN_OK`,
`POSTER_RAIL_GALLERY_OK`, `LAZY_POSTER_SHELF_OK`, `THEATRE_CATALOG_RULES_OK`, `THEATRE_API_ROWS_OK`,
`CATALOGUE_POSTER_CARD_OK`, `THEATRE_SEE_ALL_OK`, `THEATRE_ROW_PREFERENCES_OK`, `THEATRE_PAGE_OK`,
DiscoverRegression (`discover_api/page/picker` ALL PASS + `DISCOVER_BROWSER_OK`).

Adjacent: `tests/test_theatre_top10_genre_boxes.ps1` (passed), `tests/test_explicit_content_policy.ps1`
(`EXPLICIT_CONTENT_POLICY_OK`), `node tests/catalogue_polish_scope_test.mjs` (`CATALOGUE_POLISH_SCOPE_OK`).

The static render-chain guard proves `RoundedPosterImage.qml` has exactly ONE `MultiEffect` and no
`ShaderEffectSource` / GPU blur / GPU shadow (negative-controlled: a real instantiation trips it, a
comment does not).

### Native build
`cmake --build native/build-msvc --target colosseum` → **`ninja: no work to do`** (exit 0). This arc
changed **zero** C++/CMake/native sources, so the existing binary already runs the new QML (loaded
from disk at runtime). Log: `build.log`.

### Runtime QML-warning scan
The residency probe exercises the full gallery stack (TheatreCatalogPage → LazyPosterShelf →
PosterRail → CataloguePosterCard → RoundedPosterImage). Scan for `binding loop / shader / import /
image / ReferenceError / TypeError` → **none introduced**. (The only offscreen noise is the
environmental `Cannot find font directory` and `HouseScrollBar`/`OpenThemeData` lines, present before
this arc and unrelated to it.)

### Object residency + horizontal restore (deterministic probe)
`tests/capture_catalogue_perf.ps1 -Mode Probe` → **`CATALOGUE_PERF_PROBE_PASS`**. Long page = 25 rows,
900px viewport. Log: `residency-probe.log`.

| Viewport position | Live `LazyPosterShelf` (of 25) | Live card ceiling |
|---|---:|---:|
| Top | 8 | 96 |
| Middle | 10 | 120 |
| Bottom | 2 | 24 |
| Returned to top | 5 | 60 |

- **Counts plateau, not linear:** max live = 10 of 25 (bounded by the ~3-viewport window, independent
  of total rows). If lazy residency broke, this would reach 25 → the gate fails it.
- **No leak on return:** returned-to-top (5) ≤ initial plateau (8). Gate fails if it grows by >1.
- **Horizontal restore exact:** a rail scrolled to `contentX=137`, unloaded (2 viewports away), then
  remounted, restored to **137 — error 0 px** (gate tolerance 1 px).
- **Reserved height stable:** the page `implicitHeight` is unchanged as the viewport moves (each
  shelf reserves its full height whether or not its rail is live → vertical scroll never jumps).

---

## Pending: Hemanth's live eyes-on (the visual + warm-scroll gate)

Qt/D3D-rendered pixels are uncapturable from a headless agent (no in-app grab; PrintWindow/BitBlt
return black for the D3D swapchain). These are your eyes, brother — I will not fabricate screenshots.

**Launcher (already built, no rebuild needed):**
```
# with the app on screen, from the repo root:
tests\capture_catalogue_perf.ps1 -Mode App -Page Movies -Pass Warm -Seconds 25
```
This launches `native/build-msvc/colosseum.exe` with `QSG_RENDER_TIMING=1`; scroll Theatre → Movies
and open a See-all during the window. It writes `qsg-frametiming-Movies-Warm.log` here and flags any
frame over 100 ms. (If your running instance hot-reloaded, it already reflects this arc.)

**Please confirm, at 100% / 125% / 150% Windows scaling, on Theatre → Movies and a See-all:**
1. Poster edges are crisp and genuinely rounded (12 px); art never paints over the inset edge.
2. Resting card shows poster + two-line title only — no year/genre/source/rating/badge line.
3. Pointer hover reveals `★ rating` + `IMDb`, a quiet 7 px lift; **no** centered play ring, **no** Unicode star.
4. Keyboard focus shows the gold double halo but does **not** reveal the hover rating.
5. Posters appear promptly and look sharp (small 300px art at the ≤296px capped decode); no blank tiles.
6. An exhausted poster keeps the neutral placeholder — never a broken-image icon.
7. Scrolling a long page stays smooth (no sustained frames > 16.7 ms warm; no stall > 100 ms), and a
   sideways-scrolled rail returns to its position after scrolling away and back.
8. Cold Ripple wallpaper/vignette, TopBar, Theatre gold pill, TheatreTabBar, Fraunces headers, house
   gold `#f0c44a`, WorldPage margins, Customize rows, Genre mosaic, Top 10 numerals — all unchanged.

---

## Hygiene confirmations
- **On `master`**, no branch or worktree created by this arc (the other worktrees listed by
  `git worktree list` are pre-existing, other agents').
- **Unrelated dirty files preserved.** In particular the separate scroll-speed work in
  `qml/CataloguePosterGrid.qml` and `qml/DiscoverBrowser.qml` was **not** absorbed: only this arc's
  hunks were committed (surgical staging); the scroll hunks remain as unstaged working-tree changes,
  intact (+12/+12). The committed (scroll-free) versions of both files were independently rebuilt and
  passed See-all + DiscoverRegression + scope.
- **Runtime uncertainty:** warm-scroll frame budget and the visual states above are the only items
  not machine-verified here — by nature (they need the rendered display), not by omission.

## Gate
Tasks 9–10 (opt Theatre Discover / Tankoban Manga / Tankoban Comics into the gallery profile, then
full regression) remain **pending Hemanth's approval of this live Theatre pilot**. Approval of the
HTML mock is not approval of the live build.
