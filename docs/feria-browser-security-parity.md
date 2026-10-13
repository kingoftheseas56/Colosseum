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

## Required capabilities and acceptance work

“Open” below means implementation or proof remains; it does not imply every feature
is absent. Each slice must first inspect its existing owner and existing tests.

| Area | Required behavior | Remaining proof / implementation route |
| --- | --- | --- |
| Engine and updates | Supported Chromium security fixes, reproducible Qt patches, compatible DLL/helper/resources/plugins; timely signed upgrades | Open. Record native engine/base Chromium/security-patch versions and source provenance separately. Verify exact loaded modules and signed package/update integrity. Review startup flags, renderer sandbox and isolation in the shipped runtime. |
| Transport and origin security | HTTPS defaults; reject invalid, expired and wrong-host certificates; enforce same-origin, CORS, CSP and mixed-content restrictions | Policy and bounded stock/codec TLS/origin/mixed-fetch fixtures pass; connection disclosure has component/native-host proof above. Open full-shell/installed checks, remaining redirect/downgrade cases, custom schemes and complete local-file/permission review. |
| Browser/native boundary | Remote pages cannot call privileged native stores, file/process APIs or forge application permission UI | Open targeted bridge/scheme review and hostile-page probes. Review injected scripts, subframes, new windows and navigation replacement. User-selected upload/download must not expose arbitrary local paths. |
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
2. Continue runtime launch/packaging security review directly. Hemanth stopped the
   Preflight loop and scheduled checks; already-sent replies may still arrive.
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
