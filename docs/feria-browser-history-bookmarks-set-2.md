# Feria browser, set 2: persistent browsing

Status: implemented and verified on Windows against the stock Qt 6.11.1 kit and the codec-enabled runtime, and proved through the full application across a real restart. Branch `codex/feria-history-bookmarks`. Pushed, not merged, not released.

This is the record for set 2. Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map and lifecycle contract; nothing in it changed.

## Set 2 against the plan

The plan's scope is session restoration, history, bookmarks, preferences and site-data controls, with the proof "state survives restart; private activity stays excluded".

| Item | State | Proof |
| --- | --- | --- |
| History | search and single-entry delete added; one row per address | store test, real-engine probe, full-app restart |
| Bookmarks | search, edit and delete added | store test, real-engine probe, full-app restart |
| Session restoration | existed; did not work from the real shell and is repaired here | store test (fails without the fix), restore probe, full-app restart |
| Preferences | existed, unchanged | store test, full-app restart |
| Site-data controls | existed, unchanged: sign out of a provider, reset a site's permissions | real-engine probe for sign-out; permissions by the set 1 probes |
| Private activity excluded | unchanged rule | store test, real-engine probe, full-app restart |

Two faults outside the browser's own code were found by doing this through the real application and are fixed on this branch: the application crashed at every exit, and tabs opened from the shell were never saved for restore. Both are described below.

## What changed

The browser's history and bookmarks become manageable: search by title or address, edit a bookmark, delete one bookmark or one history entry. The rules live in `FeriaAccountStore`, which already owned both lists. The existing browser menu presents them. No new store, registry, database or command path was added.

**Tabs opened from the shell are now saved.** `FeriaWorld.openHost` builds each tab's descriptor with `logo: Qt.resolvedUrl(...)`, which is a url value. The store's descriptor check required text for that field and dropped the whole row, so `browserTabs` was always empty for tabs opened from a provider tile and nothing was ever offered after a restart. The fixture never saw it because its descriptors carry no logo. The check now reads a url value as its text. The private-tab exclusion and every other check are unchanged.

**The application no longer crashes at exit.** On this machine the `C:/tools` build fallback compiled the app against Boost 1.84 headers while linking a libtorrent static library built against Boost 1.90. Boost.Asio is header-only: both sides carry its inline code and the linker keeps one copy. `execution_context` gained a member ahead of `service_registry_` after 1.84, so the `io_context` libtorrent created was destroyed by the app's 1.84 destructor. Stack at the fault: `main`, `~TorrentEngine`, `lt::session::~session`, the `io_context` release, `boost::asio::execution_context::~execution_context`, reading address `0x30`. `native/CMakeLists.txt` now defaults to the 1.90 headers, moves build directories still caching the old default, and refuses to configure when the headers are not the release the library needs. Builds that take libtorrent and Boost from one package set (the CI and release workflows use vcpkg) were not affected. The same commit is also pushed alone, on top of `master`, as `fix/local-boost-libtorrent-mismatch`.

## Base and merge endpoint

Built on `codex/feria-browser-foundation` at `189909cc`, which itself sits on GLM's unmerged `glm/feria-shared-layer-slice-1`.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the foundation branch. If the foundation is rebased, these commits are rebased with it; they touch no file the foundation's contract depends on.

## Rules

### History

