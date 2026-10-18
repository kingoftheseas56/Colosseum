# Feria browser, set 3: website interaction

Status: implemented and verified on Windows against the stock Qt 6.11.1 kit and the codec-enabled runtime, and proved through the full application for the prompts a page can raise. Branch `codex/feria-website-interaction`. Pushed, not merged, not released.

Set 1 (`docs/feria-browser-foundation-set-1.md`) stays the owner map and lifecycle contract. Set 2 (`docs/feria-browser-history-bookmarks-set-2.md`) is the base of this branch.

## Base and merge endpoint

Built on `codex/feria-history-bookmarks` at `2728c373`.

Endpoint (Rule 28): this branch merges into `master` together with, or after, the set 2 branch.

## Set 3 against the plan

The plan's scope is permissions, authentication, uploads, dialogs, downloads and printing, with the proof "real website workflows complete correctly". The code for each existed before this set. What did not exist was any proof that a page's request completes in the real engine: every test of this area ran the logic against stand-in objects.

| Item | State | Proof |
| --- | --- | --- |
| Page dialogs: alert, confirm, prompt, leave-page | now owned by the browser, not by Qt's stock dialog | real-engine probe, full application |
| HTTP and proxy sign-in | now owned by the browser | real-engine probe and full application (HTTP); logic test only (proxy) |
| File uploads | now owned by the browser, answered by the system file chooser | real-engine probe |
| Permissions | existed, unchanged | real-engine probe, full application |
| Downloads | existed, unchanged | real-engine probe |
| Printing to PDF | existed, unchanged | real-engine probe |
| Passkeys, client certificates, screen sharing | existed, unchanged | logic tests with stand-ins only; see Engine-dependent items |

## What was wrong

**A tab showing a page alert could not be closed.** Page dialogs were left to Qt's stock delegate. A page inside `alert()` is blocked until the dialog is answered, and nothing answered it when the tab was closed, the account changed or a sign-out began. In the real engine the browser then never finished closing: the deck stayed in its closing state until the 15 second deadline ("A browser is still closing"), and everything waiting on it waited too. Found by the first real-engine run of this set.

**The stock dialogs did not belong to the app.** They are Qt's default-styled windows titled "Javascript Alert - http://...", outside the request queue, so they ignored the rules every other prompt follows: one at a time, refused for hidden pages, withdrawn on navigation, bound to the document that asked.

## What changed

All of it is in `qml/feria/FeriaBrowserRequests.qml`, the existing owner of website requests. No new owner.

- **Page dialogs** (`javaScriptDialogRequested`) and **sign-in** (`authenticationDialogRequested`) enter the same queue as permission, certificate, screen-share and passkey prompts, and are shown in the same prompt. Accepting the request in the signal handler keeps Qt's delegate from opening. Navigation, closing the tab, hiding the page and retiring the view refuse whatever is pending, which releases the blocked page.
- The retained close flow's own leave-page consent (`managedBeforeUnload`) is untouched and still has its one owner in `FeriaCloseController`.
- **Prompt content.** An alert shows the page's message and OK only. Confirm adds Cancel. Prompt adds a field that starts from the page's default text. Leave-page offers Stay and Leave. Sign-in shows user name and password fields; they never carry text from one prompt to the next, and the password field is masked. The title names the asking origin without credentials. Page text is plain text.
- **File choosers** (`fileDialogRequested`) are answered by the system file or folder chooser, one at a time, with a filter built from the extensions the page listed. Leaving or closing the page withdraws the chooser, and a choice made after that goes nowhere.
- **Prompt fields are legible.** The stock `TextField` rendered dark text on the dark panel. The prompt's fields, including the existing passkey PIN fields, now use the app's field look.
- Buttons and fields have object names (`feriaRequestAccept`, `feriaRequestCancel`, `feriaRequestMessage`, `feriaDialogInput`, `feriaSignInUser`, `feriaSignInPassword`) so the full application can be driven.

