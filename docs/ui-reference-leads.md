# UI Reference Leads (deferred — seeds for future arcs)

> Hemanth names the apps whose **design language** we study before reshaping a surface. This is not
> a roster of dependencies and nothing here is an active arc — it records his picks so that whoever
> scopes the future arc starts from the reference he already chose, the way Harbor seeded the
> Theatre depth-catalogue arc.
>
> **The house rule for every entry:** we translate the *interaction and visual language*, never port
> code or copy labels/data (the Harbor rule: "match the depth and behavior, don't copy the exact
> labels or data dependencies"). These are all web/Electron stacks; Colosseum is Qt/QML. And the
> house restyle law still governs anything adopted — **gray, black, white, SVG; no color, no emoji.**

## Established references (already used)

- **Kodi, Jellyfin, Harbor** — prior influences on Theatre/Player discovery, catalogue depth, and
  player chrome. Harbor most recently seeded the depth-catalogue arc
  (`specs/2026-08-01-theatre-harbor-depth-catalogue-design.md`).

## New leads (added 2026-08-07, Hemanth)

### Hayase → Theatre **series view**, especially anime · Agent 4's lane

- **Repo:** `github.com/hayase-app/interface` (the UI) · **stack:** Svelte + Tailwind, Electron shell.
- **What Hemanth flagged:** its series view — the way it presents an anime series — "looks really
  good." That visual judgment is his; the arc that adopts it studies the *running app and
  screenshots*, not just the source (the repo tree alone does not show the layout).
- **What to study when the arc is scoped:** how it lays out a series — seasons, episode lists,
  related/continue, and the anime-specific presentation (absolute vs seasonal numbering shown
  cleanly). Translate the *shape and rhythm* into Colosseum's own `TheatreSeries.qml` language.
- **Kin:** sits alongside Harbor (discovery depth) — Hayase is the **series-detail** reference where
  Harbor was the **discovery-breadth** reference.
- **Note:** Hayase itself is a torrent client (established 2026-08-07, not an HTTP source) — we take
  its **UI only**, nothing about its sourcing.

### Houdoku → **Tankoban** (comics/manga library + reader) · Agent 1's lane

- **Repo:** `github.com/xgi/houdoku` · **stack:** Electron + React + TypeScript, **Radix primitives +
  shadcn** base components. Actively developed (1,345 commits, CI live).
- **What it is:** a desktop manga reader and library manager — library view, per-series detail, and
  a customizable reader. The closest analogue to Tankoban's own job of any reference so far.
- **What to study when the arc is scoped:** its library grid and filtering/tagging, the series
  detail page, and the reader layout options. It is the most direct **shape** reference for
  Tankoban's library + reader because it solves the same problem on the desktop.
- **Kin:** first dedicated **comics/manga desktop** reference in the roster; complements the reader
  work already in `reader2/`.
- **Functional inheritance (assessed 2026-08-07) — narrow, take UI not internals.** Tankoban has
  already gone *further* than Houdoku on the core: our native reader (decode coordinator, scaled
  cache tiers, double-page pairing/coupling, long-strip geometry, render profile) is deeper than
  Houdoku's Electron reader, and our download-and-keep pipeline is Tankoban's whole backbone. Its
  Tiyo plugin-source model is not a fit (we have native sources + the new Theatre extension model).
  **The one genuinely inheritable feature is tracker sync:** Houdoku pushes reading progress to
  **AniList / MyAnimeList / MangaUpdates**; Tankoban only *pulls* AniList art/metadata today
  (`MangaSeries.qml`: `AniList art() → banner/cover/synopsis/genres/year/score`) and never writes
  progress back. Reader already knows the position (resume), so this is sending it outward, not new
  plumbing. Smaller cousin: Discord "now reading" presence (Harbor already does this for video —
  `src-tauri/src/discord_rp.rs`). **Product flag for Hemanth:** tracker sync introduces an opt-in
  user login to *their own* tracker account — distinct from the no-login-*source* rule, but still a
  sign-in where there is none today; his call whether Tankoban has one at all.

## When one of these becomes real

Scoping either arc runs through `brotherhood-brainstorming` like any surface work. The reference is
an input to that brainstorm — a source of proven interaction ideas to adapt — not a design in
itself. Pull screenshots into a disposable mock, decide what earns its place in Colosseum's own
language, and leave the rest.
