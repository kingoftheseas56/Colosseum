# A1 — Qt Quick WebView2 host proof handoff

**STATUS: COMPLETE for the bounded A1 standalone-probe slice (2026-09-26).** Proof 2 is ACCEPTED; proofs 3–6 are PROVEN. Proof 1 remains `UNTESTED-NEEDS-HUMAN-SIGNIN`. This is not a Netflix playback, real-Colosseum, or Harness receipt. Do not start A2 from this handoff.

## Governing scope

Authority used:
1. `C:/Users/Suprabha/Desktop/Brotherhood/docs/superpowers/plans/2026-09-25-feria-implementation.md` §0, §2, §3 T2–T5/T11, §5 A1.
2. `C:/Users/Suprabha/Desktop/Brotherhood/Colosseum/AGENTS.md`.
3. `C:/Users/Suprabha/Desktop/Colosseum Streaming Evidence/QT-WEBVIEW2-HOST-SETUP-2026-09-24.md` and `RESULT-2026-09-24.md`.

This work stayed a standalone `QQuickWindow` probe. Colosseum itself was not intentionally launched by this slice and no Harness pass is claimed. A separately running Colosseum process from other work was observed late in the session and was not touched.

## Authored / changed files

| Path | SHA-256 at final A1 snapshot |
|---|---|
| `native/feria/provider-host/FeriaProviderHost.h` | `6AD5BBA79520EDA1C7125BE9EA1FCB7A64F52E019107BB744AAFA0C354FCF3CB` |
| `native/feria/provider-host/FeriaProviderHost.cpp` | `5C4146E732D3E29B0DB511AFD60FF0FB81FB2819A95AD9D589C7E8448A9D7C81` |
| `native/feria/provider-host/FeriaHostItem.h` | `3A1C474C6B76A9BB2FAA6B8954A210D27FA350C561B931B1BD0C30CBA81FADE5` |
| `native/feria/provider-host/FeriaHostItem.cpp` | `43E6C30756D175AFE1533633E2A2BF95A8164CA90158A50EA2A77C240DC3048A` |
| `tests/feria_host_probe/main.cpp` | `DB590133BA130992A289CDDF1B31C0679286389988C93A0C60726DA4CD310332` |
| `tests/feria_host_probe/Main.qml` | `8ED553B0B7C9773E45BB759934A85C1CB2A474828ED6760FA0C6FF8EBD59A840` |
| `tests/feria_host_probe/CMakeLists.txt` | `2C49A85170443C04EF591B9F4973AF6A61F125200E95FB57D0DB635AC2F22251` |
| `tests/CMakeLists.txt` (existing hook; not edited in this continuation) | `74BD7C0FF334FFE508527A8993970DB1F63F9B5E79092C7D5022FBCB7EFF0D3A` |

The only shared CMake edit made by this slice is `tests/CMakeLists.txt:16-18`, with `add_subdirectory(feria_host_probe)` at line 17, guarded by `if(WIN32)`.

No A1 edit was made to `native/CMakeLists.txt`, `native/main.cpp`, `qml/Main.qml`, `qml/feria/FeriaWorld.qml`, `qml/TopBar.qml`, `native/feria/PorticoComposition.*`, or any account/progress/sync file.

## Dependency mechanism

Used the repository existing classic vcpkg path, matching Windows CI/toolchain practice. No manifest or shared dependency file was edited.

Local dependency command:
`C:\vcpkg\vcpkg.exe install webview2:x64-windows`

Installed:
- `webview2:x64-windows@1.0.3800.47`
- `wil:x64-windows@1.0.260126.7`

CMake consumes `find_package(unofficial-webview2 CONFIG REQUIRED)` and links `unofficial::webview2::webview2`.

## Git-state preservation

Baseline `git status --short` was captured before A1 work at 2026-09-25T13:50:45Z. The tree was already heavily dirty with unrelated staged/unstaged/untracked work. In particular, `native/CMakeLists.txt` and `tests/CMakeLists.txt` were already modified and `native/feria/` already contained unrelated untracked Feria work. The A1-specific provider-host and probe files did not exist.

Immediately before the four-line CMake patch, `tests/CMakeLists.txt` SHA-256 was `A6E48B46F9FBF73781DAFA03842C312FAD429949BA28AB241688AFCD5F472E6F`. BDC `patch_preview` and `patch_apply` both enforced that hash precondition. The patch was exactly four insertions.

The tree continued changing concurrently from unrelated agents after that patch, so the transfer-time `tests/CMakeLists.txt` hash differs. Do not interpret the present global dirty tree as A1 output. The A1 CMake block remains at lines 16-18.

No `git add`, commit, push, stash, reset, clean, checkout, branch, or worktree operation was performed by this slice.

## Build receipt

Standalone configure, not the Colosseum fleet:

`call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S "C:\Users\Suprabha\Desktop\Brotherhood\Colosseum\tests\feria_host_probe" -B "%LOCALAPPDATA%\Colosseum\feria-host-probe\build-a1-msvc" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"`

