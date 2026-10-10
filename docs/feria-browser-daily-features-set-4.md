# Feria browser, set 4: daily browser features

Status: implemented and verified on Windows against the stock Qt 6.11.1 kit and the codec-enabled runtime, and proved through the full application. Branch `codex/feria-daily-features`. Pushed, not merged, not released.

Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map. Set 3 (`docs/feria-browser-website-interaction-set-3.md`) is the base of this branch.

## Base and merge endpoint

Built on `codex/feria-website-interaction` at `6a626c88`.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the set 3 branch.

## How this set was made

Through the Preflight loop: nine bounded tasks, each to a fresh worker, each reviewed by rerunning its evidence before the next was sent. The mission folders `C:\Users\Suprabha\Desktop\Loop-Missions\feria-set4-01945259` and `-02` to `-09` hold every task, reply and reviewer note. Two workers stopped without a final reply and one could not author; their work was reviewed from the branch and continued by the next task. The owner's bar for the set: a functioning browser, not Chrome parity.

## Set 4 against the plan

The plan's scope is omnibox suggestions, shortcuts, context menus, spellchecking, PDF tools and DevTools, with the proof "usable end-to-end browsing".

| Item | State | Proof |
| --- | --- | --- |
| PDF viewing | PDFs now open inside the tab | real-engine probe |
| Context menu | the browser's own menu replaces Qt's stock one | real-engine probe, headless render |
| Shortcuts | existed; each now proved with a web page holding the keyboard; none added | full application |
| Address-bar suggestions | new, from saved bookmarks and history | full application, headless render |
| Spell checking | left out | see Left out |
| DevTools | left out | see Left out |

Two faults were found by proving things through the real application and are fixed here: the host view showed no page title for private tabs, and the test fixture ran in a different controls style from the app.

## What changed

**PDFs open in the tab.** `qml/feria/FeriaQtWebEngine.qml` enables the engine's built-in viewer (`settings.pluginsEnabled`, with `pdfViewerEnabled`). Before, navigating to a PDF downloaded it and the page did not change. A PDF served as an attachment still downloads.

**The right-click menu is the browser's.** New `qml/feria/FeriaContextMenu.qml`, owned by the engine wrapper, which accepts the engine's request so Qt's stock menu never opens. Entries, each present only when it applies: Open link in new tab, Copy link address; Open image in new tab, Save image, Copy image address; Cut, Copy, Paste, Select all; Back, Forward, Reload. Groups are separated, disabled entries are dimmed, and the menu opens where the page asked and stays inside the view. It is withdrawn by navigation, hiding, closing and renderer loss.

- Why replace the stock menu: its Back entry was disabled while the tab had history, it offered entries that mean nothing here (Save page, View page source, Open in new window), and it was not styled like the app.
- Back, Forward and Reload go through the wrapper's navigation controller, so they are recorded like the toolbar's. Link, image and edit entries trigger the engine's own context actions, with no script run in the page. Open image in new tab goes through the deck's existing new-tab path.

**The address bar suggests saved pages.** `qml/feria/FeriaBrowserToolbar.qml` shows up to six rows under the field while the user types: bookmarks first, then history, no address twice. The rows come from the store's own `searchBrowserBookmarks` and `searchBrowserHistory` (set 2); matching is not re-implemented in QML. Down and Up move a highlight, Enter opens the highlighted row or, with none, behaves as before with the typed text; a click opens a row; Escape closes address editing and the list. Nothing is shown for an untouched address, empty text or no match. Titles are plain text. A private tab gets the same suggestions; what is typed is recorded nowhere.

**The host shows the page's own title.** `qml/feria/PorticoDiscoveryHostView.qml` set its page title only from playback samples, which arrive only while activity is being recorded. A private tab, or any tab with recording paused, showed no title after it navigated: the browser menu's header fell back to the provider name and a bookmark saved from such a tab was titled with its address. The host now follows the engine's title as well.

**The fixture uses the app's controls style.** `native/main.cpp` forces `QT_QUICK_CONTROLS_STYLE=Basic`; `tests/feria_foundation_runtime.cpp` did not, so every real-engine probe in sets 1 to 3 ran under the default Windows style. Thirteen probes were run before and after the change with identical outcomes, and about 4,700 "style does not support customization" warnings left the logs. Headless renders made before this change (sets 2 and 3) were in the wrong style.

Small observation hooks: object names on the downloads owner and the active request owner, and a read-only `printPromptPending` on the request owner, used by the full-application runners.

## Shortcuts: what was proved

No shortcut was added or changed. An earlier worker's change making Ctrl+W application-wide was reverted as unproven. Each row was observed in the real application, headless, with a page `<textarea>` holding the keyboard first (a plain letter typed into it arrives), and with the textarea's value unchanged afterwards.

