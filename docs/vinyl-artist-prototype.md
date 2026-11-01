# Vinyl artist page prototype

This is a reviewable QML design prototype, not production artist integration.
Launch `qml/vinyl/ArtistPreview.qml` with the Qt 6 QML runner and the Basic
Controls style. Ctrl+D exposes the existing world/album/song preview controls.
Artist cards, album credits and the song's artist label open the artist surface.

## Design

Eminem is the default artist. The overview consists solely of the artist banner:
a sourced portrait when available, album-art fallback, identity, sourced blurb and
session-only save/create-playlist controls. The blurred album-art atmosphere is
retained. Nothing in the overview fabricates audience, play or ranking metrics.

Immediately beneath the banner, the existing Colosseum `RowIndexSidebar` anchors
five single-category sections: Popular songs (or simply Songs if no provider
song shelf can be corroborated), Albums, Singles & EPs, Videos and Related artists.
Its collapsed/expanded state and active-row indication use the same component as
the other worlds. Sections without records, and their sidebar links, disappear.

The separately rendered About, `From this collection`, Shared credits, library,
live, featured-playlist and artist-playlist sections have been removed from the
artist page. Source biography stays in the banner; catalogue-generated
`releaseHistory` stays in the data projection for future use, not in the UI.

The information structure draws from [Spotify's artist profile](https://artists.spotify.com/en/blog/making-the-most-of-your-artist-profile-on-spotify)
and [YouTube Music's artist sections](https://ytmusicapi.readthedocs.io/en/stable/reference/browsing.html).
Harbor is a reference for information density, not a literal page template.

Existing Theme tokens remain authoritative: gold `#f0c44a`, ink `#f7f7f5`, muted
ink `#c9c8d0`, secondary ink `#9a99a5`, background `#06070b`. Fraunces identifies
the artist and sections; Segoe UI carries controls and metadata. Blurred album
art supplies the atmosphere. Text sits on the backdrop. Transport remains in
the bottom bar, with no hero Play/Shuffle controls.

`VinylArtistHero`, `VinylArtistSongs`, `VinylArtistShelf` and
`VinylArtistFullCategory` remain presentation over the existing data owners.
Only the song list borrows YouTube Music's track-and-album row layout. Other
categories use Colosseum's classic `WidgetHeader` plus horizontal `Flickable`
rails, shared poster primitives and source-aware tiles. Every section has a
native `See all` door, including thin sections: it opens a scrollable,
full-category list/grid and Back restores the overview's prior vertical position.
The sidebar's expanded state reduces content width without covering shelves.
Keyboard focus reveals offscreen controls; Up/Down and Page Up/Down scroll.
A one-release artist keeps the same hierarchy without invented recommendations.
Separate names in a combined credit still open the existing artist chooser.

## Behaviour and data boundaries

- The frozen `vinyl-world` catalogue supplies albums, tracks and artwork. Counts
  describe this fixture collection, not an artist's complete catalogue.
- Exact album credits and exact track credits drive the two discography views.
  The Popular songs row is sourced from the ordered YouTube Music artist-song
  snapshot and restricted to exact-titled tracks also present in the artist's
  credited catalogue; no local play counts or rankings are inferred.
  Artists without matching provider records get an unranked Songs row instead.
  `See all` for songs opens the complete unranked local catalogue song list.
- A narrow, explicit spelling map unifies known fixture variants. The old
  `people` alias map incorrectly includes unrelated people and is not used.
  This adapter is not a canonical identity system.
- Browsing an artist or album preserves playing state. Selecting a song invokes
  the existing prototype player state. Back restores the prior artist filters,
  scroll position and browsed album. It does not resume an old playing track.
- Save artist and custom playlists are session-only demonstration state.
  Create playlist starts with the displayed songs; opening a playlist exposes
  its tracks, whose selection uses the same prototype playback path. No new store is created.
  There is no automatically generated sampler playlist or fabricated curation.
  The prototype simulates transport; it does not fetch or decode live audio.
- The existing library release projection remains available in `VinylArtistData`,
  but it does not create a library section in this narrowed artist layout.
- Video cards open native source-details dialogs. An explicit source action opens
  the browser and never silently substitutes audio or claims video playback.
  Related artists use the existing Colosseum artist navigation. Returning from
  a related artist also restores a previous full-category view, if one was open.
- Production navigation and the opt-in live Vinyl session are not connected to
  this surface. The optional song artist link defaults off outside the preview.

YouTube Music remains the primary, nonexclusive catalogue. Torrent playback
remains preferred, with YouTube as the inexpensive fallback. Neither provider
defines this page's navigation or search wording.

## Review and integration follow-through

The account-free YouTube Music snapshot from 2026-10-14 (IN,
ytmusicapi 1.12.3) supplies bounded, source-linked releases, five ordered
artist songs, six videos and six related artists each for Eminem and
Ilaiyaraaja. `ArtistMedia.js` retains exact provider IDs/URLs for the ordered
songs, and `VinylArtistCatalogue.sourcedPopularSongs` resolves only exact song
title matches to Colosseum's existing playable catalogue records. Other
catalogue songs are never silently promoted into the Popular songs row.
No live-performance or provider-playlist section is shown on this page.

The artist artwork and existing remote release/single thumbnails are now frozen
locally for repeatable, network-independent preview captures.
`tests/fixtures/vinyl-world/artist-media/sources.json` records each original public
thumbnail URL, byte count and SHA-256. Catalogue source URLs, release IDs, dates,
tracks and the release-history algorithm are unchanged. These are publisher
images retained as prototype fixtures, not AI-generated replacement artwork.
Short authored biography summaries remain sourced in `ArtistContext.js`.
The release-history cross-section continues to be generated from the same
dated catalogue records, with no invented milestone prose, but it is **not
displayed** after the 2026-10-14 layout correction. No `From this collection`
or Shared credits section is rendered.
The earlier enrichment research recorded HTTP 403 from Genius documentation. No Genius metadata or lyrics are presented as retrieved;
verified song-level enrichment remains an integration gap.

Before production integration: confirm the final layout with Hemanth, then resolve
artists through provider-qualified IDs into Colosseum's existing identity owner;
add real paginated artist releases and track credits; carry explicit composer,
singer and lyricist roles when supplied; use existing collection ownership for
saved artists; and connect song selection to the existing torrent-first session.
Distinguish loading, cached and failed requests without inventing catalogue data.
Live refresh, in-app video playback and provider playlist playback remain integration
gaps. Do not derive live performances from fuzzy title matching or manufacture
artist-owned playlists, audience metrics or personalized recommendations.
The read-only Harness map inspection reports BRANCH_DRIFT against this branch;
these targeted Qt tests are not a production Harness completion certificate.

## Verification

`tests/qml/tst_vinyl_artist_page.qml` covers identity separation, world/credit
routes, nested back navigation, preservation of playing state, song selection,
keyboard sort/save, appearance filtering, multiple-name selection and thin data.
It also checks the retained generic release-history **projection** without
rendering its former cards, albums/singles routes, playlist creation and
per-artist session state. New cases check the classic row sidebar, absence of
removed sections, sourced Popular songs and unranked fallback, all five See-all
doors, return-position preservation, horizontal rail isolation, related-artist
nested Back, pointer controls, keyboard scroll/focus and provenance rejection.
The existing Vinyl album, song and page-data suites exercise shared behaviour.

Set `VINYL_CAPTURE_DIR` when running the artist suite through
`vinyl_qml_regression` to save review images at 1280x720, 1600x900 and 960x720
logical pixels. Capture windows are frameless to avoid Windows' decorated-window
size clamp; files retain the display's physical pixel density. Captures wait for
the shared poster component's decoded artwork and reveal animation.
