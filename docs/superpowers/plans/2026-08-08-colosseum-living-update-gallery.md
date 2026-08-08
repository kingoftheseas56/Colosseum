# Colosseum Living Update Gallery Implementation Plan

> **Execution contract:** Use `brotherhood-executing-plans` and execute the slices in order on
> `master`. Do not recreate the updater foundation delivered through `7c1d404`. Every user-visible
> slice closes only with the layer matrix and evidence required below.

**Goal:** Replace the updater's card/dashboard presentation with the approved monochrome living
release gallery while preserving the signed release, resumable download, rollback installer, and
Lanista contracts already on master.

**Architecture:** `UpdateService` remains the sole owner of update state and trusted release data.
QML projects its existing `release` and ordered `highlights` into a version-neutral chapter gallery;
one compile-time test-only seam makes Downloading reproducible in an isolated Lanista session but
cannot alter production behavior. The taskbar notification, chapter stage, and bottom status rail
remain local QML; remote manifests provide inert text and verified artwork only.

**Tech stack:** C++17, Qt 6.11.1 Core/QML/Quick/Quick Effects, Qt Quick Test through the registered
`colosseum.qml` CTest entry, existing house harnesses, and isolated `lanista session run`.

## Global constraints

- Work on `master`; do not create a branch or worktree without Hemanth's explicit approval.
- Preserve every updater trust, download, installer, release-publishing, and data-boundary contract.
- No new network endpoint, executable presentation field, private signing key, or live-data mutation.
- No cards, dashboard columns, gold/colour accents, emoji, or subtitle prose under the page title.
- Use Fraunces for the release/chapter display type and Inter for metadata and controls.
- Real Colosseum screenshots are the visual stage; render them monochrome in QML.
- Motion is opt-in decoration: reduced motion removes pulse and crossfade but never meaning.
- Drive only disposable tagged Lanista sessions; never the daily pipe or Hemanth's live data.
- Wait on named property equality only; no `sleep`, `qWait`, timer-based correctness, or guessed delay.
- The HTML companion is visual reference, not production code:
  `Brotherhood/.superpowers/brainstorm/33726-1786210224/content/living-gallery-pair-final.html`.

## File responsibility map

- `qml/UpdatePage.qml` — update-state vocabulary, chapter model projection, top chrome, and assembly.
- `qml/update/UpdateLivingGallery.qml` — full-bleed image stage, release/version title, chapter copy,
  numbered navigation, next action, and missing-art fallback.
- `qml/update/UpdateStatusRail.qml` — persistent action/progress rail for every service state.
- `qml/update/UpdateReleaseHero.qml` and `qml/update/UpdateHighlightCard.qml` — retire after all
  behavior is represented by the two components above; they must have no remaining references.
- `qml/Taskbar.qml` — permanent Update affordance and monochrome notification badge/pulse.
- `native/update/UpdateService.{h,cpp}` — existing production state; adds only a compile-time
  test-build presentation-state setter.
- `native/main.cpp` — reads test-only presentation-state environment values only inside
  `#ifdef COLOSSEUM_UPDATE_TESTING`.
- `tests/qml/tst_update_page.qml` — deterministic component contract for chapters, all states,
  progress/actions, narrow layout, keyboard, missing art, and reduced motion.
- `tests/update_service_harness.cpp` — proves the test seam is bounded and production transitions
  remain unchanged.
- `tests/test_update_taskbar_p0.ps1` — static taskbar reachability/accessibility/reduced-motion gate.
- `tests/test_update_lanista.ps1` and `tests/lanista_scenarios/update_downloading.json` — third
  isolated runtime state beside the existing Available and UpToDate scenarios.
- `tests/lanista_fixtures/update-available/` — reuse its already signed test-key chronicle; do not
  regenerate or weaken its signature.
- `release/presentation/1.1.0.json` and `release/presentation/artwork/` — first real reusable chapter
  payload; chapter claims must describe features actually present in the release candidate.
