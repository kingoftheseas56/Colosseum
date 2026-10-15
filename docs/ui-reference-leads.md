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
- **Functional inheritance (assessed 2026-08-07) — take UI, essentially nothing under the hood.**
  Hayase's functional core is deep AniList integration + anime + torrent streaming, and every part
  is already decided: **list/progress sync** (AniList/Kitsu/MAL) is the same feature Hemanth
  *declined* from Houdoku; **auto torrent↔episode matching** and **RSS airing feeds** are anime, a
  *deferred* lane; **torrent streaming while downloading** is what Stremio already gives Theatre
  (Tankorent 2.0 was investigated and STOPPED 2026-08-07 — Stremio is good enough); its **player**
  (all subtitle formats, multi-audio, preview thumbnails) is already matched or beaten by our native
  mpv; its **airing calendar** we already have. The one *substantial* gap is **watch-together /
  group viewing** — real and distinct, but a large social/account/server feature that cuts against a
  personal, offline-first, no-login app; **recommended against**, not just deferred. Remaining
  candidates are minor general player comforts (media keys, pause-on-lost-focus, mini-player,
  download-progress-on-the-scrubber — largely covered by the buffering readout, Discord "now
  watching" presence — Harbor precedent). None warrants an arc; a small QoL audit at most.

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

## Biblio — the frontier mode (positioning, added 2026-08-07)

Hemanth's read 2026-08-07: Tankoban and Theatre have organically settled — they have flown past
their open-source references and there is no one left in-niche to copy. **Biblio is the one mode not
there yet**, and the reason is structural, not neglect.

**The honest landscape (verified 2026-08-07):** open-source book software is *rich*, not absent —
Calibre (20-year library-manager giant), KOReader (e-ink reader), Foliate (GNOME reader), Thorium
(accessibility/Readium reader), Readest (modern cross-platform reader), Kavita (self-hosted
comics+books server). But every one is either a **librarian's tool** (Calibre: organize a big
collection, not lean-back reading) or a **bare reader** (Foliate/Thorium/Readest: open a book you
already have — no discovery, no acquisition, no library-as-experience).

**None is Biblio's actual shape:** discover → acquire → read beautifully as one continuous, no-login
flow, inside the same shell as films and comics. That *whole* has **no open-source peer**; the only
product that IS that whole is **Kindle** (and Kobo / Apple Books) — proprietary. So Biblio's
competitor is Kindle, and its open-source references cover only the *parts*.

**What this means for method — where references exist vs. where Biblio must invent:**

- **Reading pane = proven craft, study it.** Reader references, closest-in-spirit first: **Readest**
  (modern, cross-platform, consumer-feeling), **Thorium** (Readium engine + accessibility),
  **Foliate** (clean native reading), **KOReader** (deep reading-engine behavior). Biblio's epub
  reader (Reader 2, A2's lane) should stand on these, not reinvent from zero.
- **Discover → acquire → unified-library experience = no peer, invent it.** This is the part "not
  there yet." It cannot be a catch-up/parity job because no one is ahead — it is a taste project the
  way Theatre's discovery was, built from Hemanth's judgment. Lane: Agent 2.

**Corollary for Tankoban/Theatre:** "settled" = the reference era ended, not the work. Next gains
come from Hemanth's own friction in real use, not from copying anyone — there is no one ahead.

## When one of these becomes real

Scoping either arc runs through `brotherhood-brainstorming` like any surface work. The reference is
an input to that brainstorm — a source of proven interaction ideas to adapt — not a design in
itself. Pull screenshots into a disposable mock, decide what earns its place in Colosseum's own
language, and leave the rest.
