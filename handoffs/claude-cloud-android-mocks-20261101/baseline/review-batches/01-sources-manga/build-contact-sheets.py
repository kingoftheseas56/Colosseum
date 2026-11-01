from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json,hashlib,html,datetime
root=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101');b=root/'review-batches/01-sources-manga';out=b/'contact-sheets';out.mkdir(exist_ok=True)
fontroot=root/'reference/local-fonts'
fonts={n:ImageFont.truetype(str(fontroot/'segoeui.ttf'),n) for n in [15,17,19,21,25,32]}
bold={n:ImageFont.truetype(str(fontroot/'seguisb.ttf'),n) for n in [19,21,25,32]}
BG='#10141c';FG='#f3f1e9';MUT='#bec6d2';GOLD='#e7bc4a';manifest=[];checks=[];cache={}
def sha(p):
 if p not in cache:cache[p]=hashlib.sha256(p.read_bytes()).hexdigest()
 return cache[p]
def text(draw,xy,s,size=19,color=FG,strong=False):draw.text(xy,s,font=(bold if strong else fonts)[size],fill=color)
def wrap(draw,s,width,size=19):
 words=s.split();lines=[];line=''
 for word in words:
  trial=(line+' '+word).strip()
  if draw.textlength(trial,font=fonts[size])>width and line:lines.append(line);line=word
  else:line=trial
 if line:lines.append(line)
 return lines
def paragraph(draw,xy,s,width,size=19,color=MUT):
 x,y=xy
 for line in wrap(draw,s,width,size):text(draw,(x,y),line,size,color);y+=size+7
 return y
def canvas(title,sub,size):
 im=Image.new('RGB',size,BG);d=ImageDraw.Draw(im);text(d,(24,18),title,32,strong=True);paragraph(d,(24,64),sub,size[0]-48,19);return im,d
