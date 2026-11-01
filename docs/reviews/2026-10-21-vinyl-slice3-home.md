# Vinyl slice 3: Home flow

Source baseline: `ed6245f03` on `codex/vinyl-production-flow`. This record qualifies this branch; it does not establish main-branch integration or a release.

## Implemented

- Actual Colosseum Main opens Vinyl on the existing QML Home, without a mandatory taste-selection step.
- Continue listening uses the current session and at most six resumable Progress checkpoints. Native checkpoint inspection validates profile, metadata and queue identity without replacing playback while rendering Home. Explicit resume retains saved position and occurrence order.
- Existing Collection records feed independent saved-song, saved-album, saved-artist and playlist shelves. Library/Discover queries and sorting cannot filter Home. See all opens the canonical Library section; playlist cards open the selected detail.
- Home cards route to existing Album, Artist, Song and Library destinations. Browsing a saved song does not replace the queue. Back restores the originating Home context.
- Empty Home offers icon-only Search and Library actions. Library Albums exposes one folder-import icon, including when empty. The removed global shortcut row stays removed.
- Missing local items are marked unavailable; empty shelves hide; invalid/foreign-profile checkpoints are rejected. A valid cleared-session checkpoint suppresses older Continue entries.
- Reused the existing ProfilePreferences, Collection, Progress, native session and player. No new persistence system, provider or player. Activity history remains distinct from resumable checkpoints.

Luna xhigh handled bounded Home/card presentation, Home regression coverage and the Library import icon. Root integrated data/navigation/checkpoint wiring and received builds and actual-app evidence.

## Verification

Release build targets `vinyl_personal_library_contracts`, `vinyl_native_session_bridge` and `colosseum_runtime_ready` passed. Final QML manifest: `9722047bc425855b606be485acdef9895bc7868185e0149493ad7c825d05c990`.

Four focused CTest registrations passed (18.17 seconds):

| Suite | Checks passed, including lifecycle |
| --- | ---: |
| Live-world native QML | 51 |
| Personal-library native QML | 13 |
| Home native QML | 14 |
| Native personal-library contracts | 10 |
| Existing prototype Home QML, separately run | 8 |

Total: **96 checks passed**. Home coverage includes cold entry/import reachability, independent projections, typed destinations/Back, pure checkpoint reads, queue-preserving resume, profile retirement, foreign checkpoints, explicit empty sessions and failed writes.

### Actual Main flow

`tests/vinyl-live/HomeFlow.qml` instantiates real Main and native services with disposable app-data profiles. It activates existing QML controls and their production handlers; it is not a standalone replacement UI.

Fresh profile `vinyl-slice3-local-c420`:

1. Landed on Home without taste answers. Search opened focused Discover; Back returned Home.
2. Home Library action opened Albums. Its folder icon opened the existing FolderDialog; the driver supplied the user-provided Illmatic directory to its real acceptance handler. Imported one album and ten tagged tracks. The physical OS-picker interaction was not automated.
3. Saved a song and artist, created a playlist through the existing picker, and found all four saved shelves. Opened each card type and returned Home without unrelated playback changes.
4. Added a second playlist song and played its second occurrence through the existing local player; playback advanced. Continue opened Song without restarting or reallocating the queue.
5. Playlist See all opened the list instead of a stale detail. A deliberately empty Library search did not alter Home.
6. With Activity history disabled, paused and saved at 7.25 seconds, quit normally and relaunched. Continue resumed the same second occurrence above 8.5 seconds with identical queue IDs and ordering.

Remote flow separately reuses the isolated slice-2 profile containing real YouTube Music catalogue saves. Home playlist → Library detail → existing resolver/player played the saved recording **One Love (LG Main Mix) — Nas**, source **youtube**, advancing to 1.111 seconds. This does not requalify every provider or the Tankorent resolver.

Evidence directory: `D:/Colosseum-Vinyl-Slice3-20261021` contains build/CTest output, actual Main record/restore/remote logs and scene captures. Test databases, logs and captures are intentionally outside Git.

## Boundaries

- Progress snapshots support Continue; an Activity-backed listening timeline remains checklist H3. Public discovery, artist-follow releases and recommendations remain H5/H6/H8.
- Local Home projections do not depend on a successful catalogue request. Whole-app network-disconnected operation and online per-shelf loading/stale/retry are separate qualification work.
- Native Windows Main was opened for inspection. App scene captures and UI accessibility state are evidence; the Windows capture tool returned wallpaper instead of Qt pixels, so physical pointer/pixel acceptance is not claimed from that tool.
- Existing `Main.qml` `universeHallLayer` and offscreen font fallback warnings remain outside this slice. No corresponding Vinyl load/binding failure remained in successful flows.
- Initial verification-command attempts omitted runtime DLL directories or used an incorrectly decoded fixture path. Corrected invocations passed; these were setup/driver errors, not accepted product results.
