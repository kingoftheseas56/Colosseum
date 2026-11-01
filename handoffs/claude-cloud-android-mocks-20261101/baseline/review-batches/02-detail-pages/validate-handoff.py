from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit,unquote
from PIL import Image
import json,csv,re,hashlib,datetime
B=Path(__file__).parent;F=B/'finished-mocks';root=B.parent.parent
now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
class Links(HTMLParser):
 def __init__(self):super().__init__();self.links=[]
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ['href','src'] and v:self.links.append((tag,k,v))
issues=[];pending=[];checked=[];htmlFiles=list(F.rglob('*.html'))+list((B/'comparisons').glob('*.html'))+list((B/'contact-sheets').glob('*.html'))+[B/'index.html']
for p in htmlFiles:
 parser=Links();parser.feed(p.read_text(encoding='utf-8'))
 for tag,attr,url in parser.links:
  u=urlsplit(url)
  if u.scheme or not u.path:continue
  target=(p.parent/unquote(u.path)).resolve()
  row={'file':str(p.relative_to(B)),'attribute':attr,'url':url,'target':str(target)}
  if target==B/'REPORT.json' and not target.exists():pending.append(row);continue
  checked.append(row)
  if not target.is_file():issues.append(row)
for p in F.rglob('*.css'):
 for url in re.findall(r'url\(\s*["\']?([^)"\']+)["\']?\s*\)',p.read_text(encoding='utf-8')):
  u=urlsplit(url.strip())
  if u.scheme or not u.path:continue
  target=(p.parent/unquote(u.path)).resolve();row={'file':str(p.relative_to(B)),'attribute':'css-url','url':url,'target':str(target)};checked.append(row)
  if not target.is_file():issues.append(row)
caps=[];captureErrors=[]
for p in sorted((B/'captures').glob('*.png')):
 mp=p.with_suffix('.json')
 if not mp.is_file():captureErrors.append({'file':str(p),'error':'missing metadata'});continue
 m=json.loads(mp.read_text(encoding='utf-8'));actual=sha(p)
 if m['sha256']!=actual:captureErrors.append({'file':str(p),'error':'SHA mismatch'})
 im=Image.open(p)
 caps.append({'file':str(p.relative_to(B)),'sha256':actual,'dimensions':[im.width,im.height],'utc':m['utc'],'url':m['url'],'metadata':str(mp.relative_to(B))})
spec=json.loads((B/'review-data.json').read_text(encoding='utf-8'));v=json.loads((B/'verified-layouts.json').read_text(encoding='utf-8'));finalnames={r['capture']['name'] for r in v['layouts']}
runtime=[]
for p in sorted(F.rglob('*')):
 if p.is_file():runtime.append({'path':str(p.relative_to(B)),'sha256':sha(p),'bytes':p.stat().st_size})
def inputs(page):
 p=next(p for p in spec['pages'] if p['slug']==page);folder=F/p['task']
 files=[folder/p['page'],folder/'style.css',folder/'app.js']
 if '../shared/' in (folder/p['page']).read_text(encoding='utf-8'):files+=sorted((F/'shared').glob('*'))
 return [{'path':str(x.relative_to(B)),'sha256':sha(x)} for x in files if x.is_file()]
for row in v['layouts']:row['final_runtime_inputs']=inputs(row['metrics']['page'])
(B/'verified-layouts.json').write_text(json.dumps(v,indent=2),encoding='utf-8')
(B/'finished-versions.json').write_text(json.dumps({'utc':now(),'files':runtime},indent=2),encoding='utf-8')
(B/'capture-provenance.json').write_text(json.dumps({'utc':now(),'capture_count':len(caps),'verified_final_normal_captures':sorted(finalnames),'note':'Historical captures are versioned checkpoints, not all passing states. Only verified-layouts.json assigns final runtime hashes to final normal captures. Historical raw ledgers retain earlier inputs and outcomes.','captures':caps,'errors':captureErrors},indent=2),encoding='utf-8')
# Hash approved source dependencies and all loaded source/art/font resources; do not claim native render.
start=json.loads((B/'input-versions-start.json').read_text(encoding='utf-8'));resources=json.loads((B/'loaded-resources.json').read_text(encoding='utf-8'))
paths={Path(x['path']) for x in start['files'] if x.get('exists') and ('qml' in x['role'] or 'source' in x['role'])}
extra=['ChromeScrim.qml','ContinueTile.qml','TheatreRemovalDialog.qml','ComicDbLedger.qml','ComicTorrentSourcesPage.qml','ComicTorrentArchivePicker.qml','GenreMosaic.qml','collections/CollectionMembershipRow.qml','collections/CollectionSearchField.qml','collections/CollectionActionButton.qml','collections/CollectionCover.qml','collections/CollectionIcon.qml','UniverseTile.qml','UniverseChromeAction.qml','UniverseExtensionPage.qml','Universes.js']
paths.update(root/'reference'/'desktop'/'qml'/x for x in extra)
paths.update(Path(x['path']) for x in resources['resources'] if str(root/'reference') in x['path'])
src=[{'path':str(p),'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(paths) if p.is_file()]
(B/'source-versions.json').write_text(json.dumps({'utc':now(),'role':'Approved source/dependency and loaded asset/font hashes; native rendering not performed','files':src,'declared_pins':start['declaredSourcePins']},indent=2),encoding='utf-8')
gap=root/'outputs'/'19-generic-universes'/'REPORT.json'
(B/'input-gaps.json').write_text(json.dumps({'utc':now(),'gaps':[{'finding':'B2-019','path':str(gap),'exists':gap.exists(),'status':'unavailable-original-handoff','note':'Absent at initial inspection and final handoff; no replacement written to original outputs.'}]},indent=2),encoding='utf-8')
coverage=list(csv.DictReader((B/'COVERAGE.csv').open(encoding='utf-8-sig',newline='')));findings=list(csv.DictReader((B/'FINDINGS.csv').open(encoding='utf-8-sig',newline='')))
checks={'utc':now(),'html_files':len(htmlFiles),'relative_links_and_assets_checked':len(checked),'broken_links_or_assets':issues,'report_links_pending_final_write':len(pending),'capture_count':len(caps),'capture_integrity_errors':captureErrors,'coverage_rows':len(coverage),'finding_rows':len(findings),'fixed_verified':sum(x['status']=='fixed-verified' for x in findings),'fixed_unverified':[x['finding_id'] for x in findings if 'awaiting' in x['status']],'normal_verified':v['count'],'decision_pending':['B2-018'],'source_files_hashed':len(src),'finished_files_hashed':len(runtime),'historical_browser_errors':resources['errors'],'blocked_requests':resources['blocked']}
(B/'artifact-validation.json').write_text(json.dumps(checks,indent=2),encoding='utf-8')
print(json.dumps(checks,ensure_ascii=True))