- `docs/colosseum-test-verification.md` and `docs/colosseum-lanista-verification.md` — update with the
  exact new assertions, scenario, session paths, and status after the gates run.

---

### Slice 1: Make the active-download composition reproducible in a disposable test build

Purpose:
Give Lanista a deterministic, read-only way to boot the real assembled page in Downloading state
with exact byte counts, without starting a network transfer or exposing any production override.

Dependencies:
The updater foundation at `7c1d404`; no other slice in this plan.

Implementation guidance:
- Add `UpdateService::setTestingPresentationState(State state, qint64 received, qint64 total)` only
  under `#ifdef COLOSSEUM_UPDATE_TESTING`. It accepts only `Downloading`, `Paused`, `Verifying`, and
  `Ready`; clamps bytes to `0 <= received <= total`; rejects `total <= 0`; requires an already
  authenticated chronicle (`m_hasChronicle == true`); emits `changed()` and never persists.
- In `native/main.cpp`, inside the existing `COLOSSEUM_UPDATE_TESTING` block and after construction,
  read `COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE`, `COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES`, and
  `COLOSSEUM_UPDATE_TEST_TOTAL_BYTES`. If all three are present, parse them strictly and call the
  setter. Outside the test compile flag these names must not occur in the built code path.
- Extend `tests/update_service_harness.cpp` to load the existing signed fixture, apply
  `Downloading, 224395264, 330301440`, and assert state, byte counts, `progress` within `0.0001` of
  `224395264.0 / 330301440.0`, unchanged `latestVersion`, and no persisted override after restart.
- Perform the negative control by temporarily allowing a setter call before `m_hasChronicle`; the
  new harness assertion must fail, then restore the guard and record both outputs.

Behavior to preserve:
Production state transitions; signed-manifest requirement; six-hour check policy; download resume;
installer launch; Available and UpToDate seed behavior; `COLOSSEUM_UPDATE_TESTING=OFF` restoration.

Baseline:
Run the updater subset and preserve output showing the current two test-key states are green:

```powershell
C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -R "colosseum.update_" --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\test_update_lanista.ps1
```

Focused tests:
  - Qt Test: not applicable; this updater family uses the registered house harnesses listed in the
    test ledger rather than Qt Test classes.
  - Qt Quick Test: existing `colosseum.qml` remains green; no QML behavior changes in this slice.
  - Existing harnesses: extend `colosseum.update_service_harness`; then run the updater subset and
    the standard `ctest --test-dir native/build-msvc -L unit --output-on-failure` gate.
  - Negative control: remove the authenticated-chronicle guard, record the named red assertion,
    restore the guard, and rerun green.

Test seam status: available — `colosseum.update_service_harness`, `colosseum.qml`, and the unit gate
are registered in `docs/colosseum-test-verification.md`.

Lanista actions:
None; this slice creates the prerequisite consumed by Slice 3.

Completion signal:
The service harness exits zero with its existing success sentinel and the unit gate returns zero.

State / events / probes:
Harness assertions only: authenticated chronicle retained, state `Downloading`, exact bytes,
expected progress, and restart returning to the signed persisted state rather than the override.

Visual evidence:
None; this is a pure test seam.

Regression paths:
Available signed seed, UpToDate signed seed, invalid-signature rejection, offline signed restart.

Evidence artifacts:
`artifacts/update-gallery/slice-1/` containing baseline, negative-control red, restored green, and
unit-gate logs.

Bridge status: not applicable — no running-app claim is made in this internal slice.

Completion criterion:
The bounded setter exists only in update-testing builds, its negative control is proven, updater and
unit gates are green, and production builds expose no presentation-state environment override.

---

### Slice 2: Replace the card wall with the living release gallery

Purpose:
Make the release screenshot the stage, with large version/chapter typography and direct chapter
navigation in every non-installing update state.

