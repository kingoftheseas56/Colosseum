# Feria foundation categories 1–5: receiving repairs
## Result
R1–R5 repaired in canonical code commit **54249b66a28f23cbe7c56ee356cf3a7568756d82** on kingoftheseas56/Colosseum, branch **dots/feria-foundation-1-5**. Starting reviewed tip **e5c11ef6416a361bf2195c310810cfb53f6a129e**. Original product baseline remains **2916fe85812643c2e0ae45b0aec132cb6eb4e507**. No donor re-audit or categories 6–10 work.

Linux focused verification passes. Overall CI remains **FAIL** because the unchanged retained download-timing assertion fails on both candidate and baseline. Windows replay of these repairs is **NOT RUN** by Dots and remains required. No production adoption.

## What changed
- **R1:** Both destination-dispatch gates route InNewWindow to the canonical retained SessionStore/deck owner again. Same synchronous original request.openIn, exact source profile/storage-root fences, independent ID; no requestedUrl replay. No new independent desktop-window presentation is claimed. InNewDialog retains the old popup host, whose opener lifetime and nested background routing problems remain inherited hosting-batch work.
- **R2:** localhost followed by end, slash, query or fragment is classified as a hostname (case insensitive). Existing scheme, credentials, controls and local-file rejection remain. localhostish stays a search. Tests use synthetic private-token text and perform parsing only, never a remote query.
- **R3:** newSpace passes the validated normalized URL to SessionStore instead of discarding it. Runtime test creates an independent space from a scheme-less loopback startUrl and verifies the committed /home destination.
- **R4:** Fixture audio controls are explicitly separated from the tested link/button controls. POST checks require the intended URL, title and body before exact body/opener assertions. Background-link assertion also waits for its committed URL/title. Real mouse input and native OpenLinkInNewWindow WebAction remain. New two-row lifetime regression covers deck suppression and source retirement, preserved object identity and page JavaScript state.
- **R5:** Negative control reads Qt's explicit '-o <file>,txt' output, requires nonzero exit plus FAIL/searchEscaping/%2B, restores exact source bytes in finally, rebuilds and requires green. Supports Release/config subdirectory on multi-config generators. Missing log fails explicitly. Actual Windows execution remains receiving work.

## Reproduction and corrections during this repair
Tests-first commit **7bd4a12642ef48a6a4a01ff25640021a103a2108** retained unchanged production. First run timed out during apt update; no tests ran.
Next Linux run reproduced four localhost boundary failures and normalized-start failure. It also exposed two mistakes in the new test harness: native popup views expose runJavaScript rather than the wrapper's evaluateJavaScript; background documentReady alone can precede committed URL.
Test refinement **b68db903216cb03e5bcaecc975e6562bca605e62** corrects those helpers without product changes. Run **37676302151** then gave:
- native policy: 21 passed / 4 failed, all four localhost cases
- real engine: 15 passed / 3 failed: two independent destination-ownership survival cases and normalized newSpace
- original 13 engine cases remained green
R1's red test observes missing canonical ownership after lifecycle transitions; the reviewer separately reproduced actual window loss on Windows. We do not mislabel the initial helper exception as an R1 reproduction.
Only after this run were production fixes authored.

## Final executed checks
Hosted **Ubuntu 24.04.5 x86_64**, glibc 2.39, Qt/WebEngine **6.11.1**, Xvfb, CMake 3.31.6. Own cloud lacked the matching WebEngine SDK; compiled/runtime results are GitHub-hosted Linux evidence, not a claim of a local full-app launch.

