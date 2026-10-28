#!/usr/bin/env python3
"""Acquire only the exact official oracle and regenerate its inert module index."""
import hashlib
import json
import os
import pathlib
import subprocess
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1]
pin = json.loads((root / 'DEPENDENCIES.json').read_text())['oracle']
target = root / 'reference/source-pack/oracle/stremio-service-v4.21.1-server-bundle/server.js'
with urllib.request.urlopen(pin['url'], timeout=60) as response:
    data = response.read()
assert len(data) == pin['bytes'] and hashlib.sha256(data).hexdigest() == pin['sha256'], 'Oracle identity mismatch'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(data)
script = r'''
const fs=require('node:fs');
const {extractModules}=require(process.argv[1]);
const extracted=extractModules(process.argv[2]);
const modules=extracted.modules.map(m=>({id:m.id,start:m.start,end:m.end,
  start_byte:m.start,end_byte:m.end,sha256:m.sha256,
  dependencies:[...new Set([...m.source.matchAll(/__webpack_require__\((\d+)\)/g)].map(x=>Number(x[1])))].sort((a,b)=>a-b)}));
fs.writeFileSync(process.argv[3],JSON.stringify({oracleSha256:extracted.sha256,
  oracle_sha256:extracted.sha256,provenance:'Regenerated from verified official bytes; not the missing original source pack',modules},null,2)+'\n');
console.log('Indexed '+modules.length+' exact source modules; no global server entry executed.');
'''
subprocess.run([os.environ.get('NODE_BIN', 'node'), '-e', script,
                str(root / 'tools/server_lab/oracle/module_loader.cjs'), str(target),
                str(root / 'reference/MODULE-INDEX.json')], check=True)
print('Verified oracle:', target, pin['sha256'])
