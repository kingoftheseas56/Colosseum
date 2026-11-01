from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit,unquote
import json,re,datetime,hashlib
root=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101');b=root/'review-batches/01-sources-manga';now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat();cache={}
def sha(p):
 if p not in cache:cache[p]=hashlib.sha256(p.read_bytes()).hexdigest()
 return cache[p]
def read(rel):return json.loads((b/rel).read_text(encoding='utf-8-sig'))
def save(rel,j):(b/rel).write_text(json.dumps(j,indent=2,ensure_ascii=False),encoding='utf-8')
prefixes=['finished-film','finished-episode','finished-comic','finished-comic-confirm','finished-manga-sources','finished-series','finished-chapters','finished-flow-original','finished-flow-alternative']
suites=[];issues=[];matrix=[]
for prefix in prefixes:
 j=read('evidence/'+prefix+'-checks.json');tests=j.get('tests',j.get('checks',[]));failed=[t for t in tests if not t['pass']]
 suites.append({'file':j['file'],'receipt':'evidence/'+prefix+'-checks.json','checks':len(tests),'passed':len(tests)-len(failed),'failed':failed,'known_constraints':len(j.get('knownConstraints',[]))})
 for preset in ['portrait','landscape','small-portrait','small-landscape','desktop','tablet-portrait','tablet-landscape','stress-portrait','stress-landscape']:
  name=prefix+'-'+preset
  if not (b/'evidence'/(name+'.json')).exists() and preset=='tablet-portrait':name=prefix+'-tablet'
  p=b/'evidence'/(name+'.json')
  if not p.exists():issues.append({'missing_matrix_capture':name});continue
  r=json.loads(p.read_text(encoding='utf-8'));m=[x['path'] for x in r['resources'] if not (root/x['path']).is_file() or sha(root/x['path'])!=x['sha256']]
  row={'name':name,'viewport':r['viewport'],'params':r['params'],'resource_mismatches':m,'errors':r['errors'],'blocked':r['blockedRequests'],'png_exists':(b/'captures'/(name+'.png')).exists()};matrix.append(row)
  if m or row['errors'] or row['blocked'] or not row['png_exists']:issues.append(row)
 if failed:issues.append({'suite':prefix,'failed':failed})
records=[];refs=set()
for p in (b/'evidence').glob('*.json'):
 try:j=json.loads(p.read_text(encoding='utf-8-sig'))
 except Exception:continue
 if not isinstance(j,dict) or 'png' not in j or 'resources' not in j:continue
 mismatches=[]
 for x in j['resources']:
  q=root/x['path']
  if str(x['path']).replace('\\','/').startswith('reference/'):refs.add(q)
  if not q.exists() or sha(q)!=x.get('sha256'):mismatches.append(x['path'])
 origins=[str(x['path']).replace('\\','/') for x in j['resources']]
 kind='current-finished-or-handoff' if any('/finished-mocks/' in s for s in origins) else 'preserved-original-baseline'
 if mismatches:kind='earlier-working-evidence'
 png=b/'captures'/(p.stem+'.png');records.append({'receipt':str(p.relative_to(b)).replace('\\','/'),'image':'captures/'+png.name,'kind':kind,'current_resources_match':not mismatches,'mismatches':mismatches,'image_sha256':sha(png) if png.exists() else None})
save('evidence/final-evidence-index.json',{'indexed_at':now(),'meaning':'Current resource matches identify final-version renderings. Earlier working evidence remains intentionally preserved. Historical T5 images are separately labeled in the contact-sheet manifest.','captures':records})
source_map=read('evidence/source-map.json')
for x in source_map.values():refs.add(root/x['path'])
refs.add(root/'reference/desktop/qml/Theme.qml')
reference_rows=[{'path':str(p.relative_to(root)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(refs) if p.exists()]
save('evidence/reviewed-reference-files.json',{'recorded_at':now(),'note':'Supplemental file hashes for sources and resources actually referenced/read; declared snapshot pins remain in INPUTS.json. Files not in the start inventory are not retroactively claimed to have start hashes.','files':reference_rows})
class Links(HTMLParser):
 def __init__(self):super().__init__();self.links=[]
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ['href','src'] and v:self.links.append(v)
docs=list((b/'finished-mocks').rglob('*.html'))+[b/'START-HERE.html',b/'contact-sheets/index.html']+[b/n for n in ['REVIEW.md','QML-PORTING.md','DECISIONS.md']]
links=[];pending=[];bad=[]
for p in docs:
 s=p.read_text(encoding='utf-8')
 if p.suffix=='.html':
  parser=Links();parser.feed(s);found=parser.links
 else:found=re.findall(r'\[[^\]]*\]\(([^)]+)\)',s)
 for link in found:
  u=urlsplit(link)
  if u.scheme or not u.path:continue
  target=(p.parent/unquote(u.path)).resolve();row={'document':str(p.relative_to(b)).replace('\\','/'),'link':link,'exists':target.exists()}
  if target==b/'REPORT.json' and not target.exists():row['deferred']='Required report written last';pending.append(row)
  elif not target.exists():bad.append(row)
  links.append(row)
save('evidence/deliverable-link-checks.json',{'checked_at':now(),'documents':len(docs),'links':len(links),'failed':bad,'pending_report_last':pending,'checks':links})
issues+=bad
meta=read('evidence/manga-metadata-checks.json');menu=read('evidence/chapter-desktop-menu-check.json');compare=read('evidence/handoff-comparison-checks.json');index=read('evidence/handoff-index-checks.json');inputs=read('evidence/input-versions-end.json');sheets=read('evidence/contact-sheets-manifest.json')
assert meta['passed'] and menu['passed'] and index['passed'] and inputs['unchanged']
assert compare['passed']==compare['total']
assert all(x['current_resource_hashes_match'] for x in sheets['resource_hash_checks'])
assert len(matrix)==81
save('evidence/final-validation.json',{'checked_at':now(),'status':'passed' if not issues else 'failed','suites':suites,'total_page_checks':sum(x['checks'] for x in suites),'passed_page_checks':sum(x['passed'] for x in suites),'matrix_cases':len(matrix),'matrix':matrix,'supplemental':{'metadata':meta['passed'],'desktop_menu':menu['passed'],'comparison_checks':compare['total'],'comparison_passed':compare['passed'],'index_http_links':len(index['links']),'index_http_passed':index['passed'],'static_links':len(links),'static_failed':bad,'report_link_deferred_until_last_write':len(pending),'sheet_count':len(sheets['sheets']),'sheet_capture_resource_checks':len(sheets['resource_hash_checks'])},'issues':issues})
print(json.dumps({'page_checks':sum(x['checks'] for x in suites),'passed':sum(x['passed'] for x in suites),'matrix_cases':len(matrix),'contact_sheets':len(sheets['sheets']),'static_links':len(links),'deferred_report_links':len(pending),'reference_files':len(reference_rows),'indexed_captures':len(records),'issues':issues}))
assert not issues
