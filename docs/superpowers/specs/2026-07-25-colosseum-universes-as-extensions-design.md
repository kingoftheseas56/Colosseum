# Universes as a Fourth Extension Type — Design

**Status:** design approved-pending-review · **Date:** 2026-07-25 · **Author:** Agent 0 (Claude), theatre
**Brainstorm:** locked by Hemanth, 2026-07-25 · **Supersedes:** the shelf ruling of 2026-07-18 and the
`archive/universes/custom-five-2026-07/README.md` restoration gate.

---

## 1. What this is, in one sentence

A universe extension is an **installable, served list of everything under one IP, grouped by medium,
with your own resume points at the top — and nothing more unless the IP has a genuine timeline.**

It is aggregation. It is not a timeline tool, not a recommendation engine, not a fourth mode, and not a
per-IP design exercise.

## 2. Experience promise and scope

**Promise:** "Show me every work that exists under this IP, in one place, and remind me where I am in
it."

The user installs `One Piece` the way they install any other extension. It appears as a tile in a rail
at the top of Home. Tapping it opens one page listing every One Piece work — TV shows, movies, manga,
specials, novels — with a Continue section on top showing where they left off in each medium. Tapping
any work opens it exactly as it would open anywhere else in Colosseum.

**In scope:** the fourth extension type, its served payload format, the universe page, the Home rail,
the Extensions surface changes, and one shipped universe (One Piece).

**Out of scope:** see §11.

## 3. Ground truth this is built on

Verified 2026-07-25 against `master` @ `3946699`.

- **The old universe feature is dead and test-enforced dead.** `179bb87` (2026-07-19) removed 241 lines
  from `qml/Main.qml` — the cycling hero, the Hall door, both page layers, the art warmer — and
  `tests/test_universe_archive_p0.ps1:28-46` now **fails the build if those seams return**.
- **`Extensions` today means "Stremio addon", exclusively.** `qml/ExtensionsPage.qml:174-179` hardcodes
  three world tabs; `visible: root.world !== "theatre"` (`:891`) renders a designed empty state for two
  of them, and the counts line (`:157-167`) hardcodes every install as Theatre's. Biblio and Tankoban
  providers (`BiblioApi.js`, `ComicsApi.js`, …) are `.pragma library` modules with baked endpoints and
  **zero** references to the extension registry.
- **The store is permissive and needs no migration.** `ExtensionsStore::slimManifest`
  (`native/engine/ExtensionsStore.cpp:236-304`) keeps `resources` / `types` / `idPrefixes` **verbatim**;
  validation (`:341-349`) requires only a non-empty `id` and `name`, and rejects `behaviorHints.adult`.
  The persisted entry (`:386-392`) is `{id, transportUrl, installedAt, enabled, core, manifest}` in
  `%AppData%/…/extensions/installed.json`, array order = ask order.
- **`Progress` is the resume backbone.** `native/ProgressStore.h` exposes `Q_INVOKABLE recent(kind,
  limit)` (`:88`) and `get(kind, id)` (`:157`). `recent()` already groups video episodes to their series
  root via `continueGroupKey`/`seriesRootId` (`:216`, `:208`) — so a per-episode record surfaces as the
  series. Kinds in live use: `video`, `book`, `manga`, `comic`, `audiobook`.
- **The old material is a closed island.** Grep-verified: every reference to `Universes.js`,
  `UniverseApi.js`, `UniversePage.qml`, `SagaApi.js`, `McuApi.js`, `MagazineApi.js`, `CosmereApi.js`,
  `UniverseHallPage.qml` from outside the cluster is a **comment only** (`qml/GenreApi.js:52,198`;
  `qml/GenrePage.qml:18,323`; `qml/TheatreGenrePage.qml:16,328`; `qml/BiblioGenrePage.qml:17,318`).
  Deleting the cluster cannot break live genre pages.
- **Two stale tests are mutually unsatisfiable.** `tests/test_universe_hall_p0.ps1:48-56` asserts
  `Main.qml` *contains* the exact strings `test_universe_archive_p0.ps1` asserts are *absent*;
  `tests/test_universe_exhibit_p0.ps1:20` asserts a string that returns 0 matches. Neither is wired to a
  runner, so both rot silently.

