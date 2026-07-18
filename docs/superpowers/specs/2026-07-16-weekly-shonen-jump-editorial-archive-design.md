# Weekly Shonen Jump Universe Page: The Editorial Archive

**Date:** 2026-07-16  
**Status:** RETIRED 2026-07-18 — implemented that morning, then Hemanth ordered the mock-lineage concept dropped entirely ("forget about the previous page… a completely new universe page"). Superseded the same day by THE LONG RUN (fresh design, no shared vocabulary; see the 2026-07-18 commit on `MagazineUniversePage.qml`). Kept as record only.  
**Target:** Replace the current `MagazineUniversePage.qml` presentation for Weekly Shonen Jump while preserving Colosseum's provider-ID law and MAL magazine registry.

## 1. Product intent

Weekly Shonen Jump should not read as a generic list of famous manga or as a simulation of only the latest weekly issue. The page represents the magazine as a sixty-year editorial institution: a place where series entered, competed, became landmarks, ended, and were shelved into history.

The central metaphor is **The Editorial Archive**. The visitor enters through the present-day editorial desk, then moves into four monumental bound archive volumes:

1. **The Founding Years** — 1968–1979
2. **The Golden Age** — 1980–1996
3. **The Big Three Era** — 1997–2014
4. **The New Generation** — 2015–present

A manga belongs to the era in which its Weekly Shonen Jump serialization began. Long-running titles remain in their starting era rather than being duplicated across later eras.

## 2. Accuracy rules

The page remains manga-only. Anime, films, games, and crossover media do not appear merely because they originated from Jump properties.

The canonical membership gate is MAL magazine ID `83`, accessed through Jikan's exact `magazines=83` filter. Each entry keeps its MAL identity and routes to Colosseum's manga-series door.

Two current labels must be corrected:

- MAL `members` is **not circulation**. It is a MyAnimeList member/library count. The interface must say `MAL members`, `reader libraries`, or `member count`, never `circulation`.
- `status=publishing` is a current serialization registry, not a guarantee that a title appears in the literal newest printed issue. The department should be called **The Current Desk** or **Current Serialization Registry**, with a compact explanatory note.

No total, ranking, publication span, or current-status claim is invented when Jikan is unavailable.

## 3. Page architecture

### 3.1 Masthead: the archive entrance

The hero is a deep Jump-red archive wall rather than a single magazine-cover billboard.

- Left: Weekly Shonen Jump masthead, source-backed introductory excerpt, Shueisha and 1968 origin metadata.
- Center/right: four oversized bound archive spines, partially visible in perspective, labeled with the approved eras.
- The first issue cover appears as a framed archival object, not the page's dominant identity.
- Live registry total appears only after the Jikan response lands.
- Hovering an era spine reveals its date span and top three MAL-member titles; clicking scrolls to that era's reading room.

The hero establishes the complete archive as the page's throne.

### 3.2 The Current Desk

The current lineup becomes one department immediately below the hero.

Visual metaphor: an editorial worktable with the latest available cover object, manuscript slips, red-pencil marks, and a horizontal lineup of currently publishing manga.

Data:

- Exact MAL magazine registry filtered to `Publishing`.
- Title, cover, start year, MAL ID, member count, chapter count when present.
- No claim that the list is the exact contents of the latest physical issue.

Interaction:

- Click a manga to open its MangaSeries page.
- Hover raises its manuscript slip and reveals factual metadata.
- When the current registry is unavailable, the desk shows a restrained offline state and does not substitute historical titles.

### 3.3 The Hall of Champions

The existing all-time ranking survives, but it no longer controls the page.

- A compact cross-era roster of the top ten registry entries by MAL member count.
- Label: **Most Collected on MAL**, not `All-Time Vote` or `circulation`.
- Gold numerals remain as the visual motif.
- Each row shows start/end year, chapter count, and MAL member count.

This section acts as an index of cultural reach, not a claim about Jump's internal reader ballots or historical print sales.

### 3.4 The four archive volumes

Each era is presented as a large open bound volume rather than a conventional card rail.

#### Left page

