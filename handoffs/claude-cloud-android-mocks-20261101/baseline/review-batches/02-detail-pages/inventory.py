from pathlib import Path
import hashlib,json,datetime,sys
batch=Path(__file__).parent
root=batch.parent.parent
inputs=json.loads((batch/'INPUTS.json').read_text(encoding='utf-8-sig'))
records=[]
def add(path,role):
    if not path.is_file(): records.append({'path':str(path),'role':role,'exists':False}); return
    data=path.read_bytes(); st=path.stat()
    records.append({'path':str(path),'role':role,'exists':True,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'mtimeNs':st.st_mtime_ns,'lines':data.count(b'\n')+1})
for job in inputs['jobs']:
    out=Path(job['output']); add(Path(job['prompt']),'worker-brief')
    for p in sorted(out.iterdir()):
        if p.is_file(): add(p,'worker-input')
    for name in job['sources']:
        add(root/'reference'/'desktop'/'qml'/name,'approved-qml')
shared=['Theme.qml','Main.qml','Glass.qml','BackAction.qml','LibraryButton.qml','collections/AddToCollectionsButton.qml','collections/AddToCollectionsPopup.qml','collections/CollectionIcon.qml','TitlePageBar.qml','PlayerIcon.qml','TheatreFacts.js','CastRow.qml','MoreLikeThisRow.qml']
for name in shared: add(root/'reference'/'desktop'/'qml'/name,'shared-approved-source')
for p in sorted((root/'context').iterdir()):
    if p.is_file(): add(p,'context')
result={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'declaredSourcePins':inputs['references'],'files':records,'note':'Source pins declared by frozen INPUTS.json; individual inspected bytes independently SHA256-bound.'}
target=batch/('input-versions-end.json' if '--end' in sys.argv else 'input-versions-start.json')
target.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({'written':str(target),'files':len(records),'missing':[r['path'] for r in records if not r['exists']],'sources':[{'file':Path(r['path']).name,'lines':r.get('lines')} for r in records if 'source' in r['role'] or 'qml' in r['role']]}))
