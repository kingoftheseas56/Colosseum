# WebView2 consented-close feasibility probe

This explicitly run Windows probe uses real HWND-hosted WebView2 controllers,
the same controller kind used by `FeriaProviderHost`. It does **not** instantiate
Colosseum's host, SessionStore, close owner, account profiles or QML UI. It makes
no changes to production behavior and qualifies native API feasibility only.

The candidate close stimulus is native `Navigate(about:blank)`. The host handles
the real `ScriptDialogOpening` beforeunload event, retaining its arguments and
deferral. Stay completes without `Accept`; Leave accepts and completes. The
controller is disposed only after successful, correlated navigation completion
and verification that the original document has actually been replaced.

## What it checks

- Stay preserves the document marker, unsaved form, DOM/native URL, history
  length, native back/forward availability and controller.
- An immediate retry, without another page gesture or reload, gets another
  beforeunload decision. Leave reaches a new document before disposal.
- A trusted site-button navigation cancelled with Stay also permits an
  immediate host close on that same document.
- Closing the intended view preserves a sibling's document and navigation log.
- No-handler and genuinely unactivated beforeunload pages can depart without
  synthetic consent dialogs. Actual navigation completion is still required.
- Raw controller `Close()` bypasses beforeunload. `--negative-control` replaces
  the first consented close with that unsafe disposal; the safety assertion must
  fail with executable exit 1. The runner accepts RED only with the specific
  failed assertion, never for a crash or timeout.

Input goes through the targeted renderer's CDP `Input.dispatchMouseEvent` after
native `MoveFocus`. The fixture verifies `isTrusted` and user activation. No
desktop mouse, keyboard, signed-in profile or streaming account is used. Small
owned test windows are shown without initially raising the foreground window.
Each run uses a fresh disposable user-data directory and a loopback-only server
on an automatically chosen port. Profiles are retained outside the repository
for diagnosis while browser processes finish exiting.

## Observed results, 2026-10-08

At Colosseum baseline `62057bfbdaa2305a05d2d3fb02b870bc9e826638`, built with
MSVC 19.44.35223.0, Windows SDK 10.0.26100.0 and WebView2 SDK 1.0.3800.47:

| Actual WebView2 runtime | Run | Result |
| --- | --- | --- |
| 154.0.4258.62 | final-green-1 | 38 assertions passed, 0 failed; exit 0 |
| 154.0.4258.62 | final-red | 6 setup assertions passed; forced disposal rejected; exit 1 |
| 154.0.4258.62 | final-green-2 | 38 assertions passed, 0 failed; exit 0 |

Executable SHA256:
`ac6a38b0598af23b12852f89932dea3e70d007b2b9921a91d569a0b4cfad5e00`.
The runner writes full event traces, source/executable/loader hashes, runtime
version, overrides and results to its requested output directory. These are
assertion counts, not 38 independent feature tests.

Two initial assumptions were disproved and the traces were preserved:

1. **Stay can cancel before `NavigationStarting`.** This runtime emitted the
   beforeunload dialog but no starting/completed navigation for the denied
   attempt. The first probe therefore timed out waiting for a navigation ID.
   A production owner must record its captured close operation and explicit
   Stay decision independently of a navigation ID; it must still require real
   document departure for Leave. Native `Navigate` returning S_OK is not consent
   or departure. Both host retry and retry after cancelled site navigation were
   subsequently verified.
2. The initial unfocused CDP click did not reach the button. Native `MoveFocus`
   fixed input delivery. Diagnostic reads through `ExecuteScript` also observed
   activation even before trusted input on this runtime. The no-gesture test
   uses `Runtime.evaluate` with `userGesture:false` and never `ExecuteScript`
   before departing, so observation cannot accidentally activate its page.

## Run

Requires an installed WebView2 runtime, MSVC C++ tools, CMake, Python and a
WebView2 SDK CMake package exposing `unofficial::webview2::webview2` (as supplied
by vcpkg). Set `WEBVIEW2_PREFIX` to that installed SDK prefix. From the repository
root in PowerShell:

```powershell
$probeBuild = Join-Path $env:TEMP 'feria-wv2-close-build'
cmake -S tests/feria_webview2_close_probe -B $probeBuild `
  -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$env:WEBVIEW2_PREFIX"
cmake --build $probeBuild --config Release
$probeExe = Join-Path $probeBuild 'Release/feria_webview2_close_probe.exe'
python tests/feria_webview2_close_probe/run.py --exe $probeExe `
  --output (Join-Path $probeBuild 'green-1')
python tests/feria_webview2_close_probe/run.py --exe $probeExe `
  --output (Join-Path $probeBuild 'red') --negative-control
python tests/feria_webview2_close_probe/run.py --exe $probeExe `
  --output (Join-Path $probeBuild 'green-2')
```

The Python runner exits zero for the expected RED rejection, while preserving
the executable's actual exit 1 in `result.json`. Ordinary runs require exit zero
and a zero-failure summary. The probe has per-operation deadlines; the runner
has an overall 90-second deadline. No broad build or test suite is required.

## Remaining qualification

This is a narrow candidate mechanism, not a general `RequestClose` API or a
production implementation. Integration must keep internal unload routing out
of visible URL/history/restore offers and validate the captured account,
profile, space, host and document identities. Stale consent, competing
navigations, pending-deferral shutdown, renderer failure, child-frame unload,
site-requested window closure, private profiles and full-app UI remain untested
here. About:blank may temporarily change history on an approved attempt; the
probe disposes only afterwards and does not establish production history policy.

Max/Return reparenting, playback ownership, permissions, downloads, process
recovery, codec builds, DRM/CDM, Netflix and Prime playback remain outside this
probe. No Qt parity conclusion follows from these isolated Windows results.

API basis: Microsoft's [controller Close](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2controller#close),
[native navigation/events](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2),
and [script dialog Accept/deferral](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2scriptdialogopeningeventargs).