Configure exit code: 0.

Target-only build:
`call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "%LOCALAPPDATA%\Colosseum\feria-host-probe\build-a1-msvc" --target feria_host_probe`

Final target-only build exit code: 0 on 2026-09-26. The probe CMake target now copies `Main.qml` on a target-only build even when the EXE does not need relinking. No Colosseum target was built.

First compile exposed two local defects and was repaired:
1. generated WebView2 COM header lacked COM `interface` definitions under the lean Windows include path; fixed by including `<unknwn.h>` before `<WebView2.h>`;
2. `qmlRegisterType` internally subclasses the registered type, so `FeriaHostItem` could not be `final`; `final` was removed.

Runtime deployment used `C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --release --qmldir ...`; deployment exit code 0.

`dumpbin /DEPENDENTS` showed direct release dependencies including `Qt6Quick.dll`, `Qt6Gui.dll`, `Qt6Qml.dll`, `Qt6Core.dll`, and `WebView2Loader.dll`. No debug Qt DLL dependency was present.

Built EXE SHA-256: `1F326F5E092D402BE5CAA247B5AE4BA86DADB8EF7A7B65C79FB779151AC64C7D`.
Deployed `WebView2Loader.dll` SHA-256: `86545B66CDB0603BC26B626FB9AD610CB6E71F28D468F5EA66DF23B03DDA96D5`

Build/runtime root: `%LOCALAPPDATA%\Colosseum\feria-host-probe\build-a1-msvc`
Evidence root: `%LOCALAPPDATA%\Colosseum\feria-host-probe\evidence`

## Profile handling

Original profile: `%LOCALAPPDATA%\colosseum_qt_webview2_host\webview2-profile`

It was copied recursively to `%LOCALAPPDATA%\Colosseum\feria-host-probe\profile-a`. The original was not moved or modified and no cookie values were read or copied individually.

A fresh `profile-b` directory was created for the isolation proof. It has since been used for YouTube proof runs, so it is no longer pristine.

## Proof table

| # | Final status | Evidence / finding | Remaining boundary |
|---|---|---|---|
| 1 Netflix playback | **UNTESTED-NEEDS-HUMAN-SIGNIN** | Copied profile-a opens `https://www.netflix.com/in/`; signed-out samples show `hasVideo:false` and `signInText:true`. No credentials were entered. | Hemanth's own sign-in and a witnessed Netflix title playback remain separate from this probe verdict. |
| 2 bounds / DPI / resize / fullscreen | **ACCEPTED for A1** | Actual native DPR 1.5 and simulated effective DPR 1.0 both report `controllerExact:true` and `nativeChildExact:true` in windowed, resized, fullscreen, and restored states. | Real Windows 100% scaling or monitor DPI transition moves to the C1 Colosseum launch gate. Windows display settings were not changed. |
| 3 QML overlay airspace | **PROVEN** | PID 14564: host visible before, hidden while the QML overlay owns the rect, then visible again; the `a1-14564-150-overlay-open.png` image was inspected. | Standalone probe only. |
| 4 keyboard focus and Esc return | **PROVEN with probe foreground** | Final-source PID 17852 has three consecutive cycles: each pre-key `<video>.paused=false`; each real `SendInput` K (`vk=75`, `sent=2`) targets the foreground probe, appears as `keydown` `key:k` on YouTube's focused `#movie_player`, and changes `<video>.paused` to true. Each Esc (`vk=27`) reaches WebView2, emits `returned-to-qml`, reports `activeFocus=true`, and leaves media paused. | The probe checks foreground and WebView child focus before sending keys, waits for WebView's GotFocus event, and restores its own minimized window. Earlier failed keys exposed input targeting and page-focus timing, not a child-HWND hosting limit. No composition-hosting attempt was triggered. |
| 5 profile persistence / isolation | **PROVEN** | PID 14700 wrote and read `feria-a1-persisted-20260925` in profile-a; closed; PID 18564 read the same marker on relaunch; PID 17804 in profile-b read `marker:null`. | The provider's own authenticated session still depends on proof 1. These marker runs were not repeated. |
| 6 allowlist / host-owned popup | **PROVEN** | PID 20512 logged `NAV_BLOCKED` for `https://example.com/feria-a1-off-list` and `POPUP_HOSTED` for the allowed YouTube URL. `PrintWindow` captured the actual popup HWND (`0x7f0e14`), with YouTube visible; PID 24468's earlier popup image shows its video frame. PID 20512 browser snapshots before/after each contain the same 18 process IDs, and its 100 ms process watch observed no new OS browser process. | No OS default browser handoff was observed in this bounded run. |

## Key evidence paths

Netflix signed-out evidence:
- `%LOCALAPPDATA%\Colosseum\feria-host-probe\evidence\a1-1648-events.jsonl`
- `...\a1-1648-100-netflix-state-1.png`
- `...\a1-1648-100-netflix-state-2.png`

