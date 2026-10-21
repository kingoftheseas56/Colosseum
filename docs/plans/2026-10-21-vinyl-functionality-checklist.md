# Vinyl functionality checklist

Date: 2026-10-21. Source baseline: `codex/vinyl-production-flow` at `dd9d70e52`.

## Purpose and evidence

Complete the familiar music-listening journeys using Colosseum's existing QML, player, identities and stores. This checklist supplements the [development roadmap](2026-10-20-vinyl-development-roadmap.md); it does not close milestones or establish that this branch is integrated into the main branch.

This pass inspected live source and reference documentation/source. No new build, tests or app interaction were performed for this checklist. Prior app evidence is listed separately below. An unchecked item is remaining implementation or acceptance work, even when its source wiring already exists.

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
| A1 | [ ] Separate Home, Discover and Library read models. | **Partial.** `VinylLiveWorldData.qml` derives Home from the same `albums` projection controlled by catalogue mode, query and lens. Remote search albums can acquire local-library labels. | Search/filter in Discover, then return Home: its shelves and labels remain correct and unchanged by that search. |
| A2 | [ ] Make search entry from every detail page reach a visible search field. | **Partial.** TopBar search routes to Discover; `vinylCatalogueMode` changes mode/focus without routing away from Album/Song, where the header is hidden. | Invoke both entry paths from Artist, Album and Song; visible destination, selected mode and keyboard focus agree. |
| A3 | [ ] Qualify Back, list detours and tab restoration. | **Wired.** `VinylLiveWorld.qml` route capture/restore and `VinylWorld.qml` tab/list offsets exist. | Home shelf → See all → Album → Artist → Back restores each selection/scroll; repeat while audio plays. |
| A4 | [ ] Move imports into Library and remove the global utility toolbar. | **Partial.** LiveWorld still exposes file/folder, now-playing, queue and catalogue actions globally. File/folder import itself is wired. | Home shows its intended content; Library add icon/empty state imports files or folders; player and search retain clear icon entry points. |
| A5 | [ ] Qualify common controls across all Vinyl routes. | **Wired/partial.** Recent source changes use shared controls and neutral entry focus; coverage is not equivalent to whole-app acceptance. | Pointer hover covers the complete actionable row including runtime; keyboard focus appears on keyboard navigation; icon actions have accessible names and no hover descriptors. |

## B. Home and discovery

Home must be useful without an external account. Start with real local/profile data, then add proven public catalogue shelves. Do not fill it with fixtures or rename search results as recommendations.

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| H1 | [ ] Connect Continue listening to the resumable session. | **Partial.** Home carousel can show the active queued track; BA3/Progress restore exists. It is not a complete recent-listening shelf. | Relaunch mid-album, open Home and resume the intended track/position without losing queue order. |
| H2 | [ ] Show saved albums/artists and local-library shortcuts independently of search. | **Backend available.** Collection album/artist saves exist; Home currently uses filtered albums and taste picks instead of dedicated saved projections. | Save from detail, return Home, open that same stable entity; offline local/saved state remains readable. |
| H3 | [ ] Expose a bounded recent-listening read model. | **Backend available.** LiveSession writes Activity facts, but has no Vinyl history read binding. Progress checkpoints are resume state, not a listen timeline. | Retained qualified listens map to canonical entries; seeks/checkpoints do not become listens; disabling history obeys existing retention policy. |
| H4 | [ ] Give cold start a useful destination without requiring taste selection. | **Partial.** Picker persists choices; options derive from available catalogue albums, `coldShelves` is empty, and current empty copy points at global imports. | Fresh profile can skip, import from Library or browse available public discovery; no blank mandatory picker or invented popularity. |
| H5 | [ ] Qualify public Home/discovery data, then add a typed provider operation. | **Missing adapter / capability check.** `provider.py` and `VinylProvider.h` expose no Home, charts, moods or general new-release operation. | Anonymous requests return identifiable playable/navigable entities, continuation and bounded failure behavior; record region/language behavior and unavailable capabilities. |
| H6 | [ ] Populate discovery shelves and See all from accepted public data. | **Missing adapter.** Need native facade/normalization/cache and stable shelf/list IDs, not only a QML binding. | Every visible card opens the correct type; See all continues the same shelf; duplicate pages, stale replies and provider failures do not corrupt it. |
| H7 | [ ] Provide per-shelf loading, stale/offline, empty and retry behavior. | **Partial.** Current `catalogueReachable` is derived from `browseError`, conflating a request failure with connectivity. | One failing online shelf leaves local/saved/Continue usable; retry replaces only the failed request; no duplicate shelves. |
| H8 | [ ] Add artist-follow release shelves and personal recommendations only with real inputs. | **Backend available / capability check.** Taste choices and BE candidate/derivation/ranking pieces exist; no live personalized Home read path. | Recommendations use declared Collection/Activity inputs, respect profile/privacy, and degrade to a defined cold-start result. Do not claim access to a user's YouTube personalized feed. |

