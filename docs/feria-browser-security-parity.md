# Feria browser compatibility and security

Hemanth resumed development on 2026-10-14 with the goal of the Chrome capabilities
needed for real services and safe general browsing. This is the acceptance ledger
for that expanded scope. It is not a claim of full Chrome parity or a secure release.
QtWebEngine remains the production engine route. Website notifications remain
disabled; the earlier service scope did not include a password manager or ad blocker.

Hemanth clarified that streaming sites define the compatibility target. Use the
existing `PorticoData.js` watch catalog: Netflix, Prime Video, HBO Max, Disney+,
JioHotstar, Apple TV, Crunchyroll, YouTube, Hulu and MUBI. Include its streaming
music catalog: Spotify, YouTube Music and Apple Music. These are the current
repository entries, not verified current service URLs or availability promises.
Prioritize sign-in and persistent sessions, protected playback, seek/subtitles/audio
selection, fullscreen and media controls. For each site, separately record public
access, authenticated playback and account/entitlement or regional blockers.

## Security and site acceptance are different gates

Valid TLS, sandboxing, origin isolation and an up-to-date engine protect the user.
They do not guarantee that a provider accepts an embedded browser. Google explicitly
restricts embedded user agents for OAuth. A provider-supported system-browser flow
can authenticate a native integration; it does not automatically sign the embedded
website into the same session. Do not copy cookies or spoof a successful security
check to close this gap. Record the actual site, error and supported route.

DRM similarly needs a compatible CDM, codec support and service acceptance. A local
H.264 test does not prove subscription playback, HD quality or Widevine eligibility.

## Evidence at this checkpoint

