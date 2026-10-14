# Feria: secure streaming browser arc

Plan created 2026-10-14. Baseline: `60b12d129b6075e2a9b83ea4fb915431950f6cf4`
on `codex/feria-release-packaging`. Planning refresh: 2026-10-14, inspected HEAD
`8a1cfd667877596060eb009b1409f0315853659f`. This refresh organizes the full scope;
it did not begin implementation. The subsequent active-goal continuation resumed
direct implementation; checkpoints below record bounded progress. Release remains
a separate gate.

## Arc at a glance

| Order | In Hemanth's terms | What lets us move on |
| --- | --- | --- |
| 1 | Make the browser safe, and show which site you are actually using | Startup protections, connection/origin display and hostile-page boundary checks |
| 2 | Make sure our engine can actually support streaming | Pinned engine, observed sandbox, required build features, early DRM and login feasibility |
| 3 | Make windows, sign-in storage and restarts dependable | Popup input, ordinary/private separation, storage retention and clean lifecycle journeys |
| 4 | Make playback and permitted devices work properly | Media controls, web APIs, capture consent, revocation and appropriate freeze behavior |
| 5 | Finish the browser tools people need day to day | Navigation, files, PDFs, input, accessibility and harmful-content handling |
| 6 | Make the installed app as reliable as the development build | Exact packaged runtime, install/upgrade checks and authenticated updates |
| 7 | Prove the streaming sites work | Separate public, account, retained-login and actual playback results for each provider |
| 8 | Decide what we can ship and keep it secure | Evidence-linked release decision, stated limitations and an engine maintenance process |

All eight phases remain open until their detailed gates below are met. Existing
features are verified and extended where needed, not rebuilt by default. The
checkpoints are historical evidence, not a fresh runtime audit performed during
this planning refresh.

**Next bounded task: F1.4 permission lifetime repair and F2 runtime selection.**
A cross-origin iframe removal followed by an old prompt answer now reproduces a
stored grant on both accepted engine pairings. Preserve the failing regression;
the unbuilt Core patch is not a fix until compiled and qualified. Search existing
Windows engine builds and their exact feature/source provenance before choosing
a full-engine builder. The browser/native separation slice rejects a reproduced
cross-origin playback-attribution forgery. Sol 6.1 subagents and narrowly scoped Preflight
work are authorized. Codex owns receiving review and integration; use the loop
only for dispatched independent work, with quiet twenty-minute checks while pending.

## Outcome and scope

Feria should safely run the streaming services we need, with reliable sign-in,
retained sessions, playback and everyday browser controls. Required Chrome parity
means those concrete user journeys work; it does not mean cloning every Chrome
feature or promising that every provider will accept an embedded browser.

QtWebEngine remains the production route; WebView2 is not a fallback or migration
target. Discovery of a Widevine CDM inside an installed Edge package does not
change the rendering engine to Edge or WebView2. Website notifications stay disabled.
Chrome sync, a password manager, ad blocking, the Chrome Web Store and extensions
are outside this arc unless Hemanth changes scope or a required service establishes
a specific dependency. Experimental CEF remains separate.

Use the existing watch and music catalog as the initial acceptance list: Netflix,
Prime Video, HBO Max, Disney+, JioHotstar, Apple TV, Crunchyroll, YouTube, Hulu,
MUBI, Spotify, YouTube Music and Apple Music. Verify each service's actual endpoint
and regional availability when testing. Catalog presence is not compatibility proof.

The [security and compatibility ledger](feria-browser-security-parity.md) owns
detailed findings, evidence and primary references. This arc owns sequencing and
completion gates. It is a new development plan, not a reconstruction or renumbering
of Claude's original six sets.

## Starting position