Policies kept as they were: a hidden page's dialog or sign-in is refused; one prompt shows at a time; an answer for a page that has since been replaced counts as a refusal; an untrusted certificate is rejected with a notice; a download is accepted synchronously into the profile's download folder.

## Verification

Windows 11 Pro 10.0.26200, MSVC 2022, 2026-10-10. Everything ran headless (`QT_QPA_PLATFORM=offscreen`). Fixture runs used a loopback server and throwaway profiles. The full-application run used a disposable Lanista profile, opened the public MANGA Plus site anonymously once, then visited pages on a loopback server started by the runner. The sign-in page accepts only the fixture's own throwaway credentials.

### New

| Suite | Stock | Codec |
| --- | --- | --- |
| `tests/feria_interaction_probe` (new, real engine, production deck, wrapper and request owner) | 10 / 0 / 0 | 10 / 0 / 0 |
| `feria_browser_requests_probe` (logic with stand-ins; one case rewritten for the new ownership, one added for file choosers) | 26 / 1 | 26 / 1 |
| `run_app_interaction.py` (new, full application) | 3 of 3 runs pass, 75 steps each, exit code 0 | not run |

The one failing case in `feria_browser_requests_probe` is `test_download_old_account_authorization_cannot_record_in_new_account`, which fails the same way on the foundation branch and on set 2.

Real-engine cases in `tst_Interaction.qml`. Each ends with what the page itself observed, written into its title or read from its script state:

- **Page dialogs.** `alert` shows the message under "<origin> says" with OK only, no stock dialog opens as well, the page waits, and continues after OK. `confirm` returns true for OK and false for Cancel. `prompt` starts from the page's default, returns what was typed, and returns null for Cancel.
- **A tab blocked in an alert.** Navigating away completes without waiting for the alert. Closing the tab closes it and its engine view goes. Before the change the first real-engine run of this file left the deck closing for the full 15 seconds.
- **HTTP sign-in.** Cancel shows the site's own refusal page. A wrong password is asked again with empty fields. The right one opens the protected page.
- **File upload.** Cancelling the chooser leaves nothing chosen and the page can ask again. Choosing a file gives the page its name, and after the form is sent the server's echo holds the file name and the file's bytes.
- **Leaving withdraws a file chooser**, and a late choice is refused.
- **Permissions.** A notification request shows "Allow <origin> to send notifications?"; Allow gives the page `granted`; the decision survives a reload without being asked again; "Reset this site's permissions" returns it to `default`; asked again and refused, the page gets `denied`.
- **Download.** Clicking a download link saves the file with the server's bytes, lists it once in download history, and leaves the page where it was.
- **Print.** A page's own `window.print()` reaches the app; saving produces a PDF file that starts with `%PDF` and the "PDF saved." notice.

Full application (`tests/feria_interaction_probe/run_app_interaction.py`), through the real shell and address bar: alert and OK; confirm and Cancel; prompt with a typed answer; HTTP sign-in with typed credentials; a permission request allowed, then found remembered on a second page of the same site; quit through the app's own Quit command with exit code 0.

### Regression

Same suites as sets 1 and 2, on the final code.

