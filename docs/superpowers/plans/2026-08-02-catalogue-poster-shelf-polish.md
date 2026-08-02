# Catalogue Poster and Shelf Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Theatre's catalogue posters sharper and more refined while bounding off-screen shelf/image work, then expose an opt-in gallery profile for the other Colosseum worlds.

**Architecture:** Pure JavaScript policy modules define poster candidates and visual metrics. `RoundedPosterImage.qml` owns bounded image decoding, fallback, masking, edge, and cheap depth; `CataloguePosterCard.qml` owns interaction and metadata. `LazyPosterShelf.qml` keeps stable row geometry while mounting `PosterRail` only around the `WorldPage` viewport.

**Tech Stack:** Qt 6.11.1 QML/Qt Quick, `QtQuick.Effects.MultiEffect`, JavaScript `.pragma library` modules, PowerShell QML harness runner, QML Profiler/QSG timing.

## Global Constraints

- Work directly on `master`; do not create a branch or worktree.
- Preserve every unrelated dirty-worktree file and stage only the files named by the active task.
- The approved design is `docs/superpowers/specs/2026-08-02-catalogue-poster-shelf-polish-design.md`.
- The approved mock is `docs/superpowers/specs/assets/2026-08-02-theatre-catalogue-polish-mock.html`.
- Vertical scroll physics are out of scope; do not modify `qml/ScrollGlide.qml`, `tests/scroll_glide_harness.qml`, or `tests/test_scroll_glide_p0.ps1`.
- Do not change catalogue ranking, row definitions, extension placement, explicit-content rules, hero/Continue content, or See-all paging.
- Preserve the existing Cold Ripple wallpaper/vignette, `WorldPage`, clock/date `TopBar`, glass medium capsule, Theatre's gold medium pill, `TheatreTabBar`, Fraunces/Segoe typography roles, `#f0c44a` house gold, page margins, Customize control, Genre mosaic, and oversized Top 10 numerals.
- IMDb rating and attribution remain pointer-hover-only; keyboard focus must not reveal them.
- Theatre cards show poster and title only at rest; do not add a year, genre, source, rating, badge, or subtitle line.
- Use no permanent rating badge, centered play ring, Unicode poster-control glyph, per-card blurred shadow, `ShaderEffectSource`, or animated mask.
- Gallery values are exact: 148 px poster width, 2:3 art, 12 px radius, 20 px card gap, 46 px shelf gap, 18 px header gap, 13 px two-line title, 7 px hover lift, 260 ms hover, and 280 ms image reveal.
- Poster decode scale is `clamp(Screen.devicePixelRatio, 1.0, 2.0)`; the normal 148×222 card therefore decodes at no more than 296×444.
- Theatre is the live pilot. Existing Discover consumers remain on the classic profile until the eyes-on gate explicitly advances them.

---

## File map

### New files

- `qml/CatalogueVisualMetrics.js` — immutable classic/gallery geometry and timing tokens.
- `qml/PosterSourcePolicy.js` — pure URL normalization, candidate ordering, and deduplication.
- `qml/RoundedPosterImage.qml` — poster placeholder, fallback state, bounded decode, rounded mask, edge, and cheap depth.
- `qml/LazyPosterShelf.qml` — stable-height viewport-aware host for one `PosterRail`.
- `assets/icons/rating-star.svg` — house rating icon used only inside hover metadata.
- `tests/poster_source_policy_harness.qml` — pure source-policy and token contract.
- `tests/rounded_poster_image_harness.qml` — fallback/decode/mask state contract.
- `tests/lazy_poster_shelf_harness.qml` — activation, retention, height, and position persistence contract.
- `tests/catalogue_polish_scope_test.mjs` — static scope/blast-radius assertions.

### Modified files

- `qml/TheatreApi.js` — preserve Metahub poster size while retaining live-host normalization.
- `qml/CataloguePosterCard.qml` — classic/gallery profile, shared image component, title, hover/focus/accessibility.
- `qml/PosterRail.qml` — gallery geometry, keyboard navigation, and horizontal-position seam.
- `qml/CataloguePosterGrid.qml` — profile pass-through only.
- `qml/WorldPage.qml` — read-only viewport properties only.
- `qml/TheatreCatalogPage.qml` — main/extension rows use `LazyPosterShelf`.
- `qml/TheatreWorld.qml` — pass viewport coordinates and select Theatre gallery profile.
- `qml/TheatreSeeAllPage.qml` — select gallery profile.
- `qml/DiscoverBrowser.qml` — add an opt-in card-profile property; default remains classic.
- `qml/DiscoverPage.qml` — remain classic for the pilot; opt in only after Task 9 gate.
- `qml/TankobanDiscoverPage.qml` — remain classic for the pilot; opt in only after Task 9 gate.
- `tests/catalogue_poster_card_harness.qml` — gallery and classic card contracts.
- `tests/theatre_catalog_page_harness.qml` — viewport/lazy composition and behavior regression.
- `tests/theatre_see_all_harness.qml` — gallery profile pass-through.
- `tests/discover_browser_harness.qml` — explicit classic/gallery profile wiring.
- `tests/test_theatre_deep_catalogue.ps1` — new focused stages.

