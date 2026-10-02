# Android review handoff — 2026-10-02

## Stop and scope

Stopped at the user's requested review gate. This is an implementation/build checkpoint,
not a declaration that Phase 1 or full app functionality is complete. Do not merge to
master, start Phase 2, or resume implementation without the user's direction.

The Nokia T20 was explicitly excluded from this testing session. Audiobooks remain
deferred. No landscape-only restriction was implemented; portrait browsing/reading and
landscape video were recommended, with both orientations available on tablets.

## Review target and saved work

Use the existing `android-startup-fix/Colosseum` Codex worktree, branch
`codex/android-startup-fix`. The primary checkout has unrelated changes and must remain
untouched. The remote target for any subsequently authorized push is
`origin` branch `android/installable`, without force.

Last pushed source: `f458ccc6cf42042ccf46690d470b66eb6f0c5304`.
Local commits already present before this checkpoint:

- `64855902`: assign Vault comic identity before opening the archive, allowing resume
  to use the correct progress key during initial loading.
- `35c4e7c0`: earlier progress report; its pending graphics statements are superseded here.

The checkpoint commit containing this document also saves the EPUB implementation.
Review the changes after `35c4e7c0` for EPUB, and after `f458ccc6` for all unpushed work.
The new checkpoint is local only; it has not been pushed, merged, deployed, or released.

## Implemented in this checkpoint

- `native/reader2/AndroidEbookRenderer.{h,cpp}` owns the foreign WebView window,
  JNI command/event transport, generation isolation, readiness, lifecycle, and teardown.
- `native/platform/android/src/org/colosseum/reader/AndroidEbookHost.java` hosts
  Foliate through AndroidX WebKit, with exact-origin/main-frame messages, local asset
  interception, typed source errors, and publication scripts disabled.
- `ReaderPublicationSession.java` maps a random per-open token to the original
  document URI. New opens and teardown revoke old tokens. No permanent book copy is made.
- `resources/reader2/android_paper.html` and `android_boot.js` adapt the common
  Foliate glue to token-based publication fetches. The build stages EPUB assets.
- `qml/reader2/AndroidPaper.qml` embeds the foreign window. `ReaderOverlay.qml`
  gives existing reader controls separate native child windows above it.
- `ReaderShell.qml` and `Reader2Logic.js` use stable Vault book IDs on Android,
  retaining the existing desktop identity scheme.
- Host Java/JavaScript regressions and Android CI steps exercise token/event isolation
  and boot transport. Read-along returns an explicit unsupported-feature error.

## Verification at the stop gate

Fresh checks on this source:

- Java publication-session regression: PASS.
- JavaScript boot transport regression: `ANDROID_READER_BOOT_OK`.
- Android reader boundary: six checks PASS.
- Android build graph: nine tests PASS.
- Qt offscreen `reader2_logic_harness.qml`: exit 0, `VERDICT: PASS`.
- Qt offscreen `android_reader_paper_contract_harness.qml`: exit 0, `VERDICT: PASS`.
- Qt offscreen `reader2_chrome_smoke.qml`: exit 0, `VERDICT: PASS`.
- `git diff --check`: clean at inspection.

The final ARM64 native compile/link and Gradle APK build succeeded. However, the
package verifier rejects both bundled `libcrypto_3.so` and `libssl_3.so`:

```
PT_LOAD file/virtual offsets violate segment alignment; Android maps the wrong bytes
```

This is a **failed packaging acceptance gate**. Do not install or distribute this
local APK. It reproduces the known OpenSSL loader-metadata defect in locally staged
runtime libraries; the earlier CI build at `f458ccc6` passed this check. The source
fix and the local dependency artifacts must not be treated as equivalent evidence.
No dependency repair or new runtime test was started after discovering this failure,
in accordance with the requested stop for review.

All 283 packaged QML files compiled in the verifier; its only reported failures were
the two OpenSSL libraries. The five checked core EPUB assets match the current
source byte-for-byte; its `assets/reader2` graph contains no PDF modules. These checks
do not prove the complete module graph or Android WebView composition/functionality.