| Work | Verified state at this baseline | Still required |
| --- | --- | --- |
| HTTPS default for bare public addresses | Native policy tests passed; preserved in `46c2261c` | Runtime TLS, redirects and connection disclosure |
| Idle-freeze deadline fix | Failing baseline, codec/stock candidate tests and final-source callback safety tests; preserved in `60b12d12` | Installed build and real media/capture interactions |
| Orderly shutdown fixture | Positive repeatability, negative controls and source-path checks; preserved in `a526c0f4` | Final runtime/package journeys |
| Popup engine patch | Candidate binaries and bounded evidence exist | Complete ordinary/private/input/renderer receiving review and package integration |
| History, bookmarks, settings, restore and site controls | Existing implementation and earlier tests | Final-runtime restart, storage clearing and privacy qualification |
| Streaming/security acceptance | Requirements and findings recorded | Actual runtime, installed build and provider proof |

Do not count a sent worker prompt, a test fixture, a pushed commit or a loaded
home page as completed provider qualification. Historical failures remain evidence
even when a later bounded regression passes.

## Work sequence

Implementation checkpoint, 2026-10-14: F1.1's source guard and 52 policy/process
checks pass, followed by six checks of a built app's real launcher with a fixture
scene (unsafe launches exit 78; accepted launches execute page JavaScript and exit
0). F1.2 stock/codec TLS/origin/mixed-fetch fixtures pass 10/0/0, root 0 each.
Native origin policy passes 48/0/0; Windows component/host identity checks pass
15/0/0, root 0. A minimum-width popup identity regression was reproduced and fixed.
Two affected input/POST hosting journeys pass 4/0/0, root 0. The security ledger
records exact scope and failed receipts. Full-shell/installed proof and complete
native-boundary review remain open. No phase is complete from these narrower results.

Additional checkpoint: the built app's native context passed 20 remote-boundary
checks across top-level, same/cross-origin frame and replacement documents; a
harmless exposed-channel control was detected with expected exit 7. A Windows
click-opened popup passed its native-global/transport checks (3/0/0, exit 0).
A real page prototype setter reproduced forged cross-origin playback attribution
(2/1/0, exit 1). The native origin guard now rejects it before resume processing
and observation emission. Final stock/codec regressions pass 3/0/0, exit 0 each,
including recovery and same-origin canonical routes; native policy passes 82/0/0.
These checks do not authenticate page-provided media metadata or qualify an
installed package. A subsequent subframe Local Fonts fixture reproduced a stale
stored grant after requesting-frame removal on both engine pairings (2/1/0,
native 1 each). Final-source stock reproduction also fails at that assertion;
the live grant/reset/deny control passes 3/0/0, native 0. F1.4 remains open.

Runtime selection checkpoint: exact Conda and Sigil package inspection rejected
both as complete replacements (see the prebuilt assessment). The official Qt
6.11.1 source archive has been downloaded and matches its publisher checksum;
host tools are staged without global installation. No full engine has been
configured or compiled from that source yet. An observed filesystem repair flag
on the prior research volume moved new staging to a healthy volume; accepted
runtime files remain unchanged and were hash-checked before use.

F2 now has bounded Windows renderer primary-token observations: two renderers
per stock/codec pairing reached restricted, untrusted-integrity tokens while
their default-freeze fixtures passed 3/0/0, native 0 each. The observer and exact
limitations are recorded in the security ledger. This does not close the full
sandbox, installed-runtime or provider gates.

Early F2.4 capability inventory now observes Widevine WebM access and MediaKeys
creation on both pinned runtimes; H.264/AAC MP4 is accepted only on the codec
runtime. Clear Key controls and cleanup pass 3/0/0, native 0 per pairing. This
advances API feasibility only: CDM identity, encrypted playback/license handling,
robustness and provider acceptance remain open in the security ledger.

The prior public Bitmovin DRM demo observation on the codec QtWebEngine pairing
passed 3/0/0, native/driver 0, with usable Widevine keys, encrypted video playback,
a seek and pause. Its document-local video/key counters did not establish
document continuity across read-only retry, so independent review required a
fixed native epoch for baseline/seek/post-seek/pause. That source revision also
rechecks origin, Widevine system, bound encrypted/message/current usable status
and playback readiness after seek. Its only revised run failed initial readiness:
Bitmovin-origin `Just a moment...` page, zero videos/sessions, native/driver 1.
No retry or bypass followed. The new document-bound assertions were not reached
and remain runtime-unverified; the earlier pass does not qualify them. See the
security ledger and `public-drm-codec-20261014-document-bound` failure evidence.
The prior loaded CDM came from installed Edge; its fresh same-hash disk receipt
reported valid Microsoft Windows catalog selection versus the earlier Google
LLC Authenticode result. No engine switch or CDM installation occurred.
Fresh-machine CDM availability, maintenance/distribution, subscription providers,
robustness, HD entitlement and installed packaging remain open.