Dependencies:
Slice 1 only for later runtime composition; the QML work itself consumes the existing service model.

Implementation guidance:
- Write the failing cases in `tests/qml/tst_update_page.qml` first. Use five ordered feature
  highlights named `Reader`, `Discover`, `Biblio`, `Theatre`, and `The house`; give four local
  artwork URLs and leave one empty to exercise fallback.
- Add `UpdateLivingGallery.qml` with properties `release`, `chapters`, `currentIndex`,
  readonly `chapterCount`, `reducedMotion`, and signals `chapterRequested(int)` /
  `nextRequested()`. Disable Next when `chapterCount < 2`. Expose object names:
  `colosseumUpdateGallery`, `colosseumUpdateVersionTitle`, `colosseumUpdateChapterTitle`,
  `colosseumUpdateChapterBody`, `colosseumUpdateChapterNav`, `colosseumUpdateChapter_01` through
  `_08`, and `colosseumUpdateNextChapter`.
- Preserve highlight order. When the list is empty, synthesize one local copy-only chapter from the
  release title/summary and captured-motion fallback; never show an empty card or blank stage.
- Map the selected chapter's first verified artwork URL into one full-bleed `Image` with
  `PreserveAspectCrop`. Apply monochrome treatment with the existing Qt Quick Effects module and
  directional black overlays so text remains readable. Do not duplicate or blur the full-screen
  texture.
- Render release label/version at the upper left and chapter copy near the lower left. Use
  `theme.display` (Fraunces) for version and chapter title, `theme.ui` (Inter) for metadata/body.
  Do not place a tagline beneath the release title.
- The numbered navigation has a visible non-colour selected treatment, keyboard focus, accessible
  names (`Chapter 1: Reader`), click selection, Left/Right navigation, and wraparound Next.
- Crossfade only the image opacity for 220 ms; disable the behavior entirely under reduced motion.
- Refactor `UpdatePage.qml` to assemble this component, retain back/window chrome, and keep its
  existing `automationState`, `automationVersion`, state/action functions, and trusted-model filter.
- Remove `UpdateReleaseHero.qml` and `UpdateHighlightCard.qml` only after `rg` proves no references.

Behavior to preserve:
Unknown highlight kinds are filtered; remote artwork remains verified local file URLs; missing art
is harmless; Escape/back/window controls work; no click leaks to the page underneath.

Baseline:
Preserve a current 1280x720 grab from the existing `update_available.json` session showing the
hero/card layout, plus the current `colosseum.qml` result.

Focused tests:
  - Qt Test: not applicable; no native contract changes.
  - Qt Quick Test: extend the registered `colosseum.qml` suite to assert five chapter buttons,
    initial chapter copy/art, direct selection, Next wrap, unknown-kind filtering, empty-art
    fallback, long copy at width 560, accessible names, keyboard traversal, and no opacity behavior
    under reduced motion.
  - Existing harnesses: `tests/test_fullscreen_controls_p0.ps1` and the standard unit gate remain
    green.
  - Negative control: reverse the chapter projection before the test run; record the named initial
    chapter/order failures; restore source order and rerun green.

Test seam status: available — this extends the registered `colosseum.qml` target and existing
fullscreen harness; it creates no orphan runner.

Lanista actions:
Run the existing isolated Available scenario through
`powershell -ExecutionPolicy Bypass -File tests\test_update_lanista.ps1`. Its existing
`session run` path waits for `bootSplash.visible == false`, clicks
`colosseumUpdateTaskbarButton`, waits for `colosseumUpdatePage.visible == true`, reads
`automationState == "Available"`, and captures the whole window. Extend the scenario to read the
signed fixture's single `colosseumUpdateChapterTitle`, assert gallery `chapterCount == 1`, and assert
the Next control is disabled. Multi-chapter navigation is proven by the deterministic QML suite,
whose five-chapter model is not allowed to bypass signature trust in the assembled app.

