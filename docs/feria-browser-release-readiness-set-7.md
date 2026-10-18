# Feria browser, set 7: release readiness

Status: DISCOVERY COMPLETE, one small fix built (accessible names, finding 9). The browser is not release-ready: the release path ships the stock engine without the paid codecs, and nothing proves an upgrade keeps browser state. Branch `codex/feria-release-readiness`. Pushed, not merged.

Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map. Set 5 (`docs/feria-browser-native-services-set-5.md`) is the base of this branch.

## Base and merge endpoint

Built on `codex/feria-native-services` at `ed1b0a9c`.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the set 5 branch.

## The bar

The plan's wording is "accessibility, default-browser registration, updates, security and compatibility regressions", with the proof "repeatable installation and upgrade with retained state". The owner's direction: a functioning browser for a known set of services, not Chrome parity. Judged by that.

## Method

Read-only. A loop worker read the packaging scripts, the release workflow, the installer, the startup code and the three runtime folders; the reviewer re-read every cited file and line. No installer was built or run, no app was launched. Everything below is derived from source and directory listings, not from an installed build. Mission: `<local-missions>/feria-set7-01945259` (task, reply, review).

## Findings

| # | Finding | Evidence | Verdict |
| --- | --- | --- | --- |
| 1 | The release ships the STOCK engine. The release workflow installs the ordinary Qt 6.11.1 kit and runs its `windeployqt`; nothing brings in the codec-enabled runtime. A separate workflow, `build-qtwebengine-codecs.yml`, exists to build one, but the release does not consume it. A release built today opens pages and cannot play H.264/AAC, which the paid services need. | `.github/workflows/release-installer-smoke.yml:67-75,155-159`; `native/deploy-runtime.bat:15-20` | Release blocker, large |
| 2 | Widevine is not packaged or registered anywhere. The codec workflow says so itself ("Google's Widevine CDM itself is NOT bundled"). Protected playback was proven on this laptop only. | search of `native`, `qml`, `scripts`, `.github` | Release blocker for protected services; how the CDM reaches a user's machine is an owner decision |
| 3 | Installing a release over an older copy can leave the app unable to start. The ordinary install unpacks over the existing folder and removes nothing. At start the app hashes every file under `qml/` and refuses to boot (`qml_build_mismatch`) if the tree differs from what it was built with. So any QML file deleted between two versions survives the over-install and blocks the new version. Only the in-app update (`/UPDATE`, clean sibling folder then swap) is safe. Seven QML files were deleted between `v1.1.8` and `v1.1.9`, and both versions carry the check, so this may already affect a 1.1.8 user who moved to 1.1.9 by re-running the installer, WinGet or Chocolatey. Not reproduced. This is an app-wide installer defect, not a browser one. | `scripts/installer/colosseum.nsi:123-129`; `native/bootstrap/StartupLayout.cpp:44-85,142-173`; `git diff --diff-filter=D v1.1.8 v1.1.9 -- qml` | Release blocker, small fix |
| 4 | Nothing proves an upgrade keeps browser state. State lives outside the install folder (`<profile>/feria/account.json`, `<profile>/feria/browser/qtwebengine`), so by design an update and an uninstall leave it alone. The update test matrix uses invented payload files and never creates an account file or an engine profile. | `native/main.cpp:1986-2004`; `qml/feria/FeriaSessionDeck.qml:565-575`; `tests/installer/update_matrix.ps1` | Release blocker (proof missing, no deletion bug seen) |
| 5 | The account file has one version and no migration. A file with another version is left on disk untouched and the browser starts empty; writes are refused while the read error stands. Safe against data loss, not functional after a downgrade or a future format change. | `native/feria/FeriaAccountStore.cpp:183-213` | Fine for now; needs a rule before the format first changes |
| 6 | The installer smoke test checks `QtWebEngineProcess.exe` but not the engine DLLs, its resource packs, `icudtl.dat`, the locales or the QML modules the browser imports. A package missing them would pass. | `tests/installer/release_installer_smoke.ps1:36-60`; `scripts/installer/package_release.sh:75-82` | Should fix, small |
| 7 | The browser's QML ships automatically: QML is loaded from files, the whole tracked `qml/` tree is staged, and `windeployqt --qmldir` scans it for imports. No hand-kept list. | `scripts/installer/package_release.sh:69-97` | Fine |
| 8 | The built-in PDF viewer needs no separate packaging rule that could be found; whether an installed release shows a PDF inline is unproven. | set 4 proof on both runtimes; kit `resources` listing | Not determinable without an installed build |
| 9 | Accessibility: the page-prompt answer field (`feriaDialogInput`) has no accessible name, because the shared field takes its name from a placeholder it does not have. The address suggestion rows have no accessible name or role; they work from the keyboard through the address field (Up, Down, Enter). Everything else added in sets 2 to 4 carries a name. | `qml/feria/FeriaBrowserRequests.qml:400-409,461-467`; `qml/feria/FeriaBrowserToolbar.qml:232-284` | Should fix, small |
| 10 | Security as enforced today: only http, https, `about:blank` and http(s) blobs navigate; certificate errors are always refused; page script sees no app objects (set 1 probe); private tabs are off the record; the browser writes no password anywhere. | `native/feria/FeriaBrowserPolicy.h:75-94`; `FeriaBrowserRequests.qml:321-324` | Fine |
| 11 | Remote debugging: nothing in the app blocks `QTWEBENGINE_REMOTE_DEBUGGING` or a `--remote-debugging-port` flag in a shipped build. It takes a process running as the same user to set either. | no match in `native/` | Should fix, small, C++ (next app rebuild) |
| 12 | The Lanista dev bridge is on in release builds for reads and screen grabs, by the owner's 2026-08-01 ruling; clicking, typing and writes need an environment switch. It is a same-user local pipe, not a network port. A process running as the same user can already read the browser's profile folder on disk, so the bridge adds little exposure. | `native/main.cpp:2086-2092`; `native/devtools/LanistaServer.cpp:266-280,386-405` | Owner decision; recommendation: leave |
| 13 | Engine age: the engine is whatever Qt kit the release was built with (stock 6.11.1: Chromium 140, security patch 148.0.7778.96; codec runtime 6.11.2: patch 147.0.7727.138, older than stock). Nothing warns when it goes stale. | set 1 record; release workflow pin | Should fix when the codec runtime enters the release |

