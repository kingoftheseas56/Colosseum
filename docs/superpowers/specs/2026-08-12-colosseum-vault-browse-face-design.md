# Colosseum Vault — Browse Face Design

**Status:** locked brainstorm, awaiting Hemanth's review of this specification.
**Workflow:** `brotherhood-frontend-design` (Phase 1).
**Parent:** `2026-08-12-colosseum-vault-complete-locked-design.md` (`a0c735e`).
**Reference:** Jellyfin — `jellyfin-web` @ `~/Downloads/jellyfin-web`, read directly.

---

## 1. What this document is

The parent design locked what Vault *knows* and *does* across six sections and 6,437 lines. It
deliberately left one thing open, in §4.1:

> Vault should expose **Browse** and **Manage** as first-class destinations or modes. The exact
> visual navigation may change during the UI overhaul.

This document closes that opening for the **Browse** face only. It refines; it does not supersede.
Where this document is silent, the parent governs. Where they appear to conflict, the parent wins
and this document is wrong.

Manage's six areas are out of scope beyond one locked constraint: Manage is plain and text-led, and
receives none of the visual treatment specified here.

---

## 2. Experience promise

> **Browse is where you look at what you physically own, and it never pretends to know more than it
> does.**

Theatre, Tankoban and Biblio answer *what is this media*. Browse answers *what do I actually have,
where is it, and is it real*. It is a browsing surface, not a catalogue — the difference is that
every tile is backed by a file on a drive that may or may not be plugged in right now.

The parent's key rule governs every decision below:

> The file tells Vault what you physically have. The canonical world tells Colosseum what the media
> is. Your explicit decisions outrank automation.

---

## 3. Decision ledger of record

Twelve decisions, ratified by Hemanth 2026-08-12.

| # | Decision |
|---|---|
| 1 | Jellyfin's structural influence applies to Browse only. Manage stays plain and text-led. |
| 2 | Tiles are **folder-true and media-faced**: folder structure is honest, tile faces show identified media. |
| 3 | ~~The Vault door carries the health answer on arrival.~~ **Superseded for Browse 2026-08-12** (§4.10): no health banner; the rail and tiles report state, the full answer stays in Manage's Overview. Browse instead opens with the featured carousel. |
| 4 | A folder containing one film presents as that film. Companions fold into it. |
| 5 | Unidentified tiles **resolve in place**, visibly, as identification completes. |
| 6 | Items on an away drive keep their tiles in position, marked away. |
| 7 | A tile face carries artwork, title, and one physical fact — quality, copy count, or away state. |
| 8 | A series is one show tile; seasons and episodes are reached by drilling in. |
| 9 | Identification uncertainty is shown on the tile and repairable in place. |
| 10 | Navigation is a rail of roots, a grid beside it, and a breadcrumb for depth. The rail is **collapsible and collapsed by default** (amended 2026-08-12): collapsed it shows each root as a glyph with its availability dot, expanded it adds names and counts. Expanding never reveals *state* that was hidden — only detail. |
| 11 | Opening a film gives physical truth — copies, drives, evidence. Play hands off to the player. |
| 12 | The mock covers the Browse face end to end, including its failure states. |

**Claude's ordinary calls**, recorded for completeness: folders sort before films, alphabetically,
with recency available; companion files never receive their own tiles; one film held in two folders
remains two tiles in Browse while Manage groups it as one ownership.

**Originated by Claude and ratified:** #3 and #5.

---

## 4. Elements and their interactions

This is the section the surface will be judged on. Each element is defined by what it does to the
others, not by how it looks alone.

### 4.1 The elements

