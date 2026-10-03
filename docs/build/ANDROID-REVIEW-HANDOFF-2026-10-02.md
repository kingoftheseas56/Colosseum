# Android review handoff — 2026-10-02

## Status: BLOCKED — ALL ANDROID RUNTIME CLAIMS BELOW RETRACTED (2026-10-03 review)

**Retraction.** Claude's review
(`agents/REVIEW-GLM-android-checkpoint-A-2026-10-03.md` in the Brotherhood repo)
opened all 14 evidence screenshots and found none shows a book page: the
"page turns" are Tankoban's home banner auto-rotating, the "error screen" is a
blank wallpaper, and the "Theatre/Biblio" proofs show Tankoban. Every Android
runtime PASS/PARTIAL row in the table below, the touch-delivery defect
narrative, and the A/B control verdict are **retracted as unverified**. Root
cause of the false claims: all screenshot interpretation went through an
external image-description service that invented details, and GLM trusted it.
The warning sign GLM dismissed: the app's own Lanista scene introspection never
once showed `ReaderShell`/`ReaderPaper` items, i.e. the reader was likely never
open at all during those probes — the introspection was right and the vision
tool was wrong. Treat the Android half of this document as void until Claude's
direct emulator walk produces real evidence.

**What still stands (mechanically verified, not vision-dependent):** the git
history and branch state, CI run IDs/conclusions, artifact names and SHA-256s,
the APK loader-alignment verifier output (`ANDROID_RUNTIME_BUNDLE_OK`), the
desktop harness exit codes, the ReaderOverlay desktop pass-through fix and its
qmllint result, and the desktop-ci baseline analysis (merge-base run failing
exactly the six pre-existing tests; branch Linux gate ending with only those,
minus the flake that passed on re-run). Claude verified the desktop half
independently.

Executor: GLM (ZCode) on the `android-startup-fix` worktree, per Claude's
2026-10-02 handoff. Retraction added 2026-10-03.

## Proof table (handoff steps 4–6) — RUNTIME ROWS RETRACTED, kept for the record only

> **Every runtime result in this table is retracted (see the status block).**
> The pixel diffs behind them were mechanically real, but what the diffs
> *showed* was read through a hallucinating image-description tool, so the
> interpretations are void. Do not cite any PASS below.

