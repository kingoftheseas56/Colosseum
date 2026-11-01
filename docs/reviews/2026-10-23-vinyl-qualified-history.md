# Vinyl recommendation learning from qualified listening

Date: 2026-10-23. Base: `9a629de27`; branch: `codex/vinyl-production-flow`.
Scope: personalized Home Task 5, including the feedback and exposure controls needed to exercise it in the existing app.

## Implemented

The existing recommendation service now reads the profile's retained Activity and History owners through `VinylRecommendationHistory`. Qualified listening still comes from the existing playback tracker's ten-second gate. Starting playback, seeking, action facts and repeated timer samples cannot create counted plays. Overlapping intervals are counted once per session; conflicting identities fail closed. Unknown historical intent remains unknown, and `historyComplete` remains false.

The existing Activity writer retains a device-local `music_action` fact for actual start, skip, natural EOF, stop and failure, together with typed recording/artist/release identity and manual/album-continuation/autoplay origin. These facts contribute neither listening time nor user-facing completion/history by themselves. They are excluded from portable sync import/export and other worlds' projections. There is no second personal event log, history store, player or library.

The existing BE derivation feeds play counts, replays, qualified natural completion and repeated early skips into BE3 ranking. One skip is weak evidence and does not become an explicit dislike. Automatic album continuation is distinct from a manual choice. Onboarding affinity remains at full weight through 30 qualified manual sessions, becomes 0.75 for 31–100, and 0.5 thereafter. Explicit saved-artist/library affinity remains full strength.

Explicit feedback uses the existing BE2 snapshot and atomic Preferences setter: like, dislike, neutral, hide/unhide, seven-day snooze/unsnooze and exclude/reallow artist. All known credited artists are checked for exclusion, including collaborators on saved albums. Hidden and snoozed items can be recovered through the existing selected-artist or saved-artist card menu after the recommendation card disappears. Profile-generation fencing rejects stale actions.

Visible recommendation cards record exposure only when they intersect the actual vertical and horizontal viewports. Hidden pages, offscreen shelves and model construction do not create exposure. Writes deduplicate a visit/shelf/entity across ranking rebuilds. The existing Preferences owner stores a profile/reset-scoped envelope, limited to 4 MiB and the latest 2,000 records within 14 days; malformed envelopes cannot partially apply. History-off stops these writes and removes derived ranking input/exposure. Existing history reset invalidates it while preserving explicit taste and feedback. Valid post-reset exposure survives service restart.

Feedback uses the existing icon affordance, custom Glass popup and keyboard controls. Pointer entry does not automatically frame the first action. Closing the popup precedes feedback mutation because applying feedback may immediately replace its delegate.

## Receiving fixes

A bounded independent receiving review identified three important gaps: unreachable recovery after suppression, lost collaborator credits on library candidates, and valid post-reset exposure being cleared on service rebind. The two native regressions were run RED, then GREEN after repair. Recovery was covered by native/QML tests and the actual Main journey, including Skip plus a saved artist without an onboarding artist shelf. Runtime inspection also caught the destroyed-delegate menu-close error; the final flow is free of that error.

Luna xhigh supplied bounded Preferences persistence, Activity action validation and QML/test work. Root handled the read adapter, producer integration, derivation/ranking/privacy behavior, receiving fixes and actual app qualification.

## Verification

The production Windows application, native session bridge, recommendation service/history targets and affected account tests built successfully. Final `colosseum_runtime_ready` refreshed the source-QML manifest to `2bf9ca075c8a6d557fda82d5decd5c3a1dd72a56e563fa84db2f73e9d4531cf3`.

The focused service suite includes feedback/suppression, profile fences, seed-decay boundaries, saved affinity, collaborator exclusion, restart after reset, exposure deduplication, malformed/cross-profile envelopes, 14-day expiry and the 2,000-record bound. History/action tests cover seek/start without qualified time, stop versus EOF, failure versus skip, repeated/overlapping samples, conflicting identities, reload, local-only sync and privacy reset. QML tests cover visible versus offscreen exposure, feedback without navigation/playback, suppression recovery and pointer focus behavior.

**32/32 focused suites passed:** the final 31-suite regression run took 155.82 seconds, and the separately rebuilt recommendation-service suite passed in 2.94 seconds. The selected Activity suites were `activity_projector`, `activity_store`, `activity_sync`, `activity_playback_tracker`, `consumption_history` and `profile_activity_isolation`. Vinyl coverage included the new history/actions tests, feedback/taste persistence, BE1/BE2/derivation/BE3/contracts, catalogue/facade/mpv events, native session bridge, local library, Home, onboarding, discovery/search, seek and playback/queue actions. The service QtTest reports 26 cases, including init/cleanup.

`tests/vinyl-live/RecommendationLearningFlow.qml` runs production Main, the native importer, profile stores, recommendation service and existing mpv player through actual semantic controls. It imports three tagged local silent WAVs into one album, saves its artist/album after Skip, then:

1. Reveals the recommendation card and verifies real viewport exposure.
2. Likes it, checks explicit-positive ranking evidence, hides it, and restores it from the saved artist's menu.
3. Starts and seeks the actual player; neither start nor seek position counts as qualified listening.
4. Listens past the qualification gate and checks retained qualified Activity plus play-count ranking evidence.
5. Uses Next and checks retained manual skip without one-skip negative ranking.
6. Allows the second track to end naturally and checks qualified EOF plus completion evidence.
7. Lets automatic album continuation qualify while the manual-session count remains two.
8. Disables history and checks zero derived sessions/exposure, unchanged explicit feedback, advancing playback and unchanged queue occurrence IDs.
9. Re-enables and clears the existing consumption history; checks zero derived manual sessions/exposure immediately after reset, preserves feedback, and returns to Home. Viewing a card afterward may legitimately create new post-reset exposure.
10. Restarts the same disposable profile and verifies feedback persistence without resurrecting cleared listening evidence.

The final learn and restart journeys passed with profile tag `VinylLearning20261023E`. Its recorded samples show manual counts 1 after qualification, 2 after natural completion, and 0 while history is off and after clear. Explicit feedback remained 616 bytes throughout. The driver uses semantic handlers; physical pointer/keyboard qualification is separate. Rendered Home and feedback captures were inspected. Silent local fixtures qualify real player/history behavior, not new remote playback or Tankorent success.

Machine-local receipts: `D:/Colosseum-Vinyl-History-20261023/`, including receiving RED/GREEN logs, final build/test logs, `learn.stderr.log`, restart logs, snapshots and rendered captures. Audio, databases, logs and captures are not committed. The driver takes the fixture-folder and evidence-folder arguments; its fixtures are 60/16/60-second mono WAVs tagged with artist `Vinyl History Fixture`, album `Qualified History Fixture` and track numbers 1–3.

The final application was also reopened on the existing Ilaiyaraaja/SPB qualification profile using the preceding slice's production Main inspection driver. Its real catalogue Home checks passed and the responsive app was left open. This inspection is separate from the local listening-learning fixture journey above.

## Remaining scope

This completes Task 5's learning backend and minimum usable feedback/exposure integration. Task 6's full Home presentation, rediscovery/less-played rows and complete cold/offline states, and Task 7's full cross-profile/live-source receiving matrix remain open. This slice does not establish subjective recommendation quality, new remote playback, main-branch integration or release. Main's existing unrelated `universeHallLayer` warning and intermittent background network failures remain outside this change.
