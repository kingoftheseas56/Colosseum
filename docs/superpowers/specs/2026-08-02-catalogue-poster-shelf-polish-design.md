# Catalogue Poster and Shelf Polish Design

**Status:** Revised visual direction approved 2026-08-02

**Scope owner:** Theatre pilot with app-wide reusable foundations

**Approved visual reference:** `docs/superpowers/specs/assets/2026-08-02-theatre-catalogue-polish-mock.html`

## 1. Outcome

Colosseum's catalogue surfaces will feel sharper, calmer, and more expensive without becoming decorative or Harbor-branded. Theatre is the first live adopter. The work creates reusable poster, spacing, and lazy-shelf primitives that Tankoban, Comics, and Biblio can opt into after the Theatre eyes-on gate. It is an in-place refinement of Colosseum, not a replacement shell.

This design covers three concerns:

1. poster fidelity and card finish;
2. bounded shelf and image residency;
3. a shared catalogue visual rhythm.

The vertical scroll-speed rewrite is a separate arc and is explicitly out of scope. This work must consume the finished shared scroll controller without editing its physics.

## 2. Problem

The current shared catalogue card makes good artwork look cheaper than the same title in Harbor:

- Theatre rewrites Metahub medium and large poster URLs to `small`, even when a sharper file exists;
- the visible `Image` is not genuinely masked to the frame's rounded corners and can paint over the intended border;
- cards are small and tightly packed, titles have only one line, and loaded art has almost no resting depth;
- a large centered hover control and font glyphs compete with the artwork;
- every Theatre shelf is created by a vertical `Repeater`, so shelves far below the viewport retain horizontal views, delegates, and images;
- the shared poster image has no decode-size cap, allowing large remote images to consume unnecessary texture memory.

The desired result is not a new layout. It is the existing Colosseum catalogue, executed with stable geometry, better source selection, bounded image work, precise spacing, and quiet interaction feedback.

## 3. Design principles

### 3.1 Artwork is the surface

The poster remains visually dominant. Chrome stays quiet. There are no glass cards, permanent rating badges, descriptive blurbs, second hero, award decoration, or service-logo overlays.

The surrounding Colosseum shell is immutable in this arc: the persistent Cold Ripple wallpaper and vignette, `WorldPage`, pinned `TopBar`, clock/date composition, glass medium capsule, Theatre's gold medium pill, `TheatreTabBar`, Fraunces display hierarchy, Segoe UI body hierarchy, `#f0c44a` house gold, page margins, existing catalogue ordering, Customize control, Genre mosaic, and oversized Top 10 numerals all remain recognizable and structurally unchanged.

### 3.2 Metadata waits for intent

At rest, a Theatre card shows only its poster and two-line title area. It does not add a year, genre, source, rating, badge, or subtitle line. IMDb rating and rating attribution appear only under pointer hover. Keyboard focus receives a visible focus treatment but does not impersonate pointer hover or expose the hover metadata.

### 3.3 Gold is an interaction accent

Gold appears in the hover/focus edge and rating star only. It is not used as a filled card background, broad glow, or permanent outline.

### 3.4 Geometry never moves during loading

Poster, title, and shelf heights are reserved before artwork or shelf delegates arrive. A successful load, failed source, shelf mount, or shelf unload must not change vertical scroll position.

### 3.5 Performance is part of the appearance

The implementation must bound decoded image dimensions and the number of live shelves. Per-card effects must remain a single lightweight mask pass; blurred GPU shadows and independent render targets per card are prohibited.

## 4. Visual contract

### 4.1 Theatre gallery profile

| Token | Approved value |
|---|---:|
| Ordinary poster width | 148 logical px |
| Poster aspect | 2:3 |
| Poster corner radius | 12 px |
| Horizontal card gap | 20 px |
| Shelf-to-shelf gap | 46 px |
| Header-to-poster gap | 18 px |
| Shelf header | existing `WidgetHeader`: Fraunces, 22 px |
| See-all affordance | existing `WidgetHeader`: Fraunces, 17 px |
| Title top gap | 10 px |
| Title type | Segoe UI, 13 px, DemiBold |
| Title measure | exactly two reserved lines, 35 px minimum |
| Hover lift | 7 px |
| Hover transition | 260 ms, restrained cubic-out |
| Image reveal | 280 ms |
| Resting edge | 1 px white at 8% opacity |
| Hover edge | 2 px soft gold |

