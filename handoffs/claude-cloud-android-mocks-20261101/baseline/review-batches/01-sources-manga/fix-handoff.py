from pathlib import Path
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\01-sources-manga')
p=b/'finished-mocks/04-manga-series/app.js'
s=p.read_text(encoding='utf-8')
assert '${n===3?"Continue · p. 78":""}' in s
s=s.replace('${n===3?"Continue · p. 78":""}','')
s=s.replace(') ? local fixture signal.',') — local fixture signal.')
p.write_text(s,encoding='utf-8')
for rel in ['build-indexes.py','finished-mocks/compare.html']:
 p=b/rel;s=p.read_text(encoding='utf-8')
 s=s.replace('selected=Math.max(0,Math.min(8,Number(q.get("page"))||0))','selected=flowPair?7:Math.max(0,Math.min(8,Number(q.get("page"))||0))')
 s=s.replace('a.set("fontScale",z)','a.set("scale",z)')
 s=s.replace('"search-error","metadata","resolving"','"search-error","metadata","metadata-zero","resolving"')
 s=s.replace('job.startsWith("04")?["default","expanded","selected","language-open","page-open","long","disabled","loading","empty"]','job.startsWith("04")?(pages[selected].file.includes("chapters")?["default","expanded","language-open","page-open","long","disabled","loading","empty","error","saved-unavailable"]:["default","expanded","selected","long"])')
 s=s.replace('["long","disabled","loading","empty"].includes(state)','["long","disabled","loading","empty","error","saved-unavailable"].includes(state)')
 p.write_text(s,encoding='utf-8')
print('Patched source-only volume caption and comparison controls.')