## 4. Primary user journey

1. **Discover.** Extensions → **Universes** tab → the universe appears in a list, described plainly.
2. **Install.** Same flow as any addon — `Extensions.preview` then `Extensions.install`. No new UI.
3. **Appears on Home.** A `Universes` rail at the **top of `contentCol`, above Continue**, shows a tile
   per installed, enabled universe in ask order.
4. **Open.** Tap the tile → the universe page.
5. **Orient.** Continue section on top (only if there is progress), then one row per served section in
   served order: `TV Shows · Movies · Manga · Specials · Novels`.
6. **Enter.** Tap any entry → it opens the normal way for its medium (§7).
7. **Leave.** Back returns to Home with the rail intact.

## 5. The universe extension contract

### 5.1 Classification — derived, never stored

**A universe extension is any installed extension whose manifest declares the `universe` resource.**

```json
{
  "id": "com.colosseum.universe.onepiece",
  "name": "One Piece",
  "version": "1.0.0",
  "description": "Every One Piece work in one place.",
  "logo": "https://…/one-piece-logo.png",
  "background": "https://…/one-piece-bg.jpg",
  "resources": ["universe"],
  "types": ["universe"]
}
```

Rationale: `slimManifest` already preserves `resources` verbatim, so this needs **no C++ store change,
no new persisted field, and no `installed.json` migration**. It also matches how the rest of the app
already behaves — Discover derives everything from installed manifests and stores no local table
(`AddonClient.discoverCatalogSpecs`, `qml/AddonClient.js:406-430`).

`logo` and `background` are optional and used for the Home rail tile and the page header.

### 5.2 The universe resource

One extension serves exactly one universe, so the resource needs no id:

```
GET <transportUrl base>/universe.json
```

```json
{
  "universe": {
    "id": "one-piece",
    "title": "One Piece",
    "logo": "…", "background": "…",
    "updatedAt": 1753400000,
    "sections": [
      { "id": "tv", "title": "TV Shows", "kind": "video", "entries": [
        { "id": "tt0388629",  "type": "series", "title": "One Piece", "year": "1999" },
        { "id": "…",          "type": "series", "title": "One Pace" },
        { "id": "tt11737520", "type": "series", "title": "One Piece (Live Action)", "year": "2023" },
        { "id": "tt30476502", "type": "series", "title": "The One Piece", "year": "2027" },
        { "id": "…",          "type": "series", "title": "One Piece: Fishman Island Special Edition" }
      ]},
      { "id": "movies",   "title": "Movies",   "kind": "video", "entries": [ /* 15 films, type: movie */ ] },
      { "id": "manga",    "title": "Manga",    "kind": "manga", "entries": [
        { "id": "30013", "provider": "anilist", "title": "One Piece" },
        { "id": "…",     "provider": "anilist", "title": "One Piece: Digitally Colored" },
        { "id": "…",     "provider": "anilist", "title": "<spin-off>" }
      ]},
      { "id": "specials", "title": "Specials", "kind": "video", "entries": [ /* incl. Fan Letter */ ] },
      { "id": "novels",   "title": "Novels",   "kind": "book",  "entries": [
        { "id": "…", "provider": "applebooks", "title": "One Piece: Ace's Story", "edition": "novel" }
      ]}
    ]
  }
}
```

**Field contract**

| Field | Required | Notes |
|---|---|---|
| `sections[]` | yes | **Array order is display order.** The server decides; the client never re-sorts. |
| `section.kind` | yes | `video` \| `manga` \| `comic` \| `book`. Load-bearing: selects the tile component **and** the `Progress` kind used for Continue matching. Unknown kind → section skipped, logged. |
| `section.title` | yes | Rendered verbatim. Section names are curation, not code. |
| `entry.id` | yes | A verified provider identity. See §5.4. |
| `entry.type` | video only | `movie` \| `series`. **Required for video** — a universe tile that reaches Theatre without a type opens a series as a movie and dies. |
| `entry.provider` | non-video | `anilist` \| `mal` \| `applebooks` \| `getcomics`. |
| `entry.title` | yes | Display + fallback. |
| `entry.year`, `entry.poster`, `entry.note`, `entry.edition` | no | `poster` overrides provider art; `note` is one short line; `edition` disambiguates (§5.4). |

