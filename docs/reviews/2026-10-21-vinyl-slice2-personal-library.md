# Vinyl slice 2: saved songs and playlists

## Implemented

- Library destinations for Albums, Artists, Songs and Playlists, using the existing shared world tabs and icon actions.
- Album-row and Song-page save/unsave and playlist pickers; saved-song titles play in place, while their covers open Song.
- Empty playlist creation, naming, adding tracks or albums, independent duplicate occurrences, reordering, removing, deletion, detail Back and route restoration.
- Play, selected-item play, shuffle and append through the existing `VinylBackendSession`, native queue and player. Playlist ordering remains independent of queue shuffle.
- Exact album/edition/disc/track references. Unavailable items remain editable, play-all excludes them, and an unavailable saved song can still be removed.
- Removed the five global integration shortcuts and their wrapper. Header, Album, Artist and Song reclaim the space. TopBar search and player cover/queue actions remain the existing destinations. Import dialogs and callable import APIs remain; a dedicated Library import-management entry is follow-up work.

## Ownership and integrity

Collection remains the only personal-library persistence owner. Native personal commands validate complete rows and perform one `replaceItems` transaction for retained source metadata plus membership/playlist changes. A saved online song does not add its album to saved Albums. Existing source metadata is preserved when saving an older queue snapshot.

BA5 playlist/name and item identity validation and BA1 ordered occurrence operations are reused. Local/provider IDs are retained without inventing recording MBIDs. Duplicate items have distinct IDs; legacy playlists receive deterministic IDs and are upgraded on their next mutation. No new database, player, history or provider stack was added.

Receiving fixed initial native-store/profile binding order, a reactive `rowFor` mutation loop, duplicate picker instances, a picker callback accessing a destroyed delegate context, missing playlist Back behavior, accidental focus highlighting, queue truncation, unavailable-save removal, and QVariant sequence normalization for multidisc navigation.

## Verification

Source base: `c901e9995`, branch `codex/vinyl-production-flow`. Windows Release build targets `colosseum_runtime_ready`, `vinyl_native_session_bridge` and `vinyl_personal_library_contracts` succeeded with Qt 6.11.1/MSVC 2022.

Focused CTest registrations:

```text
colosseum.vinyl_personal_library
colosseum.qml.vinyl_personal_library_native
colosseum.qml.vinyl_live_world_native
```

The three registrations passed: 9 native, 13 personal-library QML and 51 world QML checks, including lifecycle checks. Coverage includes atomic invalid/write-failure paths, duplicate/legacy edits, unavailable files, exact multidisc navigation, stale snapshots, type collisions, same-profile retirement/reentry, queue capacity, pointer/keyboard routing, dialog search, reachable album actions and shortcut-row absence. Existing tests were updated for the already-shared artist tabs, deliberate focus restoration, large scrollable artwork, and the new Library destination.

### Actual application

`tests/vinyl-live/PersonalLibraryFlow.qml` derives from the real `Main` and runs in the actual executable with the real native services, Collection, provider and player. Test profiles use isolated `COLOSSEUM_APPDATA_TAG` values.

1. Imported the supplied tagged Illmatic folder: 1 album, 10 tracks.
2. Saved from an album row and Song page; created an empty playlist through its dialog.
3. Added duplicate tracks, moved one exact occurrence and removed the other; renamed the playlist.
4. Returned to the playlist list, added another song through the Album picker, and reopened detail.
5. Observed correct local playback advancing, playlist append from two to four queue entries, shuffle, saved-song removal and saved-song playback.
6. Quit normally, restarted with the same profile, found saved songs and the renamed playlist, played its second item with the correct queue index, and deleted the playlist while retaining the saved song and source album.
7. Separately searched the real YouTube Music catalogue for Nas/Illmatic, opened a real album, saved a track, created a playlist through the picker, found it in Library, and played it through the existing resolver/player. The selected recording was **One Love (LG Main Mix) — Nas**, source **youtube**, with advancing playback at 0.619 seconds. This proves this playlist path; it does not qualify all provider items.

Local record/restart also passed in visible native Windows rendering after shortcut removal. Captures were inspected. Functional offscreen runs are separate from Windows visual evidence.

Local evidence directory: `D:/Colosseum-Vinyl-Slice2-20261021` (build, CTest/native/QML outputs, record/restore/remote logs and UI captures). Logs/captures and test-profile databases are not repository artifacts.

## Remaining boundaries

- Broader live provider-expiry, different-account switching, import-management UI and search-result save controls remain checklist work.
- Playlist append is implemented; explicit Play next across every entry point remains Q3.
- Saving metadata does not download audio. Shared playlists, synchronization and recommendations are separate scope.
- Offscreen tests report the Qt font-directory fallback warning. The actual app retains the pre-existing `Main.qml` `universeHallLayer` warning; no corresponding Vinyl load/binding failure remained in the successful runs.
- Authored, built, automated-tested and actual-app-tested are recorded here. Branch push, main integration and release are separate states.
