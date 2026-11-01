# Vinyl slice 1: state and navigation receiving

Date: 2026-10-21. Candidate follows `022e0241a` on `codex/vinyl-production-flow`.

## Implemented

- Separate transient Home, Discover and Library query, lens, sort, mode and view/list/scroll state. Collection, Progress and the existing player remain authoritative.
- Home uses the complete library/taste projection independently of search; only imported/local albums receive the on-device carousel label. The inactive local-filter field is hidden on Home.
- Discover defaults to catalogue search. Local mode belongs to Discover; Library remains its own collection projection. Results retain their submitted query and cannot be presented as matches for a different query.
- Both search entry points reach a visible, focused Discover field from detail pages. Search detours preserve a Back route, retire pending browse work and restore origin page/query/list/scroll/focus.
- Returning to Library retains a new Discover search; returning to an originating Discover detail restores that origin's query/results. Tab switching preserves each tab's See all detour and its own return offset.
- The library carousel CTA opens the main Library page, even if a Library list detour was previously saved. Track rows continue to play in place; browsing does not replace the playing identity.
- Catalogue/local empty text reflects the active mode. Catalogue request status no longer appears on unrelated Home/Library pages.

Luna xhigh implemented the bounded navigation changes and performed a read-only receiving review. Two receiving findings (stale Discover view and a saved Library-list CTA) were reproduced in failing regressions, repaired and rechecked. The main agent implemented/reviewed state ownership and independently ran the checks below.

## Build identity

- Target: `colosseum_runtime_ready`, existing Release/MSVC/Qt 6.11.1 build. Exit 0; no native recompilation was required for these QML changes.
- Native executable SHA-256: `229B8969A57D0B6F86A0889C50518D976E369062FF5DD286F39E9FBE4B194224`.
- Final runtime QML manifest: `07c1b73bf1194943426e28ff9c583a5068248a9e9afb13e2a67784710151651e`.
- Final-source hashes and process/tag identity are retained in the local slice evidence receipt. Logs, screenshots and profile databases are not committed.

## Focused checks

| Check | Result / boundary |
| --- | --- |
| `tests/qml/tst_vinyl_live_world_state.qml` | 12 passed including lifecycle cases; initial seven cases failed against the old projection. Additional route/independent-search regressions were also observed failing before repair. |
| Selected `tests/vinyl-local-session/tst_live_world.qml` cases through `vinyl_native_session_bridge` | 19 passed including lifecycle cases: 17 state/navigation/local-search/playback-identity tests. Includes new search detours, independent list routes, canonical Library CTA, neutral pointer entry and keyboard ignition. |
| `tests/qml/tst_vinyl_world_navigation.qml` | 11 passed including lifecycle cases; its tab-return assertion now retains the list offset first, then checks shelf offset after Back. |
| `tests/qml/tst_vinyl_discover_page.qml` and `tst_vinyl_home_page.qml` | 11 and 8 passed respectively, including lifecycle cases. Existing prototype compatibility checks; these are not the production-app proof. |
| `git diff --check` | Passed; normal Windows line-ending notices only. |

The worker's earlier unfiltered native world-file attempt reported other UI assertions and lacked a complete final receipt. That attempt is not presented as a passing whole-file/suite run. This slice's receiving gate is the independently rerun focused set plus the application journey. No whole-product test-suite claim is made.

## Actual application journey

`tests/vinyl-live/BrowseStateFlow.qml` derives from the actual `Main` and runs with the production `colosseum.exe`, native catalogue source, stores, import pipeline and player in a disposable profile. Arguments supply a tagged music folder and an evidence directory; successful runs pause playback and retain the final Home view for inspection.

The journey exercised:

1. Import the supplied tagged Illmatic music and find a real 10-track album.
2. Apply a no-match Library query; Home still has its library shelf. Return to Library and recover the query/sort/scroll.
3. Open Album and activate its second track. Verify in-place playback and advancing local audio.
4. Enter Discover from Album, submit a real `Nas` catalogue search, receive results, and verify the same local track continues advancing.
5. Back to Album; use the mode icon to reach focused local Discover; Back to Album and Library with the original Library context.
6. Re-enter Discover and recover its retained query/mode/catalogue results; open the real Nas artist through an artist See all list.
7. Search from Artist; Back to Artist and the prior Discover list. Return Home and verify its shelf identities are unchanged.
8. Open the playing Song, enter Search, Back to Song, then pause and return Home with the playing identity intact.

Final-source run emitted:

```text
VINYL_BROWSE_PASS {"albums":1,"tracks":10,"track":"vault:fca693b3c422974eeb00960d0696d194f14e8a1e","position":19.814,"query":"","tab":"home","source":"local"}
```

Local evidence directory: `Colosseum-Vinyl-Slice1-20261021`, with `accepted/flow.stderr.log`, process/source receipts, and Home/Discover captures. The earlier visible Main run also passed. Physical Escape stopped Computer Use during this task; no further Computer Use input was issued. The final-source application rerun used Qt's offscreen platform and real native services/player. It qualifies the automated journey, not a final pointer-driven visual review. Owned test processes were retired after evidence capture.

## Remaining boundaries

- Home content expansion, playlist/song-save journeys and toolbar relocation remain later checklist slices. This is the prerequisite state/navigation slice.
- Full provider-failure, offline/profile-switch runtime, responsive visual and release qualification remain separate gates. Profile reset and cancelled/stale query behavior have focused regression coverage here.
- Offscreen Qt reported a missing font directory; captures are functional evidence, not typography approval. The known `Main.qml` `universeHallLayer` warning was also observed; this slice did not modify that owner.
- The candidate is committed/pushed on its working branch separately from main-branch integration or release.
