# Comic Reader — Design

Date: 2026-07-23
Owner: Agent 1 (Comics)
Decided with: Hemanth, via Brotherhood granular brainstorm (visual companion session
`.superpowers/brainstorm/1784-1784801790/`)
Approved mockup: `docs/superpowers/mocks/2026-07-23-comicreader-visual-identity.html`
(open in a browser — all four surfaces were eyes-on approved 2026-07-23)

## Decision

Build **Comic Reader**: a brand-new manga/comic/Tankoban reader for Colosseum, from scratch —
the Book Reader 2 pattern. A native C++ Qt engine with a new modular QML chrome replaces the
current `qml/MangaReader.qml`, which was shaped around WeebCentral's HTML reading experience
rather than built as an advanced native reader. Codex's TB2 migration plan
(`2026-07-23-tankoban2-reader-migration-design.md`) is **superseded** by this design; its
engine insights are inherited freely, its parity-port constraints are not.

Comic Reader is not built from a blank page. Four lineage readers govern behavior and feel:

| Reader | Where | What it contributes |
|---|---|---|
| Tankoban-Max | `~/Desktop/Tankoban-Max` | The family's UX truth: smooth-wheel feel, loupe, HUD manners |
| TankobanQTGroundWork | `~/Desktop/TankobanQTGroundWork-main/app_qt/ui/readers/comic_reader.py` | Auto-coupling probe, navigator, end card, gutter shadow, go-to-page, keys help, memory saver |
| Tankoban 2 | `~/Desktop/Tankoban 2/src/ui/readers/` | The proven native engine: canonical pairing, PageCache policies, DecodeTask, strip canvas, SmoothScrollArea |
| Colosseum current | `qml/MangaReader.qml` | The app integration: download-fed entries, Progress, acquisition boundary, chapters/issues/Tankoban volumes |

Outside references (UX only): Mihon, OpenComic, YACReader at `~/Desktop/Tankoban reference/`.

## Scope rulings (Hemanth, 2026-07-23 — the decision ledger)

1. **Two modes only: Long Strip and Double Page.**
2. **MangaPlus is absorbed, not cut.** Double Page has two souls: manga opens **RTL with RTL
   image pairing** (the MangaPlus lineage as the manga default); comics open **LTR with LTR
   pairing**. One mode, direction-aware identity.
3. **Direction = smart default + toggle.** Kind decides the default (manga→RTL,
   western→LTR); the toggle stays for flipped rips; the override persists per series.