| Phase | Objective | Depends on | Completion gate |
| --- | --- | --- | --- |
| 1 | Secure startup and browser/native boundaries | Current source | Unsafe configurations rejected and hostile-page negative tests pass |
| 2 | Pin a maintained runtime and test streaming feasibility early | Phase 1 startup guard; remaining source review can continue alongside it | Exact engine provenance, required feature inventory and explicit DRM/login feasibility decisions |
| 3 | Finish browser state, popup and lifecycle reliability | Qualified candidate runtime from phase 2 | Targeted ordinary/private, storage, recovery and shutdown journeys pass |
| 4 | Finish account-free media, web APIs and device permissions | Phases 1–3 relevant boundaries | Required capabilities pass in the selected runtime with consent and revocation |
| 5 | Complete everyday browser interaction and file safety | Selected runtime and security boundaries | Required daily-use journeys pass with real input and safe file handling |
| 6 | Qualify the installed package and update path | Phases 2–5 relevant implementation | Matching packaged runtime, clean installs/upgrades and security checks pass |
| 7 | Qualify actual streaming providers | Early phase 2 feasibility; phase 6 build for final acceptance | Per-provider login, retained session and playback results recorded |
| 8 | Close the release gate and establish maintenance | All required gates, or explicitly accepted scope changes | Evidence-linked release decision and actionable engine-update process |

Phases group work, not large all-at-once tasks. Execute one bounded objective at a
time. Run early provider probes in phase 2; do not postpone discovering a DRM or
embedded-login blocker until phase 7. Later provider failures may reopen a specific
earlier task. Do not require unrelated optional features before testing streaming.

### Phase 1 — Secure startup and privileged boundaries

- **F1.1 — Guard production WebEngine startup.** Inspect all effective
  environment and command-line inputs before `QtWebEngineQuick::initialize()`.
  Reject configurations that disable the sandbox, bypass certificate checks,
  weaken origin security or expose remote debugging. Preserve legitimate graphics
  configuration and account for the shared engine's EPUB use. Decide any developer
  debugging route explicitly; it must not become an implicit production bypass.
  Verify real parser behavior and both accepted and rejected configurations, with
  rejection before engine initialization. Do not claim sandbox activity from flag
  rejection alone; that needs runtime proof in F2.2/F6.1.
- **F1.2 — Transport and origin controls.** Exercise invalid, expired and wrong-host
  TLS certificates, redirects, explicit HTTP, mixed content, CORS, CSP and same-origin
  access. Review local-file and custom-scheme permissions. Keep an understandable
  origin/connection display and no silent TLS-to-HTTP fallback.
- **F1.3 — Browser/native separation.** Trace every script, bridge and scheme exposed
  to remote content. Prove hostile pages, subframes and popups cannot reach native
  stores, arbitrary files or process APIs, or manufacture trusted permission UI.
- **F1.4 — Permission and authentication ownership.** Verify requesting origin,
  navigation changes, private context, cancellation and revocation for each existing
  prompt, including clipboard, HTTP/proxy authentication, client certificates and
  WebAuthn. Preserve denial as a normal outcome.

Proof: source review plus focused policy tests and local hostile-page runtime
fixtures. Physical passkeys/OS dialogs remain separate checks where required.

### Phase 2 — Runtime foundation and early streaming feasibility

- **F2.1 — Choose and record the engine baseline.** Record Qt, WebEngine, Chromium
  base and backported security level separately. Verify source/patch provenance,
  build configuration and compatible DLL/helper/plugin/resource identities. Review
  the popup patch against that baseline and retain a reproducible build recipe.
- **F2.2 — Verify actual runtime protection.** Check renderer sandbox/isolation and
  loaded modules in the candidate, including renderer processes. Resolve the missing
  stock renderer provenance receipt rather than inheriting a summary's pass claim.
