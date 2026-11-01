# Vinyl: functioning foundation first

Date: 30 October 2026.

This is a product-scope note and V2 deferral record, not an implementation plan or a claim that the proposed experience is implemented. Hemanth's direction is to finish a usable end-to-end music mode with basic versions of the useful capabilities, then return to enrichment. The specific starter mix and guide selections below are recommendations for that foundation.

## Current direction and corrections

- Five distinct saved artist choices are required before access to normal Vinyl routes. The gate applies across Home, Explore, Discover, Library and direct navigation; it has no Skip bypass. Existing preferences and drafts must be preserved.
- Keep YouTube Music as the primary catalogue, Colosseum's existing stores/history/identity owners, and the existing session/player/queue.
- Home is personal listening and recommendations. Saved music, user-authored playlists and collection management belong in Library, including the rejected Your music / Evening rotation shelf.
- Explore should reuse the other worlds' browsing experience: sidebars, entity selection, filter picker, sorting, result walls, pagination and navigation conventions, adapted to music capabilities. It is a neutral catalogue browser, not a feed of regional charts or recommendation shelves.
- Discover is the fourth-tab direction for deliberately curated album lists. No automatic Top in your region, Trending, Popular in your country or location-driven chart rows belong in Discover or Explore. The four-surface split is Home / Explore / Discover / Library; final presentation remains to be reviewed.
- A listener's device location or the provider's catalogue market is not their musical taste. Home personalization must come from explicit artist choices and qualified profile signals. Region is an explicit user choice where supported, not an automatically applied taste filter. This does not exclude music from any region when a user's tastes, selected filters or a deliberate curated guide warrant it.
- Keep the shared wallpaper, house QML components, icon actions and top-level search entry. Preserve the existing ban on a separate Home search bar, hover descriptors and prototype explanation paragraphs.
- The five-artist setup has its own dedicated artist search field. This is an onboarding exception; after admission, ordinary Vinyl search uses the top-left entry.
- Rich recommendation tuning, large-scale metadata/relationship enrichment and the expanded feature menu move to the [V2 backlog](2026-10-30-vinyl-v2-feature-backlog.md). Correct artist/album/track identity and playable destinations remain foundation requirements.

## Relationship to the existing plan

The [development roadmap](2026-10-20-vinyl-development-roadmap.md), [personalized Home plan](2026-10-22-vinyl-personalized-home.md) and [Home design](2026-10-22-vinyl-personalized-home-design.md) already cover seed-based recommendations, native ownership, qualified history, Home rows, navigation, playlists and real player integration.

Some older text is superseded by later user decisions: optional onboarding, Skip, no minimum artist count, access to other Vinyl surfaces before onboarding, and saved-playlist shelves on Home. The 22 October design also explicitly excluded radio/autoplay; the umbrella roadmap records radio/refill as remaining M7R work. A ranker, a candidate enum or a static playlist labelled Radio is not a working radio implementation.

Read-only source inspection on 30 October found that VinylHomePage consumes existing Home rows and loading/error states, while VinylLiveWorldData still builds carousel slides from the current session and a local-album fallback. VinylDiscoverPage has a row-index sidebar, not proof of parity with the other worlds' catalogue/filter browser. These are seams to extend, not evidence that the new foundation is finished. No build or runtime verification was performed for this scope note.

### Regional content diagnosis and required correction

The current [artist suggestion request](../../native/vinyl/VinylRecommendationService.cpp) explicitly supplies region IN for an empty artist query. The [catalogue transport](../../scripts/vinyl/provider.py) also defaults its region/location to IN, and [live discovery presentation](../../qml/vinyl/VinylLiveWorldData.qml) builds New releases, Trending, Popular artists and Charts landing rows. These are source-level routes for unsolicited regional content; this inspection did not attribute every displayed recording to a particular runtime response. The disposable Home mock also used Indian artists as illustrative sample content, which must not become production taste evidence.

The foundation must replace the automatic chart/feed landing behavior with the catalogue/curation split above, remove hardcoded region selection as a taste default, and ensure stale regional shelf data is not reintroduced as fallback content. Do not solve this by swapping India for another forced country. Any market required for catalogue availability remains distinct from recommendation affinity and explicit music-region filtering. Source correction and real-app verification remain implementation work.

## Recommended Home foundation

### Carousel

Reuse the existing carousel with up to five manually navigated spotlights. Lead with an eligible unfinished session within 72 hours of actual playback; otherwise use a relevant release or discovery. Fill other positions from real artist/release/discovery candidates. Keep the daily selection, explicit refresh and back-navigation stability already discussed. Deduplicate the selection and preserve playback during refresh. Do not manufacture five items when candidates are sparse. Richer paired presentation and multi-window exposure/cooldown tuning can follow in V2; no new cooldown duration is set here.

### Everyday listening

- Quick Picks: a small set of playable songs drawn from the existing recommendation/history pools.
- From your artists: albums/releases from the saved artist choices and supported affinities.
- Discover something new: existing verified adjacent artists/albums, with discovery open beyond the original five choices.
- New releases only where trustworthy dates are present. Unknown dates must not produce a New label.
- Keep existing history-based rediscovery/less-played behavior when its supporting history is available; hide unsupported or empty rows.

