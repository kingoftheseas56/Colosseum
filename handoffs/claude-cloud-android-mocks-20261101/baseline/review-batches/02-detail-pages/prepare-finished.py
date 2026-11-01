from pathlib import Path
import json,hashlib
base=Path(__file__).parent; root=base.parent.parent
inputs=json.loads((base/'INPUTS.json').read_text(encoding='utf-8-sig'))
finished=base/'finished-mocks'; finished.mkdir(exist_ok=True)
manifest=[]
for job in inputs['jobs']:
    source=Path(job['output']); dest=finished/job['task']; dest.mkdir(exist_ok=True)
    for name in job['pages']+['index.html','style.css','app.js']:
        p=source/name
        if not p.exists(): continue
        q=dest/name
        if q.exists(): raise SystemExit('Refusing to overwrite existing finished file: '+str(q))
        data=p.read_text(encoding='utf-8-sig').replace('../../reference/','../../../../reference/')
        q.write_text(data,encoding='utf-8')
        manifest.append({'original':str(p),'originalSha256':hashlib.sha256(p.read_bytes()).hexdigest(),'finished':str(q),'initialSha256':hashlib.sha256(q.read_bytes()).hexdigest()})
(finished/'shared').mkdir(exist_ok=True)
(base/'finished-lineage.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({'copied':len(manifest),'finishedRoot':str(finished),'originalsUntouched':True}))