---

### Task 1: Lock metrics and poster-source policy

**Files:**
- Create: `qml/CatalogueVisualMetrics.js`
- Create: `qml/PosterSourcePolicy.js`
- Create: `tests/poster_source_policy_harness.qml`
- Modify: `qml/TheatreApi.js:136-147`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Produces `PosterSourcePolicy.candidates(url, explicitCandidates) -> string[]`.
- Produces `PosterSourcePolicy.liveUrl(url) -> string`.
- Produces immutable `CatalogueVisualMetrics.classic` and `.gallery` objects.
- Does not perform network requests or access QML items.

- [ ] **Step 1: Write the failing source-policy harness**

Create fixtures and exact assertions:

```qml
import QtQuick
import "../qml/PosterSourcePolicy.js" as Policy
import "../qml/CatalogueVisualMetrics.js" as Metrics

Item {
    Component.onCompleted: {
        var small = "https://images.metahub.space/poster/small/tt1375666/img"
        var got = Policy.candidates(small, [])
        ok(got.length === 2, "Metahub emits medium + small")
        ok(got[0] === "https://live.metahub.space/poster/medium/tt1375666/img", "medium first")
        ok(got[1] === "https://live.metahub.space/poster/small/tt1375666/img", "small fallback")
        ok(Policy.candidates("https://covers.example/a.jpg", []).join("|")
           === "https://covers.example/a.jpg", "foreign URL unchanged")
        ok(Metrics.gallery.posterWidth === 148 && Metrics.gallery.posterRadius === 12,
           "approved gallery geometry")
    }
}
```

Also assert large→medium/small, medium→medium/small, duplicate explicit candidates collapse, empty input returns `[]`, and candidate order is stable.

- [ ] **Step 2: Wire a failing `PosterPolicy` runner stage**

Add `Stage-PosterPolicy` to `tests/test_theatre_deep_catalogue.ps1`, invoking the harness and requiring `POSTER_SOURCE_POLICY_OK`.

Run:

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage PosterPolicy
```

Expected: FAIL because both policy modules are absent.

- [ ] **Step 3: Implement exact visual tokens**

Create `.pragma library` data with no derived world behavior:

```javascript
.pragma library

var classic = Object.freeze({
    posterWidth: 132, posterRatio: 1.5, posterRadius: 8,
    cardGap: 18, shelfGap: 26, headerGap: 14,
    titlePixels: 12, titleLines: 1, hoverLift: 4,
    hoverDuration: 160, imageRevealDuration: 160
})
var gallery = Object.freeze({
    posterWidth: 148, posterRatio: 1.5, posterRadius: 12,
    cardGap: 20, shelfGap: 46, headerGap: 18,
    titlePixels: 13, titleLines: 2, titleMinHeight: 35,
    hoverLift: 7,
    hoverDuration: 260, imageRevealDuration: 280
})
```

- [ ] **Step 4: Implement pure candidate normalization**

Use `live.metahub.space`, recognize only `/poster/(small|medium|large)/<id>/img`, return medium then small, and dedupe explicit candidates after normalization. Non-Metahub URLs remain byte-for-byte unchanged.

- [ ] **Step 5: Stop the permanent small rewrite**

In `TheatreApi.normalizeArtUrl`, retain hostname normalization but delete only the medium/large→small replacements. Background URL handling remains unchanged.

- [ ] **Step 6: Run policy and API regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage PosterPolicy
& tests/test_theatre_deep_catalogue.ps1 -Stage ApiRows
```

Expected: both exit 0; the first prints `POSTER_SOURCE_POLICY_OK`.

- [ ] **Step 7: Commit Task 1**

```powershell
git add qml/CatalogueVisualMetrics.js qml/PosterSourcePolicy.js qml/TheatreApi.js tests/poster_source_policy_harness.qml tests/test_theatre_deep_catalogue.ps1
git diff --cached --check
git commit -m "feat(catalogue): add poster source and metric policy"
```

---

### Task 2: Build the bounded rounded poster renderer

