# Vinyl foundation: reuse from Tankoban, Biblio and Theatre

This is part of the single Dots implementation project in `DOTS-JOB.md`. Build 0.25 confirms the ownership decisions below; the applicable implementation follows across the four gated builds; it is not another open-ended audit or parallel job.

Evidence basis: committed app base `3fcdd4be620281d70b59961450d638a77439da91`, compared with Vinyl donor `501bf8a3baa003d70190c00dd6d24810dc2677aa`. This is source review, not a claim that the proposed integration has run.

## Reuse decisions

| Concern | Existing owner / useful examples | Vinyl decision and present gap |
|---|---|---|
| World shell, global header and docked world tabs | `qml/WorldPage.qml`, `WorldTabBar.qml`; TankobanWorld, BiblioWorld and TheatreWorld use WorldPage | Reuse appropriate shell/lifecycle/navigation facilities. VinylWorld and VinylLiveWorld are custom Item roots, so equivalent behavior is not established by their current rendering. Preserve the accepted artist detail tabs; do not force world-level navigation into that destination. |
| Detail headers and controls | `BackAction.qml`, `TitlePageBar.qml`, `Theme.qml`, `WidgetHeader.qml`, `KeyboardAction.qml`, `FocusRing.qml` | Shared picker and BackAction are already in the artist donor. Keep them. Reuse destination styling and interaction behavior without copying window-management actions into nested pages. Do not rewrite already-correct chrome. |
| Search and source/filter controls | `SearchSurface.qml`, `DiscoverSidebar.qml`, `DiscoverPicker.qml`, `native/SearchHistoryStore.h` | Reuse presentation and scoped search-history behavior, including remember/clear preferences; inject local search behind it. Filter popup and collapsed source rail are separate controls. External catalogue adapters remain deferred. |
| Collection keyboard navigation and scrolling | `KeyboardCollectionController.qml`, `KeyboardSpatialNavigator.qml`, `ScrollGlide.qml`; WorldPage's focus-region and row-parking behavior | Prefer these for matching grid/list/rail behavior. Check overlap with Vinyl-specific Keys handlers before changing them; one action per key, preserved boundary propagation and focus return. Existing artist ScrollGlide use does not prove every Vinyl surface shares the same keyboard law. |
| Saved library state | `native/CollectionStore.h`: `add`, `remove`, `has`, `items`, revision and persistence health | VinylLiveSession already uses Collection for saved albums. Complete artist/playlist/library integration through the appropriate owner/extension and surface failed writes. Prototype savedNames/customPlaylists are not production storage. |
| Continue/history/resume | `native/ProgressStore.h`: `record`, `recordSilent`, `recent`, `get`, health and flush behavior | VinylLiveSession already records/restores music progress. Verify completeness of queue, profile, track/edition identity and restart semantics. Reuse the owner rather than creating a second music history database. |
| Music-player lifecycle | `MpvItem` and `qml/AudiobookSession.qml`'s one-session/multiple-remotes pattern; Theatre player integration | Keep the existing Vinyl music-session owner and player backend. Borrow proven lifecycle/progress/discontinuity handling; do not instantiate AudiobookSession as the song player or copy audiobook chapter semantics into a music queue. Cross-world handoff still needs explicit integration and testing. |
| App-level open sessions/taskbar | `native/SessionStore.h`: `openOrSwitch`, `switchTo`, `close`, `saveState`; Main.qml capture/restore glue | Integrate Vinyl with the established open-session behavior where appropriate. Sessions owns shell-open state, not the authoritative music queue. Do not confuse a restored page with a restored playback session. |
| Activity/privacy/preferences | `native/account/ActivityPlaybackTracker.h` (registered as `Colosseum.Activity`), `ActivityLaneHelpers.js`, AudiobookSession's ProfileActivity sink; `native/account/ProfilePreferencesStore.h` | Extend established activity identities/capabilities for music where needed, respect profile/opt-out policy and measure real consumption. Pause, seek, source change, EOF and profile switch need correct transitions. Existing bookstore/video event identities cannot simply be relabelled as music. ProfileActivity absence/failure must not break playback. |
| Background work and cancellation | `native/work/BackgroundWorkCoordinator.h`, BackgroundActivityRegistry and existing foreground-pressure policy | If local import/metadata/artwork processing needs background work, use the shared scheduling/cancellation owner where it fits. Do not invent another global worker pool or block playback with scanning. Small synchronous operations do not require artificial background jobs. |

`CollectionStore` and `ProgressStore` also have isolated appdata/test-storage seams. Native acceptance runs must use isolated storage rather than alter a real user's library or listening history.

## What remains music-specific

Reuse does not eliminate music-domain work: artist/release/edition/disc/recording identity and mappings; the music queue and repeat/shuffle semantics; local audio ingestion and playable-source resolution; synchronized album/song/lyrics/credits presentation; optional catalogue and streaming adapters. Extend existing Vinyl components for these responsibilities. Comics pages, book chapters and television episodes are useful lifecycle examples, not interchangeable music models.

Playlist persistence needs an ownership check. Reusing Collection for saved items does not establish that it already provides a complete ordered playlist API. Extend the correct existing profile owner only after checking its capabilities; do not silently store unrelated mutable state in arbitrary collection payloads.

## Scope additions exposed by the review

The original handoff covered pages, local playback, queue, saves and restart. It did not make these app-level cases explicit enough:

1. Enter/leave Vinyl through the native app and restore it through the established taskbar/open-session route without duplicate pages or lost playback identity.
2. Switch among music, Biblio audiobook and Theatre playback under the app's intended audio-handoff policy; no unintended competing audio or stale remotes.
3. Honor search-history and activity preferences, including profile separation and failure/absence of optional activity recording.
4. Verify durable-write failure, storage isolation and shutdown flushing through existing stores rather than only checking a UI toggle.
5. Verify shared keyboard behavior across collections, world boundaries and nested detail pages, not only the artist destination.

These are foundation acceptance cases, independent of external providers. Add them to the one candidate's native-app matrix. Do not expand this into reader/video-player redesign, universal cross-world backend refactoring, cloud sync work or a new generalized media framework.

## Deliverable within the same job

Include a compact final reuse inventory: existing owner, used/extended/kept-specific, changed files, regression tests and runtime evidence. A rejected reuse candidate needs a concrete technical reason. Preserve current app behavior and reuse the pinned current owners instead of wholesale donor replacements.