| Command/check | Result |
|---|---|
| cmake -S tests/feria_foundation_build -B foundation-build -G Ninja -DCMAKE_BUILD_TYPE=Release | exit 0 |
| cmake --build foundation-build --parallel 2 | exit 0 |
| ctest --test-dir foundation-build --output-on-failure | exit 0, 2/2 targets |
| python3 tests/feria_foundation_build/negative_control.py . foundation-build | exit 0; injected query defect exit 1; restored exact source exit 0 |
| xvfb-run -a -s '-screen 0 1280x800x24' foundation-build/feria_foundation_runtime -input tests/feria_foundation_probe | exit 0; 18/18 including setup/cleanup, 16 substantive data cases |
| same runner -input tests/feria_session_probe | exit 1; 10 passed/1 failed |
| unchanged original retained-runtime baseline comparison | exit 1; same 10/1, original assertion unchanged |
| FERIA_PREVIEW_CAPTURE=... runner --preview tests/feria_foundation_probe/Preview.qml | exit 0, actual window PNG inspected |
| own-cloud PySide6 6.11.1 qmlformat parsing: wrapper/deck/Navigation test | exit 0 for each, syntax only |
| python3 -m py_compile negative_control.py | exit 0 |
| final archive checksum, ZIP CRC and safe paths | PASS |

Retained failure: test_downloadContinuesAfterTabCloses, 'The delayed fixture must still be downloading', before the tab-close behavior. No download assertion changed or skipped. Existing reviewer Windows 11/11 is reported historical evidence, not a diagnosis or Linux waiver.

Expected warnings: no audio device on hosted runner, software/virtual-display graphics, existing WebEngineProfile deprecation. Screenshot is a functional synthetic-page harness, not approved Feria styling, live provider evidence or audio qualification.

## CI and source ledger
Canonical commits:
1. 7bd4a12642ef48a6a4a01ff25640021a103a2108 — tests first and fixture geometry
2. b68db903216cb03e5bcaecc975e6562bca605e62 — actual popup API and committed target test gates
3. 54249b66a28f23cbe7c56ee356cf3a7568756d82 — repairs and corrected interface/donor delta

Alternate CI branch: deliveringsake-rgb/Colosseum, dots/feria-foundation-ci. Exact 32 canonical component blobs are checked before execution. Fork's older root is not the source basis.
- 90ef2837eac71c0d248f3c9150ea032615a303c1: run37670228671/job112959807969, cancelled after setup timeout, no compile/tests. Ubuntu mirror apt update stalled.
- 691c3f97e3d3b1601605afea2bf18b0a2c2ad66b: run37675144270/job112976594647, red plus harness corrections identified
- 2c6022794b2ab5375cb49b5fce4aacc727dbfaab: run37676302151/job112980575207, decisive red
- 438ac6cece5acbf850d13eeb3519bb3d03783f90: run37677299958/job112983986323, focused green; unchanged retained red

CI setup now checks installed dependencies before apt, bounds mirror/download timeouts, and still fails on missing dependencies. It changes no proxy/security settings and waives no gate. Canonical repository .github files were not changed.

Final artifact: **11508105111**, filename **feria-foundation-evidence-438ac6cece5acbf850d13eeb3519bb3d03783f90.zip**, 110637 bytes.
SHA256 **e16a6850507ee7173d660c71230465e3577e4fa9eb3958983d2db31c7333f293**.
[Run](https://github.com/deliveringsake-rgb/Colosseum/actions/runs/37677299958)
[Artifact](https://github.com/deliveringsake-rgb/Colosseum/actions/runs/37677299958/artifacts/11508105111)
[Repair diff](https://github.com/kingoftheseas56/Colosseum/compare/e5c11ef6416a361bf2195c310810cfb53f6a129e...54249b66a28f23cbe7c56ee356cf3a7568756d82)

## Receiving gate and boundaries
Re-run original plus added cases on Windows Qt6.11.1, especially real context-menu request lifetime, POST commit gating and explicit-file negative control. Then review adoption. Full Colosseum/Main/Lanista, live accounts/providers, DRM/codecs, app minimize pause semantics, startup restoration and categories6–10 remain unverified/excluded.

All implementation was performed by Dots in its own cloud, with direct GitHub connector commits and GitHub Actions. No Codex, Work, BDC, user-machine or delegated coding path. No main/master adoption, PR, merge, tag or release. Source lives on its working branch; Dots-Colosseum is the evidence index.


---

# Original delivery (historical, superseded by receiving repairs above)

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