- **F2.3 — Restore required build capabilities.** Inspect the existing codec recipe's
  disabled WebRTC, PDF/printing, spellcheck and geolocation features. Enable required
  capabilities deliberately; verify actual runtime behavior. Assess other disabled
  features only against a concrete requirement.
- **F2.4 — Run early streaming feasibility probes.** Establish codecs/MSE/EME and
  legitimate CDM availability, then probe public provider pages and supported login
  routes. Separate engine defects, embedded-browser policy, DRM licensing, device,
  region and account restrictions. Decide supported routes or explicit blockers
  before substantial provider-specific implementation. Never copy authentication
  cookies or weaken browser security to manufacture acceptance.

Proof: pinned manifest, build/patch records, actual process evidence and a small
feasibility matrix. A codec sample is not protected provider playback. A supported
system-browser OAuth flow does not automatically sign the embedded website in.

### Phase 3 — Reliable tabs, storage and recovery

- **F3.1 — Close popup qualification.** Review exact existing evidence and untouched
  hosting-test changes. Fill only missing journeys: typing/clicking after closing
  the opener, ordinary/private/nested popups, target reuse, focus, navigation and
  beforeunload. Retire the baseline lifetime safeguard only after the patched
  runtime is qualified. Keep renderer/crash and shutdown controls meaningful.
- **F3.2 — Resolve the cookie-retention discrepancy.** Reproduce the ordinary-cookie
  loss with an isolated profile and exact cookie attributes. Verify cookies,
  localStorage, IndexedDB, cache and service-worker state individually across restart.
- **F3.3 — Finish privacy and site controls.** Prove private disk isolation, separation
  from ordinary sessions, permission reset, site-data clearing and provider sign-out
  across relevant origins. Confirm existing history/bookmark/settings/restore
  behavior in a two-launch final-runtime journey and recovery after an abnormal exit.
- **F3.4 — Verify lifecycle under real work.** Keep audio, downloads and active capture
  alive as appropriate; check freeze/thaw, callback retirement, renderer recovery
  and orderly shutdown. Preserve the real default freeze interval and negative
  teardown controls. Do not relabel synthetic controls as provider proof.

Proof: isolated profiles, exact process exits, storage observations and real input;
preserve failed receipts and investigate the smallest demonstrated cause.

### Phase 4 — Media, platform APIs and device permissions

- **F4.1 — Account-free playback.** Verify required codecs, adaptive media/MSE,
  fullscreen, seek/resume, subtitles, alternate audio, volume/mute, media sessions
  and hardware acceleration/fallback. Implement picture-in-picture if required by
  the accepted streaming journeys.
- **F4.2 — Permission-dependent media.** Verify camera, microphone, screen capture,
  device choice, cancellation, revocation and OS permission denial. Ensure recording
  or active calls are not silently frozen. Keep unrelated hardware APIs closed until
  a concrete use case and safe permission route exist.
- **F4.3 — Required web platform behavior.** Exercise service workers, fetch,
  WebSockets, storage quota, WASM and canvas/WebGL against the pinned runtime; use
  provider failures to identify additional necessary APIs. Report unsupported APIs
  honestly rather than treating a user-agent change as an implementation.
- **F4.4 — Native media integration.** Verify media keys, audio output changes and
  device sleep/resume where relevant. Separate simulated events from physical input.

Proof: local fixtures and agent-operated runtime tests first; hardware-specific
results only when that hardware is actually available and exercised.

### Phase 5 — Everyday use and harmful-content handling

- **F5.1 — Finish existing daily-use failures.** Diagnose the codec context-image
  opening failure, then verify navigation/back/forward/reload, find, zoom, selection,
  context menus and popup-abuse handling without adding unnecessary browser chrome.
- **F5.2 — Uploads and downloads.** Verify user-selected upload boundaries, progress,
  cancel/retry, safe filenames/path handling, completion ownership and external-open
  consent. Determine Windows attachment provenance and safe handling of executable
  or misleading files. Avoid silent execution or arbitrary local-file exposure.