**Files:**
- Create: `qml/RoundedPosterImage.qml`
- Create: `tests/rounded_poster_image_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Consumes `sources: string[]`, `radius: real`, and `revealDuration: int`.
- Produces readonly `candidateIndex: int`, `activeSource: url`, `ready: bool`, `exhausted: bool`, `decodeWidth: int`, and `decodeHeight: int`.
- Produces `advanceCandidate() -> bool` for deterministic state testing; production calls it on `Image.Error`.

- [ ] **Step 1: Write the failing image-state harness**

Instantiate a 148×222 image with two fake candidates and `testDevicePixelRatio: 2`. Assert:

```qml
ok(poster.decodeWidth === 296 && poster.decodeHeight === 444, "2x decode cap")
ok(poster.candidateIndex === 0, "first candidate active")
ok(poster.advanceCandidate() === true && poster.candidateIndex === 1, "advance to fallback")
ok(poster.advanceCandidate() === false && poster.exhausted, "exhaust honestly")
ok(poster.placeholderVisible, "placeholder survives exhaustion")
ok(poster.maskPassCount === 1, "one rounded mask pass")
```

Add a `testDevicePixelRatio` property defaulting to `0`; `0` means use `Screen.devicePixelRatio`, and a positive test value supplies deterministic harness scaling.

- [ ] **Step 2: Add and run the failing `RoundedPoster` stage**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage RoundedPoster
```

Expected: FAIL because `RoundedPosterImage.qml` does not exist.

- [ ] **Step 3: Implement fallback and decode state**

The component resets `candidateIndex` whenever `sources` changes, calls `advanceCandidate()` only once per `Image.Error`, and never wraps to candidate zero. Use:

```qml
readonly property real effectiveScale: Math.max(1, Math.min(2,
    testDevicePixelRatio > 0 ? testDevicePixelRatio : Screen.devicePixelRatio))
readonly property int decodeWidth: Math.ceil(width * effectiveScale)
readonly property int decodeHeight: Math.ceil(height * effectiveScale)
```

The source `Image` uses `asynchronous: true`, `cache: true`, `smooth: true`, `mipmap: true`, and binds both `sourceSize` dimensions.

- [ ] **Step 4: Implement one rounded mask and cheap depth**

Use one stable rounded mask item with `layer.enabled: true` as the `MultiEffect.maskSource`. Apply the mask in a single `MultiEffect` pass. Do not enable `blurEnabled` or `shadowEnabled` on `MultiEffect`.

Place two rounded black rectangles behind the masked art:

```qml
Rectangle { x: 0; y: 3; width: root.width; height: root.height; radius: root.radius + 1;
            color: Qt.rgba(0, 0, 0, root.hovered ? 0.42 : 0.28) }
Rectangle { x: -2; y: root.hovered ? 11 : 7; width: root.width + 4; height: root.height;
            radius: root.radius + 3; color: Qt.rgba(0, 0, 0, root.hovered ? 0.20 : 0.10) }
```

Paint the 8%-white resting edge or 2 px soft-gold hover edge above the effect output. The placeholder uses the exact neutral gradient from the spec and remains behind the image.

- [ ] **Step 5: Prove no forbidden render chain entered**

Add static checks to the harness runner or a small source assertion that `RoundedPosterImage.qml` contains one `MultiEffect`, contains no `ShaderEffectSource`, and does not set `shadowEnabled: true` or `blurEnabled: true`.

- [ ] **Step 6: Run the focused harness**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage RoundedPoster
```

Expected: `ROUNDED_POSTER_IMAGE_OK`, exit 0, and no QML import warning for `QtQuick.Effects`.

- [ ] **Step 7: Commit Task 2**

```powershell
git add qml/RoundedPosterImage.qml tests/rounded_poster_image_harness.qml tests/test_theatre_deep_catalogue.ps1
git diff --cached --check
git commit -m "feat(catalogue): render bounded rounded poster art"
```

---

### Task 3: Add the Theatre gallery card profile

**Files:**
- Create: `assets/icons/rating-star.svg`
- Modify: `qml/CataloguePosterCard.qml:1-157`
- Modify: `tests/catalogue_poster_card_harness.qml`

**Interfaces:**
- Adds `visualProfile: string` with accepted values `"classic"` and `"gallery"`; default is `"classic"`.
- Adds `hoverSourceText: string`.
- Consumes `PosterSourcePolicy.candidates(item.cover, item.coverCandidates || [])`.
- Preserves `item`, `keyboardFocused`, `skeleton`, `testHovered`, and `activated(item)`.

- [ ] **Step 1: Expand the card harness before editing the card**

Create one classic card and one gallery card. Assert:

```qml
ok(classicCard.posterWidthToken === 132, "classic unchanged")
ok(galleryCard.posterWidthToken === 148, "gallery width")
ok(galleryCard.posterRadiusToken === 12, "gallery radius")
ok(galleryCard.titleLineCount === 2 && galleryCard.titleReserve === 35, "two-line reserve")
ok(!galleryCard.ratingVisible, "rating hidden at rest")
galleryCard.testHovered = true
ok(galleryCard.ratingVisible && galleryCard.hoverSourceText === "IMDb", "hover metadata")
galleryCard.testHovered = false; galleryCard.keyboardFocused = true
ok(!galleryCard.ratingVisible, "focus does not imitate hover")
ok(!galleryCard.centerPlayVisible, "gallery has no centered play ring")
```

Also assert missing rating produces no empty source label, skeletons never activate, the exact original item is emitted, and `Accessible.name` equals the title.

- [ ] **Step 2: Run the failing card stage**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Cards
```