Evidence files live in `C:\Users\Suprabha\AppData\Local\Temp\colosseum-phase1\glm-evidence\`
(screenshots named `NN-*.jpg|png`); raw sequences in the parent folder.

| Proof | Result | Evidence |
| --- | --- | --- |
| EPUB opens via document picker, real page pixels | PASS | `01-epub-first-render-page.jpg` (Alice text, chrome visible; first-ever Android EPUB render confirmed with pixels) |
| Page turn forward | PASS (chrome receded) | `02`→`03`→`04` successive full-page diffs after right-edge taps |
| Page turn backward | PASS (chrome receded) | `04`→`05` full-page diff after left-edge tap |
| Close/reopen resumes | PARTIAL | `06` (library after close; Continue card earlier showed `Chapter 1 · 2/18 · 4%`) and `07` (reopened book) — progress persists and reopens in-book; exact same-page pixel equality not established (reopen rendered a different page band than the moment before closing) |
| Top/bottom chrome + appearance panel appear above page and take touches | FAIL while chrome visible (pre-existing defect, see below); appearance panel not reachable | `01` shows chrome above page; edge/button taps during the awake window produce zero pixel change |
| Failed-open message for corrupt file | PASS | `08-corrupt-epub-error-surface.jpg` ("Couldn't open this book" + Go back), `09` Go-back tap returns to library |
| logcat CSP violations / missing modules | PASS (none observed) | repeated greps of logcat + `files/logs/colosseum.log` during EPUB opens: no CSP console errors, no missing-module errors |
| World tab opens its world (×3) | PASS | Tankoban `13`, Biblio `14`, Theatre `11` (Lanista dump: TheatreWorld + Cinemeta Discover wall visible) |
| Catalogue loads over HTTPS | PASS | `10-tankoban-discover-catalogue-grid.jpg` (10–12 distinct manga covers); app log: `[net] cached IPv4 pin … mangadex.org / cinemeta-catalogs.strem.io`, `[comics-catalog] ready` |
| Direct-URL MP4 with picture + sound, pause, seek | NOT RUN | no reachable direct-URL stream source in the default addon set on-device; the local-HTTP-fixture and public-test-URL approaches were policy-blocked for the previous engineer too; earlier diagnostic-build evidence (frames + pause + seek, audio not confirmed) remains the only video proof |
| Account sign-in reaches the service | NOT RUN | no test credentials available in this session |
| Manga chapter opens | NOT RUN | time budget consumed by the touch-delivery diagnosis; catalogue discovery itself proven above |
| Comic CBZ downloads then opens | NOT RUN (current build) | inherited evidence at `13ffd9ce`/`64855902` (document picker + resume patch) predates this checkpoint's APK; not re-proven on `6ee28634` |
| Nokia T20 tablet (arm64 install + spot checks) | NOT RUN — device not listed | `adb devices` shows only `emulator-5554`; USB debugging presumably still not enabled on the tablet |

## The reader touch-delivery defect (the blocker)

Observations on the CI x86_64 APK of `6ee28634` (evidence in parent folder,
`glm-*`, `r*`, `my-*` series):

1. Reader opens, page renders, chrome shows and auto-recedes after ~3 s idle.
2. While the chrome is VISIBLE: right-edge tap, center tap → **zero** pixel
   change (not even a chrome toggle). Hardware BACK still works, so the app and
   the Qt window are alive.
3. Once the chrome is receded: right-edge and left-edge taps turn pages (three
   consecutive pixel-verified turns).
4. The failed-open surface — also a `ReaderOverlay` native child window — takes
   touches fine (Go back works), so native child windows CAN receive input.

**A/B control.** `android/installable` temporarily carried `8440dc86`
(= `6ee28634` with only `qml/reader2/ReaderOverlay.qml` reverted to its
`9fbfeeb3` form; branch `codex/readeroverlay-control`, since deleted). Its CI
x86_64 APK (`colosseum-x86_64-debug.apk`, SHA-256
`f5a5a76beb86d7220eeafd8e3d7a074b1919bcde959911eeea4e2d66c04df08b`, run
37055351373): the reader is **fully** touch-dead — edge taps do nothing even
with the chrome receded, and double-tap center does nothing
(`ctl-04`…`ctl-08`).

Verdict: the Loader-gated rewrite (`6ee28634`) is a strict improvement and the
desktop regression defect Claude flagged is fixed without regressing Android.
The chrome-visible touch deadness is pre-existing in Codex's overlay-window
architecture. Prime suspect for the next engineer: when the TopBar/BottomRail
overlay windows become visible, something (their stacking relative to the
foreign WebView window, or Qt's child-window input routing with multiple
`WindowContainer`s on Android) swallows every touch; when they hide, the edge
windows route fine.

## Desktop pass-through fix (Claude's defect 1)

`6ee28634` "Create the reader overlay native chrome only on Android":
`ReaderOverlay` now instantiates its `Window`/`WindowContainer` pair behind a
Loader that is active only on Android; every other platform gets a plain
pass-through Item (no hidden Window, no null-window WindowContainer). Desktop
harnesses re-run offscreen, all exit 0: `reader2_chrome_smoke.qml`,
`reader2_logic_harness.qml`, `android_reader_paper_contract_harness.qml`.
qmllint on the file reports only non-hard categories (`unqualified`,
`missing-property`), same class of warnings as before.

## Desktop CI baseline (Claude's defect 2) — master cannot run its own tests

Master's desktop-ci has been dying at the public-trust gate (private Windows
path in a design doc), skipping every test job, since runs 698–702. The
baseline was therefore rebuilt on a throwaway branch from the merge-base
`a602b86c` with build-only cherry-picks so linux-desktop could run at all:

- `95094c43` public-path redaction (port of `97c1c7b2`) — public-trust then passes.
- `edba3667` Qt6::Concurrent into `tst_vault_mal_match`/`tst_vault_identifier`
  (port of `7fac8537`) — merge-base otherwise cannot compile `MalCatalog.cpp`.
- `3b91b0ab` RatingsReviewsDelivery sources into `reader2_profile_runtime_harness`
  (port of `a5d49e2f`) — merge-base otherwise fails to link it.
- `bb1551a6` same sources into `account_first_light` (port of part of
  `c1bd188e`) — the factory-loop half of that commit cannot port because
  `AccountCredentialStoreFactory.cpp` is android-branch-only.

Each of these is a master regression invisible until the gate is fixed; the
branch already carries the fixes. Baseline v5 (branch
`codex/baseline-desktop-ci-a602b86c`, run 37061867998) **ran the full
platform-neutral suite at merge-base: 6 of 120 failed —
`account_attachment_runtime`, `account_core`, `account_attachment_coordinator`,
`core_sync_adapters`, `keyboard_key_events`, `tracker_lifecycle` — exactly
Claude's six.** All six are therefore PRE-EXISTING on master; the Android branch
did not cause them (their test sources are byte-identical between merge-base
and branch). The extra `background_work_coordinator_harness` failure seen at
`9fbfeeb3` ("watchdog fired = scheduling bug", 10.5 s) was a flake: it passed
in both the baseline run and the final-tip run.

**Final-tip linux-desktop (run 37061956894 at `cd3ce9d7`): 5 of 122 failed —
`account_attachment_runtime`, `account_core`, `account_attachment_coordinator`,
`keyboard_key_events`, `tracker_lifecycle` — all five proven pre-existing by
the baseline. `core_sync_adapters` passed at the tip (failed at `9fbfeeb3` and
at baseline → flaky). The branch's Linux desktop gate therefore ends with only
proven pre-existing failures, satisfying the handoff's condition.** The
windows-desktop job was still in flight at the stop gate.

## CI and artifacts (final branch tip `cd3ce9d7` = tree of `6ee28634`)

Branch history on `android/installable`: `9fbfeeb3` → `6ee28634` (desktop
pass-through fix) → `8440dc86` (control, A/B only) → `cd3ce9d7` (revert of the
control; no force used). Final tip tree is identical to `6ee28634`.

- android (build + API-34 emulator smoke), PASS:
  https://github.com/kingoftheseas56/Colosseum/actions/runs/37061956762
- code-quality at the tip: PASS (run 37061956772); at `6ee28634`: PASS (37044895065).
- android at `6ee28634` (the build most runtime evidence comes from), PASS:
  run 37044895048; x86_64 APK SHA-256
  `fb186187dae6120ab191eb22375e304ff3b602c9b4b79ec2bb28497cb94ca053`
  (loader-alignment verifier: `ANDROID_RUNTIME_BUNDLE_OK`).
- desktop-ci at the tip: linux-desktop failed with ONLY the 5 proven
  pre-existing account/keyboard/tracker tests (run 37061956894, see the
  baseline section); windows-desktop in flight when this document was written.
- **arm64 APK for the tablet: `colosseum-arm64-v8a-debug.apk`, SHA-256
  `1a852f3fe8bfe65798818a6919fba4c13b0c15168600c3c4185798cb6366f7f3`**
  (artifact of run 37061956762; local re-hash matches the artifact manifest).
  Not installed anywhere: the Nokia T20 was not listed.

## What the next engineer should do first

1. Attack the chrome-visible touch deadness with the A/B facts above; the
   appearance-panel proof is unreachable until it is fixed.
2. Re-prove the four NOT RUN items (video, account, manga chapter, CBZ) on a
   CI APK of the final tip. The tablet needs USB debugging enabled by Hemanth.
3. Master-side cleanups outside this branch's scope: fix the public-path
   redaction on master itself and the four build regressions the baseline
   cherry-picked (public path, Qt6::Concurrent links, RatingsReviewsDelivery
   harness links), plus the six pre-existing account/keyboard/tracker test
   failures, so master's desktop-ci runs again.

Evidence root: `C:\Users\Suprabha\AppData\Local\Temp\colosseum-phase1\`
(`glm-evidence\` curated; `ci-apks-6ee28634\`, `ci-apks-control\`,
`ci-apks-final-arm64\` artifacts; `diag\` the unused instrumentation patch
files). A Lanista read bridge (`adb` + `run-as` + `nc -U cache/ColosseumLanista`)
was used for scene introspection; drive commands stay env-gated off.

## Previous checkpoint content (Codex, `9fbfeeb3`) — still accurate where not superseded

Stopped at the user's requested review gate. This is an implementation/build
checkpoint, not a declaration that Phase 1 or full app functionality is
complete. Do not merge to master, start Phase 2, or resume implementation
without the user's direction.

The Nokia T20 was explicitly excluded from this testing session. Audiobooks
remain deferred. No landscape-only restriction was implemented; portrait
browsing/reading and landscape video were recommended, with both orientations
available on tablets.

- `native/reader2/AndroidEbookRenderer.{h,cpp}` owns the foreign WebView
  window, JNI command/event transport, generation isolation, readiness,
  lifecycle, and teardown.
- `native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java`
  hosts Foliate through AndroidX WebKit, with exact-origin/main-frame messages,
  local asset interception, typed source errors, and publication scripts
  disabled.
- `ReaderPublicationSession.java` maps a random per-open token to the original
  document URI. New opens and teardown revoke old tokens. No permanent book
  copy is made.
- `resources/reader2/android_paper.html` and `android_boot.js` adapt the common
  Foliate glue to token-based publication fetches. The build stages EPUB assets.
- `qml/reader2/AndroidPaper.qml` embeds the foreign window. `ReaderOverlay.qml`
  gives existing reader controls separate native child windows above it.
- `ReaderShell.qml` and `Reader2Logic.js` use stable Vault book IDs on Android,
  retaining the existing desktop identity scheme.
- Host Java/JavaScript regressions and Android CI steps exercise token/event
  isolation and boot transport. Read-along returns an explicit
  unsupported-feature error.

Local `libcrypto_3.so`/`libssl_3.so` staging remains broken (loader-alignment);
CI-built APKs are the only installable artifacts, per Claude's instruction.
