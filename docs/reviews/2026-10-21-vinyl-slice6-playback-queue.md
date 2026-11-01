# Vinyl slice 6: playback and queue behaviour

Baseline: `5c8fc331dadc428ec4619e04d75601f2e29183ae`, branch `codex/vinyl-production-flow`. This is branch qualification, not main integration or a release.

## Changes

- Existing Album, Song, Artist track and personal Library/playlist destinations now expose distinct icon-only Play next and Add to queue actions, with accessible names, capability/availability guards and wrapping layouts. Both current Song and a browsed Song use the existing session; neither action replaces the active queue occurrence.
- Bulk Album Play retains the shown edition. Raw song indices map to their exact disc/track position before unavailable rows are removed. Bulk Play/Shuffle/Queue skip known unavailable tracks; explicit unavailable-track actions fail without retargeting. The 10,000-row limit fails explicitly instead of silently truncating bulk playback. Public playlist bulk actions still require a complete server listing before admission.
- Appending to an empty queue now selects its first occurrence paused without resolving or starting audio. This keeps the existing player bar and Queue action reachable after removal of the old global utility toolbar. Appending also refreshes retained source registration for future queue playback.
- Native clear resets shuffle; empty queues cannot enter shuffle. Manual next/previous retains pause, while explicit queue-row Play starts playback. Existing occurrence IDs continue to distinguish duplicates through move/remove, shuffle and persistence.
- Fixed a reproduced rapid pause/resume race. The observed mpv pause cache can lag a queued command, so the Vinyl engine port now sends each adapter pause intent asynchronously instead of dropping a resume based on the stale observed value. A real short-WAV regression failed before this repair and passed afterward.
- The existing player timeline now previews pointer scrubs and commits one clamped seek on release. Escape, pointer loss, disablement, or a different queue occurrence/recording cancels the scrub without applying a stale target. Paused playback supports pointer and keyboard seeking. Only the player cover opens Song.

Luna xhigh supplied the bounded page controls, timeline implementation/interaction tests and native queue regression file. Root integrated the existing session/store adapters, repaired native behaviour, reviewed the changes and ran receiving checks and real Main journeys. No new player, queue store, identity system or provider stack was added.

A final bounded Luna xhigh read-only review found no concrete correctness issues in the changed native queue/session and edition/playlist position-mapping paths. This source review is separate from the runtime evidence below.

## Verification

Release targets passed: `vinyl_catalogue_source_test`, `vinyl_native_session_bridge`, `colosseum_runtime_ready`. Final QML manifest: `1f49e7e1c894880c1119c08c623ccde0963d7dd26f626e179873d4ee37919d0d`.

Eleven focused CTest registrations passed, zero failures, 53.33 seconds. This includes the existing native source/fallback bridge, catalogue, personal-library, Home/Discover/search suites and three new suites:

- `player_seek`: seven pointer/keyboard cases plus lifecycle, covering preview/release, one-seek clicks, clamping, Escape/ungrab/disable/track-change cancellation and paused keyboard seeking.
- `playback_queue`: ten cases plus lifecycle, covering occurrence identity, Next/append order, empty paused selection, edits, shuffle after clearing/edits, repeat boundaries, paused navigation, cancellation/profile retirement and real WAV pause/resume through EOF.
- `playback_actions`: six cases plus lifecycle, covering exact alternate-edition/interleaved-disc mapping, unavailable rows, duplicate source context, shown-edition queueing and overflow rejection.

The existing source/fallback tests also cover canonical tag checks, one fallback after real decoder failure, failed fallback without loops, source replacement and profile fencing. These controlled regressions are separate from public provider availability.

### Actual app: local pages, queue and restart

`tests/vinyl-live/PlaybackQueueFlow.qml` hosts production Main, native stores/source/player and the existing controls in a disposable profile. It invokes their semantic handlers; it does not claim physical OS-picker or desktop-pointer automation. QML interaction tests separately send pointer/key events to the timeline. The known music fixture is supplied to the existing import acceptance handler; a task-owned ASCII junction avoids command-line encoding loss for the user's Unicode folder path. No music was copied or modified.

