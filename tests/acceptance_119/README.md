# Colosseum 1.1.9 non-Feria acceptance

These scripts extend **native Lanista**. They are acceptance tests, not a release declaration. Start from alternate-fork pin `a6104f7a8d9aab9766e71f031b7908beaeca85ad`; the candidate branch adds scenarios and read-only QML observations. See `manifest.json`, `coverage.json` and the delivery evidence for executed results.

## Prerequisites

- Python 3.10+, Pillow for real screenshot decoding, and FFmpeg/ffprobe for playable fixtures.
- The matching 1.1.9 application, its QML/resources, and compiled native `lanista`.
- A graphical desktop for assembled-app tests. Component/offscreen results are separate.
- A reviewed build identity JSON: `source_sha`, `version`, `exe_sha256`, `qml_tree_sha256`, and recorded Qt/runtime/build provenance. `source_sha` identifies the fixed baseline; record candidate commit and native/QML differences alongside it. An observed hash alone does not establish provenance.

Never use the daily app, default pipe, personal library, account credentials, production signing key or public update feed as a fixture. The runner clears inherited Colosseum test overrides and creates owned profiles/pipes. A fresh output directory is required. No cleanup action targets unrelated processes or profiles.

## Build and fixtures

Use the repository's supported Release configure with Qt 6.11.1 and MpvQt. Build `colosseum`, `lanista` and selected native tests; refresh `colosseum_runtime_ready` after final QML changes. The fresh Linux package helper uses the existing assembler and dependency checker; see `package_linux.sh --help` for its parameters.

Generate real fixtures into a new empty directory:

    python tests/acceptance_119/fixtures.py --root /absolute/disposable/fixtures
    python -m unittest discover -s tests/acceptance_119 -p 'test_*.py' -v

Inspect the candidate without claiming it qualified:

    python tests/acceptance_119/run.py --inspect --exe /package/usr/bin/colosseum --qml /package/qml/Main.qml

Have the build owner record the inspected values in the reviewed identity JSON after independently verifying the source/build relationship. Do not simply regenerate the expected identity whenever a mismatch occurs.

## Run selected or complete journeys

Paths are examples, not workstation defaults. Set `--qml` to the actual package's QML entry; use the source root only for the checked-in scenarios/checkpoints. The package root must be writable for native Lanista artifacts.

    python tests/acceptance_119/run.py --source-root . --manifest tests/acceptance_119/manifest.json --package-root /package --lanista /package/usr/bin/lanista --exe /package/usr/bin/colosseum --qml /package/qml/Main.qml --fixture-root /absolute/disposable/fixtures --identity /evidence/candidate-identity.json --output-root /evidence/fresh-run --full

For one case replace `--full` with `--case VAULT-FILM`. For a restart-only phase, use a new output directory plus `--phase restart --resume /evidence/fresh-run/run.json --case READER-COMIC-RESTART`. The predecessor must be an owned successful preparation phase with the same executable and QML identity. No reseeding on restart.

`run.json` is updated after each case. It records protocol replies, assertions/timings, screenshots, executable/QML hashes, source baseline, owned pipe/tag/PID, launch environment and terminal status. Failed cases remain failed; a nonzero native process exit, crash, forced termination, unreadable screenshot, missing reply or missing prerequisite cannot become a pass. A negative-control PASS means the specified wrong assertion was detected with all other checks intact; it is not a product-behavior pass.

The suite intentionally includes explicit blocked receiving prerequisites. A full report containing BLOCKED/NOT RUN/FAIL is not an overall qualified release. A passing selected subset does not imply unselected cases passed.

## Layer-specific checks

- `account_checks.py --help`: focused native tracker/Watch Party tests and isolated synthetic service integration. The four desktop credential-vault restart trials require Windows; Linux service tests do not replace them.
- `updater_checks.py --help`: production-signature/public-key/static contracts and actual native trust/discovery/download/service harnesses. Tests use fixtures; no production feed mutation.
- `windows_receiving.md`: short receiving commands and manual 1.1.8 -> 1.1.9 migration procedure. Installer hash/size and compiled-versus-packaged source identities are distinct.
- Normal fresh-extraction **AppRun with no QML override** must be checked separately: Lanista `session run` explicitly supplies QML and therefore bypasses the packaged startup manifest gate. A version-only launch is not graphical qualification.

## Normal packaged AppRun diagnostics

From a real Linux desktop terminal, run the extracted artifact without a QML override:

```sh
python3 tests/acceptance_119/package_smoke.py \
  --package-root "$FRESH_PACKAGE" --source-root "$SOURCE_ROOT" \
  --output-root "$NEW_OUTPUT" \
  --scenario tests/lanista_scenarios/acceptance_119/atlas_four_sea_journey.json \
  --width 1280 --height 720 --keep-going
```

Use an existing selected scenario path for `--scenario`; the output directory must be new. `--keep-going` is optional and forwards directly to native Lanista. It gathers later step diagnostics after an assertion fails; native exit 1 remains overall **FAIL**, with normal-quit, screenshot, identity and process-cleanup checks unchanged. Later steps that depend on a failed transition may also fail. Omit the flag for the default stop-on-first-failure behavior.

## Deliberate boundaries

Seeded Recent entries and Vault roots exercise the real reader/scanner and UI, but do not prove native FileDialog/FolderDialog operation. Missing-file guards are not friendly error-message qualification unless that message is asserted. Restart-triggered refresh is not live-watcher verification. Local images/player fixtures do not qualify live catalogues, streaming providers, audible output or DRM. Native component tests and modeled runner tests are labeled separately from assembled-app journeys.

Automatic Windows updating remains BLOCKED without an authentic higher-version signed artifact and supported private staging route. Same-version checks, test-key fixtures and modeled progress do not prove installation/restart. No website, stable feed, public release or production branch is changed by this suite.
