# Vinyl Home / Discover fixture prototype
Open **qml/vinyl/Preview.qml** in the established QML preview workflow (Qt6.11.1). It opens the world immediately. VinylPagesPrototype.qml remains the original backward-compatible album-first host; use its World developer button to enter the world, or supply page="world" on construction.
The host, original song/album fixtures and new catalogue fixture files must remain in their repository-relative locations. No network is used. Qt Quick, Controls, Effects, SVG/image plugins and the bundled Fraunces font are required. RoundedPosterImage uses the repository's shipped roundedposter.frag.qsb shader unchanged.

## Owners
- VinylWorldData: catalogue, profile, picker answers/seeds, lens, cached shelves, Home rows and world actions
- VinylWorldLists.js: deterministic fixture projections; no production rank/recommendation claim
- VinylPageData: existing album/song/playback simulation, plus snapshot album loading and simulated mix/station sequence
- VinylPagesPrototype: only owner that knows both; world scroll and route return positions live here/in VinylWorld's presentation state
- VinylWorld consumes a host-supplied Flickable. It owns no Window, wallpaper, top bar or irreplaceable page scroller
- Shared carousel, tabs, header, rounded-art, row index, Theme and Glass are unchanged

## Actions
setLens(key,value), setKind(kind), setProfile(new|skipped|seasoned), answerPicker(artistIds), skipPicker(), resetPicker(), setCatalogueReachable(bool), activate(item), openList(key,home), resume().
Catalogue and artworkRoot are injected. World item records include stable id, albumId, kind, title, subtitle, cover, aspect, round, and optional reason/reasonSubjectId/reasonSubjectType. Artist activation is intentionally a no-op.
loadAlbum(snapshotAlbum,resolvedCoverUrl) changes the inspected album only; it does not replace the playing edition. Host handles song-card activation explicitly. Mix/station clocks are simulated, never real playback.
All Home recommendation reasons are tied to supplied history/saved items/picks. No cold charts coexist with personal rows. The lens is held for the running session, and refused changes while offline leave cached shelves intact.

## Prototype rulings / limitations
- A single shared lens instance remains above Discover and its See-all route, avoiding duplicate automation identities
- Session Up next is text in the existing FeaturedCarousel blurb. Its public API has no arbitrary queue-panel slot; no shared component was forked to recreate the mock's superseded transport/queue stage
- New releases use a stated fixture cutoff of snapshot year minus2, not a production formula. “New from artists you play” is absent when selected artists have no qualifying release; this follows the spec's honest-empty rule rather than inventing a new release to satisfy the plan's blanket example
- Home recent items and recommendations are fixture projections. The backend's evidence ranking and seed decay are not implemented here
- people alias conflicts are preserved in the input; exact/canonical-normalized names win, ambiguous aliases are left separate rather than silently merging unrelated identities
- Artist imagery is representative album art, disclosed in the fixture README
- Original prototype's fixed quality chip, lyric/shape limitations remain receiving-review notes outside this world slice
- Hemanth broadly approved the cloud QML preview appearance on 2026-10-07 at 21:57 UTC: “I’ve seen a lot of the QML previews from your cloud desktop they look perfect to me” (Sentinel_eb6ca3acc74c8191af93b8b7aa6a6ac4). This is not an explicit verdict on every acceptance item or per-slice walkthrough. Complete keyboard reach, real-app integration, Windows replay and adoption remain separate

## Receiving journeys
lanista session run tests/lanista_scenarios/vinyl_discover_journey.json --qml qml/vinyl/VinylPagesPrototype.qml --tag vinylworldD --drive
lanista session run tests/lanista_scenarios/vinyl_home_journey.json --qml qml/vinyl/VinylPagesPrototype.qml --tag vinylworldH --drive
Both explicitly enter World via the developer strip and make no bridge window-size claim. They are supplied for receiving execution, not represented as executed here.