Expected: FAIL on the gallery-profile properties.

- [ ] **Step 3: Add the house rating SVG**

Create a five-point star SVG filled with the approved gold `#f0c44a`. Render it through a plain QML `Image`; do not add a colorizing effect or font-glyph fallback.

- [ ] **Step 4: Replace the card's raw Image with `RoundedPosterImage`**

Bind candidate URLs, radius, reveal duration, skeleton state, and hover state. Keep the existing test hover seam. Classic keeps its existing dimensions and transition values; gallery selects the approved metrics.

- [ ] **Step 5: Implement the gallery information hierarchy**

For gallery:

- remove the centered play ring;
- use a bottom-only scrim;
- show SVG star + rating at bottom-left and source at bottom-right only on pointer hover;
- reserve exactly two title lines;
- show no permanent subtitle or factual metadata line;
- lift the complete poster surface 7 px in 260 ms;
- scale the image no more than 1.02;
- retain the existing double focus halo without hover metadata.

Set `Accessible.role: Accessible.Button` and `Accessible.name: capText` on the activation surface.

- [ ] **Step 6: Run card and Discover regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Cards
& tests/test_theatre_deep_catalogue.ps1 -Stage DiscoverRegression
```

Expected: both pass; existing Discover remains classic.

- [ ] **Step 7: Commit Task 3**

```powershell
git add assets/icons/rating-star.svg qml/CataloguePosterCard.qml tests/catalogue_poster_card_harness.qml
git diff --cached --check
git commit -m "feat(catalogue): add restrained gallery poster profile"
```

---

### Task 4: Upgrade Theatre rails without changing horizontal physics

**Files:**
- Modify: `qml/PosterRail.qml:10-92`
- Create: `tests/poster_rail_gallery_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Adds `visualProfile: string = "classic"`.
- Adds `initialContentX: real = 0`.
- Adds readonly `currentContentX: real` and signal `horizontalPositionChanged(real x)`.
- Adds keyboard `currentIndex` state while preserving `itemRequested(item)` and `seeAllRequested(pin)`.

- [ ] **Step 1: Write the failing rail harness**

Build a six-item gallery rail. Assert ordinary poster width 148, gap 20, the two-line title area is reserved with no subtitle area, initial content position restores, changing `contentX` emits the new value, Left/Right changes `currentIndex`, and Enter emits the original item.

Add a ranked rail assertion proving rank numerals remain present and its poster still measures 148 px.

- [ ] **Step 2: Run the failing rail stage**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage GalleryRail
```

Expected: FAIL because the rail has no profile/position seam.

- [ ] **Step 3: Parameterize geometry through metrics**

Use gallery width/gap and calculate list height from poster height + the two-line title reserve. Keep `ListView.Horizontal`, `reuseItems: true`, existing cache policy, `StopAtBounds`, and all horizontal gesture physics unchanged. Continue using the existing `WidgetHeader` unchanged so its 22 px Fraunces title and 17 px Fraunces See-all treatment remain house-authentic.

- [ ] **Step 4: Add position restoration**

On component completion, clamp `initialContentX` into the ListView's valid content range and assign it. Emit `horizontalPositionChanged(list.contentX)` from `onContentXChanged`; do not write back through a binding loop.

- [ ] **Step 5: Add keyboard/remote traversal**

Make the rail focusable. Left/Right changes `currentIndex` and calls `positionViewAtIndex(currentIndex, ListView.Contain)`. Enter/Return emits `itemRequested(visibleItems[currentIndex])`. Pass `keyboardFocused` only to the current card while the rail owns keyboard focus.

- [ ] **Step 6: Run focused and row regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage GalleryRail
& tests/test_theatre_deep_catalogue.ps1 -Stage Page
& tests/test_theatre_top10_genre_boxes.ps1
```

Expected: all exit 0.

- [ ] **Step 7: Commit Task 4**

```powershell
git add qml/PosterRail.qml tests/poster_rail_gallery_harness.qml tests/test_theatre_deep_catalogue.ps1
git diff --cached --check
git commit -m "feat(theatre): add gallery rail geometry and focus"
```