| Shortcut | Observed |
| --- | --- |
| Ctrl+L | address bar opens with focus; Escape returns the keyboard to the page without a click |
| Ctrl+R, F5 | the page reloads |
| Alt+Left, Alt+Right | Back and Forward |
| Ctrl+T | a new tab |
| Ctrl+Tab, Ctrl+Shift+Tab | the active tab changes and changes back |
| Ctrl+Shift+N | a private tab |
| Ctrl+W | closes the tab being shown and returns to Feria's home; see below |
| Ctrl+Shift+T | reopens the closed tab |
| Ctrl+H, Ctrl+D, Ctrl+J | History, Bookmarks, Downloads open |
| Ctrl+F, then Escape | Find opens and closes |
| Ctrl+=, Ctrl+-, Ctrl+0 | zoom in, out, reset |
| Ctrl+P | a print-to-PDF request starts |

**Ctrl+W returns to Feria's home even when another tab of the service is open.** That is the designed behaviour, not a fault: `native/SessionStore.h`, `close()`, records the owner's ruling of 2026-08-08, "Sessions are taskbar windows, not tabs: closing one reveals the desktop behind it". Each browser tab is such a session. The other tab stays open and is shown when the service is opened again. Whether browser tabs should behave differently has been put to the owner; nothing was changed.

## Verification

Windows 11 Pro 10.0.26200, MSVC 2022, 2026-10-10 and 2026-10-11. Everything ran headless (`QT_QPA_PLATFORM=offscreen`). Fixture runs used a loopback server and throwaway profiles. Full-application runs used disposable Lanista profiles, opened the public MANGA Plus site anonymously, then visited pages on a loopback server started by the runner. No test touched the Windows clipboard, and after the first task none wrote outside the fixture's own folders.

### New

| Suite | Stock | Codec |
| --- | --- | --- |
| `tests/feria_daily_probe` (real engine: PDFs, context menu, discovery cases) | 34 / 0 / 0 | 34 / 0 / 0 |
| `run_app_suggestions.py` (full application, five scenarios, each run twice) | history 87/0, keyboard 95/0, bookmarks 76/0, private 81/0, escape 78/0; all ten launches exit code 0 | not run |
| `run_app_shortcuts.py` (full application) | tabs 174/0, single-close 61/0, escape-focus 47/0, navigation 124/0, each with exit code 0; panels 171/0 without a clean exit | not run |

Real-engine cases in `tests/feria_daily_probe/tst_Daily.qml` end with what was observed: a PDF opens in the tab with no download; Back and Forward cross it; it is recorded in history once; an attachment PDF downloads into the fixture's folder and the page stays; a private tab views a PDF and records nothing; the menu on an empty page holds exactly Back, Forward, Reload and no stock menu is open; Back and Forward from the menu navigate; Reload reloads; Open link in new tab gives a second tab and the first does not move; Save image writes the image's bytes; Open image in new tab opens it; in an editable field Cut and Copy are disabled with nothing selected and Select all selects the field; right-clicking on selected text offers Copy; the menu is withdrawn by navigation and by hiding the tab; disabled rows use the dim colour; groups are separated without stray separators; the menu opens at the requested position and is kept inside the view.

Not proven by action: Cut, Copy, Paste, Copy link address and Copy image address. Triggering them would overwrite the clipboard of the person using the machine, and a headless run was not shown to be isolated from it. They are present, enabled correctly, and wired to the engine's own actions.

### Regression, final commit

| Suite | Stock | Codec |
| --- | --- | --- |
| `feria_account_store_test` | 215 checks, exit 0 | 215 checks, exit 0 |
| `feria_runtime_test`, `feria_operations_test`, `feria_foundation_policy_test`, `feria_session_ownership_test`, `feria_hosting_state_test`, `feria_hosting_geometry_test` | 17, 15, 26, 8, 15, 6 passed, 0 failed | same |
| `feria_interaction_probe` | 10 / 0 / 0 | 10 / 0 / 0 |
| `feria_history_probe` | 9 / 0 / 0 | 9 / 0 / 0 in six runs; crashed in one, see below |
| `feria_runtime_probe` | 15 / 0 / 0 | 15 / 0 / 0 |
| `feria_foundation_probe` | 18 / 0 / 1 | 18 / 0 / 1 |
| `feria_session_probe` | 12 / 0 / 0 | 12 / 0 / 0 |
| `feria_lifecycle_probe` | 21 / 0 / 0 | 21 / 0 / 0 |
| `feria_renderer_probe` | 4 / 0 / 0 | 4 / 0 / 0 |
| `feria_restore_probe` | 8 / 0 / 0 | 8 / 0 / 0 |
| `feria_hosting_probe` | 40 / 0 / 1 | 40 / 0 / 1 |
| `feria_download_metadata_probe` | 5 / 0 / 0 | 5 / 0 / 0 |
| `feria_browser_menu_probe` | 11 / 1, baseline case | 11 / 1, baseline case |
| `feria_browser_requests_probe` | 26 / 1, baseline case | 26 / 1, baseline case |
| `feria_page_tools_probe` | 10 / 2, baseline cases | 10 / 2, baseline cases |
| Journeys: shutdown x5, shutdown control, retention | all as expected | not rerun for this set |
| Set 3 interaction proof (full application) | 75 steps, 0 failed, exit code 0 | not run |
| Set 2 restart proof (full application, two launches) | pass, both exits 0 | not run |

