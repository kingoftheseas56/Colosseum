# Colosseum Home — AF2 Redesign (the receding-spotlight front door)

**Status:** Approved by Hemanth on 2026-07-19 ("done, this is perfect").
**Audit surface:** AF2 map surface "Home + hub windows" (`Home.xml` / `Custom_110x_Hub_*`).
**Approved reference mock:** `agents/colosseum-home-af2-audit-mock.html` (v2 — glass + all three worlds).
**Anchor:** Arctic Fuse 2 — see `docs/superpowers/specs/2026-07-19-arctic-fuse-2-reference-map.md`. We steal **composition, cadence, and motion**, never assets or the data pipeline (TMDbHelper/fanart.tv stay banned; art comes from our own lanes).

## Objective

Make Home a single cinematic **front door** that spans all three worlds (Tankoban, Theatre, Biblio) — a featured spotlight that **recedes** as a glass widget board rises over the living wallpaper, on AF2's row cadence. Today Home is a near-static wallpaper with a Continue widget; AF2's signature is a fanart hero that gets out of the way as the widgets take focus. That recede is the one bold move; everything else stays quiet in our established language.

## Design language (unchanged house doctrine — this redesign inherits, not invents)

- **Glass over a living wallpaper.** Glass is the constant material: frosted panels (backdrop blur) float over full-bleed art that persists behind them. The board never goes flat black.
- **Ink ramp:** near-white `#ededed` at a fixed alpha ladder (100/72/50/30/12) over near-black `#05060a`. (Matches AF2 `colors/defaults.xml` and our reader doctrine.)
- **Gold `#f0c44a` is the only real accent** — used ONLY for progress fills and the single primary action. Never a fill-everywhere.
- **Per-world tint** (Tankoban amber / Theatre blue / Biblio violet) appears only as a small identity cue (a dot, a section tag), never as a surface fill — it must not compete with gold.

### Type

- **Display/headers + hero:** a geometric-humanist sans (the approved mock used **Figtree**, which AF2 ships and is Switzer-adjacent). Rail headers, section titles, the hero fact line and buttons.
- **Labels / metadata / data:** the house UI face **Segoe UI** (already the HUD token), tabular numerals for counts/times.
- **Build note:** the app currently bundles Fraunces (editorial serif) as `theme.display`; the approved Home look is a *sans* display, not the serif. Implementation must either bundle Figtree (OFL, free) as a new display token (e.g. `theme.displaySans`) or confirm an already-loaded substitute. This is the one new dependency the redesign introduces — flagged for the plan.

### Cadence (from AF2 `Includes_Constants.xml`, scaled from its 1080 grid)

- Page gutter `view_pad = 80` → our page margin token.
- Header band `view_top = 180` → the top-menu + hero vertical origin.
- Widget row `view_row = 510` → the landscape-rail height rhythm; every rail obeys one cadence.

## Components

Each is a small unit with one job, wired through existing signals.

1. **WorldsBar (top menu).** Glass pill bar: `Home · Tankoban · Theatre · Biblio`. Home selected on the front door; tapping a world routes to that world page (existing `worldStack` routing). Search + wallpaper actions on the right. **Recedes** (fades + lifts slightly) as the board scrolls up.
2. **Spotlight (the hero / signature).** The featured title, full-bleed art behind. Shows the **Cinemeta/metahub logo** (derived from the imdb id — same path just proven in the player loader) with a text-title fallback; an eyebrow ("Featured · across your worlds"); a fact line; a **primary Play** (gold-free white, gold reserved for progress) and secondary actions that adapt to the title's worlds (e.g. *Watch* + *Read* when a title lives in both Theatre and Tankoban); a resume/progress line. **Recedes** on scroll: lifts and fades as the board rises.
3. **GlassBoard.** A frosted glass sheet (backdrop blur + glass tint + top edge highlight) that starts ~one viewport down and **rises over the persistent wallpaper**. Holds the rails.
4. **Rail** (reused for every row). A section header (optional world tag + title + "See all") over a horizontally-scrolling track on the `view_row` cadence. Variants by card shape.
5. **Card** (three shapes, one glass frame):
   - **Landscape** (video/Theatre + Continue) — edge-lit glass thumb, optional world tag, gold progress bar.
   - **Portrait** (Tankoban volumes) — cover art, narrower.
   - **Jacket** (Biblio) — a **typographic book jacket** (colored spine + title/author), because our book lanes (LibGen/OceanofPDF) have no clean cover-art CDN. This is the deliberate Biblio card look, not a placeholder.

