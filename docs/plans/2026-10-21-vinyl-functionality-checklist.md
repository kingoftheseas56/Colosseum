# Vinyl functionality checklist

Date: 2026-10-21. Source baseline: `codex/vinyl-production-flow` at `dd9d70e52`.

## Purpose and evidence

Complete the familiar music-listening journeys using Colosseum's existing QML, player, identities and stores. This checklist supplements the [development roadmap](2026-10-20-vinyl-development-roadmap.md); it does not close milestones or establish that this branch is integrated into the main branch.

The initial inventory inspected live source and reference documentation/source without new runtime checks. **Slice 1 has since been implemented and received:** see the [state/navigation receiving record](../reviews/2026-10-21-vinyl-slice1-state-navigation.md) for build, focused tests and actual-application evidence. An unchecked item is remaining implementation or acceptance work, even when its source wiring already exists.

Status terms:

- **Wired:** a live source path exists; the acceptance check remains necessary.
- **Partial:** some of the journey exists, with the specific missing part stated.
- **Backend available:** reusable machinery exists, but the complete app path does not.
- **Missing adapter:** the live provider/app interface lacks the required operation.
- **Capability check:** availability or data quality must be established before promising the feature.

### Reference baseline

| Reference inspected | Behavior used | Limit of the evidence |
| --- | --- | --- |
| User's saved YouTube Music Home HTML, 2026-10-21 | Shelf organization: library, keep listening, quick picks, albums, playlists and new releases. | Static capture only. It does not establish API access, click behavior or anonymous personalization. Private account contents are not copied into this plan. |
| [YouTube Music navigation documentation](https://support.google.com/youtubemusic/answer/13401025?hl=en) | Home, Explore, Library, categorized search and artist destinations. | Signed-in YouTube recommendations are a separate capability from public catalogue browsing. |
| [YouTube Music playlist documentation](https://support.google.com/youtubemusic/answer/7205933?hl=en) | Create, add, remove, reorder, rename and delete playlists; reach actions from listening and browsing. | Desktop interactions must follow Colosseum's controls. External collaboration/account features are not implied. |
| [AudioTube Home source](https://github.com/KDE/audiotube/blob/master/src/contents/ui/HomePage.qml) | Typed song, album, artist, playlist and video activation from Home shelves. | Source inspection, not a live reference-app session. Adopt an explicit Colosseum action contract; do not copy implementation code. |
| [AudioTube provider playlist](https://github.com/KDE/audiotube/blob/master/src/contents/ui/PlaylistPage.qml) and [local playlist](https://github.com/KDE/audiotube/blob/master/src/contents/ui/LocalPlaylistPage.qml) | Playlist detail, play/shuffle, item actions and empty state. | Public provider playlists and user-owned playlists need distinct identities and ownership. |
| [Elisa](https://apps.kde.org/elisa/) and [Feishin](https://github.com/jeffvli/feishin) | Local-library organization, playlists and listening-feature coverage. | Secondary completeness references. Feishin is a music-server client, not evidence of YouTube provider capability. |

Reference source links are moving upstream branches, inspected on the date above. The goal is familiar behavior in the existing Colosseum interface; another visual redesign is unnecessary for this slice.

## Interaction and ownership contracts

1. **Navigation:** Home / Discover / Library retain independent scroll, query/filter and route state. Back restores the originating context. Artist and album cards open details; a song search card opens its Song detail. Track rows play in place. The player cover opens the current Song; transport and timeline interactions do not navigate.
2. **Playback actions:** Play replaces the queue with the chosen collection and starts at the intended item. Shuffle starts that collection shuffled. Play next inserts immediately after the current occurrence; Add to queue appends. These are separate actions. Queue-row activation plays that occurrence.
3. **Identity:** links retain individual collaborator IDs. Tracks retain their recording and release/edition/disc/track context; playlists and queues also retain unique item/occurrence identities so duplicates can be edited independently. Never infer a new combined artist from a display string.
4. **Authority:** existing Collection owns saves and playlists; Progress owns resumable session state; Activity owns retained listening facts; ProfilePreferences owns preferences; the existing native session/player owns playback. Public catalogue caches are not a replacement for personal state or downloaded audio.
5. **Sources:** YouTube Music is the primary catalogue. Imported audio plays directly. Remote playback prefers a qualified Tankorent match, with YouTube fallback. A source symbol is an indicator, not a source picker. Correct song/version takes precedence over accepting a merely similar result.
6. **Presentation:** reuse shared world tabs/cards/focus behavior and existing Vinyl pages. Actions use icons with accessible names; no hover descriptors, explanatory design copy, automatic entry highlight, or separate artwork-selection option. Navigation and filter labels remain readable text.
7. **Resilience:** every request has a bounded loading, empty, error, retry and cancellation path. Stale responses cannot replace a newer query, track or profile. Missing optional content hides or disables its action honestly. Cached browsing must not imply offline stream availability.

## A. Navigation and action reachability

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| A1 | [x] Separate Home, Discover and Library read models. | **Received in slice 1.** Per-tab query/lens/sort/mode, independent Home/Library projections and query-bound catalogue snapshots; remote saves are not labelled on-device. | Focused state regressions and actual Main flow passed; Discover search no longer changes Home or Library. |
| A2 | [x] Make search entry from every detail page reach a visible search field. | **Received in slice 1.** Both search entries route to a focused Discover field with a return route. Home's inert filter is hidden. | Main flow covered TopBar entry from Album/Artist/Song and mode-icon entry from Album; focused pointer/keyboard tests covered the shared handlers. |
| A3 | [x] Qualify Back, list detours and tab restoration. | **Received in slice 1.** Routes restore origin query/results, tab/list/scroll/selection/focus and preserve changes made in other tabs. Carousel opens canonical Library. | Main flow and native QML regressions cover detail/search/list returns while playback continues. Wider visual/device qualification remains in A5/M5. |
| A4 | [x] Move imports into Library and remove the global utility toolbar. | **Received in slice 3.** Global shortcuts remain removed; Library Albums has one icon-only folder-import action, including when empty. | Fresh Main Home → Library → existing folder dialog → tagged import passed. The automated driver supplies the selected fixture folder to the real dialog acceptance handler; physical OS-picker interaction was not automated. |
| A5 | [ ] Qualify common controls across all Vinyl routes. | **Wired/partial.** Recent source changes use shared controls and neutral entry focus; coverage is not equivalent to whole-app acceptance. | Pointer hover covers the complete actionable row including runtime; keyboard focus appears on keyboard navigation; icon actions have accessible names and no hover descriptors. |

## B. Home and discovery

Home must be useful without an external account. Start with real local/profile data, then add proven public catalogue shelves. Do not fill it with fixtures or rename search results as recommendations.

**Priority correction, 2026-10-22:** Finish first-Home artist selection, catalogue enrichment and personalized Home before returning to Tankorent playback. The slice-3 fallback Home did not complete the planned artist-onboarding experience. See the [Troi/Music Assistant study](../reviews/2026-10-22-vinyl-recommendation-reference-study.md), [design](2026-10-22-vinyl-personalized-home-design.md) and [concrete implementation plan](2026-10-22-vinyl-personalized-home.md). These are reviewed source/planning artifacts, not implementation or runtime completion.

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| H1 | [x] Connect Continue listening to the resumable session. | **Received in slice 3.** Current session and bounded resumable Progress checkpoints use native validation without mutating playback while rendering. | Main restart resumed the second playlist occurrence at its saved position, preserving queue occurrence IDs/order with Activity history disabled. |
| H2 | [x] Show saved albums/artists and local-library shortcuts independently of search. | **Received in slice 3.** Collection-backed saved songs, albums, artists and playlists feed independent Home shelves; on-device carousel remains available. | Main saved and opened all four types, returned Home, and retained shelves despite a no-match Library filter. Local resume and saved state do not require a catalogue request. |
| H3 | [ ] Expose a bounded recent-listening read model. | **Backend available.** LiveSession writes Activity facts, but has no Vinyl history read binding. Progress checkpoints are resume state, not a listen timeline. | Retained qualified listens map to canonical entries; seeks/checkpoints do not become listens; disabling history obeys existing retention policy. |
| H4 | [ ] Complete the planned first-Home artist setup with a useful Skip path. | **Fallback Home received in slice 3; original onboarding incomplete.** The current picker offers imported-library artists and opens only through reset/edit; saved selections do not drive Home. | First meaningful Home visit offers catalogue artist selection; no minimum; Done/Skip persist per profile; search/Back retain draft; editing can cancel without erasing saved tastes. Skipping still gives useful honest content. |
| H5 | [x] Qualify public discovery data, then add typed provider operations. | **Received in slice 4 for anonymous IN catalogue.** Bundled ytmusicapi Explore/charts feed new releases, trending music, chart artists and public chart playlists through the existing provider/facade/cache. Personalized Home and moods remain outside this accepted subset. | Actual Main loaded four public shelves and opened real release, artist, Song and public playlist destinations. Responses retain source identity, region/time and explicit bounded-list semantics. |
| H6 | [ ] Populate discovery shelves and See all from accepted public data. | **Core received in slice 4; search continuation received in slice 5.** Four public discovery shelves remain bounded snapshots. Categorized searches and public playlist tracks now follow server continuations; Explore/charts shelves do not expose an exhaustive continuation contract. | Main See all/Back, search/clear, exact playlist/song playback and save passed. Do not present the bounded discovery snapshots as exhaustive catalogue pagination. |
| H7 | [ ] Provide per-shelf loading, stale/offline, empty and retry behavior. | **Local Home and bounded Discover received.** Discover keeps successful/prior shelves during partial failure, uses the existing metadata cache, labels cached results and exposes Retry. Broader stale-age/device/provider coverage remains. | Native/QML tests cover stale reply/profile fences, invalid/empty/failed shelves and cached refresh. Actual Main passed with unavailable catalogue runtime both with cache and in a fresh profile; Home remained usable. |
| H8 | [ ] Add artist-follow release shelves and personal recommendations only with real inputs. | **Backend available / capability check.** Taste choices and BE candidate/derivation/ranking pieces exist; no live personalized Home read path. | Recommendations use declared Collection/Activity inputs, respect profile/privacy, and degrade to a defined cold-start result. Do not claim access to a user's YouTube personalized feed. |

## C. Search and catalogue

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| S1 | [x] Complete categorized search for artists, albums, songs and playlists. | **Received in slice 5.** Four typed provider search pages retain exact artist/release/recording/public-playlist identities; existing QML categories, cards and destinations are reused. | Actual Main opened, saved and played an exact second-page Song and opened a searched public playlist. Provider/QML tests cover all four categories and category changes without losing loaded pages. |
| S2 | [x] Keep query, mode, filters and results consistent. | **Received through slice 5.** Four search categories retain independent loaded pages; replacement/clear/profile retirement cancel stale work. Album metadata filters are hidden for online results. | State regressions cover clear/replacement/cancel/profile reset; actual Main covers paged See all, Song/playlist details, Back and Home independence. Broader provider failure qualification remains Q6/S5. |
| S3 | [ ] Preserve recent searches and make deletion/clearing behavior explicit. | **Wired/partial.** SearchHistory record/list and recent-entry rerun exist; history-management controls need coverage. | Reopen a search in the right mode; profile switch isolates it; use existing history policy for removal/retention. |
| S4 | [x] Complete general provider playlist browsing and continuation. | **Received in slice 5 for anonymous public playlists.** Server continuations replace the old 200-item cap. Browse loads one page; Play/Shuffle/whole-playlist Queue require the complete server listing, bounded at 10,000 tracks. Unknown header counts are allowed when the continuation proves exhaustion. | Actual Main opened 100 of a declared 131, fetched a full 127-row playback queue and then appended the remaining browse page. The four unreturned entries are reported explicitly. Tests preserve duplicate/unavailable positions and reject incomplete whole-playlist queue responses. |
| S5 | [ ] Preserve identity and cache safety through every catalogue page. | **Paged identity/fencing received in slice 5.** Existing metadata cache keys include operation, query/kind/region/cursor; source registration retains exact recordings. Query/profile cancellation, failed-page retention/retry and native full-playlist metadata retention have regressions. | Actual Main preserves paged Back context and plays exact searched recordings. Broader offline-cache and provider-expiry journeys remain qualification work; stream URLs remain transient and are resolved again. |

## D. Artist, Album and Song

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| D1 | [ ] Qualify artist tabs, release continuation and artwork together. | **Wired/partial.** Artist source/artwork paths and shared tabs/cards exist; some optional popular-song/playlist projections remain empty. | Known artist opens with independent photos rotating where available; tabs, pagination, empty sections and Back behave coherently; no album cover substituted as an artist photo. |
| D2 | [ ] Qualify per-collaborator links on every surface. | **Wired/partial.** LiveWorld resolves individual artists from source records; prior app proof covers Slick Rick and Nas. | Album, Song, results and player each open the chosen collaborator's page; ambiguous identities do not silently merge. |
| D3 | [ ] Preserve album edition/disc context through playback, save and queue. | **Core received in slice 6.** Bulk Play uses the shown edition; raw track indices map to their exact disc/position before unavailable rows are omitted. Explicit song/queue actions preserve edition and duplicate-position context. | Native-bridge regressions cover alternate editions, interleaved discs and duplicate recordings; Main exercised the imported fixture's selected disc. A larger live multidisc/edition visual acceptance pass remains. |
| D4 | [ ] Qualify the large cover and reachable details at supported window sizes. | **Wired.** Cover now caps at 440 logical pixels and column width; scroll behavior must be judged with the actual viewport. | At desktop/high DPI the cover is dominant; on shorter/narrow windows all metadata/actions remain reachable without overlap or clipped track runtimes. |
| D5 | [x] Keep row playback and Song navigation intentional. | **Received through slice 6.** Queue actions preserve playback and selected recording; only the player cover opens the current Song. Timeline click/drag/keyboard actions stay in place. | Actual Main exercised cover-to-Song, current and browsed Song queue actions, Album return and paused timeline seeking without navigation; pointer cancellation is separately covered by QML interaction tests. |
| D6 | [ ] Make Album videos and related albums actionable. | **Partial.** Album forwards `videoRequested` but LiveWorld has no Album handler. “More by this artist” renders plain noninteractive columns. | Every displayed video/related-album tile has a defined pointer/keyboard action and destination, or is withheld until supported. |
| D7 | [ ] Complete Song feature data coverage. | **Partial.** LiveSession displays supplied lyrics/notes/credits; metadata importer lacks general lyric-tag acquisition, provider has no dedicated lyrics/credits operation. BC timed-text/translation/annotation components are not the live session's implementation. | Supplied plain/timed lyrics and credits display against the correct recording; missing data disables cleanly; seek/track changes clear stale content. Qualify each additional acquisition source before enabling it. |
| D8 | [ ] Define authored notes and related-video scope separately. | **Backend available.** Imported notes are read-only metadata; user note editing and BF video/context integration lack a complete app path. | If promoted, notes persist in profile state separately from source metadata; audio/video transitions retain the intended track and player ownership. |

## E. Playback and queue

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| Q1 | [ ] Qualify exact-song playback and fallback across realistic cases. | **Fresh local and YouTube flow evidence in slice 6.** Real Main decoded exact Stan/YouTube audio through WASAPI, sought, paused/resumed and advanced the queue at EOF. Intermittent last-second remote seek timeouts remain open despite subsequent passing controls. The fresh N.Y. State of Mind torrent probe found no exact match and fell back to YouTube; it is not a new torrent-success proof. CUE spans remain unsupported. | Retain the wider torrent-success/manifest/provider-failure matrix and diagnose the intermittent remote seek. Native regressions cover mismatched tags, decoder fallback once and failed fallback without loops. |
| Q2 | [ ] Establish an end-to-end startup latency acceptance budget. | **Partial.** Torrent discovery has a 4-second overall deadline and concurrent YouTube preparation. This is not a bound on audible startup; resolution/player loading remain separate. | Measure cold/warm starts on several songs through advancing audio; record provider/network context and outliers. A single ~5.5-second fallback observation does not establish a service guarantee. |
| Q3 | [x] Complete Play next versus Append across all entry points. | **Received in slice 6.** Distinct icon actions exist on Album/track, current/browsed Song, Artist track, saved-song and personal playlist/item destinations. Empty-queue insertion selects a paused occurrence so the existing player/queue controls remain reachable. | Main exercised those actions without replacing the active occurrence; order and duplicate occurrence IDs persisted. Bulk actions skip known unavailable rows and reject queue overflow without truncation. |
| Q4 | [x] Qualify queue edit, shuffle, repeat and EOF behavior. | **Received for the core queue contract in slice 6.** Native ownership is retained; clearing resets shuffle, an empty queue cannot enter shuffle, and paused transport navigation retains pause. Rapid pause/resume no longer drops the latest engine intent. | Native tests and actual Main cover move/remove, shuffle/unshuffle after edits, repeat off/all/one, duplicate EOF traversal, restart after EOF, queue clear, and restart restoration of IDs/order/position/repeat. Wider device/provider combinations remain Q1/Q6. |
| Q5 | [x] Finish seeking and player click boundaries. | **Received in slice 6.** Pointer press/drag previews time and release commits one clamped seek. Escape, pointer loss, track/occurrence change or disablement cancel it. Loaded paused playback remains seekable. | Pointer/keyboard QML tests cover drag/click/cancel/bounds while paused; Main exercised those same handlers against real local playback without navigation. Only cover activation opens Song. |
| Q6 | [ ] Qualify cancellation, failure and profile retirement. | **Core regression coverage expanded in slice 6.** Native tests cover clear/profile retirement while resolving, source identity changes and fallback failures. Actual YouTube Main flow retired its queue/profile without late playback. | Broader live provider/network and physical account-switch failure combinations remain acceptance work. Do not equate mock transport failures with whole-machine offline qualification. |
| Q7 | [ ] Keep radio/autoplay an explicit later feature. | **Missing complete app path.** Existing queue EOF advancement is not recommendation-driven refill; M7R remains separate. | When promoted, specify seed, queue ownership, user-vs-generated entries, duplication/fatigue and refill; preserve manual queue edits and stop behavior. |

## F. Library, playlists and personal state

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| L1 | [ ] Make Library a complete destination for saved albums, artists, songs, playlists and local imports. | **Slices 2/3 core received.** Library has Albums/Artists/Songs/Playlists and an icon-only folder-import entry; album rows and Song pages save individual tracks through Collection. | Album/Song saves, Library unsave and fresh-profile folder import passed in Main. Search-result save controls remain acceptance work. |
| L2 | [x] Complete playlist detail and whole-playlist playback first. | **Received in slice 2.** Playlist detail plays, shuffles, appends and activates individual occurrences through the existing session/player. | Actual Main creation, detail, playback, shuffle, append, Back and restart playback passed. |
| L3 | [x] Add playlist rename, add/insert, remove, reorder and delete through Collection. | **Received in slice 2.** Native commands reuse BA5 validation/IDs and BA1 ordered occurrences behind Collection transactions. Append then reorder supports placement; duplicate items have independent IDs. | Main exercised create/rename/add/duplicate move/remove/delete and restart. Native/QML regressions cover legacy duplicates, failed writes and profile retirement; broader account/profile switching remains L6. |
| L4 | [ ] Preserve unavailable playlist items and cross-source identities. | **Core implemented/tested in slice 2.** Exact album/edition/disc/track refs hydrate without retargeting. Missing entries remain visible and editable; play-all skips unavailable entries. | Native/QML tests cover missing local paths, exact multidisc references and stale source rejection. Live provider expiry and reimport identity ceremonies remain qualification work. |
| L5 | [ ] Qualify tagged imports and library maintenance. | **Wired/partial.** Async files/folder import, tag grouping and source identity exist; rescan/move/missing-file/reimport behavior needs end-to-end coverage. | Import the supplied multidisc Illmatic fixture; verify album/disc/track/artwork, repeat import, missing files and cancel/error; unrelated saved metadata survives. |
| L6 | [ ] Qualify persistence, privacy and write failures across the whole journey. | **Wired/partial.** Profile-scoped Collection, Progress, Activity and preference owners exist. | Saves, playlists, queue/resume and taste survive restart; profiles stay isolated; history-off remains useful; a failed write reports failure and does not pretend success. |

## Backend reuse boundary

The declared native build graph links `colosseum_vinyl_backend` through `colosseum_vinyl_app`. An old comment in the backend CMake file says otherwise; the live build graph and call paths take precedence. This checklist did not rebuild it.

| Existing owner / component | Reuse decision |
| --- | --- |
| `VinylBackendSession`, existing native player, `VinylCatalogueSource`, `VinylProvider`, `VinylTorrentProvider` | Extend current interfaces; do not add a second playback or provider orchestration stack. |
| Collection / BA5 | Album and artist saves already have live paths. Add track membership and typed playlist mutations under Collection; preserve occurrence identity. |
| Progress / BA3 | Keep resume/checkpoints here. They are not listening-history events. |
| Activity / BA4 | Keep durable retained listens in existing Activity ownership. Expose a bounded Vinyl read projection; BA4 already participates in qualification. |
| BC timed text / translation / annotation | Reusable components, not proof of acquired lyric data or a complete Song feature. Integrate after data contract and ownership are settled. |
| BE recommendations / BF context / BG catalogue pieces | Reuse applicable contracts and algorithms. Their presence does not establish a live Home feed, arbitrary playlist API or radio sequence. Verify actual inputs/outputs and provider capability first. |

Source anchors: `qml/vinyl/VinylLiveWorld{,Data}.qml`, `VinylLiveSession.qml`, `VinylLiveArtistData.qml`, `VinylAlbum{Page,Sections}.qml`, `VinylTrackRow.qml`, `VinylPlayerBar.qml`, `VinylQueuePanel.qml`; `native/CollectionStore.h`, `native/ProgressStore.h`, `native/account/ActivityStore.h`; `native/vinyl/Vinyl{BackendSession,CatalogueSource,Provider,TorrentProvider}.{h,cpp}`; `scripts/vinyl/provider.py`; `native/vinyl/backend/`.

## Implementation order

These are proposed execution slices, not newly completed milestones. Bring a minimal useful Home forward from M7 while completing its M4 data/destination prerequisites. Keep the wider roadmap's milestone acceptance separate.

### 1. Separate browse state and make destinations reliable

- A1–A3, S2, D5. Define separate Home/Discover/Library inputs, typed card routes, explicit queue semantics and search entry behavior.
- Done when Discover searches cannot alter Home or local Library and every current card/action has an intentional destination. Keep track-row in-place playback.

### 2. Finish the personal-library destinations Home needs

- L1–L4 and Q3: track saves, playlist detail, Collection-backed mutations, play/shuffle/append/next.
- Done when a saved-song/playlist card can open, play and be edited reliably, with duplicate/unavailable occurrence behavior. Reuse BA5; do not invent another store.
- Slice 2 implementation and receiving evidence: [saved songs and playlists](../reviews/2026-10-21-vinyl-slice2-personal-library.md). Explicit Play next across all entry points remains Q3; this slice implements playlist append.

### 3. Ship the first useful Home in the existing QML

- H1–H4, H7, A4: Continue, saved music, playlists, local shortcuts; bounded recent listens once Activity projection is available. Move imports under Library.
- Done when both a new offline profile and a returning listener have an honest useful Home. Every shelf item reaches a working destination; no fixture content or search-derived local mislabels.
- Slice 3 implementation and receiving evidence: [Home flow](../reviews/2026-10-21-vinyl-slice3-home.md). Continue uses resumable Progress state; Activity recent-listening projection remains H3, and online discovery remains H5/H6.

### 4. Complete online discovery and search breadth

- H5–H6, S1, S4–S5: first a bounded anonymous provider proof, then adapters for accepted public shelves, songs and playlists, continuation and caches.
- Done when real online Home shelves and categorized results navigate/play end to end, including partial provider failure. If a capability is unavailable, record that result and retain the usable local Home.
- Slice 4 received the bounded Discover population: [public catalogue receiving record](../reviews/2026-10-21-vinyl-slice4-discover.md). Slice 5 received categorized song/playlist search and search/public-playlist continuation: [search and pagination receiving record](../reviews/2026-10-21-vinyl-slice5-search-pagination.md). Public personalized Home and exhaustive Explore/charts continuation remain separate capability work.

### 5. Complete cross-page listening quality

- D1–D4, D6, Q1–Q6, L5–L6 and A5: video/related-card reachability, source correctness/latency, seek, identity, focus, responsive geometry, persistence and retirement.
- These checks also apply to each earlier slice; this gate closes remaining combinations rather than postponing basic usability.
- Slice 6 received the playback/queue portion: [playback and queue receiving record](../reviews/2026-10-21-vinyl-slice6-playback-queue.md). Source availability/latency, richer Song data and video/related-card work retain their separate gates.

### 6. Add listener-aware and extended listening features

- H8, D7–D8, then Q7 only when promoted: real recommendation inputs, stronger lyrics/credits acquisition, translation/notes/video/context and radio.
- Done only with real data and the full app journey. These features must not delay a useful Home, search, personal library or playlist editor.

## Prior runtime evidence and the next qualification gate

### Coverage boundaries

This checklist covers the core music journeys and identifies extended features without promising full parity with every reference app. Keep the following visible in the backlog; their current implementation was not audited in this pass:

- Volume/mute, output-device changes, media keys and OS media integration: qualify the existing player facilities before adding Vinyl-specific controls.
- Gapless transitions, crossfade and loudness normalization: separate playback capabilities, not consequences of queue advancement.
- Download-for-offline, download management and storage limits: saving metadata is not downloading audio. Extend an existing download/cache owner if promoted.
- Shared playlists, external account synchronization, scrobbling, podcast/show subscriptions and casting: optional scope with independent provider/account checks. Their appearance in a reference app or captured Home is not an implementation commitment.

### Evidence ledger

- [M3 receiving record](../reviews/2026-10-21-vinyl-m3-receiving.md): prior actual-app torrent and YouTube-fallback playback evidence. It does not qualify all tracks/providers or the checklist above.
- Earlier UI receiving in this workstream observed independent Slick Rick/Nas links, shared artist controls and a 440-logical-pixel album cover. A fallback run reached advancing YouTube playback at approximately 5.5 seconds after selection. These are individual prior observations, not fresh runs or whole-flow acceptance here.
- The next implementation slice must capture: exact build/source identity, profile/data conditions, action sequence, result, timing where relevant and unresolved failures. Keep automated checks, agent app interaction and Hemanth acceptance distinct.
- Integrated acceptance journey: **Home → Search → Artist → Album → row playback → cover opens Song → Queue edit → save/create playlist → Library → playlist playback → Back → restart/resume**, plus fresh-profile, offline, provider-failure, history-off and profile-switch cases.
- Build/test/runtime acceptance, main-branch integration and release remain separate gates. This document is a source-backed work checklist.
