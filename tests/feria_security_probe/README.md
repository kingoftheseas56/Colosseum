# Feria security fixtures

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

- `FeriaObservationOrigin::test_pageCannotAttributePlaybackToAnotherOrigin` uses
  a real page prototype setter to forge the production probe's URL. It requires
  cross-origin rejection, recovery and same-origin canonical-route acceptance.
- `FeriaPopupBoundaries::test_clickOpenedPopupHasNoNativeBridge` uses a real click
  and POST popup. Use the Windows platform and `FERIA_FIXTURE_FOREGROUND=1` for the
  existing first-exposure fixture activation; its focus assertion must remain.

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