150% / DPR 1.5 geometry evidence:
- `...\a1-7056-events.jsonl`
- `...\a1-7056-150-geometry-windowed.png`
- `...\a1-7056-150-geometry-resized.png`
- `...\a1-7056-150-geometry-fullscreen.png`
- `...\a1-7056-150-geometry-restored.png`

Effective 100% / DPR 1.0 geometry evidence:
- `...\a1-24612-events.jsonl`
- `...\a1-24612-100-geometry-windowed.png`
- `...\a1-24612-100-geometry-resized.png`
- `...\a1-24612-100-geometry-fullscreen.png`
- `...\a1-24612-100-geometry-restored.png`

Overlay proof:
- `...\a1-14564-events.jsonl`
- `...\a1-14564-150-overlay-before.png`
- `...\a1-14564-150-overlay-open.png`
- `...\a1-14564-150-overlay-restored.png`

Focus proof:
- `...\a1-17852-events.jsonl` (final-source three complete K/Esc cycles, including page `keydown` and `<video>.paused` reads)
- `...\a1-17852-150-focus-cycle-3-after-escape.png` (probe HWND only)
- `...\a1-20312-events.jsonl` (earlier three-cycle proof); `...\a1-2472-events.jsonl` diagnoses non-foreground `SendInput` (`send-key-skipped`, foreground PID 19992). PID 20600/26692/26296 runs exposed probe minimization or input/page-focus timing before the final repair.

Persistence and isolation:
- `...\a1-14700-events.jsonl` (write), `...\a1-18564-events.jsonl` (relaunch read), `...\a1-17804-events.jsonl` (profile-b marker null).

Navigation and popup:
- `...\a1-20512-events.jsonl`
- `...\a1-20512-150-policy-popup-owned-final.png` (popup HWND `0x7f0e14`)
- `...\a1-24468-150-policy-popup-owned-final.png` (loaded YouTube video frame in popup HWND `0xbb0796`)
- `...\a1-20512-policy-browser-before.json`, `...\a1-20512-policy-browser-after.json` (18 identical browser PIDs; no `browser-process-observed` event).

Every event log records the probe PID; screenshot filenames also embed that PID.

## Implementation notes

`FeriaProviderHost` owns WebView2 environment/controller lifecycle, physical bounds, visibility, allowlist policy, host-owned popup WebView2 windows, ExecuteScript, and the WebView2 accelerator-key Esc callback.

`FeriaHostItem` owns the QML placeholder geometry, conversion from logical scene rect to physical parent-HWND client pixels, airspace hide/show, WebView/QML focus handoff, and JSON geometry capture comparing expected physical rect, controller bounds, and actual child HWND bounds.

The geometry mechanism deliberately derives scale from the real native client size divided by the QQuickWindow logical size rather than trusting `devicePixelRatio` alone. This directly targets the old logical-vs-physical sizing defect. The probe screenshot helper now calls `PrintWindow` for a specific HWND and never captures the desktop or sets `HWND_TOPMOST`. The focus proof sends no key until the probe owns foreground and its WebView child owns focus; it then verifies the key in the page and reads `<video>.paused`. The policy proof snapshots browser processes before and after and watches for new browser processes during the popup.

Probe automation modes currently present: `geometry`, `overlay`, `focus`, `profile-write`, `profile-read`, `profile-fresh`, `policy`, `netflix`, `manual`.

## Remaining boundaries

1. Proof 1 needs Hemanth's own Netflix sign-in and playback witness. Never enter credentials on his behalf.
2. The real Windows 100%/DPI-transition check belongs to C1. No display settings were changed here.
3. This was a standalone QQuickWindow probe. No Colosseum launch, Harness pass, or integrated-provider claim is made.
4. `profile-b` is no longer pristine. Other agents continued changing the shared dirty tree; no add, commit, push, stash, reset, clean, checkout, branch, or worktree action was taken here.

## Exact launch command

Manual standalone probe launch at the machine normal 150% Qt DPR:

```powershell
$env:PATH = 'C:\Qt\6.11.1\msvc2022_64\bin;' + $env:PATH
& "$env:LOCALAPPDATA\Colosseum\feria-host-probe\build-a1-msvc\feria_host_probe.exe" `
  --profile "$env:LOCALAPPDATA\Colosseum\feria-host-probe\profile-a" `
  --url "https://www.netflix.com" `
  --automation manual `
  --evidence "$env:LOCALAPPDATA\Colosseum\feria-host-probe\evidence" `
  --scale-label 150
```

## Final A1 disposition

The bounded technical A1 slice is complete under the 2026-09-26 review criteria: proof 2 ACCEPTED, proofs 3–6 PROVEN, proof 1 `UNTESTED-NEEDS-HUMAN-SIGNIN`. Stop here; no A2 work is part of this handoff.