Completion signal:
`colosseumUpdatePage.visible == true` followed by strict equality on `automationState ==
"Available"` and a non-empty `colosseumUpdateChapterTitle`.

State / events / probes:
`qml-get` for `automationVersion`, `automationState`, gallery `currentIndex`, chapter title, chapter
body, and fallback-art state; expected values come from the signed fixture, never from hardcoded
pixel inference.

Visual evidence:
Whole-window Available grab at 1280x720 plus a normal-display grab. Both must show one continuous
image stage, Fraunces version/chapter type, numbered navigation, and no old hero/card wall.
Assemble them with the baseline into `lanista brief update-gallery-s2` for Hemanth.

Regression paths:
Open Update from the taskbar, Back, reopen, select a later chapter, Next wrap, narrow width, missing
art, reduced motion, and unknown highlight kind.

Evidence artifacts:
`artifacts/update-gallery/slice-2/` for QML/negative-control/unit logs and the Lanista session
manifest; `agents/eyes-on/2026-08-08-update-gallery-s2/` for baseline/after grabs and Hemanth's
recorded verdict.

Bridge status: available — every command is in the Lanista ledger and the existing Available
scenario already uses an isolated signed seed.

Completion criterion:
All deterministic gates pass, the isolated Available route/state/grab matches the chapter contract,
and Hemanth records approval of the actual QML composition. Until his verdict, report
`Implemented, verification pending`, never Runtime-validated.

---

### Slice 3: Put every updater action into the persistent bottom status rail

Purpose:
Make Up to date and Updating feel like the same living page while showing exact state, progress,
and the only safe action for that state.

Dependencies:
Slices 1-2.

Implementation guidance:
- Add `UpdateStatusRail.qml` with properties `state`, `statusText`, `primaryLabel`,
  `primaryEnabled`, `cancelVisible`, `receivedBytes`, `totalBytes`, `progress`, and signals
  `primaryClicked()` / `pauseClicked()`. Expose `colosseumUpdateStatusRail`,
  `colosseumUpdateStatusText`, `colosseumUpdatePrimaryAction`, `colosseumUpdateProgress`,
  `colosseumUpdateProgressText`, and `colosseumUpdatePause`.
- Keep the rail fixed at the bottom while the gallery fills the remaining viewport. In UpToDate,
  render `Everything is up to date` and `Check for updates`. In Downloading, render `Update in
  progress`, exact binary-friendly byte text (`214 MB of 315 MB · 68%` for the fixture), a thin
  track, and `Pause download`. Paused renders `Update paused` plus `Resume download`; Ready renders
  `Ready to enter <display version>` plus `Restart and update`.
- Checking, Verifying, and Installing have no enabled action. RecoverableError retries download;
  VerificationFailure checks again; ManualUpdateRequired keeps its existing explicit manual path.
- Wire Pause to the existing `cancelDownload()` service command. Do not add a separate downloader
  operation or rename the service API.
- Retain exact numeric progress and accessibility text when reduced motion is on; disable only
  width animation. Unknown total size shows received bytes and an indeterminate textual state, not
  a fabricated percentage or ETA.

Behavior to preserve:
Explicit user consent before download/install; no action while Checking/Verifying/Installing; cancel
becomes resumable Paused; exact service receiver binding; gallery remains visible throughout.

Baseline:
Record the current Qt Quick Test state table and a current UpToDate Lanista grab before editing.

Focused tests:
  - Qt Test: not applicable; the existing native service harness already owns state transitions.
  - Qt Quick Test: data-drive all service states and assert exact status/action copy, enabled state,
    dispatch target, byte/percentage formatting, unknown-size behavior, gallery visibility, and
    reduced-motion progress semantics.
  - Existing harnesses: updater CTest subset, `tests/test_update_data_boundary.ps1`, and the unit
    gate remain green.
  - Negative control: enable the primary action during Verifying; the existing/new safety case must
    fail, then restore the safe mapping and rerun green.