- **One row per address, newest visit first, at most 1000.** A visit to an address that already has a row refreshes that row (time, title, owning tab) and moves it to the top. It keeps its id.
- That one rule covers the cases that used to differ: a **reload** refreshes its row; **Back** and **Forward** lift the row of the page arrived at; a **second tab** at the same address adds nothing. Durable history is separate from each tab's own Back/Forward stack, which is never read or rewritten.
- A **redirect** records only the address that finally loaded. The recording point is unchanged: the wrapper's `mainDocumentLoaded`, which fires once per successful main-document load with the final address.
- A stored file that repeats an address (written before this rule) is read as one row per address, newest kept.
- **Late titles.** A title that settles after the load amends the row that document recorded. It adds no row, does not move the row and does not change its time. It is refused when the tab has since started another document, when the visit token is not current, when another tab has visited the address since (that visit owns the row), when the text is empty, or when the row was removed or cleared.
- **Stale observations.** Recording still requires the tab's current visit token, which dies on account change and is renewed whenever the application navigates the tab. In addition each record carries the tab's document serial (the wrapper's `navigationGeneration`); a record older than the one that visit last recorded is refused.
- **Private tabs** record no visit and no title, as before. Saving a bookmark from a private tab is an explicit act and stays allowed, as before.
- Unchanged: authentication routes and addresses carrying credential-like parameters are not recorded; recording respects the pause switch.

### Bookmarks

- **Unique by address, at most 500.** Saving an address that is already bookmarked renames that bookmark and moves it to the top (existing behaviour).
- **Edit** changes title, address or both and keeps the bookmark's place and saved time. The new address goes through the same validation as a saved one (http or https, a host, credentials stripped), and may be typed without a scheme (`example.com/page` becomes `https://example.com/page`).
- **Duplicates are refused, never merged.** Editing a bookmark to an address another bookmark already has returns `duplicate` and changes nothing.
- Results are `{ok, reason, url, title}` with reasons `stale-account`, `not-found`, `invalid-url`, `duplicate`, `save-failed`.

### Search

Every word typed must appear in the title or the address, in any case. The address is matched both as stored and percent-decoded, so `café` and `caf%C3%A9` find the same row. An empty search returns the whole list. Order is the list's own order.

### Accounts

`FeriaAccountStore.browserGeneration` moves on every account change. Management calls name the generation their rows were read under and are refused afterwards: `editBrowserBookmark`, `removeBrowserBookmark`, `removeBrowserHistoryEntry` always; `setBrowserBookmark` and `clearBrowserHistory` when a generation is passed. Passing none keeps the older unguarded form for callers that hold no rows. The menu always passes it.

## API added to `FeriaAccountStore`

| Member | Purpose |
| --- | --- |
| `browserGeneration` (property) | account generation for the guard above |
| `searchBrowserBookmarks(query)`, `searchBrowserHistory(query)` | filtered rows |
| `isBrowserBookmarked(url)` | bookmarked state against the stored address form |
| `editBrowserBookmark(generation, url, newUrl, newTitle)` | edit with validation and duplicate refusal |
| `removeBrowserBookmark(generation, url)` | delete one bookmark; false when nothing was removed |
| `removeBrowserHistoryEntry(generation, url)` | delete one history entry; false when nothing was removed |
| `updateBrowserVisitTitle(sessionId, token, document, title)` | late title |
| `recordBrowserVisit(..., document = 0)` | now takes the document serial |
| `setBrowserBookmark(..., generation = 0)`, `clearBrowserHistory(generation = 0)` | optional guard |

## QML

- `FeriaSessionDeck.qml`: passes the document serial when recording, and forwards title changes to `updateBrowserVisitTitle`. Nothing else.
- `FeriaBrowserMenu.qml`: a search field in Bookmarks and in History; Edit and Remove per bookmark; Remove per history entry; an inline editor (name, address, Save, Cancel) that shows the store's refusal in words; separate empty and no-results messages. The menu refreshes from the store's `changed` signal, so every change shows at once. Choosing a row still emits `navigateRequested`, which the host turns into the `navigate` command through `FeriaBrowserRuntime`.

## Verification

Windows 11 Pro 10.0.26200, MSVC 2022, 2026-10-10. Everything ran headless (`QT_QPA_PLATFORM=offscreen`). Fixture runs used a loopback server and throwaway profiles. The full-application run used a disposable Lanista profile and loaded one public provider page anonymously; no personal login or profile was touched.

Runtimes are the two from set 1: stock (Qt 6.11.1, WebEngine 6.11.1, modules from `C:\Qt\6.11.1\msvc2022_64\bin`) and codec-enabled (WebEngine 6.11.2, modules from `D:\ff-stage-codec`), as printed by each run.

### New

| Suite | What it exercises | Stock | Codec |
| --- | --- | --- | --- |
| `feria_account_store_test` (native, extended) | recording rules, late titles, stale document and token, private exclusion, search, edit, duplicate and invalid refusals, delete, restart over the same file, account isolation in both directions, repeated-address files, failed writes, a shell-built tab saved and offered after restart | 215 checks, exit 0 | 215 checks, exit 0 |
| `tests/feria_history_probe` (new, real engine, production deck and menu, real store) | see below | 9 / 0 / 0 | 9 / 0 / 0 |
| `run_app_restart.py` (new, full application, two launches over one profile) | see Full application | 3 of 3 runs pass (59 + 42 steps each) | not run |

Real-engine cases in `tst_History.qml`:

- a redirect records only the final address; reload, typed navigation, Back and Forward leave one row per address in the right order; the tab's own Back stack is intact; a second tab at a visited address adds nothing;
- a title set by the page after the load reaches its visit; after the page navigates itself, a title carrying the old document is refused;
- a private tab navigating, retitling and reloading leaves no history, a forced record with its token is refused, and Bookmark current page from that tab still saves;
- account switch: the menu closes, the new account starts empty, every call still holding the first account's generation or token is refused, and returning to the first account reads its own file back unchanged;
- sign-out of one site, done the way the shell does it (close the browsers, run `FeriaSessionCleaner`, reopen): that site's cookie and local storage are gone, a second site's are intact, and history, bookmarks and the account are untouched;
- the menu against the real store: search by title and by address words, the no-results and empty messages, removing one entry, Clear history, opening a history row and a bookmark through the `navigate` command, bookmark edit with duplicate and invalid refusals, a valid edit, Cancel, bookmark search and removal.

The eight cases of the first pass ran 5 of 5 consecutive times on each runtime. Two earlier failures on the codec runtime were test waits that did not allow for the engine reporting a finished load one event-loop turn after it stops loading; the waits were corrected, the product code was not involved. The shell-tab check fails on the unfixed store with "A tab opened by the shell is saved for restore" and passes with the fix.

### Regression, same suites as set 1

| Suite | Stock | Codec | Set 1 record |
| --- | --- | --- | --- |
| `feria_runtime_test`, `feria_operations_test`, `feria_foundation_policy_test`, `feria_session_ownership_test`, `feria_hosting_state_test`, `feria_hosting_geometry_test` | 17, 15, 26, 8, 15, 6 passed, 0 failed | same | same |
| `feria_runtime_probe` | 15 / 0 / 0 | 15 / 0 / 0 | same |
| `feria_foundation_probe` | 18 / 0 / 1 | 18 / 0 / 1 | same |
| `feria_session_probe` | 12 / 0 / 0 | 12 / 0 / 0 | same |
| `feria_lifecycle_probe` | 21 / 0 / 0 | 21 / 0 / 0 | same |
| `feria_renderer_probe` | 4 / 0 / 0 | 4 / 0 / 0 | same |
| `feria_restore_probe` | 8 / 0 / 0 | 8 / 0 / 0 | same |
| `feria_hosting_probe` | 40 / 0 / 1 | 40 / 0 / 1 | same |
| `feria_download_metadata_probe` | 5 / 0 / 0 | 5 / 0 / 0 | same |
| Journeys: shutdown x5, shutdown control, retention | all as expected | all as expected | same |
| `feria_browser_menu_probe` | 11 / 1 | 11 / 1 | 11 / 1, same case (`test_sections_render_inside_dialog`, same assertion) |
| `feria_browser_requests_probe` | 25 / 1 | 25 / 1 | 25 / 1, same case |
| `feria_page_tools_probe` | 10 / 2 | 10 / 2 | 10 / 2, same cases |

The table is from the final code, after the restore fix. `feria_page_tools_probe` gave 7 / 5 once on stock in an earlier full run (three find-in-page cases in addition to the two baseline ones) and 10 / 2 in every run since. Find-in-page is not touched by this set.

### Full application

`colosseum` builds from this branch (Release, Ninja, exit 0). Everything below ran headless under Lanista with disposable profiles and loaded the public MANGA Plus site anonymously.

**Restart proof: verified.** `tests/feria_history_probe/run_app_restart.py` launches the real application twice over one profile.

- First launch (59 steps): open the provider tile, navigate the regular tab by the address bar, bookmark the page, turn "freeze inactive tabs" off, open a private tab with Ctrl+Shift+N, navigate it, confirm the menu calls it private, confirm History finds the regular page and not the private one, quit.
- Second launch (42 steps): History still finds the regular page and nothing of the private one; the bookmark is there; the setting is still off; exactly one previous tab is offered; Restore opens it at the regular page as a regular tab; nothing is left to restore; quit.
- Both launches left by themselves with exit code 0. The run passed 3 of 3 times on the final code.

Before the restore fix the same run passed the first launch and failed the second at "one previous tab is offered" (it said 0).

**Clean exit: verified headless.** Lanista closes the app through its native window, which a headless run does not have, so every scenario quits through the app's own Quit command (Ctrl+Q, `Qt.quit()`) and the session manifest is read for how the process ended.

| | Before the build fix | After |
| --- | --- | --- |
| Exit code | `0xC0000005` in 8 of 8 completed exits | 0 in every exit (19 of 19) |
| Time to leave after Quit, no browser page | about 1 to 1.5 s | 0.6 to 0.7 s |
| Time to leave, one browser page open | about 10 s, 27 s, 40 s; twice not gone after 8 s and 16 s | 1.1 to 2.4 s |

The slow exits with a page open were the same fault, not a second one: they disappeared with it.

**Infrastructure failures.** In 3 of about 35 launches the app did not answer a click within Lanista's 10 s limit (twice on the welcome screen's "continue locally", once on the provider tile) and the run stopped there. They are counted as failed runs of the tool, not results, and were not investigated.

