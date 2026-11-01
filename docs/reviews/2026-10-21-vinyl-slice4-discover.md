# Vinyl slice 4: public Discover

Source baseline: `c44696b72` on `codex/vinyl-production-flow`. This is branch qualification, not main-branch integration or a release.

## Implemented

- Empty-query Discover now loads real anonymous YouTube Music catalogue shelves: New releases, Trending, Chart artists and Charts. The bundled ytmusicapi 1.12.3 Explore/charts operations were proved with region `IN`; no user login or fabricated recommendation fixtures.
- Existing `VinylProvider` subprocess transport and metadata cache own acquisition; `VinylCatalogueSource` retains exact release, recording and playlist identities. Four independent request lanes bound failure and cancel on profile retirement/shutdown. Each discovery request has an 18-second transport deadline.
- Each shelf preserves source order, deduplicates stable identities and admits at most 40 items. Missing/invalid records, unavailable trending tracks and podcast entries are excluded. Envelopes identify source, region and fetch time and explicitly remain incomplete snapshots.
- Existing QML Discover/shelves/cards/sidebar, Album/Song/Artist routes and native player are reused. Public chart playlists use the existing collection-detail view with a Playlist label, separate from personal playlist IDs. Song cards browse without replacing playback; explicit Play uses the exact recording through the existing resolver.
- Public playlists require a known complete track count, preserve unavailable rows and reject truncated/unknown listings or more than 200 tracks. This does not pretend a partial playlist is complete.
- The existing cache serves discovery metadata immediately for 15 minutes, without extending freshness on read. Re-entering Discover after that window revalidates it. Cached results expose an icon-only refresh action; explicit refresh goes to the provider, and failures may use the existing cached metadata with an error/cache indication. Stream URLs and headers remain uncached.
- Loading/error/empty/cache states and an icon-only Retry keep successful shelves visible. Query/search state remains separate; clearing a query returns Discover without stale search notices. Unsupported album metadata filters/sort are hidden on the mixed public landing.

Luna xhigh supplied the bounded Discover UI adaptation, mocked provider contracts and native-bridge QML coverage. Root integrated provider/native/session/navigation changes and independently received builds, tests and actual Main flows.

## Verification

Release build targets passed: `vinyl_catalogue_source_test`, `vinyl_native_session_bridge`, `colosseum_runtime_ready`. Final QML manifest: `b45808a4a60cb4d8a8e3b4055810daef250114a1cc6f6d7a0f65a20b4937a11a`.

Six focused CTest registrations passed (31.28 seconds): native catalogue transport/facade; native personal-library contracts; LiveWorld, personal-library, Home and Discover native-bridge QML. The Python provider suite ran 37 cases: 36 passed and one pre-existing optional live Eminem smoke case skipped. The new discovery contract file contains 13 passing cases. Actual live acquisition/playback was qualified separately below.

Native checks cover independently routed shelves, exact-video resolution after discovery, public playlist dispatch, fresh cached reads, forced refresh fallback and cancellation. QML checks cover four typed shelves, malformed/oversized/stale replies, successful/prior shelf retention, query independence, profile clearing and See all/Back. Existing Home/Library suites remained green.

### Actual Colosseum Main

`tests/vinyl-live/DiscoverFlow.qml` instantiates the production Main with real profile stores, provider, native session and player. It invokes the shared tab/control handlers and captures the actual scene, using isolated app-data profiles.

The live flow:

1. Entered Discover from Home. Real IN responses populated 24 new releases, 17 admitted trending music entries, 40 chart artists and three public chart playlists. Counts/content vary upstream. Initial cold acquisition observed all four shelves ready in about 12.3 seconds; this is an observation, not a latency guarantee.
2. Opened the selected new release with a full track listing and returned to Discover.
3. Opened Alka Yagnik by the selected source identity. The provider retained `requestedChannelId` while resolving a different canonical channel ID for the same named artist; validation follows the requested identity, rather than incorrectly rejecting supported channel redirects.
4. Used Charts See all and Back, opened Trending 20 India with its complete 20-item listing and played its first recording through the existing player.
5. Opened that trending recording as Song without changing the queue, saved it through Collection, and explicitly played it as a single-item queue. Both playlist and Song playback used the same recording UUID `8bd90433-b35b-5dd3-a5ed-d93ea6401d8a` for **KAALI HOODIE (MUSIC VIDEO) SIDHU MOOSE WALA FT BOHEMIA | THE KIDD**, source **youtube**, with advancing playback above one second.
6. Searched the real catalogue for Nas; clearing the query restored the four public shelves.

Separate actual Main runs deliberately pointed only the catalogue Python runtime at a missing executable. With a fresh warm cache, all four shelves appeared in about 0.17 seconds; an expired-cache fallback run took about 5.29 seconds. Forced refresh retained cached content and exposed Retry/error. A fresh profile with no cache showed an honest failure/Retry state. Both returned Home successfully. These are individual observations, not latency guarantees. This tests provider-runtime failure, not a physically disconnected whole machine.

Evidence: `D:/Colosseum-Vinyl-Slice4-20261021` holds provider probes, build/test outputs, live/cached/failure logs and scene captures. Databases, captures, logs and raw provider responses are outside Git. The app is left open on Discover for inspection.

## Remaining scope and limitations

- Global song/playlist search, search suggestions, general continuation, moods/genres and personalized Home recommendations remain checklist work. See all shows the accepted snapshot; it does not promise more upstream pages.
- Public catalogue metadata is not downloaded audio. Existing Tankorent preference/YouTube fallback is reused; this slice proves YouTube playback through the new routes and does not newly qualify a torrent success.
- Default IN region is the established provider default; region/language selection and wider countries need qualification.
- Existing `Main.qml` `universeHallLayer` and offscreen font warnings remain. Some artwork requests encountered upstream HTTP/2 throttling; later captures showed loaded covers. No blanket claim that every upstream image loads.
- Initial driver attempts assumed an `activate()` method on shared tab/Back wrappers and compared canonical artist channels instead of requested channel identity. Corrected handlers/selectors passed. These setup failures are retained in local evidence, not counted as product successes.