---

### Task 5: Build viewport-aware lazy shelf residency

**Files:**
- Create: `qml/LazyPosterShelf.qml`
- Modify: `qml/WorldPage.qml:72-96`
- Create: `tests/lazy_poster_shelf_harness.qml`
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- `WorldPage` produces readonly `viewportContentY` and `viewportHeight`.
- `LazyPosterShelf` consumes `row`, `visualProfile`, `viewportTop`, `viewportHeight`, `editMode`, and row-control callbacks.
- Produces readonly `railLoaded: bool`, `reservedHeight: real`, and `savedContentX: real`.

- [ ] **Step 1: Write the failing lazy-shelf harness**

Drive one shelf with explicit geometry rather than a real wheel:

```qml
shelf.viewportHeight = 600
shelf.y = 1400
shelf.viewportTop = 0
ok(!shelf.railLoaded, "far shelf starts unloaded")
shelf.viewportTop = 850
ok(shelf.railLoaded, "loads inside one-viewport activation margin")
shelf.testSetRailContentX(173)
shelf.viewportTop = 2800
ok(!shelf.railLoaded, "unloads outside two-viewport retention margin")
ok(shelf.height === shelf.reservedHeight, "height remains exact")
shelf.viewportTop = 1200
ok(shelf.railLoaded && Math.abs(shelf.restoredContentX - 173) <= 1,
   "horizontal position restored")
```

Also prove a missing/zero viewport keeps the rail mounted for offscreen harness compatibility and that edit mode does not force far shelves live.

- [ ] **Step 2: Run the failing lazy-shelf stage**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage LazyShelves
```

Expected: FAIL because `LazyPosterShelf.qml` and viewport properties are absent.

- [ ] **Step 3: Expose read-only viewport state**

In `WorldPage`, add only:

```qml
readonly property real viewportContentY: page.contentY
readonly property real viewportHeight: page.height
```

Do not expose a writable Flickable alias and do not edit ScrollGlide.

- [ ] **Step 4: Implement activation hysteresis**

`LazyPosterShelf.evaluateResidency()` uses the shelf's local `y`/height:

```text
activation band = viewport ± 1 × viewportHeight
retention band  = viewport ± 2 × viewportHeight
```

An unloaded shelf enters when it intersects the activation band. A loaded shelf leaves only after it no longer intersects the retention band. Re-evaluate on viewport, y, height, row, and edit-mode changes.

- [ ] **Step 5: Reserve exact geometry and restore position**

The host height is the full row controls (when visible) + spacing + complete gallery rail height. A `Loader.active` binding follows `railLoaded`. The loaded `PosterRail` receives `initialContentX: savedContentX`; every `horizontalPositionChanged` updates the host property.

- [ ] **Step 6: Run the lazy-shelf harness**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage LazyShelves
```

Expected: `LAZY_POSTER_SHELF_OK`, exit 0.

- [ ] **Step 7: Commit Task 5**

```powershell
git add qml/LazyPosterShelf.qml qml/WorldPage.qml tests/lazy_poster_shelf_harness.qml tests/test_theatre_deep_catalogue.ps1
git diff --cached --check
git commit -m "feat(catalogue): mount poster shelves around viewport"
```

---

### Task 6: Compose lazy gallery shelves in Theatre

**Files:**
- Modify: `qml/TheatreCatalogPage.qml:14-225`
- Modify: `qml/TheatreWorld.qml:221-237`
- Modify: `tests/theatre_catalog_page_harness.qml`

**Interfaces:**
- Adds `TheatreCatalogPage.viewportTop: real`, `viewportHeight: real`, and `visualProfile: string`.
- Exposes readonly `liveShelfCount: int` for deterministic harness inspection.
- Preserves every row customization, extension, GenreMosaic, item, genre, and See-all signal.

- [ ] **Step 1: Expand the page harness before composition changes**

Use at least twelve fake main rows plus two extension rows. Set a 600 px viewport and assert:

- both main and extension delegates are `LazyPosterShelf` hosts;
- `liveShelfCount` is greater than zero but less than total shelf count;
- moving `viewportTop` to the bottom changes the live set without increasing it to total rows;
- `implicitHeight` is unchanged before and after moving the viewport;
- Customize mode does not make every rail live;
- extension heading remains before extension shelves and GenreMosaic remains last;
- existing rename/hide/move/reset and generation assertions remain green.