def panel(im,d,src,xy,box,label):
 p=src if isinstance(src,Path) else b/'captures'/(src+'.png')
 x,y=xy;w,h=box;text(d,(x,y-30),label,19,GOLD,True)
 original=Image.open(p).convert('RGB');scaled=original.copy();scaled.thumbnail((w,h),Image.Resampling.LANCZOS);d.rectangle((x-1,y-1,x+w,y+h),outline='#556072',width=1);im.paste(scaled,(x+(w-scaled.width)//2,y))
 receipt=b/'evidence'/(p.stem+'.json');entry={'image':str(p.relative_to(root)).replace('\\','/'),'image_sha256':sha(p),'pixel_size':list(original.size),'label':label,'receipt':None,'reference_kind':'fresh HTML mock capture'}
 if receipt.exists():
  j=json.loads(receipt.read_text(encoding='utf-8-sig'));entry.update(receipt=str(receipt.relative_to(b)).replace('\\','/'),receipt_sha256=sha(receipt),captured_at=j.get('captured_at'),params=j.get('params'),viewport=j.get('viewport'))
  mismatches=[]
  for resource in j.get('resources',[]):
   rp=root/resource['path']
   if not rp.exists() or sha(rp)!=resource.get('sha256'):mismatches.append(resource['path'])
  checks.append({'image':entry['image'],'current_resource_hashes_match':not mismatches,'mismatches':mismatches});assert not mismatches,(p.name,mismatches)
 else:entry['reference_kind']='archival component capture; exact runtime/input revision unverified'
 return entry
def finish(im,name,title,entries,note=''):
 path=out/(name+'.png');im.save(path,optimize=True);manifest.append({'file':'contact-sheets/'+path.name,'title':title,'note':note,'sha256':sha(path),'sources':entries})
 text(ImageDraw.Draw(im),(24,im.height-30),'',15)
pages=[
('film','finished-film','Film sources','film-sources.html'),
('episode','finished-episode','Episode sources','episode-sources.html'),
('comic','finished-comic','Comic sources','comic-sources.html'),
('comic-confirm','finished-comic-confirm','Comic source confirmation','comic-source-confirmation.html'),
('manga-sources','finished-manga-sources','Manga volume sources','manga-volume-sources.html'),
('series','finished-series','Manga series','manga-series.html'),
('chapters','finished-chapters','Manga chapters','manga-chapters.html'),
('flow-original','finished-flow-original','Volume flow: constrained original attempt','volume-flow-original-fit.html'),
('flow-alternative','finished-flow-alternative','Volume flow: proposed readable alternative','volume-flow-minimal-alternative.html')]
for i,(old,new,title,file) in enumerate(pages,1):
 im,d=canvas(str(i).zfill(2)+'  '+title,'Fresh local HTML renders. Left/middle: 412 × 900. Right: 900 × 412. Text 1.0; no simulated insets.',(1540,1100));entries=[]
 entries.append(panel(im,d,old+'-portrait',(24,130),(400,874),'Unmodified worker · portrait'))
 entries.append(panel(im,d,new+'-portrait',(450,130),(400,874),'Finished copy · portrait'))
 entries.append(panel(im,d,old+'-landscape',(876,130),(640,293),'Unmodified worker · landscape'))
 entries.append(panel(im,d,new+'-landscape',(876,510),(640,293),'Finished copy · landscape'))
 note='Initial positions shown. Full resolution screenshots and version receipts are linked from the sheet index. HTML desktop captures do not establish native app acceptance.'
 if old=='flow-original':note='This constrained attempt is comparison evidence, not the recommended phone fit. It remains below the source 190 px cover minimum at six tested sizes. See the flow decision sheet.'
 elif old=='flow-alternative':note='Verified proposal; Hemanth decision pending. More vertical scrolling preserves readable covers. The initial view is shown here; gallery and action positions are on the decision sheet.'
 elif old=='series':note='The finished target uses the Main.qml default composition plus the brief’s explicit header restorations. The full 107-volume fixture is reachable; scrolling views are also saved.'
 elif old=='chapters':note='The finished target uses the default shared header and MangaChapterSeriesView ledger. Optional rebuilt controls remain separately labeled, not silently combined.'
 paragraph(d,(876,845),note,640,21);text(d,(24,1042),file+'  |  Source pins and input hashes: ../evidence/input-versions-start.json',17,MUT)
 finish(im,str(i).zfill(2)+'-'+old+'-phone',title,entries,note)
for group in range(3):
 im,d=canvas('Desktop before / after · group '+str(group+1),'1280 × 700 logical viewport, text 1.0. Worker HTML on the left; finished HTML on the right. Neither side is native runtime proof.',(1340,1470));entries=[]
 for row,(old,new,title,file) in enumerate(pages[group*3:group*3+3]):
  y=190+row*420;text(d,(24,y-60),title,21,strong=True)
  entries.append(panel(im,d,old+'-desktop',(24,y),(640,350),'Unmodified worker'))
  entries.append(panel(im,d,new+'-desktop',(684,y),(640,350),'Finished target'+(' · proposal' if old=='flow-alternative' else '')))
 text(d,(24,1430),'Desktop geometry was also read against the frozen QML. See QML-PORTING.md for exact components and declared overrides.',17,MUT)
 finish(im,'desktop-'+str(group+1),'Desktop before / after '+str(group+1),entries)
im,d=canvas('Volume flow decision · tight landscape stress','792 × 354; text 1.3; 24 px top and bottom insets. All panels are fresh corrected-copy renders at the same logical settings.',(1500,1060));entries=[]
entries.append(panel(im,d,'finished-flow-original-stress-landscape',(24,140),(714,319),'Original constrained attempt · initial'))
entries.append(panel(im,d,'finished-flow-alternative-stress-landscape-gallery',(762,140),(714,319),'Proposed alternative · gallery position'))
entries.append(panel(im,d,'finished-flow-original-stress-landscape-gallery',(24,540),(714,319),'Original constrained attempt · gallery position'))
entries.append(panel(im,d,'finished-flow-alternative-stress-landscape-actions',(762,540),(714,319),'Proposed alternative · action position'))
paragraph(d,(24,900),'Observed obstacle: the constrained attempt has a 16 px unscaled cover. The proposal reserves a 190 px cover and the complete scaled caption. At this height the chrome joins the existing vertical scroller. The full gallery fits; the bottom actions require a different scroll position. This is an explicit behavior proposal awaiting Hemanth, not approval.',1450,21)
finish(im,'decision-flow-stress','Volume flow decision at short landscape stress',entries,'190 px minimum preserved by additional vertical scrolling; chrome scrolls at the tightest stress size.')
im,d=canvas('Volume flow decision · portrait','412 × 900; text 1.0; no simulated insets. Corrected original attempt versus the proposed readable gallery.',(1320,1120));entries=[]
entries.append(panel(im,d,'finished-flow-original-portrait',(24,140),(400,874),'Original attempt · initial'))
entries.append(panel(im,d,'finished-flow-alternative-portrait',(460,140),(400,874),'Proposed · initial'))
entries.append(panel(im,d,'finished-flow-alternative-portrait-gallery',(896,140),(400,874),'Proposed · gallery scrolled'))
text(d,(24,1040),'Original cover: 139 px. Proposed cover: 190 px. The measured gallery position scrolls the existing content by 56 px.',19,MUT)
finish(im,'decision-flow-portrait','Volume flow portrait comparison',entries,'Proposal retains centered enlargement and cover minimum, with 56 px gallery-position scroll in this fixture.')
im,d=canvas('Open controls and local state examples','Finished HTML only. Controls retain their original visual vocabulary; no backend or native app was invoked.',(1500,1100));entries=[]
entries.append(panel(im,d,'finished-film-provider-open-landscape',(24,140),(714,328),'Film provider menu · 900 × 412'))
entries.append(panel(im,d,'finished-chapters-page-open-landscape',(762,140),(714,328),'Chapter page menu · 900 × 412'))
entries.append(panel(im,d,'finished-comic-combined-stress',(24,560),(714,319),'Comic confirmation / archive stress'))
entries.append(panel(im,d,'finished-manga-sources-downloading-landscape',(762,560),(714,319),'Manga source progress disc'))
paragraph(d,(24,930),'The raw capture receipts record each panel’s actual viewport, text scale, simulated insets, input parameters, resource hashes and browser errors. Modal cancel/filter retention, popup focus and escape, source timers, scroll reachability and local action transitions are recorded in the per-page checks.',1450,21)
finish(im,'controls-and-states','Open controls and local states',entries)
hist=root/'reference/historical-captures'
im,d=canvas('Archival source component comparison','Historical T5 captures are component references only: their exact native runtime/input revision is unverified. Finished panels are fresh HTML mocks.',(1540,1100));entries=[]
entries.append(panel(im,d,hist/'T5-sources-before-412x900.png',(24,140),(400,874),'Archival T5 · 412 × 900'))
entries.append(panel(im,d,'finished-manga-sources-portrait',(450,140),(400,874),'Finished HTML · 412 × 900'))
entries.append(panel(im,d,hist/'T5-sources-before-1280x700.png',(876,140),(640,350),'Archival T5 · 1280 × 700'))
entries.append(panel(im,d,'finished-manga-sources-desktop',(876,560),(640,350),'Finished HTML · 1280 × 700'))
paragraph(d,(876,940),'Different fixtures and backgrounds. Use the frozen QML, not these archival pixels alone, to judge exact component fidelity.',640,19)
finish(im,'reference-t5-sources','Archival T5 source component reference',entries,'Historical capture provenance does not establish current runtime behavior.')
payload={'generated_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sheets':manifest,'resource_hash_checks':checks}
(b/'evidence/contact-sheets-manifest.json').write_text(json.dumps(payload,indent=2,ensure_ascii=False),encoding='utf-8')
style='body{font:16px/1.5 Segoe UI,sans-serif;background:#11151d;color:#eee;margin:30px}a{color:#edc34f}article{padding:20px 0;border-top:1px solid #475164}img{max-width:100%;height:auto}small{color:#bac3d0}li{margin:8px 0}'
parts=['<!doctype html><html lang="en"><meta charset="utf-8"><title>Batch 1 evidence contact sheets</title><style>'+style+'</style><p><a href="../finished-mocks/index.html">Finished mocks index</a> · <a href="../REVIEW.md">Report</a> · <a href="../evidence/contact-sheets-manifest.json">Version manifest</a></p><h1>Batch 1 evidence contact sheets</h1><p>All original / finished comparisons are freshly rendered HTML. Each image below links to the full sheet. Source screenshots retain their logical pixel dimensions. Archival references are explicitly labeled. Earlier working captures remain in captures/; this manifest identifies the final sheets and their source bytes.</p><ul>']
parts += ['<li><a href="#'+str(i)+'">'+html.escape(s['title'])+'</a></li>' for i,s in enumerate(manifest)]
parts.append('</ul>')
for i,s in enumerate(manifest):
 parts.append('<article id="'+str(i)+'"><h2>'+html.escape(s['title'])+'</h2><p>'+html.escape(s['note'])+'</p><a href="'+Path(s['file']).name+'"><img loading="lazy" src="'+Path(s['file']).name+'" alt="'+html.escape(s['title'])+'"></a><ul>')
 for e in s['sources']:
  target='../../../'+e['image'] if e['reference_kind'].startswith('archival') else '../captures/'+Path(e['image']).name
  receipt=' · <a href="../'+e['receipt']+'">version receipt</a>' if e['receipt'] else ' · archival provenance unverified'
  parts.append('<li><a href="'+target+'">'+html.escape(e['label'])+'</a>'+receipt+' <small>'+str(e['pixel_size'][0])+' × '+str(e['pixel_size'][1])+'</small></li>')
 parts.append('</ul></article>')
parts.append('</html>');(out/'index.html').write_text(''.join(parts),encoding='utf-8')
print(json.dumps({'contact_sheets':len(manifest),'source_capture_checks':len(checks),'hash_mismatches':sum(not c['current_resource_hashes_match'] for c in checks)}))
