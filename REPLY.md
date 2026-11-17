STATUS: COMPLETE

Audiobooks 1.4.0 — slice D, recording identity
Worker handoff for Claude (Agent 0); worker completion is not approval.
Branch: audiobooks/d-identity
Worktree: D:\Audiobooks140\d-identity
Starting base: 4f137612ed4dae2440d4247890e8bc318f401d60

FILES CHANGED (code commit)
- native/engine/AudiobookDownloader.h, AudiobookDownloader.cpp:
  distinct recordingKey for storage/download state/cancellation/deletion; work pairKey
  remains a resolvable alias (one recording, explicit preferred, or last played).
  Persist metadata/provenance, preserve legacy index keys and original folders,
  expose recordingsForWork/default/recent APIs, and isolate multi-work bundles.
  Legacy downloadAudiobook callers retain work-key signals.
- native/engine/AudiobookRecordingMatch.h, AudiobookRecordingMatch.cpp:
  conservative pure matcher and QObject/QML wrapper. Exact shared identifiers
  cannot override language/narrator conflicts. Probable requires unique work,
  narrator overlap and language agreement. Ambiguous/bundle/conflict => none.
- native/CMakeLists.txt, native/main.cpp: compile/register matcher context property.
- qml/AudiobookSession.qml: resolve work to recording before opening, store resume
  under recordingKey, and mark that recording as last played.
- qml/reader2/ReaderShell.qml: thread explicit recordingKey through read-along
  while retaining work pairKey and paired EPUB identity.
- qml/Main.qml: continue-collection backfill uses work pairKey; skip bundles.
- tests/audiobook_torrent_transport/test.cpp:
  distinct folders/delete isolation; preference and last-played fallback;
  legacy index support and legacy/new coexistence; detached bundle coverage.
- tests/audiobook_torrent_transport/recording_match_test.cpp:
  twelve conservative match cases including Dune conflicting-narrator negative.
- tests/audiobook_torrent_transport/session_recording_test.mjs:
  real session function regression for independent progress and ambiguity.
- tests/audiobook_torrent_transport/biblio_audiobook_test.mjs:
  re-align old fixture to actual current BiblioBook controls/generation checks;
  preserve existing behavioral assertions; no BiblioBook.qml change.
- tests/qml/tst_audiobook_activity.qml: different recordings yield distinct
  activity identities.
- tests/CMakeLists.txt, tests/audiobook_torrent_transport/CMakeLists.txt:
  register test targets, recording-session test, and Windows Qt runtime DLLs.
- REPLY.md: this handoff only.

COMMITS
- 4d091d741decb2c29aa0a2503159c9f1372a4520
  [audiobooks/d-identity] Separate recording identity from work pairing and playback progress
- REPLY.md is in its own local handoff commit with the required prefix;
  use git log -2 on this branch to see that commit's SHA (a file cannot contain
  its own commit SHA). No push, merge, rebase, reset, stash, or clean.

TESTS — OBSERVED RED THEN GREEN
- RED baseline: node tests\audiobook_torrent_transport\session_recording_test.mjs
  --baseline (reads HEAD-before-change session): exit 1; first bare-work-key
  assertion failed (old implementation did not select a recording).
  GREEN edited source: same test without --baseline, exit 0; 13 assertions.
- RED pre-existing: node tests\audiobook_torrent_transport\biblio_audiobook_test.mjs,
  exit 1 at callbacks.length (actual 0 vs expected 1), because fixture missed
  actual current BiblioBook flags/references. GREEN re-aligned fixture,
  exit 0, original checks retained.
- GREEN: audiobook_recording_match_test.exe: 14 passed, 0 failed,
  0 skipped (QtTest report: D:\Audiobooks140\build\d-identity\matcher-results.txt).
- GREEN: audiobook_transport_test.exe: 34 passed, 0 failed,
  0 skipped (QtTest report: D:\Audiobooks140\build\d-identity\transport-results.txt).
- GREEN: colosseum_qml_tests.exe -input
  D:\Audiobooks140\d-identity\tests\qml\tst_audiobook_activity.qml:
  15 passed, 0 failed, 0 skipped, with Windows Qt runtime.
  QWARN: test window not exposed; assertions still passed. Initial offscreen
  execution could not find the offscreen platform plugin and timed out;
  successful run used Qt Windows platform and Qt 6.11.1 imports.
- GREEN: node --check tests\audiobook_torrent_transport\session_recording_test.mjs,
  exit 0; git diff --cached --check, exit 0 before code commit.
- GREEN: cmd.exe /c D:\Audiobooks140\build.cmd d-identity
  colosseum_qml_tests => BUILD_OK, exit 0.
- GREEN: cmd.exe /c D:\Audiobooks140\build.cmd d-identity
  colosseum audiobook_transport_test audiobook_recording_match_test
  => BUILD_OK, exit 0, full Colosseum target linked.
  Approved-helper build log: D:\Audiobooks140\build\d-identity.log.
- QtTest native RED runs were not captured before implementation because
  the shared build lock prevented compilation. Do not infer an observed native
  RED state from the test source.

WHAT WAS NOT VERIFIED
- No real AudioBookBay/torrent downloads, AudioSilo live catalogues, or
  real multi-work bundle mapping; native fake-engine/QtTest coverage only.
- No launch of installed Colosseum, no mutation or inspection of Hemanth's
  actual library/profile, no manual Windows end-to-end read-along workflow.
- No whole-application QML runtime suite, CI, deployment, Cloudflare publish,
  or Colosseum-Data updates. The application compiled/linked but was not launched.
- Claude/Agent 0 review, integration, and approval remain outstanding.

BOUNDARY
Only the assigned worktree and approved source/test files were changed.
No push or merge. Source commit plus local REPLY handoff commit only.
