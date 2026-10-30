# Exact route coverage

The collection gateway stores canonical identity, source provenance, and edition separately. It never searches by display title to reopen a saved item. Existing persisted references are schema-loaded without current provider validation, so unavailable sources retain their membership, order, note, and display snapshot.

Vinyl albums and album-backed songs reopen through the source owner's `album` request, with exact album, edition, and track checks. Local references use the actual imported `vinyl:local-*` album/edition IDs and `vault:*` track IDs. Online release references retain the actual provider album browse key (or `ytm:album:` wrapper supported by `VinylCatalogueSource`). Artist references require a real local artist ID or exact `ytm:artist:UC…` channel ID.

The following live song presentations do not expose an existing cold album reopen route and are visibly ineligible for a new membership:

- Standalone search/discovery songs whose album snapshot has `browseId = ytm:track:<video-id>`.
- Standalone recording-match snapshots whose `browseId` is their canonical recording ID.
- Playlist-backed songs whose source snapshot has `browseId = ytm:playlist:<playlist-id>`.

`scripts/vinyl/provider.py` builds these distinct snapshot types, but its existing `album` operation accepts only a provider release key and calls `get_album`. The gateway does not present these IDs as album releases or invent a song lookup. The disabled Add action explains the missing route. Previously saved unsupported references remain visible and report an unavailable route on explicit open.

Theatre provider doors wait for the completed exact `resolvedId` before admission; a timeout with `loading = false` is insufficient. Direct IMDb doors are already qualified. Reopening uses the saved canonical anchor and retains the original provider door separately. Provider remapping or removal cannot replace the saved title, and mismatched canonical metadata reports unavailability. GCD collection opens use an explicit strict branch, including a second availability check at dispatch, and never fall through to the legacy title-search route.

Vault collections admit identified film groups from the native `video` detail projection, not episode/clip leaf records. Reopening checks the saved location and exact IMDb identity through VaultLibrary twice (before navigation and after loader creation). A Windows-only case-only path recovery consults the native video groups, rejects ambiguity, and still requires the exact stored IMDb identity; no title search or alternative copy substitution occurs. A missing or changed identity remains in the collection with a reconnect explanation.

The legacy WeebCentral owner renders MangaReadingRoomThumbnailMock in the current shell. Its Add action is in that instantiated component; the unused legacy corridor is not counted as coverage. The modern MAL view's shared title header remains the action owner for its chapter/tankobon presentation.

Scope limits: unqualified Vault files/show/season/episode/clip identities, name-only music artists, standalone or playlist-backed song snapshots without an existing exact album reopening route, and presentation objects outside title-detail owners are not silently admitted. Android/Linux and network/provider playback are separate qualification gates; this Windows feature does not claim them.