- [ ] **Step 2: Run and observe failure**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Page
```

Expected: FAIL on lazy-host/live-count assertions.

- [ ] **Step 3: Replace both vertical PosterRail repeaters**

Keep row descriptors in the current Repeaters, but make each delegate a lightweight `LazyPosterShelf`. Pass all row metadata, controls, signals, gallery profile, and viewport properties. Do not introduce a nested vertical ListView or second Flickable.

- [ ] **Step 4: Convert world coordinates once**

Give the page an id in `TheatreWorld` and bind:

```qml
viewportTop: Math.max(0, theatre.viewportContentY - y)
viewportHeight: theatre.viewportHeight
visualProfile: "gallery"
```

Because the page and other world widgets share `WorldPage.board`, `y` is the page's board-relative offset. Do not call `mapToItem()` from every shelf on every frame.

- [ ] **Step 5: Match the approved vertical rhythm**

Set `TheatreCatalogPage.spacing` to the gallery shelf gap only where the lazy host does not already reserve it. Ensure the effective shelf-to-shelf distance is exactly 46 px, not host gap plus page gap. Loading skeletons use the gallery width/gap.

- [ ] **Step 6: Run page and behavior regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage Page
& tests/test_theatre_deep_catalogue.ps1 -Stage Preferences
& tests/test_theatre_deep_catalogue.ps1 -Stage Rules
& tests/test_explicit_content_policy.ps1
```

Expected: all pass.

- [ ] **Step 7: Commit Task 6**

```powershell
git add qml/TheatreCatalogPage.qml qml/TheatreWorld.qml tests/theatre_catalog_page_harness.qml
git diff --cached --check
git commit -m "feat(theatre): compose lazy gallery catalogue shelves"
```

---

### Task 7: Apply gallery profile to Theatre See-all and guard shared scope

**Files:**
- Modify: `qml/CataloguePosterGrid.qml:10-82`
- Modify: `qml/TheatreSeeAllPage.qml:130-148`
- Modify: `qml/DiscoverBrowser.qml:20-644`
- Modify: `tests/theatre_see_all_harness.qml`
- Modify: `tests/discover_browser_harness.qml`
- Create: `tests/catalogue_polish_scope_test.mjs`

**Interfaces:**
- Adds `CataloguePosterGrid.visualProfile: string = "classic"` and passes it to cards.
- Adds `DiscoverBrowser.posterVisualProfile: string = "classic"` and passes it to cards.
- Theatre See-all selects `"gallery"`; current Discover wrappers do not yet opt in.

- [ ] **Step 1: Add failing pass-through assertions**

The See-all harness asserts its grid/card profile is gallery. The DiscoverBrowser harness asserts default construction is classic and an injected gallery value reaches its delegate. Add objectName/test aliases only where required; do not expose internal Flickables as writable product API.

- [ ] **Step 2: Write the static scope test**

The Node test reads source files and asserts:

```javascript
assert.match(theatreSeeAll, /visualProfile:\s*"gallery"/)
assert.match(discoverBrowser, /property string posterVisualProfile:\s*"classic"/)
assert.doesNotMatch(discoverPage, /posterVisualProfile:\s*"gallery"/)
assert.doesNotMatch(tankobanDiscover, /posterVisualProfile:\s*"gallery"/)
assert.doesNotMatch(scrollGlide, /CatalogueVisualMetrics|RoundedPosterImage|LazyPosterShelf/)
```

- [ ] **Step 3: Run and observe failure**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage SeeAll
node tests/catalogue_polish_scope_test.mjs
```

Expected: profile pass-through assertions fail.

- [ ] **Step 4: Add profile properties and select Theatre See-all**

Pass the string unchanged to `CataloguePosterCard`; do not duplicate metrics in the grid or browser. Set Theatre See-all to gallery. Keep `DiscoverPage` and `TankobanDiscoverPage` unchanged.

- [ ] **Step 5: Run shared regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage SeeAll
& tests/test_theatre_deep_catalogue.ps1 -Stage DiscoverRegression
node tests/catalogue_polish_scope_test.mjs
```

Expected: all pass.

- [ ] **Step 6: Commit Task 7**

```powershell
git add qml/CataloguePosterGrid.qml qml/TheatreSeeAllPage.qml qml/DiscoverBrowser.qml tests/theatre_see_all_harness.qml tests/discover_browser_harness.qml tests/catalogue_polish_scope_test.mjs
git diff --cached --check
git commit -m "feat(theatre): extend gallery profile to see all"
```

---

### Task 8: Profile and perform the Theatre pilot eyes-on gate

**Files:**
- Create: `tests/catalogue_residency_probe.qml`
- Create: `tests/capture_catalogue_perf.ps1`
- Create: `agents/eyes-on/theatre-catalogue-polish/README.md`
- Create: `agents/eyes-on/theatre-catalogue-polish/` screenshots generated by the probe/run