### Found during the final regression, not fixed here

**Signing out of a service can crash the engine.** In one of seven runs on the codec-enabled runtime, `feria_history_probe` died with an access violation in `test_signOutClearsOneSiteAndNothingElse`: `WebContentsAdapter` destructor, `didRunJavaScript`, `QQuickWebEngineViewPrivate`, `QJSEngine`. That is the engine boundary recorded in set 1: destroying a view that still owes script callbacks. `qml/feria/FeriaSessionCleaner.qml` deactivates its cleaner view as soon as the storage cleanup reports, while that view can still owe a polling script; every run of the case, passing or not, logs "attempted to evaluate a function in an invalid context". The cleaner predates this set and was not changed by it. It is the production sign-out path. Set 1's rule applies: a view is released only when drained, or discarded first. Queued as the next task.

### Not verified

- Three consecutive clean runs of every shortcut scenario. The navigation scenario passed fully in one of three reviewer runs; the other two stopped on driving (a typed address not taken; a control letter not reaching the page after Forward), not on a shortcut. The runner proves each shortcut; it is not yet a stable regression suite.
- A clean exit after Ctrl+P: the print file chooser stayed open and swallowed Quit, so that run was ended by the tool.
- The PDF viewer's own toolbar and how a PDF page looks: everything is headless.
- The context menu and suggestion list on screen. Both were looked at as headless renders in the app's style (`C:\b\ff-results\set4\review-0005\menu-reviewed.png`, `C:\b\ff-results\set4\task-0009\suggestions.png`), using stand-in pages and data.
- The full application on the codec-enabled runtime, WebView2, Linux, a second-substrate review of the reviewer's own two small fixes.

## Left out, on purpose

1. **Spell checking.** The engine's setting is off and the Qt kit ships no dictionaries (`qtwebengine_dictionaries` is absent). Turning it on means choosing languages and packaging dictionary files: a release decision, not a one-line change.
2. **DevTools for users.** A second view with `inspectedView` attaches and loads the inspector in this build, so a developer-only entry is cheap later. It is not part of browsing.
3. **New shortcuts.** Candidates noted by workers, none needed for a working browser: F6 to move between page and address bar, Ctrl+1 to Ctrl+9 for tabs, F11, Ctrl+Shift+R.
4. **Stock entries dropped from the menu**: Save page, View page source, Open in new window, Inspect, Undo, Redo, Paste and match style, spelling suggestions.

## Known limits

1. Print is still save-as-PDF only (set 3).
2. Typing a word with no dot and no match, then Enter, searches with the chosen search engine, as before; suggestions do not change that.
3. Suggestions are only saved pages. There are no search-engine suggestions and nothing is fetched while typing.
4. A PDF's viewer is the engine's; its toolbar is not styled like the app.

## Disk space

On 2026-10-11 about 02:55 the machine's C: drive reached 0 bytes free during this set's full-application runs. Every headless launch of the app under Lanista leaves a throwaway profile of about 50 MB under `AppData\Roaming\Brotherhood` and `AppData\Local\Brotherhood` (`Colosseum-dltest-<tag>`), and nothing removes them; there were 443. This set's finished profiles were moved to `D:\ff-moved-test-profiles`. Anyone running the full-application runners should check free space first and clear the profiles their own tags created.

## Reproduce

```powershell
$qt = 'C:\Qt\6.11.1\msvc2022_64'
cmake -S tests/feria_foundation_build -B <build> -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$qt"
cmake --build <build> --config Release
$env:PATH = "$qt\bin;$env:PATH"; $env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_FORCE_STDERR_LOGGING = '1'
<build>\Release\feria_foundation_runtime.exe -input tests\feria_daily_probe
# full application, headless, disposable profiles; the app loads QML from the working directory
native\build-target.bat colosseum
python tests\feria_daily_probe\run_app_suggestions.py --exe native/build-msvc/colosseum.exe --lanista native/build-msvc/lanista.exe --qt-bin "$qt\bin" --scenario all
python tests\feria_daily_probe\run_app_shortcuts.py --exe native/build-msvc/colosseum.exe --lanista native/build-msvc/lanista.exe --qt-bin "$qt\bin" --out <folder> --scenario tabs
```

Run logs for this record are in `C:\b\ff-results\set4` on Laptop 1.