Top 10 retains its oversized rank numerals, but its poster uses the same gallery profile. Ranked-cell width may grow to accommodate the numeral; poster width remains 148 px.

### 4.2 Card states

**Skeleton:** Exact final geometry. Use a deterministic dark gradient derived from stable item identity when identity exists; use the fixed neutral dark gradient `#191b21 → #101218 → #17171b` for anonymous loading skeletons. No title is invented.

**Loading:** The placeholder remains visible. The image decodes asynchronously and fades in only after `Image.Ready`.

**Ready/resting:** Crisp art, genuine 12 px crop, faint top/inset edge, two cheap offset shadow plates, and title only.

**Hovered:** The card rises 7 px, the resting shadow deepens, a restrained gold inset edge appears, and a bottom scrim reveals `★ rating` and `IMDb`. The artwork may scale no more than 1.02. There is no centered play ring.

**Keyboard focused:** A clear double soft-gold focus halo appears outside the poster. The card does not lift and hover-only metadata remains hidden.

**Source failed:** The card automatically advances to the next candidate. Exhausting all candidates leaves the stable placeholder; it never shows a broken-image icon or an empty transparent hole.

### 4.3 Icon policy

The card must not use Unicode play, star, or chevron glyphs as interface icons. The approved hover treatment requires no play icon. The rating star uses a house SVG asset. Existing unrelated glyph cleanup is out of scope.

## 5. Poster source and texture policy

### 5.1 Candidate construction

Source selection is centralized in `PosterSourcePolicy.js`, not scattered through cards or world APIs.

For a Metahub poster URL, the ordered candidates are:

1. the canonical `live.metahub.space` medium URL;
2. the canonical small URL.

If the supplied URL is large, medium is still tried first because it provides sufficient headroom without retaining the largest texture. Duplicate candidate URLs are removed. For non-Metahub art, the supplied URL is the only candidate unless the provider already supplies an explicit candidate list.

`TheatreApi.normalizeArtUrl()` continues to normalize the Metahub hostname but stops permanently rewriting the size to small. A medium failure is local to that card and falls back to small; it never blanks the long tail.

### 5.2 Decode dimensions

The displayed `Image` requests a decode size derived from rendered geometry:

```text
decode scale = clamp(Screen.devicePixelRatio, 1.0, 2.0)
decode width = ceil(rendered poster width × decode scale)
decode height = ceil(rendered poster height × decode scale)
```

For the 148×222 Theatre card, the maximum normal decode is 296×444. `smooth` is enabled. `mipmap` is enabled only for the bounded decoded image, not an unbounded original. `asynchronous` and `cache` remain enabled.

The policy controls decoded texture size, not network-file truth. It must not rewrite arbitrary provider URLs or claim a source resolution it cannot verify.

## 6. Component architecture

### 6.1 `CatalogueVisualMetrics.js`

A `.pragma library` token module contains the approved poster geometry and timing values. It has no world data and performs no I/O. It does not replace `Theme`, `WidgetHeader`, `TopBar`, `TheatreTabBar`, or any shell typography token. Consumers may override poster aspect ratio, but Theatre's gallery profile uses the approved values without local copies.

### 6.2 `PosterSourcePolicy.js`

This pure module exposes candidate normalization and deduplication. It knows URL mechanics only; it does not know catalogue ranking, title metadata, or QML state.

### 6.3 `RoundedPosterImage.qml`

This component owns:

- the placeholder;
- candidate advancement;
- bounded decode size;
- the ready fade;
- the genuine rounded mask;
- the inset edge;
- two cheap shadow plates.

The rounded crop uses one `QtQuick.Effects.MultiEffect` mask pass. Shadow blur inside `MultiEffect`, `ShaderEffectSource`, and `layer.enabled` shadow chains are prohibited. The mask source is stable and contains no animation. The component exposes observable `candidateIndex`, `activeSource`, `ready`, and `exhausted` properties for harnesses.