| Element | What it is |
|---|---|
| **Featured carousel** | Colosseum's `FeaturedCarousel`, at the head of Browse. Features what recently **arrived** — truth from the disk, not curation. Keeps Vault reading as Colosseum. |
| **Door** | The taskbar entry into Vault. See §5 note on the health statement. |
| **Root rail** | Collapsible list of configured storage roots with availability. Collapsed by default; availability survives the collapse. |
| **Breadcrumb** | Current position in the folder path. |
| **Grid** | The tiles for the current folder. |
| **Tile** | One physical thing — a film, a series, a folder, or an unresolved file. |
| **Detail sheet** | What a tile opens into: copies, drives, evidence. |
| **Repair affordance** | The identify dialog, reachable from an uncertain tile. |

### 4.2 Coexistence

A single folder may simultaneously contain identified films, a series, subfolders, files still
resolving, files Vault is unsure about, companion files, and non-media files.

- Identified films, series, subfolders, resolving files, uncertain files and **local-only** items
  all appear as tiles. Local-only is a distinct state from uncertain: Vault is *certain* the item is
  yours and *certain* no catalogue describes it (parent §2.11). It is never marked as a problem.
- Companion files (subtitles, artwork, `.nfo`, external audio, chapter sidecars) **never appear**;
  they fold into the tile they belong to and are listed on its detail sheet.
- **Extras** (trailers, deleted scenes, interviews, featurettes) are not companions and are not
  films. They **do not appear as tiles in the grid**; they fold into their film's tile and are listed
  as Extras — separately from companions — on its detail sheet, where they are playable. The parent
  design gives their presentation on the canonical media page to the destination world, and gives
  Vault the physical relationship only (parent §4.16, §H).
- Non-media files **never appear**. Vault is not a file manager.
- Ordering within a mixed folder: subfolders, then series, then films, then unresolved files. Within
  each band, alphabetical.

> **Claude's call, open to reversal.** Hiding Extras from the grid is derived from the parent rather
> than ratified by Hemanth. It follows from #4 and #7 — four featurettes standing as peers beside
> films would break both the collapse principle and the grid's scannability — but the alternative
> (an Extras band at the foot of the folder) is defensible and is a one-line change to this spec.

### 4.3 Precedence

When two rules point at the same object and disagree, this order settles it.

