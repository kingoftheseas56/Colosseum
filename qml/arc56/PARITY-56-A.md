# Arc 56-A Harbor Detail Parity Ledger

Status: **PARITY BASELINE ESTABLISHED / FINAL SIGN-OFF PENDING SWARM AUDITS**

## Authorities

- Colosseum Arc 56 branch baseline: `arc56-a-harbor-detail-qml`.
- Harbor oracle commit: `0117755855d3f43960bad3f9f62b69ef851d5991`.
- Harbor donor root: `src/views/detail.tsx`.
- Arc 56 is direct React -> QML. Arc 54's web seam is not part of this prototype.

## Deterministic comparison setup

A local Harbor development checkout at the pinned commit was rendered with deterministic mocked Cinemeta/TMDB responses for:

- The Matrix (`tt0133093`)
- Game of Thrones (`tt0944947`)

No Harbor source files were copied into Colosseum for the oracle. The mocks existed only outside the repository for comparison.

Viewport: **1440 x 900**.

Harbor default sidebar occupied 72 px, leaving a 1368 px detail viewport. Arc 56 renders the detail surface alone, so comparisons use coordinates relative to the detail viewport.

Measured Harbor geometry:

- hero: 78vh = 702 px at 900 px viewport height;
- detail horizontal inset: 48 px;
- content width: detail width - 96 px;
- TV episode section: relative x = 48 px;
- first list episode: 1272 px wide in the 1368 px Harbor detail viewport;
- first episode artwork: 200 x 112.5 px;
- hero action height: 48 px;
- episode list card radius: 16 px;
- poster rail default gap: 20 px;
- media rail gap: 16 px.

## Shared Movie / TV parity

### Matched or intentionally translated

- 78vh hero with 640 px minimum height.
- Full-bleed backdrop layer.
- Harbor top + side hero gradients.
- 48 px hero/detail horizontal insets.
- tagline geometry and uppercase tracking.
- Harbor TitlePlate logo bounds: 440 x 124 maximum.
- 80 px text-title fallback.
- metadata pill flow.
- IMDb-style primary rating pill.
- runtime and genre pills.
- 48 px Play / Watchlist action row.
- Favorite / Add to list / trailer circular actions.
- Movie-only watched and download actions.
- hero award-corner placement.
- synopsis below hero.
- Watch On section.
- Customize layout shell control.
- back-to-top affordance.
- Crew grid.
- Cast rail.
- Collection / More Like This / Similar rails.
- Media section.
- Awards & Recognition section.
- Information section.

## Movie-specific parity

Current fixture follows Harbor's The Matrix shape:

- year: 1999;
- runtime: 136 min;
- genres: Action / Science Fiction;
- The Matrix Collection label;
- movie-only watched + download hero actions;
- Collection rail present.

Harbor's collection heading uses the collection name plus a chevron. Arc 56 now mirrors that presentation.

## TV-specific parity

Current Game of Thrones fixture follows Harbor's own TMDB-derived labels:

- year pill: 2011;
- runtime pill: 57 min episodes;
- genres: Action & Adventure / Drama / Sci-Fi & Fantasy;
- eight-season selector;
- episode list / strip / grid modes;
- random episode;
- sort;
- mark-season watched control;
- search;
- watched/progress treatment.

## Episode row geometry

Harbor list-row baseline:

- 200 x 112.5 px art;
- number badge top-left;
- rating badge bottom-left;
- hover play affordance;
- title/meta/overview copy;
- detail action centered at row end;
- download action at upper end;
- 152.5 px approximate list-row height in the oracle fixture.

Arc 56 deliberately extends the list row to make room for the user-requested statistics line.

### Deliberate Arc 56 extension

Every episode displays a statistics strip containing:

- rating;
- votes;
- runtime;
- aired date;
- progress / watched state.

This is not stock Harbor behavior and therefore is excluded from pixel-height parity.

## Harbor-specific visual tokens

Arc 56 uses a local Harbor token component rather than Colosseum Theme for this prototype:

- canvas: #111213
- surface: #191b1c
- elevated: #252628
- raised: #323335
- ink: #f4f5f7
- muted ink: #a3a5a6
- accent: #f4a25c
- Switzer UI face where bundled
- Harbor display fallback behavior for the title layer

This prevents Colosseum chrome/theme choices from contaminating a Harbor parity judgment.

## Static-data / no-API rule

Arc 56-A makes no XHR/fetch/provider API calls.

The fixture supplies metadata and episode statistics. Remote image URLs are static artwork resources only.

## Current proof

- `qmllint` exits 0 for the canonical Arc 56 QML files.
- Movie mode launches under Qt 6.11.1.
- TV mode launches under Qt 6.11.1.
- Movie and TV viewport captures were compared against pinned Harbor renders.
- The visible QML scrollbar was removed because it is not part of Harbor's detail presentation.
- Movie collection naming and cast fallback plates were corrected from the comparison.
- Add-to-list and trailer hero icons were corrected from the comparison.

## Known items still awaiting independent sign-off

These are not being called complete until the corresponding swarm evidence returns:

1. exact hero-award laurel/logo drawing;
2. Media Gallery hover/action micro-interactions;
3. episode toolbar/list/strip/grid micro-parity;
4. keyboard/focus-only traversal;
5. responsive parity across the requested viewport matrix;
6. exact lower-rail hover/focus behavior;
7. full Customize Layout reorder/hide behavior;
8. native-host artwork proof using Colosseum's networking path.

## Scope boundary

56-A remains an isolated prototype.

It does not replace production Theatre QML, alter Main.qml, add native C++ seams, or depend on Arc 54.