Test seam status: available — service and QML gates are registered; Slice 1 supplies the missing
assembled Downloading fixture without inventing a Lanista command.

Lanista actions:
- Extend `tests/test_update_lanista.ps1` with a third isolated `session run` using the existing
  signed Available seed and inherited test-only values:
  `COLOSSEUM_UPDATE_TEST_PRESENTATION_STATE=Downloading`,
  `COLOSSEUM_UPDATE_TEST_RECEIVED_BYTES=224395264`, and
  `COLOSSEUM_UPDATE_TEST_TOTAL_BYTES=330301440`.
- Add `tests/lanista_scenarios/update_downloading.json`: wait for `bootSplash.visible == false`,
  click `colosseumUpdateTaskbarButton`, wait for `colosseumUpdatePage.visible == true`, require
  `automationState == "Downloading"`, read progress text, and take a whole-window grab. Do not
  click Pause; the scenario proves presentation, not downloader mutation.
- Keep the existing UpToDate scenario and require `primaryLabel == "Check for updates"` and
  status text `Everything is up to date`.
- Restore all test environment variables and rebuild `COLOSSEUM_UPDATE_TESTING=OFF` in the runner's
  existing `finally` block even when any scenario fails.

Completion signal:
Strict equality on `automationState == "Downloading"`, `colosseumUpdateProgressText == "214 MB of
315 MB · 68%"`, and `colosseumUpdatePage.visible == true`; UpToDate uses strict equality on its
status and primary label.

State / events / probes:
`qml-get` the page state/version, rail status, progress text, primary label, primary enabled state,
and gallery current chapter. Expected Downloading percentage is derived from the exact injected
bytes and must not be inferred from bar width.

Visual evidence:
Whole-window grabs for Downloading and UpToDate at 1280x720. Downloading must show the gallery,
target version, chapter controls, byte progress, thin track, and Pause action simultaneously;
UpToDate must show the same gallery with its persistent Check action.

Regression paths:
Available → open; UpToDate → Check; Downloading at 0%, 68%, and 100% in QML; Paused → Resume;
Ready → Restart; Verifying no action; reduced motion; close/reopen Update page.

Evidence artifacts:
Three isolated session directories under `artifacts/lanista-sessions/`, their exact paths in
`artifacts/update-lanista-session-paths.txt`, plus `agents/eyes-on/2026-08-08-update-gallery-s3/`.

Bridge status: bridge blocked until Slice 1 lands; available afterward because all runtime actions
use existing `session run`, `ui-click`, `ui-wait-for`, `qml-get`, and whole-window grab capabilities.

Completion criterion:
QML/state/data-boundary/unit gates pass; Available, UpToDate, and Downloading isolated sessions all
pass with manifests and grabs; the runner restores the shipping build; Hemanth approves the two
actual QML state compositions.

---

### Slice 4: Finish the monochrome taskbar signal and real release imagery

Purpose:
Complete the doorway into the gallery and prove the template with real Colosseum screenshots rather
than decorative placeholder art.

Dependencies:
Slices 2-3.

Implementation guidance:
- Change `qml/Taskbar.qml` only in the existing Update button: replace gold badge/underline values
  with silver-white/ink values, retain the permanent icon and accessible name, keep the pulse only
  while `updateUnseen && !reducedMotion`, and preserve the static badge while reduced motion is on.
  If Taskbar does not currently receive reduced motion, thread the existing shell preference into
  one new `property bool reducedMotion` without introducing a second settings source.
- Extend `tests/test_update_taskbar_p0.ps1` to require the permanent button, namespaced object names,
  accessible names for available/unavailable states, reduced-motion pulse guard, and absence of the
  retired gold literals in the Update block. Do not assert unrelated taskbar colours.