The final local record/restore runs passed:

1. Imported the supplied folder as the currently returned **Illmatic XX** album: ten tracks, one tagged disc/edition. The folder's name says two-CD, but the current importer response is not claimed as a live two-disc proof. Alternate-edition and two-disc mapping is covered separately by native-bridge regressions.
2. Whole Album Next on an empty queue produced a visible paused selection without starting audio. Queue-row Play started the exact local recording with advancing playback.
3. Album row Next and append, selected-disc context, current Song and browsed Song actions preserved playback and distinct duplicate queue IDs. Cover activation opened Song; scrubbing and cancelled preview stayed on Album.
4. Moved the current loaded occurrence without replacing its player, removed another occurrence, shuffled/unshuffled the edited order and cycled repeat modes using the actual queue controls.
5. Saved-song, whole personal playlist, playlist-item and Artist track Next/append actions used existing Collection/session adapters. Playlist Play retained three ordered occurrences, including two copies of the same recording.
6. Natural EOF advanced from the first copy to the second occurrence; Repeat one retained the occurrence, Repeat off stopped at the tail, Play restarted the ended occurrence, and Repeat all wrapped to the first occurrence.
7. Restart restored the same three queue occurrence IDs, duplicate order, paused position around six seconds and Repeat one. Resume played; paused Next remained paused. Clear retired the player and persisted an empty queue. The final visible inspection launch passed the cleared-state assertion and left the imported album open with its queue paused. Its scene capture was visually inspected for the large cover, icon controls and track runtime/action layout.

### Actual app: remote fallback

The existing `RemotePlaybackFlow.qml` was rerun against the real catalogue, acquisition providers, native player and audio output. **Stan (feat. Dido)** resolved to YouTube video `gOMhN-hfMtY`, recording `cd87bdcb-108b-5bc5-86e5-13daa63513ce`, after Tankorent reported no exact match. Opus stereo audio decoded through WASAPI with advancing playback. The final unchanged-buffering run (`remote-final-err.log`) passed seek to twelve seconds, pause/resume, seek to one second before the measured end, natural EOF queue advancement and profile retirement.

Two earlier last-second seeks timed out after mpv accepted the command and emitted seek-start without restart/EOF within ten seconds (`remote-fallback-timeout-err.log`, `remote-fallback-trace-err.log`). A five-second-tail control passed. A buffering-disabled probe also passed, but two subsequent normal-buffering controls and the final normal run passed without intervention. This does **not** establish buffering as the cause or qualify intermittent remote seeking as fixed. Diagnostic buffering overrides were removed; no speculative shared-player tuning was introduced. The failed probes remain an open Q1/Q6 reliability finding despite the successful final flow.

A separate fresh torrent-success probe requested **N.Y. State of Mind**. It found no exact torrent file and correctly chose YouTube; the probe's assertion expecting `torrent` failed. **No new public torrent-success qualification is claimed.** The existing strict matcher was not relaxed and the user's separate custom Search API work was not changed.

Evidence: `D:/Colosseum-Vinyl-Slice6-20261021` holds build/CTest logs, `record-err.log`, `restore-err.log`, `inspect-err.log`, `remote-final-err.log`, the earlier timeout/trace/control logs, the failed torrent probe, and scene captures. Databases, provider URLs, logs, screenshots and the fixture junction remain outside Git.

## Remaining gates

- Fresh torrent success, intermittent remote last-second seeking, manifest/network failure combinations and a measured startup budget remain Q1/Q2/Q6. A successful fallback is not a successful torrent test, and the final seek pass does not erase the earlier failures.
- Live multidisc/edition visual acceptance beyond the current fixture, broader device sizes and physical input acceptance retain their separate checklist gates.
- Radio/autoplay, new lyrics/video acquisition, related-card behaviour and recommendation work are outside this slice. Existing `Main.qml` `universeHallLayer` and offscreen font warnings remain unrelated.