4. **Auto-coupling by default.** The engine compares artwork continuity across the gutter of
   candidate pairs, scores both phases, and picks the alignment with a confidence value
   (QTGroundWork's probe). P-key nudge overrides to manual; the resolved state persists per
   entry and is never re-probed once resolved.
5. **Comfort kit = family union minus image effects and scale quality.** Ships: loupe,
   gutter shadow (Off/Subtle/Medium/Strong), bookmarks, go-to-page, shortcuts overlay,
   searchable chapter/volume navigator with per-entry progress, memory saver, night veil,
   double-page zoom 100–260% + pan. Cut: brightness/contrast/invert/grayscale/sepia and
   smooth/sharp/pixel scale quality.
6. **Thumbnails = summonable overlay** (grid for visual jumping), never permanent chrome.
7. **End of entry = end card**, and the card **is** the acquisition boundary: when the next
   entry isn't downloaded, its primary action becomes *Get: <title>*.
8. **Chrome = Family Gradient HUD** (mockup surface 01). **Settings = glass side sheet**
   (surface 02).
9. **Guided mode is deferred entirely.** Not part of Comic Reader. `native/guided/` and
   `qml/guided/` stay untouched on disk; no mode slot, no rebase contract in this arc. Its
   future is its own plan.

## Visual identity (approved mockup is the contract)

Lineage layout, player soul — the live player (Player 1, `qml/PlayerPage.qml` +
`qml/Theme.qml`) is the styling source:

- **Tokens:** gold `#f0c44a` — sparing, only progress/active/focus ("reading now", scrub
  fill, active chips, end-card ring). Ink ladder `#f7f7f5` / `#c9c8d0` / `#9a99a5`. Glass
  `rgba(white, .10–.18)` edges on near-black sheets. Danger `#ff8a8a`.
- **Type:** Segoe UI for all HUD/chrome (the player's approved parity face). Letter-spaced
  uppercase section labels (1.2–2.2px tracking). **Fraunces appears in exactly one place:**
  the end-card volume title.
- **Icons:** vendored Lucide (white stroke, `stroke-width:2`) via the existing PlayerIcon
  pattern; `assets/icons/lucide/` + `assets/icons/comicreader/`. No text-glyph chips.

### Surface 01 — the reader

Page owns the screen; chrome is a visitor. On mouse move, a bottom gradient rises:
- **Gold scrub thread**: track with gold fill to the current position, gold knob, bookmark
  tick marks on the track, page-number bubble above the knob on hover/drag.
- HUD row: prev/next pills · live counter (`45–46 / 230`, pair-aware) · Chapters pill ·
  thumbnails pill · two-segment mode chip (Double / Strip, active in gold) · direction pill
  (RTL/LTR; gold when active) · settings pill.
- Back pill (`‹ Library`) top-left; window verbs (minimize / fullscreen / close) top-right —
  the existing reader-session window signals, restyled.
- Thin side scroller on the right edge (drag = position), Max lineage.
- Auto-hide after 3s of stillness; single click toggles chrome; double click toggles
  fullscreen (220ms disambiguation, QTGroundWork's timing); H toggles HUD.
- Double Page renders the pair with the **gutter shadow** breathing down the spine.

### Surface 02 — settings (glass side sheet)

Slides from the right over a dimmed page. Sections in letter-spaced labels:
- **DISPLAY:** Mode (Double/Strip) · Direction (RTL·manga / LTR) · Night veil (Off/Low/High)
- **DOUBLE PAGE** (hidden in Strip): Coupling (Auto/Nudge) · Gutter shadow (4 presets) · Zoom
- **LONG STRIP** (hidden in Double): Portrait width presets · page gap · back-to-top
- **TOOLS:** Loupe · Bookmarks · Thumbnails · Shortcuts (2×2 tool grid) · Memory saver toggle
- Danger row: Clear resume · Reset series (danger red).
Opens from the settings pill or right-click. Spread override (mark page spread/normal,
cover always single) stays a direct right-click item on the page.

### Surface 03 — navigator

Summoned by the Chapters pill or O. Search box filters live. Rows: number · title · state:
- current entry burns gold ("reading · p.46/230") with a gold progress thread;
- read entries rest quiet ("read", full dim bar); partially-read show their position;
- **undownloaded entries show an inline `get` action** — download-fed law on one surface.

### Surface 04 — end card

Rises over the dimmed final page: gold completion ring · eyebrow (`VOLUME 46 · COMPLETE`) ·
title in Fraunces · quiet metadata · primary gold action **Next: <title>** (or
**Get: <title>** when not downloaded) · Replay from start · Back to library · keyboard hints
(Space next · Backspace library · Esc dismiss).

## Product contract (what must keep working)

- **Callers unchanged:** `MangaReader.qml`'s public properties and signals are preserved
  byte-for-byte at cutover (backdrop, seriesTitle, seriesId, seriesCover, chapters,
  chapterId, chapterLabel, western, pageStore, entryKind, entryLabelPrefix;
  backRequested / minimizeRequested / fullscreenRequested / closeRequested /
  sourceRequested(entryId)).
- **One reader, three lanes:** manga chapters, western comic issues, Tankoban volumes.
- **Download-fed, never streamed:** the engine accepts only local page files from the
  injected `pageStore`; unavailable entries route to acquisition (navigator `get`, end-card
  `Get`), never silent streaming.
- **Progress:** existing `Progress` records and namespaces (manga/comic/tankoban), resume
  position (page + strip fraction), Continue rows unchanged.
- **Persistence:** per-series settings (mode, direction override, portrait width, gutter,
  night veil), per-entry bookmarks, spread overrides, coupling state, resume. Existing
  persisted keys migrate on first open; nothing silently resets.

## Engine (native C++ — QML paints, C++ decides)

New `native/comicreader/` modules, exposed to QML as one backend + image provider:

- **Generation-safe decode:** opening an entry increments a generation; every decode result
  carries it; stale generations are discarded — a rapid A→B entry switch can never paint A's
  page in B. Bounded worker pool (2 decode threads), request coalescing, priorities:
  visible page first, neighbors next, strip window descending.
- **Pinned LRU page cache:** 512 MiB budget, 256 MiB with Memory saver; visible/pinned pages
  never evict; all-pinned pressure may exceed budget rather than blank the visible frame.
- **Canonical pairing** (TB2's law + QTGroundWork's probe): cover rides alone; a confirmed
  spread is one full-width unit and consumes a parity slot; manual override beats detection;
  nudge shifts parity but never pairs across a spread; pairing rebuilds don't move the page
  under the reader — the visible page snaps to its containing unit. Auto-coupling: decode
  the first candidate pairs, score inner-edge luminance continuity for both phases, adopt
  the confident winner, persist, never re-probe.
- **Strip geometry model:** stable estimated heights before decode, exact after; viewport
  window ±1.5 screens drives decode priority and eviction; scroll position is compensated
  when a page above the viewport changes height (no jumps); far pages evict without
  geometry collapse.
- **Smooth wheel:** Max's measured feel — float accumulator, ~100px per notch intake,
  0.38 drain per 16ms tick, bounded backlog (ported from the family, byte-honest).
- **Image provider:** read-only `image://comicreader/...` URLs keyed by generation + page +
  width; provider never mutates reader state.
- **Typed page failures, isolated:** `missing_file` / `decode_failed` /
  `unsupported_image` render as a quiet in-flow error placard on that page only; the entry
  keeps reading; the end card and crossing still work.

## Explicitly excluded

- Guided mode (deferred whole; source untouched).
- Image effects, scale quality (cut by ruling).
- Single Page, MangaPlus-as-separate-mode, Two-Page Scroll, TB2 split-wide.
- Archive parsing in the reader (Colosseum supplies extracted local files).
- Qt Widgets, any library/downloader/torrent behavior changes.

## Migration and rollback

Comic Reader is built beside production (`qml/comicreader/`, `native/comicreader/`), harness-first.
Production callers stay on the old reader until the acceptance gate passes; the cutover is
one commit that turns `MangaReader.qml` into a thin wrapper over the Comic Reader shell. No
legacy copy is kept in production — git history is the rollback. The old Guided integration
test becomes an honest freeze gate (Guided intentionally absent; sources intact).

## Acceptance

- All four approved mockup surfaces exist in the app and match the mockup's language
  (Hemanth eyes-on).
- Manga chapter (RTL double + strip), western issue (LTR double + strip), and a Tankoban
  volume (incl. one stitched-spread volume) each pass an eyes-on smoke: open, resume, rapid
  navigation, scrub + bubble + ticks, navigator with `get`, thumbnails overlay, settings
  sheet, spread override, end card (both downloaded-next and get-next states), fullscreen,
  close/reopen.
- Auto-coupling picks the correct phase on a known misaligned volume, and P overrides it.
- No stale page ever renders after rapid entry switching; strip stays smooth during decode;
  cache respects its budget (512/256) unless everything visible is pinned.
- Existing manga/comics/Tankoban regression suites stay green; Progress payloads unchanged.
- Guided sources untouched (`git diff --stat` clean over `native/guided/`, `qml/guided/`).
