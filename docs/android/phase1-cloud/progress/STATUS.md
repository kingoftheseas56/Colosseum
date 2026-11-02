# Cloud progress

State: IN_PROGRESS. Branch `codex/android-phase1-cloud-20261103` (pushes accepted by origin). Credit balance: UNKNOWN.
Audited base 501b23edba022a82666bd25e64f4f02ad8bb9a9f (ancestor, product source unchanged at start); Alpha 9e896bfd5dac40ef3ceb0e26e166e85ed8beecd9. Starting cloud HEAD 92e762b0.
Physical acceptance: LOCAL_PENDING on OnePlus 11R and Nokia T20 for every unit.

Host environment (session-only, not committed): aqt Qt 6.11.1 gcc_64; `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software LC_ALL=C.UTF-8 QML_XHR_ALLOW_FILE_READ=1`.

## Units

See `units.json` for per-unit status/tests/files. Summary log (UTC):

- 21:50 C0 — HOST_QML_PASS. Details: `C0-RESULT.md`. ANDROID_APK BLOCKED (no hardened Nuvio prefix in cloud).

- 22:00 B0 — HOST_QML_PASS. Overlay applied (8/8 hashes matched), all changes Android/androidHost-scoped; outer margin made fixture-testable via AdaptiveLayout.androidHost. New tests/qml/tst_android_visual_baseline.qml. Java change not compiled (NATIVE_COMPILE not attempted).
- 22:20 PF-02 — HOST_QML_PASS for SourcesSheet (new tst_android_sources_budget.qml, reproduced pre-fix). Comic/Manga sources pages: analogous budget pending (PF-02b).
- 22:30 PF-01 — HOST_QML_PASS (Downloads text actions 48 hosts; TopBar 48-tall hosts, width limited by pre-existing picker/system overlap at Android widths).
- 22:40 PF-05 — HOST_QML_PASS (speed popup bounded/scrollable; pre-fix y=-77 at 1.20).
- 22:50 BR-READER-TARGETS — HOST_QML_PASS (selection 48 hosts, bookmark delete gutter).
- 23:00 WT-02 — HOST_QML_PASS (account IME avoidance/focus reveal).
- 23:20 WT-04 — HOST_QML_PASS via native PlatformRuntime harness; full tests/qml suite has no new failures vs base (progress/HOST-QML-SUITE.md).
- 23:35 BR-SOURCE-REPLACEMENT — HOST_QML_PASS (native SessionStore harness built with g++; Main warm/active dispatch QML extract).
- 23:45 PF-04 — PORTABLE_TEST_PASS (Continue missing download -> exact Sources + explicit resume).
- 23:50 PF-03 — PORTABLE_TEST_PASS (Theatre/Sources nested native Back).
- 23:55 WT-10 — PORTABLE_TEST_PASS (Galaxy/DCAU nested Back).
- 00:05 WT-06 — PORTABLE_TEST_PASS (Featured Read/Details + cancellable deferred read).
- 00:10 WT-05, WT-07, PF-06 — PORTABLE_TEST_PASS.
- 00:20 WT-03 — HOST_QML_PASS (advisory decoder compatibility).
- 00:25 PF-08, PF-14, WT-08 — HOST_QML_PASS.
- 00:40 WT-11 — HOST_QML_PASS (East Blue containment).
- WT-01 deliberately sequenced after the smaller portable units (PF-10, PF-07, PF-09, WT-12, WT-14) so they are banked first; then WT-01, PF-02b.

## Next action

PF-10 (tasks/PF-10.md).

## Blockers

- APK: hardened Nuvio engine prefix/donor not supplied; no Android SDK/NDK provisioned. Source/host work proceeds.
