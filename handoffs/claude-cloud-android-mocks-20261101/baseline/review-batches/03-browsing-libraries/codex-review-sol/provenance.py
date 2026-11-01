import json,hashlib,subprocess
from pathlib import Path
b=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries'); root=b.parents[1]
j=json.loads((b/'evidence/INPUT-VERSIONS-START.json').read_text()); found=[]
def walk(o):
 if isinstance(o,dict):
  if 'path' in o and 'sha256' in o: found.append(o)
  for v in o.values():walk(v)
 elif isinstance(o,list):
  for v in o:walk(v)
walk(j)
out={'snapshotEntries':len(found),'hashMismatches':[],'gitSources':[]}
for x in found:
 p=Path(x['path'])
 if hashlib.sha256(p.read_bytes()).hexdigest()!=x['sha256']:out['hashMismatches'].append(str(p))
files=sorted(set(Path(x['path']) for s in j['sources'] for x in s['matches'] if '\\reference\\desktop\\' in x['path']))
for p in files:
 rel=p.relative_to(root/'reference/desktop').as_posix()
 r=subprocess.run(['git','show','a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41:'+rel],capture_output=True,cwd=r'C:\Users\Suprabha\Desktop\Brotherhood\Colosseum')
 data=p.read_bytes(); exact=r.returncode==0 and r.stdout==data
 normalized=False
 if not exact and r.returncode==0:
  try: normalized=r.stdout.decode('utf-8').replace('\r\n','\n')==data.decode('utf-8').replace('\r\n','\n')
  except UnicodeDecodeError:pass
 out['gitSources'].append({'file':rel,'exact':exact,'crlfOnly':normalized,'gitAvailable':r.returncode==0})
(b/'codex-review-sol/provenance.json').write_text(json.dumps(out,indent=2))
print(json.dumps({'snapshotEntries':out['snapshotEntries'],'hashMismatches':out['hashMismatches'],'gitSources':len(files),'gitMismatches':[x for x in out['gitSources'] if not x['exact'] and not x['crlfOnly']]}))