Unknown fields are **ignored, not rejected** — so a future `chronology` block for a timeline IP (§11) is
purely additive and needs no client change to ship.

### 5.3 A universe supplies identity and ordering only — never sources

A universe says *"One Piece is `tt0388629`"*. It never returns a stream, a torrent, a subtitle, or a
download. Resolution and playback run through the user's already-installed addons and their own library,
exactly as they do today.

Consequences, all deliberate: installing a universe can never break playback; a universe cannot become a
piracy vector; there is one copy of source policy in the app (the same ruling Hemanth made for Player 2);
and a universe works identically for a user with one addon or twenty.

### 5.4 The Universe Page Law, enforced at the boundary

`docs/UNIVERSE_PAGE_LAW.md` (ratified 2026-07-13) still binds: **every entry is a verified provider
identity, never a name search.** Because curation now arrives from outside the repo, the law becomes a
boundary check rather than an internal habit:

- An entry with no `id` is **dropped**, and the section renders without it.
- A video entry with no `type` is **dropped** — this is the documented failure that opens a series as a
  movie.
- Ambiguous works must be disambiguated by the server, not guessed by the client. *Ace's Story* exists as
  both a light novel and a manga adaptation; the old baked data filed it under **manga**
  (`qml/Universes.js`), while this design places it under **Novels**. The served entry therefore carries
  `edition` and a provider-specific `id` so the two can never be conflated.
- Dropped entries are logged once per load, never surfaced as an error dialog.

Trust and policing of *third-party* universes is deferred (§11) — at v1 the only universe is ours.

### 5.5 Serving the v1 payload

Hemanth locked "served, not bundled" so content is correctable without an app build. The cheapest
faithful way to honour that: publish `universe.json` and `manifest.json` as **static files over HTTPS
from a repo-backed host** (GitHub raw or Pages). Zero infrastructure, corrections land by commit, and it
is genuinely served — no rebuild, no release.

Client caching: the last successful `universe.json` is cached per extension id. On failure or slow
network the page renders the cached payload; only a first-ever load with no cache shows an empty state.

## 6. States, interruptions, recovery, edge cases

| Situation | Behaviour |
|---|---|
| No universe installed | The Home rail is **hidden entirely** and Continue rises to the top of Home. Nothing empty is ever shown above Continue. |
| Universe installed, extension unreachable | Page renders the **last-known cached payload**. A quiet inline note, never a blocking dialog. |
| Unreachable **and** no cache | Page shows the universe title and one plain line explaining it could not be loaded, plus retry. |
| A section has zero valid entries | Section is omitted. No empty rows. |
| No progress anywhere in this universe | Continue section omitted; the page starts at the first served section. |
| Progress exists in one medium only | Continue shows exactly that one tile. |
| Universe disabled (not removed) | Disappears from the Home rail; stays in the Installed list, toggleable. Standard addon behaviour, no special case. |
| Two universes claim the same work | Allowed and harmless — each page is independent; `Progress` is keyed by provider id, so a resume point is shared correctly between them. |
| `updatedAt` moves | Payload refetched on next page open. No push, no polling. |

## 7. Controls, feedback, integration

**The Continue section.** For each `kind` present in the universe, call `Progress.recent(kind, 0)` and
keep records whose grouped id is in that kind's served id set — `recent()` already collapses video
episodes to their series root, so the match works without per-episode bookkeeping. Show **at most one
tile per medium**, the most recent, ordered by recency. Hidden when the result is empty.

**Tap behaviour** — an entry opens the normal way for its medium. No universe-specific navigation:

| `kind` | Route |
|---|---|
| `video` | `watchRequested({ id, type, title })` → Theatre series/movie page. `type` is mandatory. |
| `manga` | `win.openSeries(title)` / `openSeriesAt(title, seriesId, chapterId)` (`qml/Main.qml:476`, `:488`) |
| `comic` | `win.openWesternAt(title, tagSlug, chapterId)` (`qml/Main.qml:542`) |
| `book` | `win.openBook(b)` (`qml/Main.qml:872`) |

