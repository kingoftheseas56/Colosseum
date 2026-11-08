# Live reading implementation ledger

Authority: HANDOFF.md; source baseline 3a33968f5ea37769bc6ba9b22a58fd0bae33d0fe on shared master. The unrelated changes are recorded in BASELINE.json and are not owned by this task.

## Checkpoints
1. Native pending-page contract: RED fixture, minimum core/decode support, focused and existing reader tests.
2. Existing MangaDownloader live mode: bounded focus-first jobs, three-slot arbitration, reuse/persistence separation, chapter LRU/cap, cancellation and failure fixtures.
3. Existing Tankoyomi setting and Chapter Mode QML adapter/wiring, Continue and crossing, stale-session tests.
4. Regressions, one desktop build and bounded Wi-Fi runtime smoke; inspect actual CI and both remote refs after scoped commits.

## Rulings
- QML live page-store adapter, not a second native scraper: delegates all network and disk ownership to MangaDownloader.
- Existing TankoyomiConfigurationPage is the setting surface; persist the opt-in flag in TankoyomiConfigurationStore beside language/provider settings.
- Core arrival API carries entry ID and local URL as well as page index so unrelated-entry callbacks are rejected and a response MIME extension change is handled.
- Pending is a boolean alongside the typed error, not MissingFile; decode refuses pending sources without a cooldown.
- Keep the handoff's whole-chapter cache (current plus two LRU, 300 MB), superseding the earlier suggested per-page eviction.
- No new VPN measurements or broad source downloads.

## State
Native pending-page contract authored in six existing comicreader C++ files. New tests/comicreader_live_pages_harness.cpp compiled and exercised actual production sources: CORE-RED.log records the expected missing-feature failure; CORE-GREEN.log records CTest 1/1 PASS (0.68s test). This proves the isolated native pending/arrival/error/retry contract, not an assembled application or the full live-reading feature. New tests/manga_live_reading_harness.cpp is authored but not yet compiled or run; its API must be checked against MangaImageHostResolver before qualification. No downloader implementation, setting or Chapter Mode wiring, real-provider smoke, commit or push yet. Build artifacts are in D:/LiveReadingTests-20261108.

2026-11-08: Hemanth asked to use window automation to read the open Claude Code conversation. Windows UI Automation TextPattern read succeeded for the existing Harbor community feature roadmap session, with screenshot confirmation. His visible instruction explicitly requests implementing directly on master and committing. Claude confirms the same HANDOFF.md scope, tests plus one real reading smoke before commit, scoped staging and both remotes; no chat message was sent or task interrupted in Claude.

Next: review the native core delta against existing reader regressions, then compile the downloader RED fixture and implement bounded live jobs using the existing transport. Full feature remains incomplete and uncommitted.

## End-to-end continuation, 2026-11-08
Hemanth explicitly authorized: "You're on pro, implement this from beginning to end." Continuing all four checkpoints without an inter-checkpoint permission stop. Current master remains 3a33968f5; the index was empty. Prior six core edits and two new harnesses are this mission's owned delta; all baseline unrelated work stays untouched.

Ruling: live entry waits for existing permanent/thumbnail request slots to drain, blocks new permanent/thumbnail waves, and then admits the live chapter. This preserves permanent requests already on the wire without abort/refetch bursts; while the live reader is open the three-slot ceiling is shared. It can delay startup when a pre-existing download is slow, rather than exceeding the measured profile.
Ruling: serialize temporary-cache filesystem work with a shared worker-side mutex and retain stopped-job lifetime until pending saves/scans finish. This prevents an immediate close/reopen or LRU eviction racing a late file write. No network policy or permanent ownership moves to QML.
Verification setup: the resumed downloader fixture initially failed compilation because its isolated CMake omitted the existing Tankoyomi crypto/add-on-store dependencies. Those dependencies are now included in the mission-only build; this is not yet a feature RED result.

## Machine interruption and current source boundary (investigated 2026-11-08)
Laptop 1 entered firmware-requested thermal hibernation Nov 8 at 01:06 IST (Kernel-Power Event 88, CPUZ HOT 373K; recurring Oct 30 and Nov 5). A separate Nov 7 bugcheck 0xCE flags possibly related wireguard.sys. Detailed verified Windows log findings, safe cooling/driver recommendations and D: dirty warning are at POWER-INTERRUPTION-20261108.md. These are distinct from Colosseum defects; do not repeat VPN tests or start unattended heavy builds before thermal qualification.

In the immediately preceding Pro turn, the isolated two-case CTest completed with **comicreader_live_pages PASS, manga_live_reading FAIL (expected RED: startLive unavailable)**. The interrupted source now contains partial declarations in MangaDownloader.h and partial helpers in MangaDownloader.cpp, but not the constructor/live job integration: source changes are **uncompiled, unfinished and uncommitted**, and the last write received `Session terminated`. The current BDC connection is restored; diagnosis was read-only. Do not trust a partial source snapshot as buildable. Next: stabilize the laptop and inspect the actual diff before continuing the same test-first downloader slice.
