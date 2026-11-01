from pathlib import Path
import json,hashlib,re,datetime
from urllib.parse import urlsplit,unquote
from html.parser import HTMLParser
B=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\03-browsing-libraries')
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def walk(v):
 if isinstance(v,dict):
  if 'path' in v and 'sha256' in v:yield v
  for x in v.values():yield from walk(x)
 elif isinstance(v,list):
  for x in v:yield from walk(x)
tracked={x['path']:x for x in walk(read(B/'evidence/INPUT-VERSIONS-START.json'))}
input_results=[{'path':p,'exists':Path(p).is_file(),'unchanged':Path(p).is_file() and sha(Path(p))==x['sha256']} for p,x in tracked.items()]
snap=read(B/'evidence/FINAL-CAPTURE-INPUTS.json')
capture_results=[{'path':x['path'],'matches':sha(B/x['path'])==x['sha256']} for x in snap['files']]
class Links(HTMLParser):
 def __init__(self):super().__init__();self.urls=[]
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ('src','href') and v:self.urls.append(v)
links=[];missing=[]
for p in [*B.glob('*.html'),*(B/'finished-mocks').rglob('*.html'),*(B/'finished-mocks').rglob('*.css')]:
 if p.suffix=='.html':h=Links();h.feed(p.read_text(encoding='utf-8-sig'));urls=h.urls
 else:urls=re.findall(r'url\([\s\"\x27]*([^\"\x27\)\s]+)',p.read_text(encoding='utf-8-sig'))
 for u in urls:
  q=urlsplit(u)
  if q.scheme or not q.path:continue
  target=(p.parent/unquote(q.path)).resolve();entry={'source':str(p.relative_to(B)),'url':u,'exists':target.exists()}
  links.append(entry)
  if not entry['exists'] and target!=B/'REPORT.json':missing.append(entry)
qa=[read(p) for p in (B/'evidence').glob('qa-*.json') if 'checks' in read(p)]
matrix=read(B/'evidence/finished-matrix.json')
stress=[read(p) for p in (B/'evidence/after').glob('*/stress-*.json')]
font_failures=[{'path':x.get('path'),'vars':x.get('stats',{}).get('fontScaleVariables')} for x in stress if '1.3' not in x.get('stats',{}).get('fontScaleVariables',{}).values()]
out={'time':datetime.datetime.now(datetime.timezone.utc).isoformat(),'tracked_input_count':len(input_results),'changed_inputs':[x for x in input_results if not x['unchanged']],'input_results':input_results,'capture_binding':{'snapshot_utc':snap['time'],'files':capture_results,'mismatches':[x for x in capture_results if not x['matches']]},'static_links':{'checked':len(links),'missing':missing,'expected_final_report':'REPORT.json written after this verification'},'matrix_captures':len(matrix),'matrix_failures':[x for x in matrix if x['overflow'] or x['missing'] or x['errors']],'stress_captures':len(stress),'font_scale_failures':font_failures,'focused_qa':{'pages':len(qa),'checks':sum(len(x['checks']) for x in qa),'failed':[{'page':x.get('name'),'checks':[c for c in x['checks'] if not c['pass']]} for x in qa if not x.get('pass',False)]},'contact_sheets':len(list((B/'contact-sheets').glob('*.jpg'))),'png_evidence':len(list((B/'evidence').rglob('*.png')))}
(B/'evidence/final-verification.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
manifest=[{'path':str(p.relative_to(B)),'bytes':p.stat().st_size,'sha256':sha(p)} for p in [*(B/'finished-mocks').rglob('*'),*B.glob('*.md'),*B.glob('*.html'),*B.glob('*.csv')] if p.is_file()]
(B/'evidence/FINAL-ARTIFACT-VERSIONS.json').write_text(json.dumps({'time':out['time'],'files':manifest},indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in out.items() if k not in ('input_results','capture_binding')},indent=2))