1. **A folder that is one film is a film** (#4). Folder-true loses to media-faced at exactly this
   one point, and nowhere else.
2. **A folder that is one season of a series is not a season tile** — it belongs to its show tile
   (#8), which lives one level up.
3. **Away beats resolving** (#6 over #5). An item on an absent drive is marked away, not shown
   mid-resolve, because Vault cannot resolve what it cannot read.
4. **Uncertain beats identified** (#9). If any part of a tile's identity is in doubt, the tile reads
   as uncertain regardless of how much else is known.
5. **Physical fact precedence on the tile face** (#7), when more than one applies: away, then
   uncertainty, then copy count, then quality. Exactly one is shown.

### 4.4 Propagation

Acting on one element changes others. Every consequence below is required behaviour.

| Action | Consequence |
|---|---|
| A file resolves | Its tile changes face in place. Grid order may change; the tile animates to its new position rather than teleporting. |
| A film is identified manually | Its tile resolves. If a rule was learned, sibling tiles it covers may resolve too, in the same pass. |
| A drive goes away | Its rail entry marks away. Every tile from that root marks away in place. Counts on the door update. |
| A drive returns | The reverse, and any items that changed while away re-resolve. |
| Uncertainty is repaired in place | The tile settles, the door's attention count decrements, Manage's Attention queue loses that entry. |
| A folder is entered | Breadcrumb extends. Rail selection follows the root the folder belongs to. |

The door's health statement is derived, never stored. It reflects the same truth Manage's Overview
reports, at the same moment.

### 4.5 Occupancy

| Element | Zero | One | Many | Far too many |
|---|---|---|---|---|
| Root rail | Empty-state invitation to add storage | Single entry, no chrome change | Scrolls within the rail | Scrolls; no truncation of names when expanded |
| Grid | Empty state specific to *why* it is empty | One tile, grid does not stretch it | Wraps and scrolls | Virtualised; scroll position preserved |
| Tile title | — | Single line | Two lines maximum, then ellipsis | Full title available on the detail sheet |
| Breadcrumb | Root only | Root plus one | Full path | Middle segments collapse; first and last always visible |

Grid empty states are distinct and must not share copy: no storage configured yet; this folder is
genuinely empty; everything here is on a drive that is away; a filter has excluded everything.

### 4.6 Latency

Latency is the normal condition of this surface, not an exception.

- A tile that has not been identified shows its **filename** on plain ground.
- It becomes artwork and proper title **in place**, while visible, as identification completes.
- The transition is a single crossfade of the face, not a spinner and not a skeleton shimmer.
- No global progress bar sits over the grid. Progress is legible from the tiles themselves.
- A tile mid-resolve remains fully interactive — openable, and repairable if it turns out uncertain.

This is the surface's signature moment and its most-seen state on any newly-added root.

### 4.7 Failure

| Failure | What the surface shows |
|---|---|
| Drive away | Tiles hold position, marked away, not openable. Nothing disappears. |
| Artwork missing | Tile falls back to typographic treatment of the real title. Never an empty frame, never a broken-image glyph. |
| Identification uncertain | Tile marked uncertain; repair opens from the tile. |
| File unreadable or corrupt | Tile marked as needing attention with the reason on its detail sheet. |
| Root permission denied | Rail entry shows the root as unreachable and distinguishes it from away. |

Degradation is always partial and always local to the tile. One bad file never makes the grid look
broken.

### 4.8 Persistence

Scroll position, current folder, rail selection and sort survive leaving Vault and returning within
a session. Current folder and sort survive an app restart. A folder that no longer exists on return
resolves to the nearest ancestor that does, with the breadcrumb reflecting where you actually are.

### 4.9 Reach

Every tile is reachable by keyboard and carries a visible focus ring. Arrow keys move within the
grid, `Enter` opens, `Backspace` ascends, and the rail is reachable by `Tab`. Right-click on a tile
exposes identify, re-identify, reveal in folder, and detail. No shortcut defined here may collide
with Colosseum's existing global shortcuts; the parent's §4.22 and keyboard section govern.

---

### 4.10 Featured carousel and the health statement (amended 2026-08-12)

Browse opens with Colosseum's `FeaturedCarousel` (`qml/FeaturedCarousel.qml` + `CarouselSlide.qml`),
so the surface still reads as Colosseum rather than as a bare grid. It features what recently
**arrived** — a fact from the user's own disk, not editorial curation.

Two translations from the shipped slide are required:

- The shipped slide carries a **blurb** — a descriptive phrase under the title. That is a tagline,
  which is banned. In Vault that slot carries the **physical fact** (e.g. `Season 2 · 1080p`) and
  nothing more. No "added today", no sentence — the kicker already says it arrived.
- The shipped slide takes per-slide gradient colours. Vault's are neutral: no colour beyond the
  house tokens.

**The health statement does not appear as a banner.** Decision #3 originally placed a plain-language
health line at the head of Browse. Ratified removal 2026-08-12: the collapsible rail already carries
roots and counts, and uncertain tiles carry their own mark, so the collection reports its own state
without a restating banner. The full plain-language health answer remains Manage's Overview
(parent §4.2); Browse does not duplicate it. Decision #3 is therefore **superseded for the Browse
face** — health is surfaced by the rail and the tiles, not by an arrival header.

## 5. Primary journey

1. Hemanth opens Vault from the taskbar door. Browse leads with the **featured carousel** —
   what recently arrived — so it reads as Colosseum, not a bare grid.
2. Below it, the collapsed **root rail** shows his drives with availability, and the **grid** shows
   the selected root's top level. Health is read from the rail and the tiles, not from a banner.
3. On a freshly-added root, most tiles show filenames on plain ground and **resolve in front of him**
   as Vault identifies them.
4. He drills into a folder. The breadcrumb extends. A folder that is one film **is** that film.
5. A tile marked uncertain is repaired from where it sits, using the identify dialog already
   shipped. Its siblings may resolve with it if a rule was learned.
6. He opens a film. The **detail sheet** shows every copy, which drive each sits on, quality, size,
   companions, and what Vault believes and why. Play hands off to the player.
7. He unplugs a drive. Its tiles stay where they are, marked away. Nothing is forgotten.

---

## 6. Visual contract

### 6.1 Tokens

Copied verbatim from `Colosseum/qml/Theme.qml`. No value outside this table may appear in the mock
or in the implementation.

| Token | Value |
|---|---|
| `gold` | `#f0c44a` |
| `ink` | `#f7f7f5` |
| `inkDim` | `#c9c8d0` |
| `inkDimmer` | `#9a99a5` |
| `edge` | `rgba(255, 255, 255, 0.18)` |
| `glassTint` | `rgba(255, 255, 255, 0.10)` |
| `glassHi` | `rgba(255, 255, 255, 0.14)` |
| `ui` / `hud` | `Segoe UI` |
| `display` | `Fraunces` |
| `margin` | `54` |

Gold is reserved for state that demands the eye: the door's attention count, and the uncertainty
mark. It is not used for ordinary selection or hover.

### 6.2 Register

- **Display face (Fraunces)** — the door's health statement, and the detail sheet's title. Nowhere
  else.
- **UI face (Segoe UI)** — everything else, including all tile text.
- Away and resolving states are expressed by **reduced ink**, not by colour.
- Uncertainty is expressed by a **gold mark**, not by a red one. There is no error colour in this
  surface.
- Artwork is the only saturated thing on screen. Chrome stays grayscale so the collection is what
  the eye lands on.

### 6.3 Card shape and density

**Card shape varies by content type.** This is Jellyfin's structure as *rendered* — verified on the
live demo, not inferred from its stylesheet — and it is adopted:

| Content | Shape |
|---|---|
| Film, series, season, folder | 2:3 poster |
| Episode, loose video clip | 16:9 still |

Going into a series is therefore a **different card**, not the same grid one level deeper. This
matters most at depth: a season of Gintama is 49 wide cards, not 49 posters.

Card corners are near-square — roughly 5px, not the app's larger panel radii. Artwork fills the card
edge to edge and **nothing is printed over it**. Title sits centered below the card on one line with
ellipsis; a second, dimmer centered line beneath carries the physical fact, occupying the slot where
Jellyfin puts the year. State uses **circular corner indicators**, never rectangular badges.

Hover dims the artwork and reveals a play affordance; it does not merely scale the card.

The grid is dense — cards around 150px wide for posters. Chrome around it stays minimal: the rail,
the breadcrumb, and nothing else competing.

---

## 7. Jellyfin reference atlas

Read directly from `~/Downloads/jellyfin-web`. Structure adopted, palette not.

| Jellyfin source | What it proves | What Vault adopts | What Vault changes |
|---|---|---|---|
| `src/components/cardbuilder/card.scss`, checked against the **rendered** Movies and Shows libraries | The card is the unit of a library, and its face is pure artwork. In the source, hover carries `transform: scale(1.07)` over `200ms ease-out`; in the rendered library grid the hover that actually fires **dims the art and reveals play plus an action row**. Corners read near-square. | The card as the unit; artwork edge to edge with nothing over it; centered one-line title; a second dim line beneath; circular corner indicators; dim-and-reveal hover. | Our second line carries the physical fact rather than the year, and our indicators report availability and certainty rather than watched state. |
| The rendered **Shows › Episodes** view | Card shape is not constant — episodes are 16:9 stills while films, shows and seasons are 2:3 posters. | Shape by content type, exactly as observed. | Ours applies the 16:9 form to loose local video clips too, which Jellyfin has no equivalent for. |
| `src/components/indicators/indicators.scss` | State belongs baked into the card edge, not floating beside it. Progress is `0.28em` tall on `rgba(51,51,51,0.8)`. | State on the card edge as a principle. | Ours marks availability, resolution and uncertainty — not playback progress, which is the canonical worlds' business. |
| `src/components/mediainfo/` | A compact metadata line reads well beneath a title. | The idea of one restrained fact line. | Ours states physical truth (quality, copies, drive), not runtime and rating. |
| `src/components/homesections/homesections.scss` | Horizontal rails organise a large library. | **Not adopted.** Rails imply curation Browse does not have; our organisation is the user's own folders. | — |
| `src/components/backdrop/backdrop.scss` | The cinematic hero is thin — 35 lines, a dimmed image behind ordinary content. | **Not adopted for Browse.** The backdrop hero belongs to Theatre. | — |

**Not adopted, deliberately:** Jellyfin's palette, its accent tinting, its Continue Watching rails,
its detail page with cast and related titles. Each of those pulls Vault toward being a canonical
media world, which the parent design rejects in three separate places.

Jellyfin is credited in the README when this ships. This surface is never restyled later to escape
the resemblance.

---

## 8. Acceptance criteria

Observable, and testable by looking.

1. Vault's door states health before entry, and the statement matches Manage's Overview.
2. A freshly-added root shows filename tiles that become artwork in place, without a page reload or
   a global spinner.
3. A folder containing one film and its subtitles presents as one film tile; the subtitles appear
   only on that film's detail sheet.
4. A series with multiple seasons presents as exactly one tile at its parent level.
5. Unplugging a drive marks its tiles in place; no tile disappears and no count is lost.
6. Every tile face carries at most one physical fact, chosen by the §4.3 precedence order.
7. An uncertain tile opens the identify dialog from where it sits, and settling it decrements the
   door's count.
8. Opening a film shows every copy with its drive, and never shows cast, synopsis, or related media.
9. A film with an `Extras` folder shows no Extra as a grid tile; its trailer and featurettes are
   listed as Extras on its detail sheet, distinct from its companions.
10. Every tile is keyboard-reachable with a visible focus ring.
11. No hex value outside the §6.1 table appears anywhere in the surface.
12. Existing Vault browsing capability — door, shelves, folders, tiles, local-file navigation —
    remains reachable and is not demoted.

---

## 9. Non-goals

- Browse does not become a canonical media world.
- Browse does not show cast, synopsis, ratings, or related titles.
- Browse does not present curated rails.
- Browse does not delete, move, or reorganise files.
- Browse does not expose codec-level detail on the tile face.
- Manage receives none of this visual treatment.

## 10. Deferred

- Manage's six areas beyond the plain, text-led constraint.
- Sort, filter and search behaviour inside Browse.
- DLNA surfacing.
- Music and photos, per the parent's non-goals.

---

## 11. Mock brief

One self-contained file at `Brotherhood/agents/colosseum-vault-browse-face-mock.html`, covering:

**Screens** — door with health; root rail and grid at a root's top level; a folder mid-drill with a
breadcrumb; a series tile and its drilled season; the physical detail sheet.

**States, shown deliberately** — no storage configured; a folder mid-resolve with filename tiles
alongside resolved ones; a drive away with its tiles marked; an uncertain tile; a very long title; a
film with missing artwork; a folder holding one film plus companions.

Real titles from Hemanth's own library throughout. Tokens verbatim from §6.1. Existing Colosseum
chrome reproduced exactly, and commented as context rather than as new work.

---

## 12. Closure

This specification is decision-complete for the Browse face. It carries no placeholders and no
invented product behaviour. Every locked decision in §3 appears in the body; every constraint is
respected; every deferred item is excluded.

Next: Hemanth's review of this document, then the mock, then `brotherhood-writing-plans`.