### Basic finite mixes and radio

Begin with two finite generated mix intents: a personal mix from the available taste-related pool, and a discovery mix emphasizing available adjacent/unfamiliar candidates. Each must produce a real track list and use the existing player/queue. Saving a selection belongs to Library. Do not promise activity, mood, tempo, global popularity or language tuning before the relevant data is qualified.

For the smallest continuous-radio proof, support an artist-seeded session. It must genuinely continue by obtaining another valid batch when needed, handle exhaustion/failure honestly, and preserve explicit user queue actions. Song/album seeds and additional steering can extend that same behavior in V2. Do not present a one-off finite list as continuous radio. If this capability is not yet qualified, omit its entry until the real flow passes.

## Recommended Discover foundation

Start with a small maintained set of original Colosseum album guides. Use Hemanth's two examples as the initial recommended scope: Classic East Coast Hip-hop and 1980s Essentials. These are proposed themes, not completed editorial selections or album rosters. Further regional or scene guides are deliberate editorial choices; there is no default Indian/local-content quota and no location-based chart substitution.

Each guide needs only a title, stable identity/version, a reviewed ordered album selection, source/curator credit, and correct catalogue destinations. Use existing album cards/list/detail routes. The basic journey is guide -> albums -> album -> song/play/save/queue -> Back. Preserve the guide and scroll position on return.

Use MusicBrainz release-group IDs where verified and review the corresponding YouTube Music album/edition matches. The curation audit found useful complete specific series and incomplete other series; there is no qualified universal feed of ready-to-ship guides. Original Colosseum selections can be informed by documented references. This does not authorize copying another publisher's full ranked list, text or artwork.

Keep the editorial membership stable when personalization changes or a catalogue match fails. Personalization can surface a guide; it must not silently rewrite it. Mark an unavailable member honestly instead of substituting an unrelated album or presenting a broken Play action. A full editorial CMS, giant metadata mirror, automatic publisher-list ingestion, long essays and a comprehensive regional guide collection are not foundation prerequisites.

## Recommended Explore foundation

Reuse the existing common browsing/navigation patterns from Theatre, Biblio, comics and manga. Keep a music-specific source/category sidebar, house filter controls, album/artist/song result presentations, paging, loading/empty/error handling, and Back restoring search/filter/scroll state. Do not build another independent browsing UI or introduce another persistent search field.

- Entity selection: Artists, Albums and Songs, with other supported types added deliberately.
- Filters: artist scope, genre, region, release type, year/decade and other supported music facets. Default region is unselected/all; do not infer a preferred musical region from device or provider location. Genre and region constrain the results rather than acting as sorting algorithms.
- Sorting: popularity, newest/release year, title/alphabetical and search relevance where their inputs are genuinely available. A popularity-sorted result wall is still a catalogue query, not an injected Top 10 in your region shelf.
- Reuse the other worlds' sidebar and filter/sort behaviors, including persistent selections, paging and Back. Do not insert Trending/Charts/New in your country recommendation rows as the default Explore experience.

Enable a predicate or ordering only where the source or indexed dataset can honor it. Missing metadata must be honest; filtering a fetched page is not proof of a globally filtered catalogue, and provider result position is not automatically a verified popularity metric. Do not confuse artist/scene provenance with chart-market geography or rights availability. Add remaining dimensions from the audit as their data becomes dependable in V2.

## End-to-end completion bar

The foundation is complete only when the ordinary app can demonstrate:

1. First entry -> select at least five distinct real artists -> persist -> enter Vinyl; restart and direct routes respect the gate.
2. Home recommendation or mix -> correct Artist/Album/Song destination -> correct music starts through the existing player.
3. Discover guide -> intended album/edition -> play/save/queue -> Back restores position.
4. Explore sidebar/filter/query -> meaningful results -> detail -> playback -> Back restores state, including across paging.
5. Queue changes and player controls behave consistently across all these routes; browsing/refresh does not replace playback.
6. Saved items/playlists are available in Library and survive restart using existing owners.
7. Missing metadata, empty candidates, provider failure and offline/cache states remain usable and truthful; user profile/privacy boundaries are preserved.
8. A visible radio entry actually refills and handles candidate exhaustion. A static fixture or a successful source lookup alone cannot qualify it.
9. A fresh/default Explore or Discover visit does not inject location-driven charts or regional recommendation rows. Entity selection, filtering and sorting change the actual result set/order and survive Back; explicit regional music choices remain possible when supported.

This scope note neither closes those checks nor reclassifies historical test receipts as current runtime evidence. Existing working behavior should be reused and retained; the baseline does not require reimplementing already-owned stores, providers or pages.

## Research inputs and V2 continuity

The two Preflight research deliveries dated 30 October provide a 207-row personalization/discovery inventory, 57 filter dimensions, 16 source/access comparisons and 32 public retrieval probes. Their observations, historical/beta status, source limitations and missing signed-in/provider-runtime checks remain distinct from implementation evidence.

The [V2 backlog](2026-10-30-vinyl-v2-feature-backlog.md) retains the feature families and a complete row-level reference index from both audits. Existing lyrics/Genius/music-video/live planning remains a separate V2 lane; this note does not send instructions to or modify another chat's work.
