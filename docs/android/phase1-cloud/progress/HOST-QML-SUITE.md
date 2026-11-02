# Host tests/qml suite baseline

Captured 2026-11-02T22:19:12Z after WT-04 (HEAD before commit 3ecd2c2). Qt 6.11.1 gcc_64 offscreen/software, QML_XHR_ALLOW_FILE_READ=1, 60 s cap per file, qmltestrunner -input <file>. 157 files; all others reported 0 failed.

Every non-passing file below fails identically (or worse) on the unmodified audited base 501b23ed tree; none is introduced by Phase1 work.

| File | Phase1 tree passed/failed | Base 501b23ed passed/failed |
| --- | --- | --- |
| WatchPartyVisualHarness.qml|timeout >60s|timeout >60s |
| tst_account_data_privacy.qml|timeout >60s|timeout >60s |
| tst_android_tv_navigation.qml|2/1|2/1 |
| tst_feria_taskbar_context.qml|2/1|2/1 |
| tst_k06_account_system_events.qml|4/2|4/2 |
| tst_main_book_return.qml|0/1|0/1 |
| tst_one_piece_badge_spacing.qml|10/4|8/6 |
| tst_one_piece_east_blue_native_atlas.qml|17/5|15/7 |
| tst_open_media_control.qml|3/1|3/1 |
| tst_player2_progress.qml|0/1|0/1 |
| tst_privacy_clears.qml|6/1|6/1 |
| tst_topbar_spatial_navigation.qml|16/1|16/1 |
| tst_vinyl_album_page.qml|13/2|13/2 |
| tst_vinyl_artist_model_replacement.qml|2/1|2/1 |
| tst_vinyl_artist_page.qml|timeout >60s|timeout >60s |
| tst_vinyl_artist_repair.qml|11/5|11/5 |
| tst_vinyl_artist_scrollbars.qml|3/1|3/1 |
| tst_vinyl_artist_shared_picker.qml|6/1|6/1 |
| tst_vinyl_live_artist.qml|6/6|6/6 |
| tst_vinyl_local_controls.qml|4/1|4/1 |
| tst_vinyl_personal_state.qml|6/1|6/1 |
| tst_vinyl_song_page.qml|19/2|19/2 |
| tst_vinyl_world_journeys.qml|3/1|3/1 |
