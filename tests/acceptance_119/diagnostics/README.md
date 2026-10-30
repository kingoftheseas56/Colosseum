# Atlas route discovery only

`atlas_route_only.json` is a diagnostic subset, intentionally absent from the acceptance manifest. It cannot replace `ATLAS-FOUR-SEA` or qualify atlas zoom, occlusion, focus restoration, or all viewport sizes. Original step labels are retained for comparison, with intentional gaps. All actual identity/image/geometry/navigation assertions and focus assertions remain; only zoom actions and zoom/retention claims are removed.

The full case remains failed for independently observed focus restoration, permanent taskbar/zoom occlusion at1280x720, and baseline map cropping at1364x1024. `--keep-going` keeps those focus failures red while permitting discovery of later sea routes. It must not convert failure into a pass. If an action enters an unexpected overlay or a sea gate does not reach its requested route, stop dependent interpretation: later missing-item failures are cascades, not independent sea findings.

## Portable Linux normal-AppRun command

Run in an actual desktop terminal with an already extracted reviewed package. Assign absolute paths to the shell variables below; choose a new output directory for each owned run. The helper creates isolated tagged profiles and a unique native Lanista pipe, validates source/package QML compatibility, and checks normal process cleanup. It does not use a public/default pipe or a QML override.

```sh
SOURCE_ROOT=/absolute/path/to/reviewed/source
PACKAGE_ROOT=/absolute/path/to/extracted/Colosseum-package
OUTPUT_ROOT=/absolute/path/to/new/atlas-route-only-evidence
python3 "$SOURCE_ROOT/tests/acceptance_119/package_smoke.py" \
  --package-root "$PACKAGE_ROOT" \
  --source-root "$SOURCE_ROOT" \
  --output-root "$OUTPUT_ROOT" \
  --scenario "$SOURCE_ROOT/tests/acceptance_119/diagnostics/atlas_route_only.json" \
  --width 1280 --height 720 --keep-going \
  --timeout-seconds 900
```

Use the same source revision/observability files as the package. Retain result.json, protocol.json, raw stdout/stderr, source/binary identity, and actual screenshots. A known focus failure is still overall FAIL. A successful subset would establish only the actions actually reached, never erase the original case's failures. This helper is Linux-only; no Windows result is implied.