- **F5.3 — Documents and input.** Verify PDF viewing/printing, spellcheck, keyboard
  shortcuts, IME, focus traversal, accessibility and touch in the selected build.
  Check actual native dialogs and assistive input where fixtures cannot prove them.
- **F5.4 — Malicious-site and file protection.** Inventory the protection actually
  supplied by the runtime and OS. Choose an appropriate supported reputation route
  where absent, documenting privacy/network implications and behavior when offline.
  Do not claim Chrome Safe Browsing or malware scanning without implementation and
  verification. Bound external protocols and preserve clear origin disclosure.

Proof: focused regressions plus real UI/file journeys. Product choices needing a
new external service or data-sharing policy require a concrete proposal to Hemanth.

### Phase 6 — Installed application and updates

- **F6.1 — Package the qualified runtime.** Replace file-presence-only verification
  with exact manifest/provenance checks for compatible runtime parts, and verify
  loaded paths and protection in the installed application. Ensure test flags,
  debug exposure and diagnostic hooks do not ship enabled.
- **F6.2 — Installation and profile migration.** Exercise clean install, upgrade,
  restart and uninstall behavior with disposable ordinary/private profiles. Protect
  saved sessions and site data according to intended policy; test failed updates
  and recovery without reverting to an insecure runtime silently.
- **F6.3 — Distribution integrity.** Verify signing and authenticated update metadata,
  package tamper rejection and the release's exact engine identities. Run a bounded
  installed smoke covering popup input, media, privacy, permissions and clean exit.

Proof: actual installed-package evidence. Build success, source tests and a pushed
branch do not substitute for installation or release approval.

### Phase 7 — Real streaming acceptance

- **F7.1 — Public checks for every target.** Record exact URL, region, installed build,
  native engine identity, public load and any freely available media result.
- **F7.2 — Authorized account checks.** For available accounts, verify login and popup
  returns, two-launch session retention, logout/site clearing and private behavior.
  Hemanth supplies consent, 2FA or entitlement only when necessary; no signup or
  purchase is implied by this plan.
- **F7.3 — Playback checks.** Verify actual protected playback where entitled, seeking,
  subtitles/audio selection, fullscreen, controls and restart/resume. Record actual
  quality and restrictions without promising HD/4K from a successful low-resolution
  stream. Inspect provider errors before assigning a cause.
- **F7.4 — Publish the support matrix.** Classify each target as verified, partially
  verified, blocked with reason, or untested. Fix bounded Feria defects; explicitly
  record provider/vendor restrictions and any supported alternative route.

Proof: provider-specific results from the installed build. A login page or successful
local EME fixture cannot close an authenticated playback row.

### Phase 8 — Release decision and ongoing security

- **F8.1 — Consolidate evidence.** Close each required row with exact source/package
  identity and test result; list remaining limitations. Review dirty ownership and
  preserve only owned changes to GitLab. Resolve release-blocking defects or seek
  an explicit scope decision with the concrete impact shown.
- **F8.2 — Maintain the engine.** Define who tracks upstream security updates, how
  updates are triaged, rebuilt with Feria patches and verified, and how emergency
  releases reach users. Specify the minimum regression set and patch-rebase proof.
  Do not create scheduled jobs as part of this planning task.
- **F8.3 — Release separately.** Produce a reviewable release candidate and acceptance
  report. Merging, publishing and declaring supported providers are separate actions
  from completing this arc document.

## Execution and ownership

Codex works directly; the Preflight loop and scheduled checks remain stopped.
Already-sent reports can be reviewed as evidence if they arrive. Do not resend them
or infer completion from worker silence. Use a separate owned lane where necessary.

For each task: inspect the existing owner, identify the smallest demonstrated gap,
implement only that gap, run relevant positive/negative checks, review the final diff,
then commit and push explicit owned paths. Record source, fixture, runtime, installed,
provider and hardware evidence separately. Broaden tests only when dependencies or
new failures justify it. Do not add parallel stores or speculative features.

