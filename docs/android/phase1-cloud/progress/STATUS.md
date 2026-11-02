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

## Next action

BR-SOURCE-REPLACEMENT (tasks/BR-SOURCE-REPLACEMENT.md).

## Blockers

- APK: hardened Nuvio engine prefix/donor not supplied; no Android SDK/NDK provisioned. Source/host work proceeds.
