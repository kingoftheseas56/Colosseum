"""Compare native converter lifecycle to the committed exact M916 fixture."""
import json, pathlib, subprocess, sys
result = subprocess.run([sys.argv[1]], check=True, capture_output=True, text=True, timeout=30)
actual = next(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{'))
expected = json.loads(pathlib.Path(sys.argv[2]).read_text())
assert actual == expected, (actual, expected)
print(result.stdout, end='')
