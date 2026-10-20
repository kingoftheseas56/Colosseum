# Offline route verification

This smoke test opens the downloaded mock's `index.html` through `file://`, then navigates every entry in the generated `shared/routes.js` through the real `MockHost.go()` shell. No web server is required. Keep the complete mock folder together.

## Install and run

Use a supported Node.js release (Node 22 or newer recommended). From the mock folder:

```sh
npm install --no-save --package-lock=false playwright
npx playwright install chromium firefox
node qa/verify.cjs chromium
node qa/verify.cjs firefox
```

On Linux, Playwright may also need its documented operating-system dependencies: `npx playwright install --with-deps chromium firefox`. Use official Playwright browser builds matching the installed package. These commands also work in PowerShell; run them separately and check each exit status.

Both engines are headless, with a 1280×720 viewport. Running the script from another working directory is supported. No cloud-specific executable or package path is embedded in the script.

## Results and review

Each invocation creates a sibling folder named `<mock-folder>-qa-results/<engine>/<timestamp>/` outside the mock's source tree. It contains `results.json` and a `screenshots/` folder, with one screenshot per route, including failed routes when the page is still usable. The console prints the report path. Exit status is `0` only when every registered route passes with no recorded issues; setup, navigation, resource, or screenshot failures return `1`.

The sweep checks:

- Host and iframe load/readiness, including the Firefox null-document transition
- Fonts and image completion before screenshots; broken images and failed fonts
- JavaScript exceptions, error-level console output, failed local requests, resource error events, unexpected dialogs, and page crashes
- HTTP(S) and WebSocket requests, which are blocked and reported as failures; service workers are disabled
- Correct requested iframe file, with the registry's default route parameters

Always inspect the screenshots alongside the JSON. A smoke-test pass does not establish visual fidelity or interactive-flow coverage. The sweep does not click each control, exercise every manifest state, compare screenshot baselines, or prove native/backend/provider parity. Account sync, playback, downloads, purchases, and integrations remain staged mock behavior.

## Optional environment overrides

Unset these for a normal Playwright-installed browser:

| Variable | Meaning |
| --- | --- |
| `QA_BROWSER_EXECUTABLE` | Explicit Playwright-compatible browser executable for the selected engine. |
| `QA_FIREFOX_LAUNCH_HELPER` | Optional CommonJS module exporting `async launchFirefox()`, returning a Playwright Browser. Firefox only; mutually exclusive with the executable override. Use only a trusted local helper. |
| `QA_OUTPUT_DIR` | Exact output folder, resolved from the current working directory. Use a fresh folder per run to avoid old screenshots. |
| `QA_TIMEOUT_MS` | Positive per-operation timeout; defaults to `15000`. |

For example, in a POSIX shell:

```sh
QA_BROWSER_EXECUTABLE=/absolute/path/to/chrome-headless-shell node qa/verify.cjs chromium
QA_FIREFOX_LAUNCH_HELPER=/absolute/path/to/launch.cjs node qa/verify.cjs firefox
```

In PowerShell, set an override with `$env:QA_BROWSER_EXECUTABLE = 'C:\path\to\chrome.exe'`, run the script, then clear it with `Remove-Item Env:QA_BROWSER_EXECUTABLE`. A Firefox helper must preserve Playwright's API and is responsible for its own launch environment.

## Verified scope

Linux verification and screenshot review are recorded with the delivered report. Windows has not been run. Portable path handling uses Node's `pathToFileURL`; this is not a claim of Windows validation. No claim of backend parity is made.

### Preserved source-font diagnostic

Firefox reports `STAT: Invalid nameID: 256` and `Table discarded` for the source's `Fraunces-Regular.ttf`. The optional STAT table refers to name IDs absent from the font's name table. Firefox discards that metadata table; it still loads and renders the face. Verification confirmed `document.fonts.check('48px Fraunces')`, a loaded face, distinct rendered metrics versus fallback serif, and visible screenshot headings. This is not a failed font face.

Both packaged copies retain source blob `eefe09f1e766c005b3ba6f2d367ec6d0b1afb189`, SHA-256 `4063dec09ee3d7170bd8c6bf1f66a6bec01a3630390a922cdb9bcfbcb7e89bbc`; no font bytes were modified. Only those two exact Firefox messages from these local files, after verifying that SHA-256, are classified as `known-font-metadata` warnings. Their raw messages, source URLs, hashes, route attribution, and counts remain in `results.json`. Any other console error or failed font still fails the run. A successful Firefox result can therefore include these explicitly reported warnings.

API references: [Playwright browser installation](https://playwright.dev/docs/browsers), [request routing](https://playwright.dev/docs/api/class-browsercontext#browser-context-route), and [WebSocket routing](https://playwright.dev/docs/api/class-browsercontext#browser-context-route-web-socket).
