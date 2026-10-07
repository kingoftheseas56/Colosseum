# Feria foundation categories 1–5

Date: 2026-10-07. Scope: incremental audit plus working branch prototypes; stop for receiving review before categories 6–10.

## Verdict

A reviewable native/QML foundation is implemented and exercised with real QtWebEngine on hosted Linux. The five-category suite passes, as do the native owner suites. This is **not** an all-green repository claim: the inherited retained-session download test fails on both candidate and unchanged baseline. No assertion is waived. Full application, Lanista, Windows and live-provider qualification remain unexecuted.

## Source and delivery

- Repository: `kingoftheseas56/Colosseum`
- Working branch: `dots/feria-foundation-1-5`
- Starting master: `2916fe85812643c2e0ae45b0aec132cb6eb4e507`
- Code candidate: `470e50067a324dc2d90d21cd74202f8e18a5aa19`
- Reused first audit: `01cc5a48dfd9b69176380d8749d576ab1d35a661`
- Alternate CI branch: `deliveringsake-rgb/Colosseum`, `dots/feria-foundation-ci`
- Final component CI: [37663061781](https://github.com/deliveringsake-rgb/Colosseum/actions/runs/37663061781), job112935201463, workflow commit3fc5a61704591140349898d1d5069f8a919e2260. Overall result FAIL due to the baseline download assertion, while scoped foundation checks PASS. The delivery record identifies the final report tip and preserved evidence. Later report-only commits do not silently change the tested code

The canonical branch contains source. The alternate account carries a bounded 32-file CI transport under foundation-candidate because cross-repository checkout returned repository-not-found. SOURCE.json records canonical commit and Git blob IDs; final CI verifies all 32 byte-for-byte. The older fork's main application tree is not the prototype's source basis. No source tree is duplicated in Dots-Colosseum.

## Final evidence artifact

[Artifact11501228147](https://github.com/deliveringsake-rgb/Colosseum/actions/runs/37663061781/artifacts/11501228147), filename feria-foundation-evidence-3fc5a61704591140349898d1d5069f8a919e2260.zip,113850 bytes. SHA256: `7c455f5681d32404c31b539878b970a3a5f571309140a9f0497efc2fcd3e77aa`. Raw command output, exact source-blob verification, native/engine tests, negative control, baseline comparison and actual screenshots are included. Artifact retention is finite; the Dots-Colosseum entry preserves the decisive evidence.

## What we already knew

The earlier report established canonical SessionStore IDs, retained browser objects, account/profile boundaries, public native Qt navigation/history, synchronous original-request lifetime, and defects in donor index/URL-only ownership. Those sources and findings are reused. The four central source files are unchanged between the earlier inspected snapshot and this baseline. The major integration drift is the app-only taskbar/app-local spaces contract.

Read DONOR-DELTA.md for the compact five-donor comparison and selections. NATIVE-API-DELTA.md contains exact primary-source pins/lines for the newly investigated Qt/Chromium and focused Falkon/Brave/CEF questions.

## What was newly established

1. Qt Quick loadingChanged is queued. The earlier blanket callback-order explanation does not establish a QML native-action defect
2. Chromium toolbar Back can skip script-created entries that explicit history offsets can traverse. The actual engine fixture reports USER_NAV true/true/1 and SCRIPT_NAV false/false/1 for native getter/controller/count. The first test incorrectly expected a script redirect to qualify as user Back history; it was replaced by distinct gesture and script-policy cases while preserving the decisive Back assertion
3. History models are QAbstractItemModel objects, not JS arrays. Use model roles and view offsets; guard chooser revisions against intervening history changes
4. Qt QUrlQuery retains literal +. Actual QtCore and native tests demonstrate the need for whole-value percent encoding for form-style search endpoints
5. Original renderer requests and URL-only context-menu requests are distinct. Preserve the object Qt supplies; do not invent POST/opener data or a deferred/success API
6. Real engine tests preserve POST body and opener, named-target reuse, noopener, background selection, normal/private profile ownership and existing site-window host routing

## Implementation

- `native/SessionStore.h`: app/space/MRU projections and app toggle over existing UUID records
- `native/feria/FeriaBrowserPolicy.cpp`: bounded remote-only typed input, credential/control/local-path guards, host:port parsing and literal-query encoding
- `qml/feria/FeriaNavigation.qml`: exact-view native actions, history model/counts and stale-offset guard
- `qml/feria/FeriaQtWebEngine.qml`: removes JavaScript history reconstruction; compatibility navigation facade; explicit tab versus existing window/dialog route
- `qml/feria/FeriaSessionDeck.qml`: exact-ID commands, provider-start newSpace, original-profile/root/source adoption fences and no wrong-view fallback
- `tests/feria_foundation_policy_test.cpp`: native policy/projection cases
- `tests/feria_foundation_runtime.cpp`: disposable real native owners, loopback HTTP/POST/slow-load fixtures and optional real-window capture
- `tests/feria_foundation_probe/tst_Navigation.qml`: real WebEngine cases, including adversarial/stale/private/request behavior
- `tests/feria_foundation_probe/Preview.qml`: runnable plain reference host with app-only entries and bounded sidebar; explicitly not final product styling
- `tests/feria_foundation_build/`: standalone CMake build and executable negative control
- This report, DONOR-DELTA.md, NATIVE-API-DELTA.md and FRONTEND-INTERFACE.md

No final `.github/` delta remains on the canonical branch. Focused workflow configuration lives on the alternate CI branch. No production Main.qml/Taskbar visual rewrite, new canonical store, engine patch or WebView2 removal.

## Verification and exact commands

Hosted environment: Ubuntu24.04.5 x86_64, glibc2.39, CMake3.31.6, Qt6.11.1, WebEngine6.11.1. Runtime functions report Chromium140.0.7339.225 and security-patch148.0.7778.96. Xvfb with actual Qt Quick/WebEngine rendering/input; not a mock or Windows qualification. Vulkan initialization falls back; absent ALSA hardware and profile-deprecation warnings are recorded. No sandbox-disable flags or settings changes were added.

```sh
cmake -S tests/feria_foundation_build -B foundation-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build foundation-build --parallel 2
ctest --test-dir foundation-build --output-on-failure
python3 tests/feria_foundation_build/negative_control.py . foundation-build
xvfb-run -a -s '-screen 0 1280x800x24' foundation-build/feria_foundation_runtime -input tests/feria_foundation_probe
xvfb-run -a -s '-screen 0 1280x800x24' foundation-build/feria_foundation_runtime -input tests/feria_session_probe
FERIA_PREVIEW_CAPTURE=/absolute/path/preview.png xvfb-run -a -s '-screen 0 1280x800x24' foundation-build/feria_foundation_runtime --preview tests/feria_foundation_probe/Preview.qml
```

| Check | Result | Meaning |
|---|---|---|
| Configure/build | PASS, exit0 | Real production owner classes and focused engine runner compiled/linked |
| Native CTest | PASS, 2/2 targets, exit0 | Foundation policy/projection plus existing ownership tests |
| Search negative control | PASS | Deliberately restore raw-plus bug: exact assertion red/exit1; restore original bytes: green/exit0 |
| New real-engine suite | PASS, 15/15, exit0 | 13 substantive cases plus init/cleanup; real QtWebEngine/native owners |
| Existing retained suite | FAIL, 10 pass/1 fail, exit1 | Download must-still-be-downloading assertion, before tab close is exercised |
| Unchanged baseline retained suite | FAIL, same10/1, exit1 | Baseline core files and original runtime fixture reproduce same download assertion |
| Functional host screenshot | PASS, exit0 | Actual QML/native WebEngine window; screenshot inspected. First capture exposed a zero-width viewport; fixed with explicit bounded sidebar anchors and a viewport-size capture guard |
| Own sandbox QML parser | PASS, exit0 | qmlformat parses five authored/changed QML files; not type/runtime proof |
| Own sandbox QtCore encoding probe | PASS, exit0 | Actual6.11.1 URL query behavior; not compiled production-policy proof |
| Own sandbox WebEngine | BLOCKED | Installed PySide Essentials has no QtWebEngine module or C++ SDK |
| Full Colosseum/Lanista, Windows, provider sign-in, media/DRM | NOT RUN | No matching full-app/runtime qualification in this batch |

The download failure was not concealed by changing assertions. An attempted early-body-byte fixture adjustment did not resolve it and was reverted. Root cause remains unestablished; baseline equivalence is evidence that this batch did not introduce the observed assertion failure, not proof all download behavior is correct.

The fork-wide QML Quality job also fails on the older fork root's existing Feria files; it does not lint the staged foundation-candidate directory as the canonical application. Its failure remains recorded; no full repository lint pass is claimed. Focused build/runtime and source parsing are separate evidence.

## Unverified integration and open choices

- Production app-only taskbar/sidebar wiring and final provider skins require receiving integration; the prototype exposes the concrete interface
- Header-X semantics, space-switch media behavior, request auto-reveal and final site-window/platform-dialog presentation remain OPEN
- App minimize/world-switch media pause policy and legacy automatic-restore preference removal are later integration work; neither is claimed complete by category4 projection
- Cross-window live transfer, close confirmation/reopen, general recovery and categories6–10 were not implemented
- No full-browser reliability/parity claim; fixtures cover specified local cases, not all live providers, redirects, authentication or every Chromium process failure
- The screenshot is a functional host/rendering receipt, not approval of production aesthetics

## Receiving instructions

Fetch the exact branch and inspect baseline-to-final diff. Build the standalone targets with Qt6.11.1, run both fixture suites and the negative control, inspect the raw CI output/captures, and replay decisive Windows cases. Keep the known baseline download failure visible. Review app/spaces frontend wiring against FRONTEND-INTERFACE.md before adoption. Stop here before categories6–10.

All authored implementation ran through Dots' own sandbox and GitHub connector/Actions. No user computer, BDC, Work, Codex task or delegated coding path was used. Two read-only research helpers checked source deltas. No merge, release, production-main write, private account sign-in or live provider test occurred.