**Interfaces:**
- Produces repeatable frame/object evidence; no product API.
- This is a required gate before Discover opt-in.

- [ ] **Step 1: Create a deterministic long-page probe**

Render 25 fake rows with stable local poster fixtures or neutral placeholders, a 900 px viewport, and expose console markers for `liveShelfCount`, live card count, top/middle/bottom/returned-top states, and restored rail position.

- [ ] **Step 2: Create the capture script**

The script sets `QSG_RENDER_TIMING=1` and `QT_FORCE_STDERR_LOGGING=1`, launches the probe/app with an explicit output directory, captures logs, and fails when:

- live rail/card counts grow linearly with all 25 rows;
- a reported frame exceeds 100 ms;
- returned-to-top live counts exceed the initial plateau by more than one shelf;
- horizontal restore error exceeds 1 logical px.

- [ ] **Step 3: Run cold and warm profiling**

```powershell
& tests/capture_catalogue_perf.ps1 -Mode Probe
& tests/capture_catalogue_perf.ps1 -Mode App -Page Movies -Pass Cold
& tests/capture_catalogue_perf.ps1 -Mode App -Page Movies -Pass Warm
```

Expected: no sustained warm frames above 16.7 ms, no frame above 100 ms, and bounded object counts.

- [ ] **Step 4: Build and perform the supported-scale eyes-on matrix**

```powershell
cmake --build native/build-msvc --target colosseum
```

Capture Theatre Movies and See-all at Windows 100%, 125%, and 150% scaling. For each scale capture resting, hovered, keyboard-focused, medium-source-success, small-fallback, exhausted-placeholder, and remounted-shelf states.

- [ ] **Step 5: Record the gate**

Write exact commands, log paths, screenshots, frame maxima, live-object plateaus, and PASS/FAIL for every spec runtime gate into the eyes-on README. A failure blocks Task 9; do not compensate by disabling lazy shelves or restoring small-only URLs.

- [ ] **Step 6: Commit Task 8 evidence**

```powershell
git add tests/catalogue_residency_probe.qml tests/capture_catalogue_perf.ps1 agents/eyes-on/theatre-catalogue-polish
git diff --cached --check
git commit -m "test(catalogue): verify poster polish and shelf residency"
```

---

### Task 9: Opt approved Discover worlds into the gallery profile

**Prerequisite:** Task 8 is PASS and Hemanth approves the live Theatre pilot screenshots. If approval is absent, stop after Task 8 with a working Theatre pilot; do not infer approval from passing tests.

**Files:**
- Modify: `qml/DiscoverPage.qml:75-92`
- Modify: `qml/TankobanDiscoverPage.qml:74-110`
- Modify: `tests/discover_browser_harness.qml`
- Modify: `tests/tankoban_discover_page_harness.qml`
- Modify: `tests/catalogue_polish_scope_test.mjs`

**Interfaces:**
- Theatre Discover sets `posterVisualProfile: "gallery"`.
- Tankoban Discover sets `posterVisualProfile: "gallery"` for both Manga and Comics types.
- Biblio remains an explicit consumer of `RoundedPosterImage` in its separate Discover/Explore implementation; this task does not redesign `Bookshelf.qml`.

- [ ] **Step 1: Capture classic baseline screenshots**

Capture Theatre Discover, Tankoban Manga Discover, and Tankoban Comics Discover with the same window geometry used for Task 8.

- [ ] **Step 2: Add failing wrapper assertions**

The Theatre wrapper harness must assert gallery. Tankoban's harness must switch between Manga and Comics and prove gallery remains selected while both resting card variants remain poster-and-title only.

- [ ] **Step 3: Lock title-only resting cards across worlds**

Theatre, Manga, and Comics pass no subtitle into the shared card. Demographic and publisher remain Discover filter/catalogue metadata, not permanent card furniture. Keep the two-line title measure identical when those fields are absent.

- [ ] **Step 4: Opt in the wrappers**

Set only the wrapper property. Do not copy gallery metrics into either wrapper and do not modify adapter paging/filter logic.

