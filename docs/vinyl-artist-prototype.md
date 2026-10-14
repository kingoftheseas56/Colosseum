# Vinyl artist page prototype

This is a reviewable QML design prototype, not production artist integration.
Launch `qml/vinyl/ArtistPreview.qml` with the Qt 6 QML runner and the Basic
Controls style. Ctrl+D exposes the existing world/album/song preview controls.
Artist cards, album credits and the song's artist label open the artist surface.

## Design

Eminem is the default artist. The first viewport pairs artist identity and a
short blurb with five selected songs. Albums, singles/EPs, session playlists,
biography, facts, career milestones and shared credits fill the scroll surface.
Section links jump to the corresponding content. Album artwork supplies the
backdrop; there is no separate portrait or invented follower/popularity count.

The information structure draws from [Spotify's artist profile](https://artists.spotify.com/en/blog/making-the-most-of-your-artist-profile-on-spotify)
and [YouTube Music's artist sections](https://ytmusicapi.readthedocs.io/en/stable/reference/browsing.html).
Harbor is a reference for information density, not a literal page template.

Existing Theme tokens remain authoritative: gold `#f0c44a`, ink `#f7f7f5`, muted
ink `#c9c8d0`, secondary ink `#9a99a5`, background `#06070b`. Fraunces identifies
the artist and sections; Segoe UI carries controls and metadata. Blurred album
art supplies the atmosphere. Text sits on the backdrop. Transport remains in
the bottom bar, with no hero Play/Shuffle controls.

The initial layout uses six album columns on larger windows and four at 960
pixels. Missing sections are omitted. A single-release artist retains the same
hierarchy without fabricated recommendations. Separate names in a combined
artist credit open a chooser instead of creating a composite artist identity.

## Behaviour and data boundaries

- The frozen `vinyl-world` catalogue supplies albums, tracks and artwork. Counts
  describe this fixture collection, not an artist's complete catalogue.
- Exact album credits and exact track credits drive the two discography views.
  Shared credits are names appearing on the selected songs, not a recommendation
  or relationship-ranking service. Songs are not labelled as popular.
- A narrow, explicit spelling map unifies known fixture variants. The old
  `people` alias map incorrectly includes unrelated people and is not used.
  This adapter is not a canonical identity system.
- Browsing an artist or album preserves playing state. Selecting a song invokes
  the existing prototype player state. Back restores the prior artist filters,
  scroll position and browsed album. It does not resume an old playing track.
- Save artist and custom playlists are session-only demonstration state.
  Create playlist starts with the displayed songs; opening a playlist exposes
  its tracks, whose selection uses the same prototype playback path. No new store is created.
  The prototype simulates transport; it does not fetch or decode live audio.
- Production navigation and the opt-in live Vinyl session are not connected to
  this surface. The optional song artist link defaults off outside the preview.

YouTube Music remains the primary, nonexclusive catalogue. Torrent playback
remains preferred, with YouTube as the inexpensive fallback. Neither provider
defines this page's navigation or search wording.

## Review and integration follow-through

Selected songs are an editorial fixture order, not live charts. Eminem's
additional releases and the Ilaiyaraaja singles are a public, account-free
YouTube Music snapshot from 2026-10-14 (IN, ytmusicapi 1.12.3); each record
retains a source URL, exact release ID, type and track credits. This is a
bounded selection, not a complete discography. New thumbnails load directly
from provider URLs and need network access; existing artwork remains local.
Short authored biography summaries and milestones link to Wikipedia/Wikidata
in the page and in `ArtistContext.js`. Genius documentation returned HTTP 403
during this pass. No Genius metadata or lyrics are presented as retrieved;
verified song-level enrichment remains an integration gap.

Before production integration: settle this layout with Hemanth/Claude; resolve
artists through provider-qualified IDs into Colosseum's existing identity owner;
add real paginated artist releases and track credits; carry explicit composer,
singer and lyricist roles when supplied; use existing collection ownership for
saved artists; and connect song selection to the existing torrent-first session.
Distinguish loading, cached and failed requests without inventing catalogue data.
Live enrichment, videos and recommendations require matched, sourced content before appearing.

## Verification

`tests/qml/tst_vinyl_artist_page.qml` covers identity separation, world/credit
routes, nested back navigation, preservation of playing state, song selection,
keyboard sort/save, appearance filtering, multiple-name selection and thin data.
It also checks Eminem releases, singles navigation, biography section jumping,
playlist creation and per-artist playlist isolation.
The existing Vinyl album, song and page-data suites exercise shared behaviour.

Set `VINYL_CAPTURE_DIR` when running the artist suite through
`vinyl_qml_regression` to save review images at 1280x720, 1600x900 and 960x720
logical pixels. Capture windows are frameless to avoid Windows' decorated-window
size clamp; files retain the display's physical pixel density. Captures wait for
the shared poster component's decoded artwork and reveal animation.