### 6.4 `CataloguePosterCard.qml`

The card remains the activation, title, hover, focus, and metadata owner. It delegates artwork rendering to `RoundedPosterImage` and exposes a `visualProfile` property:

- `"classic"`: current geometry for consumers not yet approved;
- `"gallery"`: the approved polish profile.

Theatre rails and Theatre See-all opt into `gallery` during the pilot. Discover surfaces remain `classic` until their rollout task passes eyes-on. This prevents a shared-file change from silently restyling every world.

The card accepts optional `hoverSourceText`; Theatre supplies `IMDb`. Resting metadata remains the responsibility of a world-specific outer card and is not added by Theatre's gallery profile.

### 6.5 `LazyPosterShelf.qml`

This lightweight host reserves one complete shelf's height and conditionally creates its `PosterRail` through a `Loader`.

It accepts:

- the row descriptor and edit-state inputs currently passed by `TheatreCatalogPage`;
- `viewportTop` and `viewportHeight` in TheatreCatalogPage-local coordinates;
- activation and retention margins;
- the saved horizontal position.

Activation uses a one-viewport margin. An already-live shelf remains mounted until it leaves a two-viewport retention margin. This hysteresis prevents repeated create/destroy cycles near the threshold.

The host always reserves the final shelf height. When inactive it shows either empty reserved space or the exact skeleton state when the row is approaching but not ready. It stores the rail's latest horizontal `contentX` before unload and restores it when remounted.

### 6.6 Viewport seam

`WorldPage` exposes read-only `viewportContentY` and `viewportHeight` properties backed by its existing page Flickable. It does not expose a second vertical scroller and does not change the scroll controller.

`TheatreWorld` converts the shared board coordinate into the `TheatreCatalogPage` coordinate and passes the viewport values into the catalogue page. `TheatreCatalogPage` uses `LazyPosterShelf` for both main and extension rows. Genre mosaic and row customization remain in their current order and retain existing behavior.

## 7. Shelf residency contract

- A page may contain any number of row descriptors, but live `PosterRail` instances are bounded by the viewport and retention window.
- Main and extension rows use the same lazy host.
- Horizontal `ListView.reuseItems` remains enabled.
- A hidden row visible only in Customize mode still reserves and mounts through the same policy.
- Entering Customize mode must not eagerly instantiate every poster rail.
- Reordering, hiding, renaming, or resetting rows must recompute geometry without losing the current vertical position beyond ordinary layout movement caused by the user's explicit reorder.
- Returning to a previously unloaded shelf restores its horizontal position to within 1 logical pixel.
- The Genre mosaic remains last and does not mount poster shelves.

## 8. World rollout

### 8.1 Theatre pilot

Theatre catalogue rails and Theatre See-all use the gallery profile. Theatre Discover stays classic during the first eyes-on comparison so the pilot can be evaluated against the current shared card on the same machine.

### 8.2 Theatre Discover

After the pilot passes visual and performance gates, `DiscoverPage` sets the gallery profile on its `DiscoverBrowser`. No data, filter, rating-visibility, or scrolling behavior changes.

### 8.3 Tankoban and Comics

`TankobanDiscoverPage` opts its shared `DiscoverBrowser` into the gallery profile after a separate screenshot check with manga and comic covers. The artwork component is reused, while resting cards remain poster-and-title only. Demographic and publisher filtering stay in the Discover controls rather than becoming permanent card furniture.

### 8.4 Biblio

Biblio adopts `RoundedPosterImage` and the shared edge/motion tokens when its approved Discover/Explore implementation lands. It must override the aspect policy for uncropped book covers; its already-approved canonical book card continues to own title and author outside the image primitive. Biblio adoption is a named integration task, not an automatic consequence of changing Theatre.

## 9. Performance and verification gates

### 9.1 Deterministic harnesses

Tests must prove:

