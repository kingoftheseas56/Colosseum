# Feria security fixtures

## Windows renderer token observation

`python observe_windows_tokens.py <new-output-directory> <test-driver-command...>`
observes foundation/helper descendants of that exact driver. Use a pinned runtime,
disposable profiles and a driver that records the native root exit, actual loaded
module/helper identities and cleanup. The observer does not launch a second test,
inject code, change sandbox flags or kill processes. It requires Windows and psutil.

The inspector obtains process creation FILETIME, image path and primary token
through the same process handle, cross-checks enumeration identity, and records
integrity RID, restricting-SID count, AppContainer status and job membership.
It checks the restricting-SID count against `IsTokenRestricted`. The ordinary
observer/browser provide a comparison: `TokenHasRestrictions` can be true even
without restricting SIDs, so it is not sufficient sandbox evidence by itself.

The observer's exit follows its driver's exit; it is not a qualification verdict.
Receiving review must require successful native test/root exits, matching binary
identities, nonempty renderer coverage, zero observation errors, stable source
and child cleanup. Inspect samples from actual page execution and each renderer's
final samples. A missing sample/API error is unknown, not proof of isolation.
Retain early low-integrity samples alongside later untrusted-integrity samples.

This proves only sampled primary-token properties. Membership in some job does
not establish Chromium job limits. Thread impersonation, desktop isolation,
mitigations, broker policy, installed builds and provider sessions remain separate.

## Built-application native boundary

`run_boundaries.py --exe <built-colosseum.exe> --out <fresh-directory>` loads a
fixture scene through the real executable's native context. Supply compatible Qt
DLL/plugin/QML/helper paths in the environment. It checks remote globals, empty
channel registration, local-file/qrc fetch denial, same/cross-origin frames and
replacement navigation. Run again with a different directory and `--expose-control`
to attach only a harmless fixture WebChannel object: detection and native exit 7
are required. This never registers production privileged objects remotely.

Inspect the recorded executable, source and loaded-module hashes against the
intended build. The driver records actual loaded modules; it does not qualify
their provenance, renderer sandbox, the installed package or the full shell.

## Focused real-wrapper checks

Build `feria_foundation_runtime` from `tests/feria_foundation_build` against the
current native policy. Run one selected case with `-input tests/feria_security_probe`:

- `FeriaStreamingCapabilities::test_inventory` records `canPlayType`, MSE and
  temporary-session EME capabilities at the secure loopback fixture origin.
  Clear Key/WebM access and MediaKeys creation are positive controls. Widevine
  WebM/MP4 outcomes are observations: PASS means the inventory and controls
  completed, not that Widevine or proprietary codecs were accepted. Inspect the
  `STREAMING_CAPABILITIES` JSON rows separately. No license request, session,
  encrypted playback, robustness tier or CDM binary provenance is established.
  The probe uses default CDM discovery, with no copied CDM or provider login.
- `FeriaPublicDrm::test_encryptedPlayback` is opt-in with
  `FERIA_SERVICE_URL=https://bitmovin.com/demos/drm`. It uses the public demo's
  normal licensing route in a disposable profile. A document-start observer
  records EME system names, session/message counts and key statuses, never key
  IDs, license bodies or session IDs. It requires encrypted events and current
  usable Widevine session keys belonging to the same video/MediaKeys identity,
  additional decoded frames after seeking to 12 seconds, and pause. A fixed native
  document epoch binds baseline, seek, post-seek samples and pause; replacement
  after binding explicitly fails. Only initial read-only readiness polls retry
  document-retired callbacks; actions remain strict. Post-seek repeats origin,
  Widevine, encrypted-event/session-message/current-usable-key and readiness checks. Diagnostics enter
  orderly teardown even if their read fails. The observer
  wraps native EME methods but preserves their normal results; it is test-only.
  A pass applies to this public demo and runtime, not subscription providers,
  installed packages, hardware DRM or CDM redistribution. Record CDM module
  identity separately from the Qt module/helper manifest.
- `FeriaObservationOrigin::test_pageCannotAttributePlaybackToAnotherOrigin` uses
  a real page prototype setter to forge the production probe's URL. It requires
  cross-origin rejection, recovery and same-origin canonical-route acceptance.
- `FeriaPopupBoundaries::test_clickOpenedPopupHasNoNativeBridge` uses a real click
  and POST popup. Use the Windows platform and `FERIA_FIXTURE_FOREGROUND=1` for the
  existing first-exposure fixture activation; its focus assertion must remain.
