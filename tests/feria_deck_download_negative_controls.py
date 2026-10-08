#!/usr/bin/env python3
"""Mutate one production owner fence, require failure, restore exact bytes.
Modeled JS checks only; this is not WebEngine runtime evidence.
"""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=root/'qml/feria/FeriaSessionDeck.qml'
out=Path(sys.argv[1]) if len(sys.argv)>1 else root.parent/'evidence/deck-controls'
out.mkdir(parents=True,exist_ok=True)
original=source.read_bytes()
cases=[
 ('private-transfer',b'if (privateDownloads.hasTransferDependencies) return',b'if (false) return','feria_download_profile_lifetime_test.mjs'),
 ('private-file-detach',b'if (!privateDownloads.detachCompletedProfile()) return',b'if (false) return','feria_download_profile_lifetime_test.mjs'),
 ('page-save-close',b'if (pageSavePending(entry)) { message(id, "The page is still being saved. Cancel or finish its download first."); return false }',b'if (false) { return false }','feria_page_save_lifetime_test.mjs'),
]
results=[]
for name,needle,replacement,test in cases:
 command=['node',str(root/'tests'/test)]
 green=subprocess.run(command,capture_output=True,text=True)
 (out/(name+'-before.log')).write_text(green.stdout+green.stderr)
 if green.returncode:raise SystemExit('Original prerequisite failed: '+name)
 assert needle in original,name
 # page-close phrase occurs in other navigation functions too; deliberate
 # mutation removes the same guard at every host path, including close.
 mutant=original.replace(needle,replacement)
 try:
  source.write_bytes(mutant)
  red=subprocess.run(command,capture_output=True,text=True)
  (out/(name+'-mutant.log')).write_text(red.stdout+red.stderr)
 finally:source.write_bytes(original)
 restored=subprocess.run(command,capture_output=True,text=True)
 (out/(name+'-restored.log')).write_text(restored.stdout+restored.stderr)
 results.append(dict(name=name,baseline='62057bfbdaa2305a05d2d3fb02b870bc9e826638',
   kind='modeled production deck functions',source=str(source.relative_to(root)),
   candidate_sha256=hashlib.sha256(original).hexdigest(),mutant_sha256=hashlib.sha256(mutant).hexdigest(),
   before=green.returncode,mutant=red.returncode,restored=restored.returncode))
 if not red.returncode or restored.returncode or source.read_bytes()!=original:
  raise SystemExit('Negative control failed: '+name)
(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS 3 deck fence controls: original pass, mutant fail, exact restore pass')