| Suite | Stock | Codec |
| --- | --- | --- |
| `feria_account_store_test` | 215 checks, exit 0 | 215 checks, exit 0 |
| `feria_runtime_test`, `feria_operations_test`, `feria_foundation_policy_test`, `feria_session_ownership_test`, `feria_hosting_state_test`, `feria_hosting_geometry_test` | 17, 15, 26, 8, 15, 6 passed, 0 failed | same |
| `feria_history_probe` | 9 / 0 / 0 | 9 / 0 / 0 |
| `feria_runtime_probe` | 15 / 0 / 0 | 15 / 0 / 0 |
| `feria_foundation_probe` | 18 / 0 / 1 | 18 / 0 / 1 |
| `feria_session_probe` | 12 / 0 / 0 | 12 / 0 / 0 |
| `feria_lifecycle_probe` | 21 / 0 / 0 | 21 / 0 / 0 |
| `feria_renderer_probe` | 4 / 0 / 0 | 4 / 0 / 0 |
| `feria_restore_probe` | 8 / 0 / 0 | 8 / 0 / 0 |
| `feria_hosting_probe` | 40 / 0 / 1 | 40 / 0 / 1 |
| `feria_download_metadata_probe` | 5 / 0 / 0 | 5 / 0 / 0 |
| `feria_browser_menu_probe` | 11 / 1, baseline case | 11 / 1, baseline case |
| `feria_page_tools_probe` | 10 / 2, baseline cases | 10 / 2, baseline cases |
| Journeys: shutdown x5, shutdown control, retention | all as expected | not rerun for this set |
| Set 2 restart proof (full application, two launches) | pass | not run |

### Not verified

- **Uploads, downloads and printing in the full application.** Lanista cannot click inside a page or run script in it, a download in the real app would be written to the user's own Downloads folder, and the save and file choosers are system dialogs. All three are proved in the real engine with the production components, not through the shell.
- **The system file chooser itself.** The probe stands in for the person choosing: it hands the chosen path to the same function the chooser calls.
- **Closing the app or changing account while an alert shows, in the full application.** The modal prompt takes the keyboard, so the headless run cannot send Quit past it. The real-engine case covers the same release path (the view retiring).
- Windows on screen, a second-substrate review (the GLM and DeepSeek delegates had no API key), the codec-enabled runtime in the full application, WebView2, Linux.

## Engine-dependent items

Things this set could not prove or change because they depend on the engine, the operating system or hardware. None is hidden behind a stand-in success.

1. **Passkeys.** The prompt and its state handling exist and are logic-tested. A real ceremony needs a security key or platform authenticator and a site that asks for one. Not run.
2. **Client certificates.** The chooser is logic-tested. A real handshake needs a certificate installed in the Windows store and a server that requests one. Not run.
3. **Screen and window sharing.** The source chooser is logic-tested. Headless has no screens to offer. Not run.
4. **Proxy sign-in.** Shares the HTTP sign-in prompt and is logic-tested. No proxy was available. Not run.
5. **Notifications.** The permission is granted and remembered. Whether a notification is then shown by Windows is native browser services, set 5.
6. **Printing is save-as-PDF only.** QtWebEngine Quick offers no system print dialog from QML; sending a page to a printer needs native code and is not provided.
7. **Colour inputs** still use Qt's stock picker. It is an in-page picker, not a request a page blocks on, and was left alone.
8. **Permissions are remembered by the engine's profile**, not by the account store. A private tab's profile is off the record, so its grants go with it; that was not separately tested here.
9. **A download is accepted without asking where to save.** Qt deletes a download request that is not accepted inside the signal, so a save-as question cannot be asked first with this API. It lands in the profile's download folder with Qt's collision naming.
10. **Certificate errors are always rejected.** There is no "proceed anyway".

## Reproduce

```powershell
$qt = 'C:\Qt\6.11.1\msvc2022_64'
cmake -S tests/feria_foundation_build -B <build> -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$qt"
cmake --build <build> --config Release
$env:PATH = "$qt\bin;$env:PATH"; $env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_FORCE_STDERR_LOGGING = '1'
<build>\Release\feria_foundation_runtime.exe -input tests\feria_interaction_probe
<build>\Release\feria_foundation_runtime.exe -input tests\feria_browser_requests_probe
# full application, headless, disposable profile
native\build-target.bat colosseum
python tests\feria_interaction_probe\run_app_interaction.py --exe native/build-msvc/colosseum.exe --lanista native/build-msvc/lanista.exe --qt-bin "$qt\bin"
```

Run logs for this record are in `C:\b\ff-results\set2` on Laptop 1 (`interaction-*`, `set3-*`).