The rejected ARM64 artifact is retained only as build evidence in the user's Downloads
folder, under `Colosseum-Android-Review-2026-10-02`, named
`colosseum-arm64-build-only-REJECTED.apk`. SHA-256:
`cf29b5106ad801b6a4b63d963a9d3222d08b5db5dec448f0b302e6a8ecdce0d0`.

## Earlier runtime evidence, with source boundaries

- [Android CI 37025500432](https://github.com/kingoftheseas56/Colosseum/actions/runs/37025500432)
  at `f458ccc6` succeeded for both ABIs and the API 34 emulator startup smoke.
  The downloaded x86_64 build was also installed locally on the API 36 emulator,
  re-signed with the local debug key to retain test data. Theatre rendered correctly
  with all three temporary GPU debug settings removed. The production graphics
  workaround therefore has full-app evidence, beyond the earlier standalone probe.
- CBZ at `13ffd4ce`: a three-page fixture opened through Android's document picker
  and all pages displayed. A runtime patched with the `64855902` ordering change
  restored page 2 after closing/reopening. That resume change is not in the
  `f458ccc6` CI APK.
- HTTPS catalogue at `13ffd4ce`: a previously unused Animation filter returned
  50 titles with loading false and no warning. Successful account sign-in/sync
  remains unverified.
- Earlier diagnostic x86_64 video run displayed local H.264/AAC frames, paused,
  sought, and resumed. An audio track was detected, but audible output was not tested.
- Production EPUB rendering, page turns, resume, search, selection, and appearance
  have no Android runtime pass yet. Do not infer them from host tests or compilation.

## Review priorities and unfinished qualification

1. Repair/rebuild the local OpenSSL runtime dependencies using the corrected build
   procedure, or build the checkpoint through the known-good CI lane after review.
   Require the final APK loader-metadata verifier to pass before installing it.
   Then open an EPUB through SAF and confirm visible book pixels and canonical CFI
   relocation before evaluating higher-level controls.
2. Check child-window stacking, transparency, touch delivery, chrome reveal, edge
   page turns, selection, search, appearance, and the failed-open surface. The ruler
   overlay is still ordinary QML and may be hidden behind the WebView.
3. Exercise close/reopen resume, background/foreground, rotation, revoked document
   permission, renderer loss, account state sealing, and stale events. Inspect JNI
   thread/lifetime ownership and error delivery during initial shell loading.
4. Inspect CSP and the packaged Foliate module graph on a real WebView. The optional
   `alignment_text.js` import already lacks a source file and is caught by shared glue;
   it is not packaged. Ensure EPUB requirements do not depend on it.
5. Validate manga chapter/download-then-read and audible media. Direct-URL video,
   successful authentication/sync, and physical-device qualification remain open.
6. Desktop merge gate remains red: Linux built, but 116/122 platform-neutral tests
   passed in [run 37025499672](https://github.com/kingoftheseas56/Colosseum/actions/runs/37025499672).
   Failures: `account_attachment_runtime`, `account_core`,
   `account_attachment_coordinator`, `core_sync_adapters`, `keyboard_key_events`,
   and `tracker_lifecycle`. No baseline comparison establishes whether they predate
   this branch. Windows was still running when checked for this handoff.

Automatic approval review previously rejected starting the local HTTP media fixture
server and relaunching with the public HTTPS test-video URL, reporting only
"blocked by policy". Neither rejected action was retried; direct-URL playback is
unverified. This restriction does not establish a defect in the app.

## Local evidence locations

Under the user's temporary directory:

- `colosseum-phase1/review-gate-build.log`: final build output.
- `colosseum-phase1/review-gate-package-check.log`: rejected OpenSSL metadata evidence.
- `colosseum-phase1/*-gate.err`: Qt harness verdicts.
- `colosseum-functional-smoke/graphics-production-theatre.png`: full-app graphics proof.
- `colosseum-functional-smoke/comic-new-order-page2.png` and
  `comic-new-order-resumed.png`: earlier patched-runtime resume evidence.
- `colosseum-phase1/ci-graphics-fixed`: untouched `f458ccc6` CI APKs.

The next engineer should start with this checkpoint and live source. The older
`ANDROID-INSTALLABLE-HANDOFF.md` describes earlier builds and contains superseded
status; it is not the current qualification matrix.