- Era title and date span.
- Registry-derived title count for that era.
- A concise sourced historical note where available.
- One large anchor title selected objectively as the era's highest MAL-member entry.
- Optional verified circulation milestone displayed separately from MAL data.

#### Right page

- The era's titles ordered by MAL member count by default.
- Cover grid with title, serialization span, chapter count, MAL ID, and member count.
- A sort switch for `Most collected` and `Chronological`.
- A local search field filters only within the open era.

The four volumes appear in this fixed order:

- The Founding Years, 1968–1979
- The Golden Age, 1980–1996
- The Big Three Era, 1997–2014
- The New Generation, 2015–present

### 3.5 Complete registry index

The page ends with a compact complete index rather than another poster wall.

- Alphabetical title list.
- Filter by era.
- Search by title.
- Each result routes to MangaSeries.
- This index becomes available progressively as the full Jikan registry loads.

## 4. Visual language

The page uses Jump's print and editorial vocabulary without imitating a particular copyrighted issue layout.

- **Palette:** aged Jump red, ink black, ivory paper, Colosseum gold, restrained off-white.
- **Materials:** bound cloth, paper edges, archive labels, red pencil, registration marks, issue-number stamps.
- **Typography:** Colosseum display face for history; condensed sans-serif treatment for issue metadata and archive labels.
- **Motion:** era spines shift forward, page edges flex, manuscript slips lift, and sorting causes a quick editorial reshuffle rather than a generic fade.
- **Texture:** procedural grain and gradients only. No background collage of copyrighted manga panels.

The page must still feel native to Colosseum: full-width sections, 54 px house margins, restrained glass where appropriate, and the existing back/minimize/close shell.

## 5. Components

### `MagazineUniversePage.qml`

Owns the page composition and coordinates loading states. It should be broken into focused local components rather than growing into one monolith.

Suggested components:

- `ArchiveMasthead`
- `CurrentDesk`
- `ChampionRoster`
- `ArchiveVolume`
- `ArchiveMangaTile`
- `CompleteRegistryIndex`
- `MagazineOfflineState`

Each component receives data and emits only `seriesRequested(title)` or navigation signals. Provider fetching remains outside visual delegates.

### `MagazineApi.js`

Responsibilities:

- Fetch the current publishing registry.
- Fetch the top-member summary quickly.
- Progressively fetch the complete magazine registry.
- Deduplicate by MAL ID.
- Expose progress and partial results.
- Bucket entries into the four approved eras.
- Sort by MAL members or start year.
- Maintain a session cache.

No visual wording belongs in the API module.

### `Universes.js`

Retains:

- `malMagazineId: 83`
- sourced universe hero copy
- fallback flagships grouped by the four eras
- no anime or film queries

## 6. Data flow

1. Page opens with sourced static hero copy and curated fallback era anchors.
2. `loadSummary(83)` retrieves the first popularity-ranked pages and the publishing registry.
3. The Current Desk and Hall of Champions populate as soon as their data arrives.
4. `loadArchive(83, onProgress, done)` walks the remaining Jikan pages sequentially with throttling.
5. Each completed page is deduplicated and rebucketed into the four eras.
6. Era counts and grids update progressively without shifting the user's current scroll position.
7. The complete registry index unlocks once enough data exists and continues growing until completion.
8. Clicking any item emits `seriesRequested(entry.title)` and routes through the existing manga lane.

## 7. API and performance strategy

The current implementation fetches only the top 100 entries plus up to two pages of publishing titles. That is sufficient for a highlights page but not for a sixty-year archive.

The redesigned API should use two lanes:

### Fast lane

- Top 100 by MAL members.
- Current publishing entries.
- Used for hero previews, Current Desk, and Hall of Champions.

### Archive lane

- Sequentially fetch every available page from the exact magazine registry.
- Respect Jikan throttling with a delay of at least 400 ms between requests.
- Emit partial batches so the page becomes useful before completion.
- Stop honestly on network failure and retain all pages already received.
- Cache by magazine ID for the session.

The page should not block on the complete archive before rendering.

## 8. Error handling

### Jikan unavailable before any response