### Not verified

- Windows on screen. Everything here is headless, including the exits. Windowed runs were not made because they take over the screen of whoever is using the machine.
- A second-substrate review. The GLM and DeepSeek delegates had no API key configured; the changes were reviewed by their author only.
- The exit fix on a `master` build. It was measured on this branch's build; the standalone branch was not built.
- The restart proof on the codec-enabled runtime. The application build uses the stock kit.
- Sign-out and permission reset as clicks in the real application. Sign-out is proved with the production cleaner and deck in the real engine; the account manager's buttons were not driven.
- WebView2. `feria_webview2_integration` was not rebuilt or run.
- Linux, live provider sign-ins, DRM.

## Known limits

1. **The ledger still mislabels Back and Forward.** Confirmed: a successful Back settles as `back:cancelled` followed by `navigate:succeeded`, because the Qt wrapper admits a `navigate` operation on every URL change (`FeriaQtWebEngine.qml`, `onUrlChanged`), which supersedes the traversal it belongs to. The WebView2 wrapper already guards this with a traversal flag. It does not block this set: history is keyed by address and never reads the operation kind. It is left for the slice that owns the ledger.
2. **Same-document navigations are not visits.** History is recorded on main-document loads. A site that changes its address with `pushState` or a fragment (most streaming and reader sites, after the first load) records only the page that was loaded. This is the existing recording point, not a change, and it limits how complete history is on those sites.
3. **A page left in the same event-loop turn its load finished is not recorded**, because the engine's report of the finished load then names an address the tab has already left.
4. **Each accepted late title is one write of the account file.** A page that animates its title writes on every change.
5. The edit form refuses `user@host` typed without a scheme rather than guessing.
6. **Sign-out always reports itself incomplete on QtWebEngine.** The cleaner counts a cleanup complete only if it could also enumerate service workers, which its blank page cannot do in this engine. The shell therefore shows "Site cleanup was incomplete" even when cookies and storage were removed, as the probe shows they are. The wording is honest about what could not be checked; it is left as it is.
7. **There is no control to clear one arbitrary site's data or all browsing data.** Site-data controls are per provider (sign out) and per site (permissions). Whether a general "clear browsing data" belongs in the browser is a product decision, not built here.

## Reproduce

```powershell
$qt = 'C:\Qt\6.11.1\msvc2022_64'
cmake -S tests/feria_foundation_build -B <build> -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$qt"
cmake --build <build> --config Release
$env:PATH = "$qt\bin;$env:PATH"; $env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_FORCE_STDERR_LOGGING = '1'
<build>\Release\feria_account_store_test.exe
<build>\Release\feria_foundation_runtime.exe -input tests\feria_history_probe
# full application, headless, disposable profile, two launches
native\build-target.bat colosseum
python tests\feria_history_probe\run_app_restart.py --exe native/build-msvc/colosseum.exe --lanista native/build-msvc/lanista.exe --qt-bin "$qt\bin"
```

Run logs for this record are in `C:\b\ff-results\set2` on Laptop 1.
