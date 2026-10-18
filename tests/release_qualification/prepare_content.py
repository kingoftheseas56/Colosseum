"""QA-only stock publisher inputs. Never edits application checkout or deploys."""
import argparse, hashlib, json, shutil, subprocess
from pathlib import Path
from PIL import Image, ImageDraw
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
s=args.source.resolve();w=args.output.resolve()
# Fail closed before copying or writing: never combine an old A with new source.
if w.exists():raise RuntimeError('Fresh output directory required: '+str(w))
w.mkdir(parents=True,exist_ok=False);a=w/'authoring'
for name in ['qml','assets/universes','assets/wallpaper']:
 shutil.copytree(s/name,a/name)
(a/'scripts').mkdir(exist_ok=True);shutil.copy2(s/'scripts/export_universe_content.cjs',a/'scripts/export_universe_content.cjs')
def build(version):
 output=w/('content-'+version)
 if output.exists(): raise RuntimeError('Fresh output required: '+str(output))
 cmd=['python',str(s/'server/content-api/projection/publish_content.py'),'--native-source',str(a),'--output',str(output)]
 if version=='B':cmd+=['--retain-assets-from',str(w/'content-A')]
 report=subprocess.check_output(cmd,text=True);(w/('content-'+version+'-report.json')).write_text(report)
build('A')
art=a/'assets/universes/qa-publication.png';im=Image.new('RGB',(600,900),'#183b56');ImageDraw.Draw(im).text((35,420),'COLOSSEUM QA PUBLICATION',fill='white');im.save(art)
entry={'name':'Colosseum QA Publication 1.1.9','category':'saga','c1':'#183b56','blurb':'Isolated QA collection. Existing film identity; owned test artwork. Not a public catalogue publication.','wallpaper':'../assets/universes/qa-publication.png','chips':[{'t':'1 Film','ic':'movies'}],'shelves':[{'title':'QA Films','kind':'movie','runs':[{'label':'Existing pinned work','works':[{'t':'Star Wars: Episode IV - A New Hope','id':'tt0076759','year':'1977','poster':'../assets/universes/qa-publication.png','art':'../assets/universes/qa-publication.png'}]}]}]}
f=a/'qml/Universes.js';f.write_text(f.read_text()+'\n// Isolated publication authoring only; never runtime QML.\nuniverses.push('+json.dumps(entry)+');\n')
(w/'test-universe.json').write_text(json.dumps(entry,indent=2))
build('B')
for label in ['A','B']:
 bundle=json.loads((w/('content-'+label)/'internal/content/rpc-bundle.json').read_text())['result'];docs=bundle['documents'];assert len(docs)==10
 roster=json.loads(docs['universes-roster'])['data'];assert any(x['name']==entry['name'] for x in roster['universes'])==(label=='B')
 for key,raw in docs.items():
  envelope=json.loads(raw);assert hashlib.sha256(raw.encode()).hexdigest()==bundle['manifest']['documents'][key]['sha256'];assert len(raw.encode())<=512*1024;assert envelope['schemaVersion']==1
print(json.dumps({'prepared':True,'nativeRuntimeExecuted':False,'authoringOnly':str(a),'titleIdentitySource':'qml/Universes.js Star Wars shelf: tt0076759, 1977','name':entry['name']}))