- Copy the approved captures from `C:\Users\Suprabha\Desktop\colosseum snaps` into
  `release/presentation/artwork/` with this exact mapping: `image.png` →
  `colosseum-reader.png`; `one-piece-tankoban-series.png` →
  `colosseum-tankoban-discover.png`; `image (1).png` → `colosseum-biblio.png`;
  `image (3).png` → `colosseum-theatre.png`; `image (4).png` → `colosseum-house.png`.
  Preserve the source files; optimize copies losslessly only if visual comparison proves no
  crop/text damage.
- Use the 1.0 five-chapter copy as the reference dataset in the QML test and eyes-on brief:
  `A reader built for the page.`, `Discover comes to Tankoban.`, `Reader2 grew up.`, `Theatre goes
  deeper.`, and `One collection. One vault.` The actual `release/presentation/1.1.0.json` may claim
  only features present in 1.1.0; attach appropriate approved screenshot assets through
  `artwork_assets` and list each `{asset,path}` in `artwork`.
- Run the existing generator/tooling tests to prove all artwork is hashed, every reference resolves,
  and no path or executable field reaches the signed manifest. Do not sign or publish a production
  release in this slice.

Behavior to preserve:
Taskbar open/close/session/download/extension/settings controls; update seen/unseen semantics; signed
artwork verification; draft-only publishing; no hard dependency on artwork.

Baseline:
Preserve the existing taskbar Available grab and hashes/dimensions for each source screenshot before
copying. Record the current one-card `1.1.0.json` tooling test result.

Focused tests:
  - Qt Test: not applicable; no new native contract.
  - Qt Quick Test: retain chapter/artwork/fallback and reduced-motion cases in `colosseum.qml`.
  - Existing harnesses: `tests/test_update_taskbar_p0.ps1`,
    `python tests/update_release_tooling_test.py`, updater CTest subset, and unit gate.
  - Negative control: remove one `artwork` declaration while its chapter still references the asset;
    generator/tooling test must fail `highlight references missing artwork`; restore and rerun green.

Test seam status: available — every named command is present in the test ledger; no new runner.

Lanista actions:
Rerun the three isolated updater sessions from Slice 3. Use `qml-get` to prove
`colosseumUpdateBadge.visible == true` in Available and the page route after clicking the permanent
button. Use whole-window grabs for the taskbar notification and both gallery states. Do not drive the
daily app.

Completion signal:
Available waits for the badge `visible == true`; every page run waits for `bootSplash.visible ==
false`, then `colosseumUpdatePage.visible == true`, then its exact `automationState`.

State / events / probes:
Badge visibility, update page state/version, chapter title/current index, progress text for
Downloading, and primary label/status for UpToDate.

Visual evidence:
One taskbar-available grab, one Updating grab, and one post-update/UpToDate grab at both 1280x720 and
normal display size. `lanista brief update-living-gallery` assembles them beside the approved HTML
reference. Hemanth judges typography, crop, contrast, spacing, image choice, and overall life.

Regression paths:
Reduced motion on/off, unseen → opened/seen, badge remains while available, update installed/up to
date clears badge, missing artwork fallback, narrow page, taskbar auto-hide/reveal, Back/reopen.

Evidence artifacts:
`artifacts/update-gallery/slice-4/` for all deterministic logs and image hashes;
`agents/eyes-on/2026-08-08-update-living-gallery/` for the final visual brief and Hemanth verdict.

Bridge status: available — existing update scenarios and named update controls are in the Lanista
ledger; aesthetic closure remains explicitly human-only.

Completion criterion:
All named gates pass; three isolated Lanista scenarios are Runtime-validated; real artwork is hashed
and rendered or falls back honestly; Hemanth records **approved** on the actual QML eyes-on brief;
the diff contains no old card references or gold Update notification literals.

---

### Slice 5: Close the updater on clean Windows installations before release approval

Purpose:
Prove that the completed living gallery carries a real signed update from the public 1.0 baseline
through bootstrap, interruption/resume, side-by-side install, healthy restart, cleanup, and rollback
without losing representative user state.