Deep-linking *into* a specific episode or book chapter is **not** part of this design — video and books
have no opener that accepts one, and with aggregation there is no beat to enter at.

**Accessibility / house style.** Reuses existing house components — `WidgetHeader` for section titles,
the existing poster/cover/jacket tiles per `kind`, `HouseScrollBar`, `ScrollGlide`, `BackAction`, the
standard back/system control cluster. Keyboard: `Esc` closes the page and joins the existing Esc chain.
No new visual language, no bespoke per-IP layout — this is the constraint that got the previous
generation benched.

## 8. Surfaces to change

| # | Surface | Change |
|---|---|---|
| 1 | `qml/UniversesRail.qml` **(new)** | Home rail, matching the rhythm of Home's existing rows: a `WidgetHeader` reading **Universes**, then a horizontal strip of modest tiles carrying the IP name over its `logo`/`background`. No "See all" link at v1 (§11). Model = installed extensions whose manifest `resources` contains `universe` and `enabled === true`, in ask order. `visible: count > 0`. |
| 2 | `qml/Main.qml` | Insert the rail as the **first** child of `contentCol` (before the Continue block at `:1410`); add a lazy `Loader` layer for the universe page + its Esc-chain entry. |
| 3 | `qml/UniverseExtensionPage.qml` **(new)** | The page: header, Continue section, N served sections. One renderer for every universe, forever. |
| 4 | `qml/UniverseExtApi.js` **(new)** | Fetch + cache + validate `universe.json`; drop invalid entries per §5.4. |
| 5 | `qml/ExtensionsPage.qml` | Add a fourth world tab `{ key: "universes", title: "Universes", live: true }` (`:174-179`); make the Installed pane filter/section by world so universes don't pollute the Theatre list; fix the hardcoded counts line (`:157-167`) so it stops claiming every install is Theatre's. |
| 6 | `qml/ExtensionsCatalog.js` | Add the One Piece universe to the curated Discover rail so it is installable without pasting a link. |
| 7 | `tests/test_universe_archive_p0.ps1` | **Rewrite.** It currently blocks *any* universe seam. It must instead guard what we actually want dead: the old baked `Universes.js` path. |
| 8 | `tests/test_universe_exhibit_p0.ps1`, `tests/test_universe_hall_p0.ps1` | **Delete.** Mutually unsatisfiable and unwired (§3). |
| 9 | Old cluster — **delete** | `qml/Universes.js`, `UniverseApi.js`, `UniversePage.qml`, `UniverseHallPage.qml`, `SagaUniversePage.qml`, `EraUniversePage.qml`, `GalaxyUniversePage.qml`, `StudioUniversePage.qml`, `SagaApi.js`, `CosmereApi.js`, `MagazineApi.js`, `McuApi.js`, `_universecheck.qml`, and their orphaned harnesses/tests. Grep-verified safe (§3). Stale comments naming `UniversePage` in the three genre pages should be reworded, not left dangling. |
| 10 | The served payload | `manifest.json` + `universe.json` for One Piece, published per §5.5. |

**No C++ change is required.** This is deliberate: classification is derived from the manifest, and the
store already persists the full slimmed manifest.

## 9. Acceptance criteria

Observable, in order:

1. With no universe installed, Home's first row is **Continue** and no empty band appears above it.
2. Extensions shows a **Universes** tab that is live, not an "arrives later" empty state; the counts line
   no longer reports every install as Theatre's.
3. Installing the One Piece universe from the curated rail requires no pasted link and no restart.
4. A `Universes` rail appears at the **top of Home** with a One Piece tile.
5. The universe page lists exactly five sections in served order — **TV Shows, Movies, Manga, Specials,
   Novels** — containing the works Hemanth enumerated: One Piece, One Pace, Live Action, The One Piece
   (2027), Fishman Island Special Edition; 15 films; the manga, the digitally coloured edition, the
   spin-off; the specials including Fan Letter; and *Ace's Story* as a **novel**.
6. Tapping the anime opens the Theatre series page; tapping the manga opens the manga series; tapping the
   novel opens the book. None opens as the wrong media type.