## C. Search and catalogue

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| S1 | [ ] Complete categorized search for artists, albums, songs and playlists. | **Partial.** `catalogueSearch` fetches artists/albums; `search` fetches albums. Song projection from known album data is not global song search. | A song/title search finds the intended song and collaborators without first opening its album; each result routes by type. |
| S2 | [ ] Keep query, mode, filters and results consistent. | **Partial.** Local search updates on query; catalogue search submits on Enter. Empty catalogue results still have local-only copy. | Enter, clear, retry, rapid query replacement and switching Local/Catalogue never display results under a different query or misleading empty text. |
| S3 | [ ] Preserve recent searches and make deletion/clearing behavior explicit. | **Wired/partial.** SearchHistory record/list and recent-entry rerun exist; history-management controls need coverage. | Reopen a search in the right mode; profile switch isolates it; use existing history policy for removal/retention. |
| S4 | [ ] Complete general provider playlist browsing and continuation. | **Missing adapter.** Internal artist playlist expansion is not a public playlist-detail API. | Open a public playlist, load all accepted pages without duplication, play/shuffle/append it, and preserve unavailable entries honestly. |
| S5 | [ ] Preserve identity and cache safety through every catalogue page. | **Wired/partial.** CatalogueSource has stable records and request fencing; new entity types must extend these contracts. | Two identically named releases/artists stay distinct; cached metadata survives offline; expired stream URLs are resolved again rather than persisted as library identity. |

## D. Artist, Album and Song

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| D1 | [ ] Qualify artist tabs, release continuation and artwork together. | **Wired/partial.** Artist source/artwork paths and shared tabs/cards exist; some optional popular-song/playlist projections remain empty. | Known artist opens with independent photos rotating where available; tabs, pagination, empty sections and Back behave coherently; no album cover substituted as an artist photo. |
| D2 | [ ] Qualify per-collaborator links on every surface. | **Wired/partial.** LiveWorld resolves individual artists from source records; prior app proof covers Slick Rick and Nas. | Album, Song, results and player each open the chosen collaborator's page; ambiguous identities do not silently merge. |
| D3 | [ ] Preserve album edition/disc context through playback, save and queue. | **Wired.** Album selectors and session occurrence context exist. | Open a multidisc release, switch disc/edition, play and queue a track, then Back; displayed and played versions agree. |
| D4 | [ ] Qualify the large cover and reachable details at supported window sizes. | **Wired.** Cover now caps at 440 logical pixels and column width; scroll behavior must be judged with the actual viewport. | At desktop/high DPI the cover is dominant; on shorter/narrow windows all metadata/actions remain reachable without overlap or clipped track runtimes. |
| D5 | [ ] Keep row playback and Song navigation intentional. | **Wired.** `VinylTrackRow` plays in place. Album's declared `songRequested` signal currently has no emitter. The dormant signal alone is not a missing row-play behavior. | Track-row activation starts that row without navigating; player-cover activation opens Song; a browse-song card opens Song detail without unrelated queue replacement. |
| D6 | [ ] Make Album videos and related albums actionable. | **Partial.** Album forwards `videoRequested` but LiveWorld has no Album handler. “More by this artist” renders plain noninteractive columns. | Every displayed video/related-album tile has a defined pointer/keyboard action and destination, or is withheld until supported. |
| D7 | [ ] Complete Song feature data coverage. | **Partial.** LiveSession displays supplied lyrics/notes/credits; metadata importer lacks general lyric-tag acquisition, provider has no dedicated lyrics/credits operation. BC timed-text/translation/annotation components are not the live session's implementation. | Supplied plain/timed lyrics and credits display against the correct recording; missing data disables cleanly; seek/track changes clear stale content. Qualify each additional acquisition source before enabling it. |
| D8 | [ ] Define authored notes and related-video scope separately. | **Backend available.** Imported notes are read-only metadata; user note editing and BF video/context integration lack a complete app path. | If promoted, notes persist in profile state separately from source metadata; audio/video transitions retain the intended track and player ownership. |