- Production startup now validates external Chromium options before WebEngine and
  application initialization. Presence-based sandbox disabling, remote debugging
  and all options outside a reviewed graphics/logging list are refused with exit
  78; raw values are not logged. Policy baseline: 8 passed/39 failed with no guard;
  implementation: 52 passed/0 failed, including native subprocess checks of argv
  and inherited environment. A built Colosseum executable then passed six native
  process checks: four unsafe launches exited 78 before the engine marker, and
  default/allowed-graphics launches rendered a fixture document, executed its
  JavaScript and exited 0. This uses the real launcher with an explicit fixture
  scene; it is not full-shell, installed-package or sandbox-token proof.
  See [permitted options](build/windows.md#webengine-startup-configuration).
- The real-wrapper transport fixture passed 10/0/0 with native exit 0 on each
  accepted stock and codec candidate. It proves valid HTTPS, rejection of expired,
  wrong-host and untrusted certificates, a redirect to expired TLS, CORS with a
  positive control, cross-origin iframe rejection, CSP with a positive control,
  local-file fetch rejection and absence of selected native globals. A working
  non-trustworthy HTTP origin is the positive control for an HTTPS-to-HTTP fetch
  rejected with Chromium's mixed-content diagnostic. Test CA trust
  is profile-only; OS trust was unchanged. Source hashes stayed stable and observed
  WebEngine modules matched the candidate manifest. Both final runs recorded
  sampled helper identities and zero remaining sampled children. Retain offscreen GPU errors and existing profile-creation
  deprecation warnings: these runs do not prove GPU/media rendering, every native
  boundary, all mixed-content types, sandbox activity or installed-provider behavior.
- Site identity now uses a native origin formatter that omits URL credentials,
  paths and query strings and displays ASCII IDNs. Main-host and popup headers
  distinguish HTTP, connecting, unverified and loaded HTTPS states without labeling
  a site trustworthy. Native policy tests: 48/0/0. Windows component and real-host
  identity tests: 15/0/0, exit 0, with inspected screenshots. The minimum 320-pixel
  host initially hid the identity; a dedicated row fixes the reproduced failure.
  Two existing hosting journeys additionally passed (4/0/0 including setup/cleanup),
  covering typed input through host transfer and original POST popup geometry.
  Windows runs use the fixture's existing first-exposure foreground activation;
  earlier missing-focus and offscreen-load failures are preserved. The worker-owned
  hosting file was not edited or adopted wholesale. Full-shell/installed identity
  acceptance and broader popup qualification remain open.

Evidence identifiers for these bounded checks: `feria-security-startup-20261014`
(`app-check-01/result.json`), `feria-security-transport-20261014-stock-02` and
`feria-security-transport-20261014-codec-02` (`result.json`, `qtest.txt`), and
`feria-origin-build-20261014` (`origin-green.txt`, `identity-final.txt`,
`identity-final-exit.txt`, `identity-final-evidence`, `hosting-final-result.json`).
The startup receipt identifies the tested executable by SHA-256; transport receipts
identify the pre-header wrapper hashes. The later popup/header changes have their
own actual-host checks, not a retroactive transport qualification claim.

- `FeriaBrowserPolicy::navigationUrl()` now defaults bare public addresses to HTTPS,
  including ports. Explicit HTTP is preserved, as are `localhost`, `127.0.0.1` and
  `::1` loopback defaults. There is no automatic HTTP retry on TLS failure. This is
  address normalization, not a browser-wide HTTPS-only or HSTS implementation.
  New regression: 6 expected failures before the fix; full native policy suite:
  37 passed, 0 failed, root exit 0 after the fix. No installed-app claim.
- `FeriaBrowserRequests.qml` rejects certificate errors and exposes origin-scoped
  permission, client-certificate and WebAuthn flows. Existing request tests cover
  cancellation, stale owners and certificate rejection; presence is not fresh
  physical-device or provider qualification.
- Shutdown fixture correction was verified and preserved in commit `a526c0f4`.
  Popup binary qualification and installation remain separate outstanding gates.
- The idle-freeze postponement regression now has a valid failing baseline:
  the callback completed after the real 30-second deadline, but the page stayed
  Active (QtTest 2 passed/1 failed, native exit 1). The fix retains the elapsed
  deadline while JavaScript drains and defers freezing until callbacks finish,
  rechecking eligibility and pending work. Losing eligibility or changing the
  configured delay resets the countdown. Codec candidate: 4/0/0, native exit 0;
  stock candidate: 8/0/0, native exit 0, including the original default/audio
  cases and new callback-handoff/eligibility-reset/delay-reset checks. These are
  synthetic regression and runtime-fixture proofs, not streaming-site acceptance
  or attribution of every historical full-suite failure. The earlier stalled
  run and unknown exit remain recorded separately.
  Final repository-source codec check: 7/0/0, native exit 0, covering the three
  new safety cases plus repeated mixed-tab closure and discarded late callbacks.
  Each passing run used disposable profiles, checked exact runtime module
  identities and left no candidate processes. No installed-package claim.
- The checked-in codec recipe explicitly disables printing/PDF, spellchecking,
  WebRTC, geolocation and extensions. It cannot be assumed to satisfy this target.
  This is evidence about the recipe, not proof of features in every existing binary.
- `verify_browser_runtime.ps1` checks required files exist and are nonempty.
  That alone does not establish matching hashes, sandbox activity or security fixes.

### Streaming capability checkpoint, 2026-10-14

The real-wrapper `FeriaStreamingCapabilities::test_inventory` records codec/MSE
declarations and temporary EME access plus MediaKeys construction. Both pinned
runtime pairings passed the inventory and Clear Key/WebM controls (3/0/0,
native/driver exit 0), with stable source, matching Qt modules/helpers and no
remaining observed children. The loopback origin was a secure context; profiles
were disposable, and no CDM installation, explicit path override or provider
account was used.

| Observed capability | Stock | Codec |
| --- | --- | --- |
| VP8/Vorbis `canPlayType` and MSE | Advertised | Advertised |
| H.264/AAC `canPlayType` and MSE | Unsupported | Advertised |
| Clear Key WebM access and MediaKeys creation | Accepted | Accepted |
| Widevine WebM access and MediaKeys creation | Accepted | Accepted |
| Widevine H.264/AAC MP4 access and MediaKeys creation | Rejected (`NotSupportedError`) | Accepted |

Evidence: `streaming-capability-stock-20261014` and
`streaming-capability-codec-20261014`, each with `qtest.txt`, `result.json`, launch
identity and file-backed logs. PASS denotes a completed inventory and its positive
controls; Widevine acceptance is recorded separately and is not required for the
inventory test to pass. Requested sessions were temporary, distinctive identifiers
and persistent state were disallowed, and returned robustness was unspecified.
This does not establish a hardware/software robustness tier, session creation,
license acquisition, usable keys, decryption, encrypted playback, the actual CDM
binary path/hash, installed-package discovery or provider acceptance. Offscreen
GPU context warnings also prevent inferring hardware-accelerated playback.

### Public encrypted playback checkpoint, 2026-10-14

The earlier opt-in public [Bitmovin DRM demo](https://bitmovin.com/demos/drm)
probe passed through Feria's real wrapper on the codec QtWebEngine pairing:
3 passed, 0 failed, 0 skipped, native/driver exit 0. It ties MediaKeys ownership,
session messages/current usable statuses and encrypted events to a video/keys
identity local to a document. That video had two encrypted events, one Widevine session/message and
five usable keys. After seeking to 12 seconds, the same video/MediaKeys identity
decoded additional frames, then paused at 14.568225 seconds. The final snapshot
recorded readyState 4, 1920x1080 dimensions, 251 decoded/total frames and 61 dropped
frames. Dimensions do not establish HD entitlement or playback performance.
Qt module/helper hashes matched, source stayed stable and no observed children
remained. No account, copied CDM, security bypass or engine change was used.

The identity-checked descendant observer captured 27 CDM module samples from
QtWebEngine's Widevine utility process, PID 4156, creation timestamp
1791939866.31334. Its mapped `widevinecdm.dll` came from the installed Microsoft
Edge 155.0.4283.45 package: version 4.10.3050.0, 22716072 bytes, SHA256
`0e28a9203f8ddedcdf04785b52c460f63306c8b8bd57ccd17d73da93b366c28a`.
The fresh disk receipt reports Valid, SignatureType Catalog, signer Microsoft
Windows. The earlier receipt for the same hash reports Valid Authenticode, signer
Google LLC. These are separate Windows signature-selection observations; the
fresh result is not described as an embedded Google-signature verification.
This is default CDM discovery by **QtWebEngine**, not WebView2 rendering or an Edge
engine fallback. A mapped path plus signed disk hash is not an in-memory image
integrity measurement. Twelve observer errors record vanished-process intervals;
those remain unknown.

Earlier passing evidence under the local FeriaEngineResearch root:
`public-drm-codec-20261014-retired-poll/cdm-observation.json`,
`native/qtest.txt`, `native/result.json`, driver stdout/stderr, the fresh
`public-drm-codec-20261014-retired-poll/signature-receipt.json`, and
`cdm-codec-observation-20261014/signature-receipt.json`.
Native root PID 24104 and owned driver PID 23328 exited 0. The focused driver
checks source stability, module/helper manifests and observed-child cleanup.

Preserved failure history: `public-drm-codec-20261014` failed QML compilation
because WebEngineScript is a value type; `public-drm-codec-20261014-script-value`
passed the earlier startup-playback check using the collection dictionary API.
The hardened `public-drm-codec-20261014-bound-video` failed its JavaScript callback
wait before any video/session appeared (native/driver 1, no CDM captured).
Feria retires callbacks on document replacement. The corrected helper allows
only read-only polls to retry a retired document; actions remain strict and all
video/key ownership, seek and decoding assertions remain. One corrected native
launch produced that passing observation.

Independent receiving review found that video/key counters restart in each
injected document, so matching those IDs across read-only retry did not prove
document continuity. The current source fixes a native `javascriptEpoch` before
baseline capture and binds baseline, seek, every post-seek sample and pause to
that epoch. Replacement after binding explicitly fails; initial readiness polling
alone may retry. Post-seek also rechecks Bitmovin origin, Widevine system,
bound encrypted events, session message/current usable keys and playback readiness.

The single permitted revised run, `public-drm-codec-20261014-document-bound`,
failed before baseline capture at the initial origin-readiness poll (QTest line
141): 2 passed, 1 failed, native PID 23476/driver PID 26252 exit 1. Its final
snapshot was Bitmovin origin, title `Just a moment...`, document epoch 2, zero
videos/sessions/messages/encrypted events. No CDM was captured; this is unknown,
not absence. Six observer errors comprise partial-read access failures and
vanished-process intervals. Source stability, Qt module/helper manifests and
observed-child cleanup passed. No retry or security bypass followed. Evidence is
that lane's `native/qtest.txt`, `native/result.json`, `cdm-observation.json` and
driver stdout/stderr. The page state is consistent with an interstitial; the
specific service challenge cause and failed origin-poll callback are unproven.
The revised document-bound DRM/seek assertions were not reached and remain
runtime-unverified. The prior pass cannot qualify those new assertions.

The prior run is bounded public-demo encrypted playback via its ordinary license route
on this machine. Subscription providers, hardware acceleration, robustness tier,
HD entitlement, full sandbox qualification, installed-package acceptance,
fresh-machine CDM availability and redistribution rights remain unverified.
Supported CDM discovery/maintenance and packaging remain open.

### Browser/native boundary checkpoint, 2026-10-14

Windows process-token checkpoint: `observe_windows_tokens.py` sampled actual
renderer primary tokens while the real wrapper ran the default 30-second freeze
fixture. Stock and codec each passed 3/0/0 with native/driver exit 0, matching
loaded module/helper identities, stable source and no remaining observed children.
The final observer binds creation FILETIME, image path and token query to one
retained Windows process handle; API failures remain explicit evidence errors.

Stock recorded 60 renderer samples across two processes (57 untrusted-integrity,
3 early low-integrity); codec recorded 62 (59 untrusted, 3 early low). Every sample
had one restricting SID and agreed with `IsTokenRestricted`; each renderer's last
sample was untrusted. All were members of a job, without AppContainer membership.
The ordinary observer/browser had medium integrity and no restricting SID list;
`TokenHasRestrictions` was still true, so that flag alone is not sufficient proof.
There were no observation errors. Evidence: `sandbox-stock-handle-20261014` and
`sandbox-codec-handle-20261014`, each with `tokens.json`, `receiving.json` and
native test/module/exit receipts. Earlier recordings are retained separately;
their observer used a cached psutil creation-time comparison and is superseded
by the handle-bound observer (SHA256
`19dd482e5443afa5a8a1aa5fd9650a710aa53befe74d0516ed825e5af4079c9d`).

This establishes sampled primary-token restrictions in the two pinned offscreen
fixtures. It does not establish job limits, thread impersonation, desktop
isolation, mitigation settings, broker policy, installed runtime or provider
acceptance. Job membership alone is not a Chromium sandbox policy measurement.
See [Chromium's Windows sandbox design](https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md)
and [Microsoft's token information classes](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-token_information_class).

Source review found no Feria WebChannel registration exposing the native context
to remote pages. The local Reader2 channel is a separate view. QML context objects
alone do not create remote JavaScript globals. In the built Colosseum executable,
an explicit fixture scene passed 20 checks across top-level content, same-origin
and cross-origin frames, and replacement navigation: production native owners
existed on the QML side; tested native globals/transport were absent remotely;
local-file and qrc fetches were denied. A negative control registering only a
harmless fixture object exposed the transport and was detected (expected root 7).
The positive run exited 0. These runs used the stock SDK modules, not the patched
popup pairing, and record their exact paths/hashes and no remaining sampled children.

A separate Windows real-click POST popup check passed 3/0/0, root 0 against the
patched stock runtime, with page JavaScript execution and native globals/transport
absence. The first attempt failed focus before opening the popup. A dedicated
first-exposure test window corrected setup while retaining the focus assertion;
that failed receipt remains. This is not complete popup qualification.

The injected playback probe did have a concrete integrity gap: a hostile page's
prototype setter could rewrite its returned `href` to another origin. The real
wrapper regression reproduced one forged `playbackObserved` event (2/1/0, root 1).
`sameWebOrigin()` now compares canonical HTTP(S) scheme, host and effective port
against the engine URL before the resume gate receives an observation; mismatches
settle as errors, before any trusted visit token is attached. Native policy tests
pass 82/0/0. Final stock and codec runtime tests pass 3/0/0, root 0 each, proving
the setter actually runs, forged output is rejected, later valid output recovers,
and different same-origin canonical routes still work. Source hashes stayed stable,
observed modules/helpers matched the accepted manifests, and sampled children exited.
This establishes origin ownership, not truthfulness of same-origin titles or media
measurements; header/account-store effects are inferred from the rejection point,
not independently recorded by this fixture.
The existing Qt wrapper/store deterministic integration was updated to load
resource-free inline HTML at its injected sample's HTTPS origin instead of
`about:blank`. Its unchanged resume, hold/error, out-of-order result and store
assertions pass 3/0/0, root 0 on stock (`feria-wrapper-origin-20261014-stock`).
This uses synthetic samples and makes no provider or WebView2 qualification claim.

Evidence identifiers: `feria-native-boundary-20261014-05` and
`feria-native-boundary-20261014-control-02` (`result.json`, logs),
`feria-popup-boundary-20261014-01` (failed focus) and `-02` (passing receipt),
`feria-observation-origin-20261014-red-02`,
`feria-observation-origin-20261014-final-stock`,
`feria-observation-origin-20261014-final-codec` (`result.json`, `qtest.txt`), and
`feria-origin-trust-20261014-sol` (`red.txt`, `green.txt`). The built-app scene has
media observation disabled and tests native-context separation using an earlier
executable; the rebuilt foundation runner supplies the guard's final-source
runtime proof. No installed
app or renderer-sandbox claim follows.

Remaining F1 work includes custom-scheme/external-protocol and file-picker paths,
subframe request invalidation, and full-shell/installed verification. Review found
that permission objects supply their own origin, but file/capture UI sometimes
uses the top-level URL. Removal is now reproduced below; navigation and replacement
still need their own regressions. Legitimate iframe requests must remain supported
when their origin differs from the top-level site.
Follow-up, 2026-10-14: the delegated `LocalFontsAccess` fixture reproduced a stale
grant on BOTH accepted stock and codec pairings. A real click in a localhost
iframe under a 127.0.0.1 page raises the application's prompt with the correct
requesting origin. After removal and native `mainFrame.children.length === 0`,
answering the old prompt stores Granted for the removed frame's origin. Both
regressions report 2 passed / 1 failed, native exit 1. This violates Feria's
document-bound consent requirement. Qt documents persistent permissions as
origin-level state; do not describe this finding as a published Qt CVE.

Evidence: `feria-frame-permission-20261014-stock-02` and
`feria-frame-permission-20261014-codec-01`, with final-source stock reproduction
`feria-frame-permission-20261014-stock-final-red` (`qtest.txt`, `result.json`).
All recorded exact candidate module/helper matches, stable QML source and no
remaining sampled children. Final-source stock positive control
`feria-frame-permission-20261014-stock-positive-02` passes 3/0/0, native 0:
live consent returns font entries, explicit profile reset returns Ask, and a
new request can be denied with no returned font data. No font names are logged.
Earlier fixture failures remain preserved. These are browser fixture results,
not installed-app or live-provider acceptance.

The unbuilt engine candidate binds request handles to a weak document and adds
callback owner-lifetime guards. It remains outside the production build and is
not a verified fix. Core and matching consumers require compilation together;
the accepted Quick-only build over prebuilt Core cannot validate it. Proactive
prompt cancellation, same-URL replacement/reload, simultaneous requests, BFCache
and combined capture callback behavior remain qualification gates. Source-only
inspection of Qt v6.11.2 and current development permission handles found no
requesting-document validity check; a newer binary alone is not proof of repair.

## Required capabilities and acceptance work

“Open” below means implementation or proof remains; it does not imply every feature
is absent. Each slice must first inspect its existing owner and existing tests.

| Area | Required behavior | Remaining proof / implementation route |
| --- | --- | --- |
| Engine and updates | Supported Chromium security fixes, reproducible Qt patches, compatible DLL/helper/resources/plugins; timely signed upgrades | Open. Record native engine/base Chromium/security-patch versions and source provenance separately. Verify exact loaded modules and signed package/update integrity. Review startup flags, renderer sandbox and isolation in the shipped runtime. |
| Transport and origin security | HTTPS defaults; reject invalid, expired and wrong-host certificates; enforce same-origin, CORS, CSP and mixed-content restrictions | Policy and bounded stock/codec TLS/origin/mixed-fetch fixtures pass; connection disclosure has component/native-host proof above. Open full-shell/installed checks, remaining redirect/downgrade cases, custom schemes and complete local-file/permission review. |
| Browser/native boundary | Remote pages cannot call privileged native stores, file/process APIs or forge application permission UI | Bounded built-app/frame/popup probes pass; reproduced cross-origin playback attribution is fixed with stock/codec regressions. Open scheme/file-picker coverage, subframe permission ownership and installed verification. User-selected upload/download must not expose arbitrary local paths. |
| Identity and login | Cookies, cross-site redirects, popup login, HTTP/proxy auth, passkeys and client certificates work with correct consent and cancellation | Existing request UI; open actual authentication flows. Use supported external login where required. Verify PIN cancellation/cleanup, origin display, private profiles and no stale grants. Hardware passkey acceptance needs an authenticator. |
| Persistence and privacy | Correct cookies, localStorage, IndexedDB, cache/service-worker state, session restoration and private-mode isolation | Existing stores/tests; open reported retention-cookie failure and two-launch proof of the final runtime. Test clearing site data/sign-out across related origins, permission revocation, private disk absence and crash recovery. Never infer all storage from localStorage alone. |
| Tabs and popups | Real typing/clicking after opener closes; correct target reuse, focus, back/forward, beforeunload, private ownership and clean shutdown | Open final receiving review of ordinary/private/nested popup evidence and installed-runtime journeys. Preserve the baseline lifetime safeguard until the patched runtime is qualified. |
| Lifecycle and reliability | Idle tabs freeze without losing callbacks; audio/capture/download work stays alive; renderer failure recovers safely | Freeze fix has red/green, independent stock and final-source callback/retirement proof. Open final-package and provider/capture interactions. Retain shutdown and renderer failure controls. |
| Media | HTML audio/video, required codecs, MSE/EME, fullscreen, seek, subtitles, audio selection, media session, hardware acceleration and PiP where required | Existing local codec evidence; open target-site playback and final-package tests. Treat DRM/CDM approval, actual media keys, device routing and capture as separate checks. |
| Calls and devices | WebRTC audio/video, desktop capture, clipboard and geolocation with explicit origin-scoped consent and revocation | Open browser-capable build and real capture/cancel/revoke flows. Preserve operating-system permission gates. USB/Bluetooth/serial/MIDI need separate need/API assessment before exposure; do not grant them globally. |
| Everyday browser tools | Upload/download, safe filenames, progress/cancel/retry, find, zoom, selection, context menus, PDF/print, spellcheck, keyboard/IME, accessibility and touch | Much exists; open reported context-image failure, packaged PDF/print/spellcheck, accessibility/IME and actual input checks. Keep keyboard/assistive-device evidence distinct from offscreen fixtures. |
| Web platform compatibility | Service workers, fetch/WebSockets, storage quotas, WASM, canvas/WebGL and required modern APIs | Open small capability fixtures and actual target-site journeys against the pinned engine. Detect unsupported APIs honestly. A user-agent string change cannot supply a missing implementation. |
| Harmful sites and files | Clear origin/connection display, popup abuse controls, safe external protocols, download consent, reputation protection and OS attachment provenance | Open source/API assessment. Do not assume Chrome Safe Browsing or malware scanning is included. Verify Windows attachment markings/launch behavior and choose a supported reputation integration if needed. |
| Provider acceptance | Must-use sites load, sign in, retain sessions and perform their core action in the installed build | Open per-site matrix. Record exact URL, runtime identity, account-free result, account/DRM requirement, failure category and final result. A successful home page is insufficient. |

Chrome account sync, Chrome Web Store compatibility, a password vault, payments,
translation and extensions are separate product decisions unless a must-use service
establishes a concrete dependency. Notifications remain deliberately off.

## Execution order and ownership

1. HTTPS source change and native tests are preserved in `46c2261c`. The evidence
   drive is restored; the freeze change is integrated with receiving verification.
2. Continue runtime launch/packaging security review with Codex receiving review.
   Hemanth reauthorized Sol 6.1 subagents and selective Preflight Spawn/Loop use;
   use independent bounded scopes and twenty-minute checks for dispatched work.
   Preserve every failed receipt and the original worker's hosting-test edit.
3. Implement the smallest proven security gap, with a regression and negative
   controls. Keep each direct development slice to one objective. Codex owns review,
   integration, explicit-path commits and GitLab preservation.
4. Select/build a runtime configuration that retains required browser features,
   carries the popup fix and records security-patch provenance. Qualify features
   in the actual package rather than infer them from build flags.
5. Complete targeted browser journeys and installed-package restart/update checks.
   Then run the named provider matrix, using Hemanth only where account consent,
   an authenticator, paid entitlement or physical hardware is actually required.

Automated fixtures and agent-operated runtime checks handle most work. Human
testing is not a blanket prerequisite. Account/device checks remain explicitly
unverified until performed. No aggregate “all secure” or “all sites supported”
claim follows from passing component tests.

Download-opening checkpoint: native authority now comes from registered typed
engine completion, with existing historical rows, ephemeral private downloads,
single-use confirmation and revalidation before opening. Focused native tests
pass (10/0), metadata regressions pass (8/0), and stock Qt 6.11.1 download fixtures
pass (5/0, root exit 0), including private/history cancellation and observed
`Zone.Identifier` with `ZoneId=3`. This enforces opening policy; Chromium's existing
attachment marking was not absent. Compiled `IAttachmentExecute` integration has
not actually launched a file or verified Windows dialog ownership, scanner or
reputation outcomes. Null HWND/null process handle asynchronous Execute still
needs its message-pump/long-lived-window contract qualified. Custom-codec and
installed behavior, OS launch races and metadata-preserving edits remain open;
F5.2 and Phase 5 are not complete.

## Primary references (checked 2026-10-14)

- [Qt 6.11 security considerations](https://doc.qt.io/qt-6.11/qtwebengine-security.html)
- [Qt settings and security defaults](https://doc.qt.io/qt-6.11/qwebenginesettings.html)
- [Qt browser features, codecs and DRM](https://doc.qt.io/qt-6.11/qtwebengine-features.html)
- [Qt Chromium base and backported security versions](https://wiki.qt.io/QtWebEngine/ChromiumVersions)
- [Google OAuth browser policy](https://developers.google.com/identity/protocols/oauth2/policies#use-secure-browsers)

Qt's current table lists 6.11.1 with Chromium base 140.0.7339.264 and security fixes
through 148.0.7778.96, and 6.11.2 with the same base and security fixes through
151.0.7922.71. These are upstream release facts, not verification of Feria's custom
binaries. Verify the source/security metadata and actual package before adopting it.