- Metahub medium precedes small and duplicates are removed;
- non-Metahub URLs are not rewritten;
- a failed candidate advances and exhaustion retains the placeholder;
- decode dimensions cap at 2× rendered size;
- gallery geometry and hover-only rating rules remain exact;
- keyboard focus does not reveal rating metadata;
- lazy shelves activate at one viewport, retain to two, then unload;
- inactive shelves keep exact height;
- horizontal position survives unload/remount;
- main and extension rows use the lazy host;
- classic-profile Discover contracts remain green during the Theatre pilot.

### 9.2 Runtime gates

Use the same populated Theatre Movies page for every comparison.

- Warm vertical scrolling at 60 Hz must have no sustained sequence of frames above 16.7 ms.
- Cold artwork arrival may miss one frame but must not produce a stall above 100 ms.
- The count of live `PosterRail` and `CataloguePosterCard` objects must plateau as the user moves down a long page; it must not grow in proportion to total row count.
- Returning to a shelf must restore horizontal position and must not expose a blank band for more than one frame after entering the activation window.
- At 100%, 125%, and 150% Windows scaling, poster edges remain crisp and genuinely rounded.
- The gallery profile must not increase steady-state GPU memory without bound. Compare top, bottom, and returned-to-top states after a five-second idle.

If the single-pass rounded mask causes sustained frame-budget failure, the task is not accepted. The implementation must be profiled and corrected rather than disabling lazy residency or shipping blurred corners.

## 10. Accessibility and interaction

- Existing pointer activation remains unchanged.
- Existing grid keyboard activation and focus rings remain unchanged in behavior.
- Theatre rails gain keyboard/remote navigation as part of gallery adoption: Left/Right moves between cards, Enter activates, and focus follows the current item.
- Focused cards remain scrolled into view horizontally.
- Every card exposes an accessible button role and accessible name equal to its title.
- Hover-only IMDb metadata is not treated as required information for activation.
- Reduced-motion mode, when present globally, sets lift and image-scale animation duration to zero while preserving state changes and focus visibility.

## 11. Error handling

- One failed poster candidate never fails the shelf.
- Exhausted artwork leaves the stable placeholder and title.
- An invalid or empty cover produces one empty candidate set and performs no retry loop.
- Loader destruction cancels the delegate tree naturally; stale `Image.status` changes cannot mutate a remounted shelf host.
- A missing viewport host falls back to mounted shelves in offscreen harnesses, preventing false blank pages.
- Lazy mounting does not alter catalogue error, empty, extension-missing, or See-all states.

## 12. Non-goals

- No vertical scroll-physics changes.
- No horizontal rail physics redesign beyond restoring position and keyboard focus.
- No new hero, Continue Watching row, awards row, recommendation copy, or catalogue category.
- No catalogue ranking or data-source changes beyond poster URL candidates.
- No Theme singleton migration.
- No global icon cleanup outside the touched poster card.
- No account-based metadata or new API key.
- No automatic redesign of Biblio's current home shelf.

## 13. Acceptance criteria

1. Theatre rails and See-all render the approved gallery profile.
2. Medium Metahub art is attempted first and small remains a reliable fallback.
3. Loaded artwork has a genuine 12 px rounded crop and cannot cover the inset edge.
4. IMDb rating and attribution remain pointer-hover-only.
5. No centered play ring or Unicode poster-control glyph remains.
6. Poster decode dimensions are bounded to at most 2× rendered geometry.
7. Theatre main and extension shelves mount lazily with stable reserved height.
8. Horizontal rail position survives unload/remount.
9. Live shelf/card counts plateau on a long page.
10. Existing row customization, See-all, explicit-content, and extension placement behavior remains intact.
11. Theatre Discover remains unchanged until the Theatre pilot passes.
12. Tankoban/Comics and Biblio adoption remain explicit opt-in integration steps.
13. Focus, activation, and accessible naming pass keyboard/remote checks.
14. The focused and adjacent regression suite, native build, QML profiling, and eyes-on matrix pass on master.
15. Cold Ripple, the wallpaper vignette, `TopBar`, `TheatreTabBar`, Fraunces headers, house gold, WorldPage margins, Customize rows, Genre mosaic, and Top 10 numerals remain recognizably Colosseum and are not restyled by this arc.