## E. Playback and queue

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| Q1 | [ ] Qualify exact-song playback and fallback across realistic cases. | **Wired, prior app evidence.** Existing player/native session handles direct audio, qualified torrent and YouTube fallback. CUE-span playback is unsupported. | Check track/version/duration plus advancing playback; cover a torrent success, no match, manifest failure and YouTube failure without accepting another song. |
| Q2 | [ ] Establish an end-to-end startup latency acceptance budget. | **Partial.** Torrent discovery has a 4-second overall deadline and concurrent YouTube preparation. This is not a bound on audible startup; resolution/player loading remain separate. | Measure cold/warm starts on several songs through advancing audio; record provider/network context and outliers. A single ~5.5-second fallback observation does not establish a service guarantee. |
| Q3 | [ ] Complete Play next versus Append across all entry points. | **Partial.** Album/track append and native queue mutation exist; app-wide explicit next-insertion coverage is incomplete. | Start A, Play next B, Append C; order is A → B → existing remainder → C; duplicates remain independent occurrences. |
| Q4 | [ ] Qualify queue edit, shuffle, repeat and EOF behavior. | **Wired.** Queue panel/native owner expose play, next/previous, clear, move, remove, shuffle and repeat. | Edit current and noncurrent duplicates; remove/reorder around EOF; verify repeat modes, shuffled traversal, empty state and checkpoint restore. |
| Q5 | [ ] Finish seeking and player click boundaries. | **Partial.** Timeline supports click and keyboard seek; pointer drag scrubbing is absent. Its enabled condition is `sourceState === "playing"`; verify actual paused behavior before changing it. | Click/drag/key-seek while playing and paused, cancel a drag, hit start/end; timeline never navigates and only cover activation opens Song. |
| Q6 | [ ] Qualify cancellation, failure and profile retirement. | **Wired/partial.** Native request/session fencing exists; broader provider scenarios remain acceptance work. | Rapidly switch tracks, cancel, switch profile and shut down while resolving; no late audio, old-profile writes, or stale source indicator. |
| Q7 | [ ] Keep radio/autoplay an explicit later feature. | **Missing complete app path.** Existing queue EOF advancement is not recommendation-driven refill; M7R remains separate. | When promoted, specify seed, queue ownership, user-vs-generated entries, duplication/fatigue and refill; preserve manual queue edits and stop behavior. |

## F. Library, playlists and personal state

| ID | Remaining checklist item | Current source / concrete gap | Acceptance check |
| --- | --- | --- | --- |
| L1 | [ ] Make Library a complete destination for saved albums, artists, songs, playlists and local imports. | **Partial.** Album/artist membership exists; no live individual track-save path or complete playlist/library view. | Save from any detail/result, find the same entity in Library, unsave it and confirm source metadata/imported audio remains intact. |
| L2 | [ ] Complete playlist detail and whole-playlist playback first. | **Partial.** Artist page creates/lists Collection playlists and exposes individual items; complete playlist detail/play-all/shuffle is absent. | Create and reopen a playlist, play/shuffle/append it in stored order, return to the same list position. Home playlist cards must not ship before this destination works. |
| L3 | [ ] Add playlist rename, add/insert, remove, reorder and delete through Collection. | **Backend available.** Collection has create/list and generic mutations; BA5 `VinylPlaylist` supports typed item mutations. Adapt it behind the existing owner. | Edit a duplicate occurrence independently, persist order/name, restart and switch profiles; deletion removes the playlist rather than source tracks. |
| L4 | [ ] Preserve unavailable playlist items and cross-source identities. | **Partial.** Stored playlist references need reliable hydration and unavailable-item behavior. | Remove a file, expire a provider item or reimport another edition; the playlist marks the original unavailable instead of retargeting it silently. |
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

### 3. Ship the first useful Home in the existing QML

- H1–H4, H7, A4: Continue, saved music, playlists, local shortcuts; bounded recent listens once Activity projection is available. Move imports under Library.
- Done when both a new offline profile and a returning listener have an honest useful Home. Every shelf item reaches a working destination; no fixture content or search-derived local mislabels.

### 4. Complete online discovery and search breadth

- H5–H6, S1, S4–S5: first a bounded anonymous provider proof, then adapters for accepted public shelves, songs and playlists, continuation and caches.
- Done when real online Home shelves and categorized results navigate/play end to end, including partial provider failure. If a capability is unavailable, record that result and retain the usable local Home.

### 5. Complete cross-page listening quality

- D1–D4, D6, Q1–Q6, L5–L6 and A5: video/related-card reachability, source correctness/latency, seek, identity, focus, responsive geometry, persistence and retirement.
- These checks also apply to each earlier slice; this gate closes remaining combinations rather than postponing basic usability.

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
