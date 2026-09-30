#!/usr/bin/env bash
set -euo pipefail
: "${SOURCE:?}" "${BUILD:?}" "${EVIDENCE:?}"
exec > >(tee "$EVIDENCE/tests.log") 2>&1
cd "$SOURCE"
python -m unittest -v tests.test_linux_runtime_dependency_gate \
  tests.test_linux_sqlite_plugin_staging_contract tests.test_linux_ctest_platform_contract
python scripts/linux_runtime_dependency_gate.py
ctest --test-dir "$BUILD" --show-only=json-v1 > "$EVIDENCE/ctest-inventory.json"
python - "$EVIDENCE/ctest-inventory.json" <<'PY'
import json, sys
tests = json.load(open(sys.argv[1]))['tests']
assert tests, 'empty CTest inventory'
names = {test['name'] for test in tests}
assert {'colosseum.qml', 'colosseum.catalog_vault_client_harness',
        'colosseum.qttest.journey_play_video'} <= names, 'required qualification tests absent'
x11 = []
canary = False
for test in tests:
    props = {p['name']: p['value'] for p in test.get('properties', [])}
    labels = {str(v).lower() for v in props.get('LABELS', [])}
    cmd = test.get('command', [])
    if 'windows' not in labels:
        assert cmd, test['name']
        assert 'powershell' not in str(cmd[0]).lower(), test['name']
    if 'requires-x11' in labels:
        x11.append(test['name'])
    if test['name'] == 'colosseum.selftest.red_canary':
        canary = props.get('WILL_FAIL') is True
assert x11 == ['colosseum.qttest.journey_play_video'], x11
assert canary, 'red canary WILL_FAIL missing'
PY
# Run both groups even if one fails; preserve their individual exit statuses.
set +e
QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
  ctest --test-dir "$BUILD" -LE 'windows|requires-x11' --output-on-failure --parallel 1
neutral=$?
QT_QPA_PLATFORM=xcb QT_FORCE_STDERR_LOGGING=1 LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a ctest --test-dir "$BUILD" -L requires-x11 -LE windows --output-on-failure --parallel 1
x11=$?
set -e
printf 'neutral_ctest_exit=%s\nx11_ctest_exit=%s\n' "$neutral" "$x11" > "$EVIDENCE/ctest-status.txt"
test "$neutral:$x11" = 0:0