| Who / method | Work |
| --- | --- |
| Codex, direct source and automated tests | Guards, native boundaries, runtime recipe/manifest, regressions, controlled TLS/storage/media fixtures and evidence review |
| Codex, actual app/runtime interaction | Popup input, site controls, downloads/dialogs, installed journeys and public provider probes |
| Hemanth when necessary | Account consent/2FA, paid entitlement, physical authenticator or unavailable hardware; external-service/product decisions |
| Preflight loop | Not used; restarting it requires Hemanth's explicit instruction |

Human testing is not a blanket completion prerequisite. Tests requiring unavailable
accounts/devices remain explicitly unverified while independent work proceeds.

## Blockers and uncertainties at planning time

| Item | Impact and next action |
| --- | --- |
| Runtime security report was absent at the last check | Nonblocking: inspect startup directly for F1.1; review an exact late report if available |
| Original broad qualification reply was absent; hosting test remains worker-owned dirt | Preserve `tests/feria_hosting_probe/tst_Hosting.qml`; receiving review is required before adoption. Missing reply does not prove worker termination |
| Codec context-image failure, cookie discrepancy and missing stock renderer module evidence | Specific open F3/F5 gates, not reasons to repeat every old suite |
| D: previously disappeared and was restored | Evidence/build availability risk; recheck before runtime work. Preserve partial artifacts if unavailable; do not substitute unverified binaries |
| Feature-reduced codec recipe and incomplete package verification | F2/F6 must resolve actual feature and runtime-provenance gaps before installed support claims |
| DRM/CDM, embedded-login policy, accounts, region and hardware | Early F2.4 feasibility separates external constraints from fixable Feria defects; final provider proof waits only on the relevant prerequisite |
| No final installed/provider qualification for the latest changes | Remains open through F6/F7; source and fixture success cannot close it |

## Completed bounded receiving slice and next task

**Receiving slice: startup, mixed-content fetch and site-identity changes.**

The planning refresh found uncommitted origin-display, transport-test and startup
test-driver changes alongside the original worker-owned hosting test. Their
presence is not acceptance. Review the current bytes and existing receipts before
deciding which additional checks are necessary.

- [x] Review `tests/webengine_startup_build/run_app.py` and its existing native
  process receipts: rejected configurations must exit before WebEngine starts;
  accepted configurations must run actual page JavaScript and exit cleanly.
- [x] Review `tests/feria_security_probe/run_transport.py` and
  `tst_transport.qml.in`: distinguish a working HTTP positive control from actual
  mixed-content blocking, and keep any fixture-only DNS setting out of production.
- [x] Review `native/feria/FeriaBrowserPolicy.h`, `FeriaSiteIdentity.qml` and its
  three host integrations. Confirm the displayed origin removes credentials and
  URL secrets, follows navigation, and does not equate HTTPS with site trust.
- [x] Check existing policy and site-identity results; finish only missing visual,
  host geometry or input checks affected by the new header. Preserve earlier
  failures and `tests/feria_hosting_probe/tst_Hosting.qml` ownership.
- [x] Update the security ledger with exact source/runtime evidence and limitations;
  review the final diff, commit explicit owned paths and verify GitLab preservation.

**Result:** the bounded slice's verification record is in the security ledger.
This does not close all of F1 or qualify full-shell/installed/provider behavior.
**Next: F1.4 permission lifetime repair**, while bringing forward F2 runtime
and streaming feasibility checks as their prerequisites are met. The F1.3 slice
above provides bounded source/runtime proof; it is not exhaustive bridge, scheme,
file-picker or native permission qualification. A separate source-only Preflight
task is researching the codec recipe's disabled browser features, without build
or repository-write ownership.

**Blockers:** the current Core patch is unbuilt. No full Chromium source/object
cache or adequate Windows CI builder has been verified. Existing prebuilt engines
are being researched before committing to that build; codec support does not
prove the document-lifetime fix or all required browser features. No account is
required for the permission regression. D: evidence and
runtime availability were confirmed during this receiving slice; recheck before
later runtime work. An unavailable build or receipt blocks only the claim
that depends on it. Preserve unowned changes. Provider accounts, DRM availability,
region and hardware can block later individual checks, not independent source work.