- `FeriaFramePermissions::test_removedFrameCannotReceiveLateGrant` is an open
  security regression: it currently FAILS on both accepted engine pairings.
  A real click requests Local Fonts access from a delegated cross-origin iframe;
  the test removes that frame, waits for the native frame tree to become empty,
  then answers the old application prompt. The origin must not receive a stored
  grant. Do not skip or invert this assertion to qualify the current runtime.
- `FeriaFramePermissions::test_replacedFrameCannotReceiveLateGrant` keeps the
  exact iframe URL while either reloading its document or replacing its element.
  Fresh child reports establish replacement before answering the old prompt.
  Both rows currently FAIL on stock (2 passed, 2 failed, native exit 2); this
  prevents a URL-only guard from being mistaken for document lifetime protection.
- `FeriaFramePermissions::test_liveFrameGrantAndExplicitReset` is its positive
  control: a live frame receives actual font entries after consent, profile reset
  returns to Ask, and a fresh request can be denied. Only counts are inspected;
  font names and contents are never logged. Chromium may resolve denial with an
  empty list, so the test checks the permission state and absence of returned data.
  All frame cases require the Windows platform, foreground opt-in above, and a
  runner rebuilt with the `/services-permission-*` HTTP routes. Profiles are
  disposable and unique per case.

Set `FERIA_FIXTURE_PORT=0`, a disposable `FERIA_FIXTURE_ROOT` and
`FERIA_SESSION_EVIDENCE_DIR`, and the intended runtime's paths. Record the direct
native exit, source hashes, loaded module/helper identities and child cleanup.
Do not overlap browser runs or substitute the old runner for one rebuilt with
`sameWebOrigin()`. The tests inspect emitted observations, not saved account rows.
The standalone build also exposes `feria_wrapper_qml_test`; its focused
`FeriaWrapperStore::test_pending_restore:QtWebEngine` case in
`tests/feria_wrapper_probe` preserves deterministic resume/store assertions with
inline HTML at the sample's origin. It does not contact the streaming service or
qualify the separate WebView2 row.

## TLS and origin fixture

Run `run_transport.py` with Python 3.11+, `cryptography` and `psutil`, an accepted
candidate manifest and a fresh output directory:

```powershell
python tests/feria_security_probe/run_transport.py `
  --manifest <candidate-manifest.json> --variant codec --out <new-evidence-directory>
```

Repeat with `--variant stock` and another output directory for the stock pairing.
The manifest uses the existing popup-candidate contract: `pairings`, each with
`variant`, `runtimeRoot` and `selectedBinaries` containing `path` and `sha256`.
The selected runtime must contain the accepted `feria_foundation_runtime` runner.
The script records its hash as well as checking the manifest's selected files;
receiving review must match the recorded runner to the accepted build. It refuses
to launch while an existing foundation runner or WebEngine helper is observed.

Five ephemeral HTTPS listeners bind only to 127.0.0.1. A generated CA is trusted
only by the fixture's off-the-record WebEngine profile, through
`WebEngineProfilePrototype.additionalTrustedCertificateFiles`. The system trust
store is never modified. Certificate keys, profiles and evidence stay in the output
directory and must not be committed or distributed with the app.

The generated QML imports the current repository's real `FeriaQtWebEngine` and its
request controller. It proves successful trusted TLS before asserting distinct
expired, wrong-host and untrusted failures, including an HTTPS redirect to an
expired endpoint. It checks CORS denial with a successful CORS control, cross-origin
iframe denial, CSP enforcement with an inline-script control, local-file fetch
denial and absence of selected native objects in the remote JavaScript context.
An additional plain HTTP listener provides a positive control at
`insecure.fixture.test`. A test-only Chromium host-resolver rule maps that name to
the local listener, avoiding the trustworthy-loopback exception. The HTTPS page
must fail its HTTP fetch with an actual mixed-content diagnostic. The production
startup guard intentionally rejects this diagnostic resolver switch. No TLS,
origin or sandbox bypass is enabled.

It records the native root exit directly with file-backed stdout/stderr, source
hashes, observed module/helper paths and remaining sampled child processes. A
timeout is an incomplete run, not success: inspect its recorded PID and do not
start another browser test until ownership/activity is resolved. The runner does
not kill an unknown or timed-out process.

Scope limits: this is a real-engine fixture, not the installed app, a full bridge
audit, exhaustive mixed-content coverage, sandbox-token proof or provider acceptance.
Offscreen graphics warnings do not establish working GPU playback. The wrapper's
existing fallback profile currently emits Qt's profile-construction deprecation
warning; retain it in the raw log rather than hiding it.