## Content — all three worlds on one surface

- **Continue** (one rail, spanning worlds): the native Continue/Progress store across Theatre, Tankoban, and Biblio, newest-touched first, each card **world-tagged**. This is what makes Home a unified front door, not a Theatre page.
- **Theatre · Featured this week** — landscape rail from the Theatre world's existing featured source.
- **Tankoban · New volumes** — portrait rail from the manga volume index.
- **Biblio · On your shelf** — jacket rail from the Biblio collection/reading state.
- **Featured hero pick:** the **most-recently-touched title across all worlds**, so the primary action resumes exactly what you were mid-way through. It bridges worlds when the title exists in more than one (One Piece → Watch + Read). A curated/rotating featured can layer on later — out of scope here.

## Motion — the recede

Scroll-driven, cheap, and reduced-motion-aware:
- `0 → ~0.8 viewport`: the spotlight translates up (~170px) and fades to 0; the WorldsBar fades and lifts slightly; the wallpaper parallaxes down a touch and the GlassBoard rises to eclipse them.
- Past that: normal rail scrolling under the (receded) hero.
- `prefers-reduced-motion`: the hero simply cross-fades out as the board reaches it — no parallax.

## Data flow

`Main.qml` already owns the Home surface, `worldStack` routing, wallpaper, and a Continue widget. The redesign:
- Replaces the current Home body with WorldsBar + Spotlight + GlassBoard.
- Feeds Continue from the existing cross-world Progress/Collection store (no new backend).
- Feeds each world rail from that world's existing data source (Theatre featured / manga volume index / Biblio shelf) — read-only, no new pipelines.
- Derives the hero logo from the imdb id via metahub (existing helper), art from our lanes (AniList/MAL/Cinemeta/metahub/Wallhaven). No C++ change required.

## Constraints

- Composition/cadence/motion only — never AF2 files, never TMDbHelper/fanart.tv.
- Glass over wallpaper; the board must remain translucent (art visible behind).
- Gold only on progress + the primary action; per-world tint stays a faint cue.
- QML paints, C++ decides: no raw network on the GUI thread; reuse existing stores/lanes.
- Keep the existing world routing and keep-alive world pages intact.

## Acceptance criteria

1. Home shows a featured spotlight that **recedes** (lifts + fades) as the glass board rises on scroll; reduced-motion cross-fades instead.
2. The board is **frosted glass over the persistent wallpaper** — art stays visible behind it, never flat black.
3. Home carries **all three worlds**: a world-tagged Continue rail spanning them, plus a Theatre, a Tankoban, and a Biblio rail.
4. Rails obey one `view_row` cadence with `view_pad` gutters; cards use the correct shape per medium (landscape / portrait / jacket).
5. The hero shows the metahub-derived logo with a clean text fallback, and offers world-appropriate actions (Watch and/or Read).
6. Ink ramp + gold discipline hold: gold appears only on progress and the primary action; per-world tint is a faint cue only.
7. Tapping a world in the top bar routes to that world page; existing routing and keep-alive pages are unbroken.
8. No new networking on the GUI thread; content comes from existing stores/lanes; no TMDbHelper/fanart.tv.

## Out of scope

- The other AF2 backlog surfaces (Library walls, Search, PiP, VideoOSD) — separate audits.
- Any change to the world pages themselves (this is the Home shell only).
- New art pipelines for book covers — Biblio uses the typographic jacket by design.