Dependencies:
Slices 1-4 and a clean committed release-candidate tree. This slice may create and verify GitHub
**drafts** but may not publish them.

Implementation guidance:
- Run every existing deterministic updater gate from the committed tree: full build, updater CTest
  subset, registered QML target, taskbar contract, data-boundary contract, installer matrix, release
  tooling, and all three isolated Lanista scenarios. A failing unrelated unit test is reported with
  exact evidence and cannot be silently relabelled as updater-green.
- Build the bootstrap installer and the next-version fixture from clean archives of their exact tag
  candidates. Generate signed manifests/artwork with the external production key, upload draft
  releases, re-download every asset, and run `verify_update_release.py`. Never copy key bytes into
  the repository or artifact bundle.
- On a clean Windows test account, install public Colosseum 1.0. Create representative settings,
  progress, one owned/downloaded item in Tankoban, Biblio, and Theatre, plus one partially resumable
  download where the public version supports it. Record the AppData file list, sizes, and store-level
  identifiers before the bootstrap install.
- Install the updater-enabled bootstrap once by hand, because public 1.0 cannot acquire an updater
  retroactively. Confirm compiled version identity, the living Update page, and survival of every
  representative item.
- Point only the controlled test account at the signed next-version draft using the existing release
  test configuration. Discover it in Colosseum, start the download, interrupt by closing the app,
  relaunch, resume, verify, and invoke Restart and update. Confirm the new compiled version reopens,
  the prior payload remains until shell-ready acknowledgement, and bounded cleanup removes only the
  exact backup afterward.
- Repeat with a signed manifest whose test payload intentionally fails the required payload sentinel
  after extraction. Confirm rollback relaunches the prior version, preserves all user state, records
  the failed target, and suppresses automatic retry.
- After the committed-artifact rerun, use `brotherhood-review` on the complete implementation diff
  and runtime evidence against the 13-item Definition of Done in the approved spec. Score every item
  MET/PARTIAL/NOT-MET and require a final APPROVE; fix every PARTIAL/NOT-MET before handoff.

Behavior to preserve:
Normal fresh install/uninstall, manual bootstrap truth, draft invisibility to stable users, explicit
download/install consent, signed asset trust, safe backup boundaries, and all user data outside the
replaceable payload.

Baseline:
Preserve public 1.0 installer hash, installed version, install-root listing, representative AppData
inventory/store values, and a screenshot of the public app before bootstrap. Preserve the clean
release-candidate `git status`, commit SHA, and tag mapping.

Focused tests:
  - Qt Test: not applicable; the updater entries are registered house harnesses, not Qt Test
    classes, and the ledger forbids relabelling their layer.
  - Qt Quick Test: run `ctest --test-dir native/build-msvc -R colosseum.qml
    --output-on-failure` from the committed build.
  - Existing harnesses: `native\build-msvc.bat`,
    `C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir native/build-msvc -R
    "colosseum.update_" --output-on-failure`, `tests\test_update_taskbar_p0.ps1`,
    `tests\test_update_data_boundary.ps1`, `tests\installer\update_matrix.ps1`,
    `python tests\update_release_tooling_test.py`, `tests\test_update_lanista.ps1`, and the
    standard `-L unit` gate.
  - Negative control: the sentinel-broken signed draft must enter rollback and reopen the prior
    version; if it installs successfully or loses state, the slice is red.

Test seam status: available — every deterministic command is registered in the test ledger; the
clean-account journey is the release integration matrix already required by the approved spec.

Lanista actions:
Run the three isolated scenarios from Slice 4 against the committed candidate. The public 1.0
bootstrap portion is `human-witnessed:` install public 1.0, create the named representative state,
install the bootstrap, open Update from the taskbar, and record the before/after state inventory and
Hemanth's page verdict. Public 1.0 has no Lanista/update bridge, so no bridge command may be invented
for that half.