## Fixed on this branch

Finding 9 (`e4bb91b4`, tests `54a3a7c9`, `24dd4e8c`). The page-prompt answer field is named "Answer to the page's question". The suggestion list is a named list; each row is a list item named by its title and address, reports whether it is selected, and opens on the accessibility press action. Keyboard behaviour, layout and Tab order are unchanged. Proof: `tests/feria_interaction_probe` reads the names, roles and selection from the live items and re-checks Down, Up, Enter and Escape; 11 passed 0 failed on both runtimes, `tests/feria_daily_probe` 34 passed 0 failed on both, by the worker and again by the reviewer (`C:\b\ff-results\set7\task-0002`, `review-0002`). No screen reader was run.

## Left out on purpose

Registering Colosseum as the Windows default browser. Feria exists so a set of services run inside Colosseum; taking over links system-wide serves nothing there and adds obligations.

## Order of work

1. Accessible names for the prompt field and the suggestion rows: done, see below.
2. Over-install cleanup in the installer and the fuller smoke list (provable only in CI: it needs a built installer).
3. An upgrade test that carries a real account file and engine profile from one version to the next (CI).
4. The codec-enabled runtime as the release's engine, with its version and patch level recorded (large; decides whether the paid services play at all in a release).
5. Widevine delivery (owner decision first).
6. Remote-debugging block, at the next C++ change.

## Not verified

No installer was built, installed, upgraded or removed. No installed Colosseum was found at `C:\Program Files\Colosseum` or `%LOCALAPPDATA%\Programs\Colosseum`. Finding 3 was derived from the code, not reproduced. No screen reader was run.
