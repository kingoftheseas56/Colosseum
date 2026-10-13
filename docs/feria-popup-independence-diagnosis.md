# Feria popup independence: diagnosis and integration routes

Date: 2026-10-13. Source baseline: `24499a7a3ce6d047134306dd2ec1de98f5d07da0` on `codex/feria-release-packaging`.

Status: the initial diagnosis reproduced the unguarded failure on both runtimes. The follow-up below built the patched Quick adapter and passed independent receiving checks for the exact stock/codec pairings. Whole-app, installed-package and broader feature qualification remain pending; baseline runtimes retain the guard.

## User-visible problem

A website opens a child browsing window. The child works while its original creation view exists. Destroy that original view and a click in the child can crash the process. Feria therefore refuses independent opener retirement on unqualified runtimes, asking for its child windows to close first. This is an input-owner dependency, separate from JavaScript callback retirement and the earlier application-exit fix.

## Mechanism and existing fix

In upstream `qquickwebengineview.cpp`, `WebEngineQuickWidgetDelegate` captures a `QPointer<QQuickWebEngineView> m_parentView` at creation. Adoption subsequently calls `Bind(client)`, which rebinds the visual/delegate relationship but does not update that focus-owner pointer. The pointer becomes null when its original view is destroyed. `ActiveFocusOnPress()` then calls `m_parentView->property(...)` without a null check.

The corresponding core path is `RenderWidgetHostViewQtDelegateItem::adapterClientChanged()` -> `WidgetDelegate::Bind(client)`. Mouse press calls `ActiveFocusOnPress()` before forwarding the event. The existing patch updates `m_parentView` from the newly bound client's `holdingQObject()`, adds a null check, and puts `_colosseumAdoptionInputRebound=true` on the view constructor. It changes only the Quick adapter's `.cpp`; it changes no public class layout or Core/Chromium source.

Primary source snapshots inspected:

- [Qt 6.11.1 Quick adapter](https://github.com/qt/qtwebengine/blob/v6.11.1/src/webenginequick/api/qquickwebengineview.cpp), [6.11.2 Quick adapter](https://github.com/qt/qtwebengine/blob/v6.11.2/src/webenginequick/api/qquickwebengineview.cpp).
- [Core delegate input and rebinding](https://github.com/qt/qtwebengine/blob/v6.11.1/src/core/render_widget_host_view_qt_delegate_item.cpp).
- [Quick target build dependencies](https://github.com/qt/qtwebengine/blob/v6.11.1/src/webenginequick/CMakeLists.txt).

The downloaded Quick adapter file was identical for 6.11.1 and 6.11.2: SHA-256 `c66d61aed5a07ad4493e14430cf1e3a48937742110f92b34dedf3d294b879e74`. `git apply --check` succeeds for `patches/qtwebengine/6.11.1-adopted-input-owner.patch` against both. Patch SHA-256: `df9299a486876df15b7cd84f8c5bb22344afedc4cfeacaf956d8fb3fba4d4469`.

The inspected upstream `6.11` and `dev` branch snapshots still contain the old focus-owner behavior. This does not claim that no upstream issue or fix exists elsewhere; a routine upgrade has not been established as a solution.

## Runtime evidence

A disposable raw-Qt control used a shared off-the-record profile, the fixture's local `/hosting` page, and native Qt Test mouse/key events. It adopted the original `window.open` request, verified typing in the child, destroyed the opener, then clicked and typed in the child again. It deliberately bypasses Feria's dependency guard to isolate the engine defect; it is not a production configuration or a normal acceptance-suite case.

| Runtime reported by the running engine | Unguarded control | Production guarded popup checks |
| --- | --- | --- |
| Qt 6.11.1 / WebEngine 6.11.1 | Input before opener destruction passed; click after destruction faulted, `0xc0000005`, root exit `-1073741819` | 5 passed / 0 failed / 1 skipped, exit 0 |
| Qt 6.11.1 / WebEngine 6.11.2 codec overlay | Same input sequence and native exception | 5 passed / 0 failed / 1 skipped, exit 0 |

The fault stack records `QObject::property` and `RenderWidgetHostViewQtDelegateItem::mousePressEvent`. The exact intervening Quick function is inferred from the inspected source; nearest-symbol stack labels are not treated as precise debug symbols. This reproduces the predicted null focus-owner access without conflating it with incidental offscreen GPU warnings.

Guarded checks covered original POST/geometry preservation, rejecting duplicate adoption, and nested children releasing leaf-first. The skipped case is exactly `FeriaHosting::test_repairedEngineClosesOpenerAndKeepsChildInput`: both runtimes lack the compiled marker. That skip is an outstanding capability, not a passing independence result.

Raw logs, the isolated unsafe control and their hash inventory are retained locally under the ignored `artifacts/feria-popup-diagnosis-20261013/` directory. No personal profile, provider login or system clipboard was used.

## Integration routes

| Route | Requirements and limits | Assessment |
| --- | --- | --- |
| Reconstructed Quick-only target | Pinned Qt source and internal Core headers, exact SDK/private imports, repository build-internals initialization, and per-pairing semantic checks. Rebuild the adapter/plugin over complete isolated runtimes. | Demonstrated by the follow-up; preferred next integration route for the two tested pairings. |
| Incremental Quick-adapter rebuild | Recover the original matching WebEngine build tree, source commit, compiler/configuration and Core import library. Apply the patch and build `WebEngineQuick` plus `qtwebenginequickplugin`. Stage their output over an isolated copy of that exact complete runtime. Unchanged Core/Chromium work should be reusable, but this was not built here. | Preferred first investigation: smallest compiled change. |
| Fresh complete WebEngine build | Pin the base Qt kit and WebEngine source separately, apply the checked patch, preserve required features, then produce a complete engine/helper/resources/QML runtime and manifest. This recompiles Chromium and is a multi-hour build. | Reliable fallback when the matching original tree cannot be recovered. |
| Upstream release containing the fix | Verify actual source behavior and run the same semantic tests; a version number alone is insufficient. | Future option; no qualifying release was established here. |

Installed Qt kit private headers exist, but internal Core headers such as `web_contents_adapter.h` are absent from the inspected kit. The Quick source target directly includes `../core` and uses Qt build internals. At diagnosis, a fresh Quick-only rebuild was not established. The follow-up demonstrates a pinned-source/build-internals wrapper against the exact installed Core private imports; source reconstruction and per-pairing qualification remain necessary.

The binary file-version resources report 6.11.1 even where the running engine reports WebEngine 6.11.2. Runtime API reports, original build provenance and loaded module paths must establish the pairing; the Windows file-version field alone cannot do that.

The current repository already contains part of the full-build integration in `.github/workflows/build-qtwebengine-codecs.yml`:

1. It applies the patch only when `QT_VERSION == 6.11.1`. Source compatibility with 6.11.2 does not automatically enable that step for 6.11.2; an unsupported or unpatched requested version must fail explicitly.
2. It records a patch hash and `runtimeQualification=pending`, but performs no popup input qualification before upload.
3. It explicitly disables printing/PDF, spellchecking and WebRTC, among other features. Its current output cannot replace the tested browser runtime unchanged without losing capabilities. Preserve the intended feature set and verify it in the candidate.
4. Ordinary app deployment/release still runs the stock kit's `windeployqt`; it does not consume this custom engine artifact. Add explicit verified-artifact consumption before packaging, and ensure a later deployment step cannot overwrite the patched adapter.
5. `verify_browser_runtime.ps1` verifies files exist and are nonempty. It does not verify patch presence, ABI compatibility or child input. Those require separate semantic and manifest gates.

No matching original engine build tree was found in the inspected kit/runtime locations. That is a bounded lookup, not a claim about every disk or other machine.

## Candidate acceptance and stop line

For either compiled route:

1. Stage a separate runtime. Record base kit/source/compiler/configuration, feature switches, patch hash and binary hashes. Verify the probe actually loads the candidate Quick/Core/helper/QML files.
2. Require `test_repairedEngineClosesOpenerAndKeepsChildInput` to pass rather than skip. It must observe opener destruction, continued child typing, a closed/absent opener, host transfer and complete drain.
3. Extend that proof to ordinary/private profiles, nested children, original POST and named targets, before-unload Stay/Leave, renderer loss, and repeated close/exit. Preserve the original native window request; recreating a child from its URL loses POST/opener/document behavior and is not equivalent adoption.
4. Run the affected hosting/lifecycle/runtime probes and a windowed full-app fixture journey. Verify PDF viewing and codec playback before promoting a full rebuilt runtime; test other promised features explicitly.
5. Integrate the proven runtime into deployment/installer inputs and repeat the independence journey in an installed disposable build. A runtime marker or manifest alone is insufficient qualification.

Current production behavior should retain the guard until a candidate passes these gates. Keeping a hidden opener merely retains the dependency; changing focus flags or removing the guard does not fix ownership. No account signup or human interaction is needed for the fixture acceptance. Actual provider-popup flows remain a later check.

Codex should own build pairing, engine/deployment integration and receiving review. A bounded loop worker can build a supplied configuration and run the declared tests. The primary sources and local reproduction answered the initial diagnosis; no Preflight research dispatch was needed at that stage. The follow-up used the loop for the build and component checks. No merge or release is claimed.

## Quick-only build and receiving follow-up, 2026-10-13

On the owner's instruction, loop task `popup-quick-0001` produced a Quick-only Release candidate from official Qt source commit `eb0793cc4b76e93cf669f586fd68c76019f40ec9` (v6.11.1). Chromium was not present or rebuilt. Its configure failure was resolved by importing the required private CMake targets and wrapping the Quick source target in `qt_build_repo_begin()` / `qt_build_repo_end()`. The build completed 89 Ninja steps; a final repeat reported no work and unchanged output hashes.

The minimal wrapper uses the pinned source tree and its `src/core` headers:

```cmake
cmake_minimum_required(VERSION 3.22)
project(QtWebEngine VERSION 6.11.1 LANGUAGES C CXX)
find_package(Qt6 6.11.1 EXACT CONFIG REQUIRED COMPONENTS BuildInternals)
qt_internal_project_setup()
find_package(Qt6 6.11.1 EXACT CONFIG REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 WebChannel WebChannelQuick WebEngineCore PrintSupport
    CorePrivate GuiPrivate QmlPrivate QuickPrivate WebEngineCorePrivate)
qt_make_features_available(Qt6::WebEngineCore)
set(QT_GENERATE_SBOM OFF)
qt_build_repo_begin()
add_subdirectory(src/webenginequick)
qt_build_repo_end()
```

Configure Release/Ninja with the exact 6.11.1 SDK prefix and a separate build/install directory, then build targets `WebEngineQuick` and `qtwebenginequickplugin`. The worker used MSVC 19.44.35227.0 and CMake 3.31. This reconstructs the Quick target; it does not reuse an original engine build tree or modify the SDK.

Candidate hashes:

- `Qt6WebEngineQuick.dll`: `7bd239f5479604eb311969b894bba67841c3f04a5988b01f15a4675103a3de49`.
- `qtwebenginequickplugin.dll`: `71227044fb0fd17d494b1e92a376de1849ea68877da7520a609a292cda9b1992`.

The two files were staged into separate full copies of the stock and codec runtimes. Core/helper/resources and other plugins remain byte-identical to their selected baseline. The newly built delegates library was not substituted for the baseline's delegate dependency. Those exact pairings, rather than arbitrary mixed Qt DLLs, are the accepted component candidates.

The worker reported 18 passes / 0 failures / 0 skips over four windowed executions. Codex independently verified all 17 manifest binary records, recorded loaded Qt module/helper hashes and source diff, then ran four fresh offscreen receiving processes: raw control 3/0/0 and four production hosting cases 6/0/0 on each pairing, all root exits 0. The production independence case now passes instead of skipping: opener destruction, continued child input, closed/absent opener, host transfer and drain are observed.

The first fast offscreen raw receiving check read `beforeafte` immediately after typing, expecting `beforeafter`; it failed without a native crash. A receiving-only copy waits up to five seconds for the exact expected text without resending input. Both runtimes observed the final key on the third read and passed. This is an asynchronous observation correction; the production tests were unchanged. The original receiving failure remains recorded. QML's dynamic-property read still prints `undefined`; the native `QObject::property` capability check and executed production case provide the positive gate, not that QML value.

Loop reply SHA-256 `d48bfcc7f17a79b0bc1e6488b9045a97d7970bd06af0fbe2d90918560307c6a2` was acknowledged with `review-closed` as accepted for this bounded slice, and the mission stopped complete. Local build provenance/receipts are retained in the isolated experiment's `qualification/`; independent receiving logs and timing-adjusted raw fixture are retained in ignored `artifacts/feria-popup-receiving-20261013/`.

The Quick-only route is now demonstrated for these two pairings. Remaining promotion gates are extended production ordinary/private and lifecycle/renderer coverage, functional PDF/WebRTC/codec regression checks, whole-app and installed-disposable journeys, and reproducible artifact consumption in deployment. No baseline runtime was replaced, application source changed, provider account used, merge performed or release made in this follow-up.

## Shutdown fixture receiving follow-up, 2026-10-13

The extended qualification exposed a stale shutdown-fixture predicate: it opened five engines, retired one, then still required five engines at exit. Current retirement can correctly destroy that closed engine before the exit step. The fixture now proves five distinct registered views and ordinary/private profile allocation before retirement, observes the closed engine become absent, and requires four surviving views with two profiles. Its final predicate explicitly retains the running script, slow navigation, download and nonreleasable shared profile. Retention journeys and product lifetime code are unchanged.

The isolated candidate passed four fresh stock and four fresh codec shutdowns in loop task `popup-shutdown-repeat-0007`. Codex validated every native exit, disposable profile, exact loaded module identity and ordered zero-count teardown; independent receiving runs also passed on both pairings. Task `popup-shutdown-control-0008` separately ran the existing deliberately skipped-shutdown control once per pairing. Each scene reached its pending-work assertion, then the native process returned the expected exit 3 with four views/two profiles alive and profile-before-view teardown count 2, without crashing. These are negative-control results, excluded from positive shutdown counts.

Codex integrated only the shutdown predicate correction, retained the repository-relative Feria import, and ran a fresh source-path positive shutdown on each candidate: native exit 0, scenario PASS, views/profiles drained to zero and no profile-before-view teardown. No native rebuild was needed. Isolated Windows import errors and codec-runner setup mistakes were corrected in disposable lanes; their original failed or blocked receipts remain preserved.

Accepted local receipts are retained under `feria-popup-shutdown-repeat-20261013-07`, `feria-popup-shutdown-control-20261013-08`, and `feria-popup-source-verification-20261013-08` in the isolated engine-research lane. This closes the corrected shutdown-fixture proof only. The original broad qualification handoff is still missing; reported codec daily/freezing failures, retention-cookie discrepancy, and stock renderer provenance need separate receiving work. No installed-package qualification, baseline runtime replacement, merge or release is claimed.