Completion signal:
For isolated sessions, exact `automationState` equality as defined in Slices 2-4. For the installed
journey, the relaunched executable reports the expected compiled version, the shell reaches its
existing ready state, the installer result is `success` or the deliberate control is `rollback`,
and every named representative store identifier equals its baseline value.

State / events / probes:
Compiled application version; installer/update logs; install-root canonical/backup paths; AppData
inventory and store identifiers; failed-target record; cached signed chronicle version; Lanista page
state/version/chapter/progress/status properties on the updater-enabled builds.

Visual evidence:
Public 1.0 baseline, bootstrap post-install gallery, next-version Downloading, successful post-update
UpToDate, and rollback-returned prior-version grabs at normal display size. Hemanth must confirm the
actual installed compositions, not only the HTML companion.

Regression paths:
Fresh install, manual bootstrap, check failure/offline chronicle, interrupted download/restart/resume,
successful swap/restart/cleanup, forced rollback, normal uninstall after update, and relaunch after
the cached chronicle is offline.

Evidence artifacts:
`artifacts/update-gallery/release-candidate/<commit>/` containing command logs, draft verification,
installer/update logs, before/after inventories, hashes, and session manifests; final eyes-on brief
under `agents/eyes-on/2026-08-08-update-release-candidate/`. No signing key material is copied.

Bridge status: available for updater-enabled builds; public 1.0 is explicitly human-witnessed
because it predates Lanista and the updater. This is a known product boundary, not a fabricated gap.

Completion criterion:
Every deterministic and isolated runtime gate is green; successful and rollback installed journeys
both preserve state; version/tag/manifest/installer identity matches without manual metadata edits;
Hemanth approves the installed QML surface; the GitHub releases remain drafts pending his separate
publication decision; the post-implementation Brotherhood review ends APPROVE with no PARTIAL or
NOT-MET item.

---

## Final Definition of Done

- [ ] Secure updater, installer, release trust, data boundary, and publishing tests remain green.
- [ ] The taskbar Update icon is permanent and its monochrome badge works with motion disabled.
- [ ] The page is one living screenshot stage, not a hero/card/dashboard layout.
- [ ] Fraunces version/chapter type and Inter metadata match the approved hierarchy.
- [ ] Ordered numbered chapters support direct selection, Next, wrap, keyboard, and accessibility.
- [ ] Missing or invalid artwork falls back without hiding release copy or controls.
- [ ] UpToDate shows the chronicle, `Everything is up to date`, and `Check for updates`.
- [ ] Downloading shows the chronicle, target version, exact bytes, percentage, progress, and Pause.
- [ ] Unsafe actions remain disabled in Checking, Verifying, and Installing.
- [ ] Reduced motion removes pulse/crossfade/progress animation but no state meaning.
- [ ] Available, UpToDate, and Downloading pass isolated Lanista sessions with preserved evidence.
- [ ] 1280x720 and normal-display visual briefs exist, and Hemanth approves the actual QML result.
- [ ] No daily-app drive, live-data mutation, private key, remote executable content, or sleep exists.
- [ ] Public 1.0 → bootstrap → updater-enabled next version preserves representative state on a
  clean Windows account, and the deliberate broken payload proves rollback.
- [ ] Version, tag, manifest, installer, and release asset metadata derive without hand editing.

## Executor reporting format

After each slice, report the exact layer matrix required by `brotherhood-executing-plans`:

```text
Qt Test: pass / fail / not run / not applicable
Qt Quick Test: pass / fail / not run / not applicable
Existing harnesses: pass / fail / not run / not applicable
Lanista: replayed / failed / bridge blocked / not run / not applicable
Human aesthetic verdict: approved / rejected / pending / not applicable
Overall: Runtime-validated / Test-reported / Implemented, verification pending /
         Bridge blocked / Verification failed / Plan contradicted
```

Do not publish a GitHub release as part of this plan. Publication remains a separate explicit
release decision after the final eyes-on verdict.
