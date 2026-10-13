# Feria popup independence: diagnosis and integration routes

Date: 2026-10-13. Source baseline: `24499a7a3ce6d047134306dd2ec1de98f5d07da0` on `codex/feria-release-packaging`.

Status: the unguarded engine failure was reproduced on both available runtimes. The existing patch matches both inspected source releases. No patched engine was compiled, installed or qualified in this diagnosis; the production guard remains enabled.

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
| Incremental Quick-adapter rebuild | Recover the original matching WebEngine build tree, source commit, compiler/configuration and Core import library. Apply the patch and build `WebEngineQuick` plus `qtwebenginequickplugin`. Stage their output over an isolated copy of that exact complete runtime. Unchanged Core/Chromium work should be reusable, but this was not built here. | Preferred first investigation: smallest compiled change. |
| Fresh complete WebEngine build | Pin the base Qt kit and WebEngine source separately, apply the checked patch, preserve required features, then produce a complete engine/helper/resources/QML runtime and manifest. This recompiles Chromium and is a multi-hour build. | Reliable fallback when the matching original tree cannot be recovered. |
| Upstream release containing the fix | Verify actual source behavior and run the same semantic tests; a version number alone is insufficient. | Future option; no qualifying release was established here. |

Installed Qt kit private headers exist, but internal Core headers such as `web_contents_adapter.h` are absent from the inspected kit. The Quick source target directly includes `../core` and uses Qt build internals. A fresh standalone Quick rebuild against installed DLLs alone is therefore not an established shortcut. It would need reconstructed source/configuration and ABI verification, rather than a normal app build.

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

Codex should own build pairing, engine/deployment integration and receiving review. A bounded loop worker can build a supplied configuration and run the declared tests. The primary sources and local reproduction answered the diagnosis; no Preflight research dispatch was needed for this slice. This report does not authorize or claim a merge, release or engine build.
