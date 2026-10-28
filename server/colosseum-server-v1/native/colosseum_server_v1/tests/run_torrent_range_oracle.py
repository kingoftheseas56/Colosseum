"""Compare the exact pinned M176's first range with the production C++ policy."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--oracle', required=True)
p.add_argument('--index', required=True)
p.add_argument('--executable', required=True)
p.add_argument('--node', default='node')
a = p.parse_args()
bundle = Path(a.oracle).read_bytes()
assert hashlib.sha256(bundle).hexdigest() == '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f'
entry = next(m for m in json.loads(Path(a.index).read_text())['modules'] if m['id'] == 176)
module = bundle[entry['start_byte']:entry['end_byte']]
assert hashlib.sha256(module).hexdigest() == entry['sha256']
rng = random.Random(176)
parts = ['', '0', '1', '2', '15', '16', '99', '100000000000000000000000000000', 'junk', '2junk', '+4', ' 3', '0x4', '4.5']
cases = [[n, h] for n in (0, 1, 16, 65537) for h in ('', 'bytes=0-0', 'bytes=2-', 'bytes=-3', 'bytes=1-2,4-5', 'bytes=100-200,0-0')]
for _ in range(500):
    cases.append([rng.choice([0,1,16,65537]), rng.choice(['bytes=', 'items=', '=', '']) + ','.join(rng.choice(parts)+'-'+rng.choice(parts) for _ in range(rng.randint(1,3)))])
js = 'const module={exports:{}};(' + module.decode() + ')(module,module.exports,()=>{throw Error("unexpected dependency")});\n'
js += 'const cases='+json.dumps(cases)+';for(const [n,h] of cases){const r=module.exports(n,h)[0];console.log(r?`${r.start},${r.end}`:"null");}'
expected = subprocess.run([a.node, '-e', js], check=True, capture_output=True, text=True).stdout.splitlines()
actual = subprocess.run([a.executable, '--probe'], input=''.join(str(n)+'\t'+h+'\n' for n,h in cases), check=True, capture_output=True, text=True).stdout.splitlines()
assert len(expected) == len(actual) == len(cases)
for case, e, got in zip(cases, expected, actual):
    assert e == got, (case, e, got)
print(f'PASS: {len(cases)} exact M176 differential first-range cases; bundle SHA256 verified')