7. With progress in both the anime and the manga, Continue shows **two tiles, one per medium**. With
   progress in neither, the Continue section is absent.
8. Blocking network access to the universe host still renders the full page from cache.
9. Disabling the extension removes the tile from Home; re-enabling restores it.
10. `Universes.js` no longer exists in the tree and the build is green — no test asserts a universe seam
    must be absent, and none asserts the Hall must be present.

## 10. Non-goals

- **Not a fourth mode.** The three modes stay three; a universe is reached from Home.
- **No timeline, chronology, canon tags, or "start here" doorways** for an IP without an inherent
  timeline. Ratified twice: *"we are only here to do aggregation of all content under a given IP."*
- **No per-IP page design.** Variation lives in served data, never in QML.
- **No live frontier comparison** ("the manga runs 40 chapters ahead") — withdrawn as beyond aggregation.
- **No sources, streams, or downloads** from a universe extension.
- **No "See all" / Hall of Worlds** — a one-item rail does not need one.
- **The five archived custom pages stay archived**, One Piece's Grand Line chamber included.

## 11. Deferred

| Item | Why it waits |
|---|---|
| Timeline treatment for IPs that genuinely have one (MCU phases, DCAU chronology) | Hemanth's rule: ordering is a property of certain IPs, not of the page. Shape is already additive-compatible (§5.2). |
| Third-party trust and policing | Only our universe exists at v1. |
| The roster beyond One Piece | Hemanth chooses fresh; the old 21 are explicitly **not** the list. |
| Deep-linking into an episode or book chapter | Needs engine work; no aggregation value. |
| A universe count/badge on the Home rail | Wait until more than one universe exists. |

## 12. Alternatives considered and discarded

| Alternative | Why discarded |
|---|---|
| **Beat ledger** — rows are story beats, mediums as columns | My own proposal, ratified then **cut by Hemanth**: One Piece is one continuous story, so charting it adds structure without adding an answer. It was a named construct the problem never asked for. |
| **One template for all universes** | Ratified then cut — "it still can't be just the one template." Resolved by making sections **data**, so content varies per IP at zero design cost. |
| **One bundled "Universes" extension** holding every IP | Rejected: no way to decline an IP, and the Home rail would show everything. One extension per universe matches the Marvel/DC addons Hemanth cited. |
| **Bundling content in the app** | Rejected: every correction would wait for a release. Proven stale in practice — the baked pin said the WIT remake was 2026; it is 2027. |
| **Restoring the 21 curated universes** | Discarded by Hemanth. They were benched for lacking bespoke pages, a gate this design dissolves — but the roster itself is being chosen fresh. |
| **Full cross-medium alignment for all IPs before shipping** | Rejected: hundreds of spans each needing live verification, with nothing visible until the last one landed. |
| **Canon / non-canon tagging** | Rejected as editorial design beyond aggregation. |
| **Storing a `world` field on the installed entry** | Unnecessary: classification derives from the manifest's `resources`, so there is no migration and no second source of truth. |

## 13. Risks

1. **The guard test is a hard build blocker.** `test_universe_archive_p0.ps1` fails on any universe seam.
   Rewrite it **first**, as its own commit, before any new seam lands.
2. **`Main.qml` is a shared file.** The rail insert and the page layer touch it. Declare the exact edits
   in `agents/chat.md` before touching it, per standing shared-file discipline — A4's Great Swap is
   editing the same file this week.
3. **The Extensions Installed pane currently has no sectioning at all.** Adding a fourth world without
   filtering it would mix universes into the Theatre list. Item 5 in §8 is not optional polish.
4. **One Pace and the Fishman Island Special Edition are fan re-edits.** They may have no Cinemeta id. If
   an id cannot be verified, the law says drop the entry rather than fall back to a name — so Hemanth
   should be told which of his five TV entries could not be pinned rather than having them silently
   vanish.

## 14. Provenance

Every Locked decision in §1–§7 traces to Hemanth's ratification on 2026-07-25. Two decisions were
ratified and then **superseded by him in the same session** — the beat ledger and the single template
(§12) — and two proposals of mine were **withdrawn** by applying his aggregation rule rather than making
him state it a third time: the live frontier comparison and beat-level entry.
