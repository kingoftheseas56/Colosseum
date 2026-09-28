# Arc 56-A — Harbor detail QML prototype

This folder is the isolated Arc 56-A prototype for translating Harbor's movie and TV detail presentation directly from React into Colosseum QML.

## Authorities

- Colosseum implementation basis: `origin/master` at `129a9efb5de5bc0ada8ab59ff1ade4b41927bec4`.
- Harbor visual/interaction oracle: `harborstremio/harbor` at `0117755855d3f43960bad3f9f62b69ef851d5991`.
- Harbor donor surfaces: `src/views/detail.tsx`, `detail/title-plate.tsx`, `detail/hero-backdrop.tsx`, `detail/hero-ratings.tsx`, `detail/watch-on.tsx`, `detail/series-episodes.tsx`, and `detail/series-episode-row.tsx`.

The prototype does not depend on Arc 54's web UI seam and does not change Colosseum's production detail route.

## Files

- `HarborDetailPrototype.qml` — standalone switchable Movie/TV host.
- `HarborDetailView.qml` — shared detail composition.
- `HarborHero.qml` — Harbor-style backdrop, title plate, pills, and hero actions.
- `HarborEpisodeBrowser.qml` — TV season controls plus list, strip, and grid episode layouts.
- `HarborEpisodeRow.qml` — Harbor-style list row.
- `HarborEpisodeStats.qml` — Arc 56 extension: statistics beneath every episode.
- `HarborPosterRail.qml` — collection/related/similar rails.
- `HarborPill.qml`, `HarborActionButton.qml`, `HarborIcon.qml` — shared primitives.
- `HarborDetailFixtures.js` — static Movie/TV data for the prototype. It performs no API requests.

## Scope boundary

Arc 56-A proves presentation and local interaction only. It intentionally does not replace `TheatreSeries.qml`, alter `Main.qml`, add a native seam, or integrate Harbor's APIs/data architecture.

Colosseum's current QML/C++ remains the application authority for playback, Collection, Progress, downloads, sources, identity, and routing. Production integration is a later Arc 56 package after the QML prototype is accepted.

The statistics row is an intentional Colosseum extension beyond Harbor: IMDb-style score, vote count, runtime, air date, and watched/progress state are shown beneath each episode when the prototype fixture supplies them.

## Prototype controls

- `1` — Movie fixture.
- `2` — TV fixture.
- `--movie` / `--series` — choose the initial fixture.
- `--capture=<path>` — capture the current viewport after startup, then exit.

All metadata is static fixture data. Poster, backdrop, logo, and episode artwork URLs are static image resources only; there are no XHR/fetch/API calls in this prototype.
