# Site identity checks

Build `feria_foundation_policy_test` and `feria_foundation_runtime` from
`tests/feria_foundation_build` against the current native sources. Older runners
without `FeriaBrowserPolicy.displayOrigin()` cannot exercise this component.

Run the policy target and the runtime with `-input tests/feria_site_identity_probe`.
Set the Qt DLL, plugin, QML, WebEngine helper/resource/locale paths to the intended
compatible runtime. Use a fresh `FERIA_FIXTURE_ROOT` and
`FERIA_SESSION_EVIDENCE_DIR`. The fixture creates its local HTTP server when
`FERIA_FIXTURE_PORT=0`.

For the Windows rendered/real-input checks, set `QT_QPA_PLATFORM=windows` and
`FERIA_FIXTURE_FOREGROUND=1`. The latter enables the existing test runner's
first-exposure activation of its own windows. It does not relax test focus
assertions. Avoid overlapping browser tests; these checks briefly show windows.
Capture the native process exit as well as the QtTest log. Inspect the generated
narrow, ordinary-host and compatibility-popup screenshots.

The suite checks sanitized connection display, usable identity space in a
320-pixel host, nonoverlap with remote content, navigation updates and the legacy
popup path using an actual clicked POST popup. Related existing hosting checks:
`FeriaHosting::test_nativeHostsAreExactAndDisposable` and
`FeriaHosting::test_siteDialogCanonicalGeometryAndOriginalPost`.

These checks do not qualify the full installed shell, provider authentication,
renderer sandbox, physical keyboard or every popup lifecycle journey.
