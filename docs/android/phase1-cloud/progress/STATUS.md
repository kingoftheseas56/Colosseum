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

## Next action

PF-01 presentation-independent shared touch sizing (tasks/PF-01.md).

## Blockers

- APK: hardened Nuvio engine prefix/donor not supplied; no Android SDK/NDK provisioned. Source/host work proceeds.