- [ ] **Step 5: Run Discover and Tankoban regressions**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage DiscoverRegression
& tests/test_tankoban_discover.ps1
node tests/catalogue_polish_scope_test.mjs
```

Expected: all pass and the scope test now requires both approved wrapper opt-ins.

- [ ] **Step 6: Capture after screenshots and compare**

Record paired before/after images for Theatre, Manga, and Comics. Confirm manga/comic covers are not overcropped and no rating appears at rest.

- [ ] **Step 7: Commit Task 9**

```powershell
git add qml/DiscoverPage.qml qml/TankobanDiscoverPage.qml tests/discover_browser_harness.qml tests/tankoban_discover_page_harness.qml tests/catalogue_polish_scope_test.mjs agents/eyes-on/theatre-catalogue-polish
git diff --cached --check
git commit -m "feat(discover): adopt shared gallery poster profile"
```

---

### Task 10: Full regression, build, and final contract review

**Files:**
- Modify: `tests/test_theatre_deep_catalogue.ps1`

**Interfaces:**
- Produces final acceptance evidence; no new product API.

- [ ] **Step 1: Add all new stages to `-Stage All`**

The runner executes `PosterPolicy`, `RoundedPoster`, `Cards`, `GalleryRail`, `LazyShelves`, `Rules`, `ApiRows`, `SeeAll`, `Preferences`, `Page`, and `DiscoverRegression`, then prints `THEATRE_DEEP_CATALOGUE_OK` only after every stage exits 0.

- [ ] **Step 2: Run focused and adjacent suites**

```powershell
& tests/test_theatre_deep_catalogue.ps1 -Stage All
& tests/test_theatre_top10_genre_boxes.ps1
& tests/test_theatre_anime_parity.ps1
& tests/test_theatre_search_p0.ps1
& tests/test_explicit_content_policy.ps1
& tests/test_content_preferences.ps1
& tests/test_scroll_glide_p0.ps1
& tests/test_tankoban_discover.ps1
node tests/catalogue_polish_scope_test.mjs
node tests/extension_world_isolation_test.mjs
node tests/extension_reorder_world_test.mjs
```

Expected: all exit 0. The scroll suite proves this arc did not regress or rewrite the separate controller.

- [ ] **Step 3: Build the application**

```powershell
cmake --build native/build-msvc --target colosseum
```

Expected: successful build with no new QML import, binding-loop, shader, or image warnings.

- [ ] **Step 4: Perform final live checks**

Verify:

- Theatre landing still owns the existing hero and Continue rows;
- Theatre catalogue rows and See-all use gallery geometry;
- no centered play ring or Unicode star remains in gallery cards;
- rating/source are invisible at rest and pointer-hover-only;
- keyboard focus is visible and activates the correct item;
- medium poster success is sharp and medium failure falls back to small;
- scrolling a long page keeps live shelves bounded;
- returning restores rail position;
- Customize rows, extensions, GenreMosaic, See-all, and explicit-content behavior remain correct;
- approved Discover wrappers use gallery only after Task 9's gate;
- Biblio's current home shelf is unchanged.
- Cold Ripple, the wallpaper vignette, `TopBar`, `TheatreTabBar`, Fraunces shelf headers, house gold, WorldPage margins, Customize rows, Genre mosaic, and Top 10 numerals remain visually and structurally unchanged.

- [ ] **Step 5: Inspect scope before the final commit**

```powershell
git status --short
git diff --check
git diff --stat HEAD~10..HEAD
```

Confirm no branch/worktree was created, no scroll-controller file entered a catalogue-polish commit, and unrelated pre-existing changes remain unstaged.

- [ ] **Step 6: Commit final runner/documentation updates**

```powershell
git add tests/test_theatre_deep_catalogue.ps1
git diff --cached --check
git commit -m "test(catalogue): lock poster and shelf polish contract"
```

---

## Definition of Done

- [ ] All 15 acceptance criteria in the approved spec have a passing automated or named eyes-on check.
- [ ] Theatre rails and Theatre See-all use the gallery profile.
- [ ] Medium Metahub art falls back to small per card without blanking long-tail titles.
- [ ] Genuine rounded masking, bounded decode sizing, inset edge, and cheap depth pass at 100%, 125%, and 150% scaling.
- [ ] Rating/source remain pointer-hover-only and keyboard focus remains independently visible.
- [ ] Main and extension shelves share lazy residency with stable height and restored horizontal position.
- [ ] Live shelf/card counts plateau instead of growing with total row count.
- [ ] Existing catalogue data, row customization, extensions, explicit policy, See-all, and scroll behavior remain green.
- [ ] Theatre Discover and Tankoban/Comics opt in only after the live Theatre gate.
- [ ] Biblio receives a reusable image/profile interface without an unapproved redesign of its current home shelf.
- [ ] The Colosseum shell and house aesthetic remain intact; only poster rendering, card spacing/motion, and shelf residency change.
- [ ] All work and scoped commits occur directly on `master`; no branch or worktree is created.
- [ ] Only task-owned files are staged; unrelated dirty-worktree changes remain untouched.

## Recommended execution method

Use **inline execution with `superpowers:executing-plans`** on the existing master checkout. Tasks 2–7 repeatedly touch the same QML card, rail, page, and harness runner; one continuous implementer minimizes binding-contract drift and shared-file collisions. Stop for review after Tasks 3, 6, and 8. Do not create a branch or worktree.