- Hero and four era volumes remain visible using curated fallback flagships.
- Current Desk shows `Current registry unavailable`.
- Hall of Champions uses the curated flagship list but removes numeric ranking claims.
- Complete index remains unavailable.

### Partial archive failure

- Display all received titles.
- Show `Archive partially loaded` with the number of records received.
- Never fabricate the missing era counts or registry total.
- A retry action resumes from the failed page.

### Missing entry fields

- Missing cover: use the existing neutral manga placeholder.
- Missing chapter count: omit it.
- Missing end year for a finished title: show the start year only.
- Publishing title: show `since YEAR`.

## 9. Copy and provenance

The hero uses the approved source-copy system rather than an AI-written franchise blurb.

Recommended hero line:

> The world's most popular manga factory.

Source and treatment metadata must remain attached in `UniverseCopy.js` or equivalent. All other long prose must be sourced, provider-returned, or replaced with neutral factual metadata.

Era names are editorial navigation labels approved for this page. Historical notes must carry source metadata. Interface labels such as `The Current Desk`, `Open Volume`, `Most Collected on MAL`, and `Complete Registry` are product microcopy and do not require citation.

## 10. Testing

### API tests

- Magazine ID 83 is present and used for all registry calls.
- Entries are deduplicated by MAL ID.
- All four era boundaries are exact and inclusive.
- A title beginning in 1997 enters the Big Three Era.
- A title beginning in 2015 enters the New Generation.
- Long-running titles appear only in their starting era.
- `members` is never formatted or labeled as circulation.
- Partial responses remain usable.
- Retry resumes without duplicating entries.

### UI tests

- Page renders with no network.
- Current Desk hides invented content when unavailable.
- Every live tile routes to MangaSeries.
- Sort switching is stable and deterministic.
- Era navigation preserves the fixed four-era order.
- Complete registry search filters without mutating source data.
- 1366×768 remains usable without clipped primary controls.
- 1920×1080 preserves the intended archive scale.

### Regression tests

- Weekly Shonen Jump remains manga-only.
- Other universe templates are unaffected.
- Back, minimize, close, search, and scroll behavior remain intact.
- Jikan failures do not block the rest of Colosseum.

## 11. Acceptance criteria

The page is complete when:

- The archive, not the current issue, is the dominant visual idea.
- The four approved eras are the primary navigation structure.
- The present-day lineup exists as one department called The Current Desk.
- The MAL registry is progressively represented beyond the current top-100 cap.
- MAL members are never mislabeled as circulation or an official Jump vote.
- Every manga entry opens the manga-series page.
- Offline and partial states remain coherent and honest.
- The result feels bespoke to Weekly Shonen Jump and unmistakably part of Colosseum.

---

## 12. Implementation record (2026-07-18, Agent 5 (Claude))

Shipped as a full rewrite of `MagazineUniversePage.qml` + `MagazineApi.js`, with the WSJ
entry in `Universes.js` extended (sourced `heroLine`, verified print `milestones`, sourced
`eraNotes`, curated `fallbackEras`). Gates: `tests/magazine_registry_harness.qml` (pure
logic — era boundaries, dedup, sorts), `tests/magazine_page_load_harness.qml` (page born
offline stands whole and honest), `tests/test_magazine_universe_p0.ps1` (shape contract,
including a hard ban on the word "circulation" across the lane).

Deliberate deviations from §3:

1. **Per-era local search cut.** One search lives in the Complete Registry index with
   per-volume filter pills — the per-era field duplicated that machinery (reduction reflex).
2. **Era grids collapse to 18 with an "open the full volume" expander** instead of
   unbounded inline grids — first-paint discipline; the full era is one click away.
3. **Archive throttle is page-driven** (QML Timer, 460 ms + one silent 2.6 s retry per
   page before surfacing RESUME FILING) — pacing is a UI concern, the JS lane stays pure.
4. The four era spines live in the masthead as specced; their hover reveal renders in a
   shared strip under the shelf rather than per-spine popovers.

Verification note: Jikan was fully unreachable throughout the build session (connection
timeout), so the live filing walk awaits the feed's return — the offline and partial
states specced in §8 are exactly what shipped, and are what the harnesses prove.
