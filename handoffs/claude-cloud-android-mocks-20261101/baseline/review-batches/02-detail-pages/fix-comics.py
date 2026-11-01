SOURCE_FUNCTIONS="function sourceTable(){\n const e=st.sourceEdition,results=sourceRows(),state=st.sourceState;\n if(state===\"inspecting\")return '<div class=\"pack-state\"><h3>Inspecting pack…</h3><p>'+safe(e.title)+'</p></div>';\n if(state===\"empty\")return '<p class=\"pack-empty\">No torrents matched this query. Try another title or ISBN.</p>';\n if(state===\"incomplete\")return '<div class=\"pack-state\"><p>This pack is missing issues this edition needs.</p><p class=\"gold\">#5 · #6</p><div class=\"pack-actions\"><button data-action=\"source-return\">Try another source</button><button data-action=\"source-manual\">Search manually</button></div></div>';\n if(state===\"combined\")return '<div class=\"pack-state\"><p>Only a combined archive covers this edition — it likely includes other editions too.</p><div class=\"pack-actions\"><button data-action=\"source-return\">Go back</button><button class=\"gold-button\" data-action=\"source-confirm\">Download whole archive anyway</button></div></div>';\n if(state===\"ambiguous\")return '<div class=\"source-table-head\"><h3>2 comic archives</h3><span class=\"gold\">CHOOSE ONE</span></div>'+[[\"Batman - Detective Comics - Rebirth Deluxe Book 1.cbz\",\"428.5 MB\"],[\"Batman - Detective Comics - Rebirth Deluxe Book 2.cbz\",\"512.2 MB\"]].map((f,i)=>'<button class=\"archive-file\" data-action=\"archive-choose\" data-index=\"'+i+'\"><span class=\"source-badge\">CBZ</span><span class=\"result-copy\"><strong>'+safe(f[0])+'</strong><small>'+f[1]+'</small></span><span class=\"pick-disc\">↓</span></button>').join(\"\");\n return '<div class=\"source-table-head\"><h3>3 results</h3><span>Pirate Bay · ExtraTorrents · Torrents-CSV</span></div>'+results.map((r,i)=>'<div class=\"source-result\" tabindex=\"0\" role=\"button\" aria-label=\"Choose '+attr(r.source)+'\" data-source-index=\"'+i+'\"><div class=\"source-badge\">'+r.source.charAt(0)+'</div><div class=\"result-copy\"><strong>'+r.source+'</strong><span class=\"confidence confidence-'+i+'\">'+r.confidence+'</span><p>'+safe(r.name)+'</p>'+(r.evidence?'<small class=\"evidence '+(i===0?'coverage':'')+'\">'+r.evidence+'</small>':\"\")+'</div><button class=\"source-choose\" data-action=\"source-choose\" data-index=\"'+i+'\" aria-label=\"Choose '+attr(r.source)+'\">↓</button></div>').join(\"\");\n}\nfunction sourceOverlay(){\n const e=st.sourceEdition;if(!e)return \"\";\n const identity=[e.title,e.isbn?\"ISBN \"+e.isbn:\"\",e.collects||\"\"].filter(Boolean).join(\"      ·      \");\n const weak=st.confirmingWeak?'<div class=\"weak-shade\"><section class=\"weak-dialog\" role=\"dialog\" aria-modal=\"true\" aria-label=\"Weak match confirmation\"><p>This release does not closely match the collected edition.</p><small>'+safe(e.title+' Collection')+'</small><div class=\"pack-actions\"><button data-action=\"weak-cancel\">Go back</button><button class=\"gold-button\" data-action=\"weak-confirm\">Choose anyway</button></div></section></div>':\"\";\n return '<section class=\"source-overlay\" role=\"dialog\" aria-modal=\"true\" aria-label=\"Find alternate sources\"><div class=\"source-back\">'+backButton().replace('data-action=\"back\"','data-action=\"close-sources\"')+'</div><div class=\"source-scroll\"><div class=\"source-banner\" style=\"background-image:url(&quot;'+art.gothamWash+'&quot;)\"><div class=\"source-heading\"><div class=\"gold-eyebrow\">ALTERNATE SOURCES · COLLECTED EDITION</div><h2>'+safe(e.title)+'</h2><div class=\"source-subhead\">'+safe(identity)+'</div></div></div><div class=\"source-body\">'+\n ([\"results\",\"empty\"].includes(st.sourceState)?'<div class=\"source-query\"><input id=\"source-query\" placeholder=\"Search another title or ISBN\" aria-label=\"Search another title or ISBN\" value=\"'+attr(st.sourceQuery)+'\"><button class=\"gold-button\" data-action=\"source-search\">Search</button></div>':\"\")+\n '<div class=\"source-table glass-panel\">'+sourceTable()+'</div></div></div>'+weak+'</section>';\n}\nfunction openCollections(trigger){\n st.collectionPopup=false;\n BatchCollections.open({host:document.getElementById(\"app\"),trigger:trigger||document.querySelector('[data-action=\"collections\"]'),key:\"comic:Batman\",title:\"Batman\",kind:\"comic\",kindLabel:\"Comic\",year:\"DC Comics\",cover:art.gotham,onSaved:ids=>{st.collections=ids;st.status=ids.length+\" memberships saved in this local fixture.\";const n=document.getElementById(\"review-status\");if(n)n.textContent=st.status;}});\n}\nfunction focusSource(selector){\n requestAnimationFrame(()=>document.querySelector(selector||'.source-overlay [data-action=\"close-sources\"]')?.focus({preventScroll:true}));\n}\nfunction finishPack(){\n const id=st.sourceEdition.id;st.editionDownloads[id]=\"queued\";st.sourceEdition=null;st.confirmingWeak=false;\n st.status=\"Pack choice accepted in this local fixture; no download or service operation.\";renderStage();\n requestAnimationFrame(()=>document.querySelector('.edition-row[data-edition=\"'+id+'\"]')?.focus({preventScroll:true}));\n}\nfunction inspectPack(index){\n st.confirmingWeak=false;st.sourceSelection=index;st.sourceState=\"inspecting\";renderStage();focusSource();\n const identity=st.sourceEdition.id;\n window.setTimeout(()=>{if(st.sourceEdition?.id!==identity||st.sourceState!==\"inspecting\")return;st.sourceState=index===0?\"combined\":index===1?\"incomplete\":\"ambiguous\";renderStage();focusSource();},350);\n}\n"
from pathlib import Path
p=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks\18-comic-details\app.js")
s=p.read_text(encoding="utf-8")
s=s.replace('let phone=Math.min(st.w,st.h)<600 && Math.max(st.w,st.h)<=1000;','let phone=Math.min(st.w,st.h)<600;')
s=s.replace('let desktop=!phone && st.w>=1100 && st.h<=900;','let desktop=params.get("host")==="desktop" || (!phone && st.w>=1000 && st.h<=750);')
s=s.replace('(d.desktop?"desktop ":"")','(d.desktop?"desktop ":"android-host ")')
s=s.replace('''<img class="collection-ico" src="'+assets+'icons/book-library.svg" alt="">''','''<svg class="collection-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/></svg>''')
s=s.replace('''<div class="release-row" data-release="''','''<div class="release-row" role="button" tabindex="0" data-release="''')
s=s.replace('''let label=dl==="done"?''','''let label=st.pendingRead===r.id && ["queued","downloading"].includes(dl)?"Reading when ready":dl==="done"?''')
s=s.replace('''function series(){
  const shown=seriesItems();''','''function series(title="Batman"){
  const shown=seriesItems();''')
s=s.replace('''<h1 class="display">Batman</h1><div class="facts-row">''','''<h1 class="display">'+safe(title)+'</h1><div class="facts-row">''')
s=s.replace('''function archive(){
  if(st.archiveView==="board"){''','''function archive(){
  if(st.archiveView==="releases")return series(st.archiveBox);
  if(st.archiveView==="board"){''')
a=s.index("function sourceOverlay(){");b=s.index("function stateChoices()",a)
s=s[:a]+SOURCE_FUNCTIONS+"\n"+s[b:]
s=s.replace('''["long","Expanded synopsis"]''','''["long","Expanded synopsis stress"],["source-inspecting","Pack: inspecting"],["source-incomplete","Pack: missing issues"],["source-combined","Pack: combined archive"],["source-ambiguous","Pack: choose archive"],["source-weak","Weak match confirmation"]''')
s=s.replace('''Covers are local DCAU artwork stand-ins, not actual cover reproductions.''','''Covers are local DCAU artwork stand-ins, not actual cover reproductions. Pack fixtures: Pirate Bay shows combined; ExtraTorrents shows incomplete; Torrents-CSV asks for weak-match confirmation then shows archive choices.''')
s=s.replace('''<a href="FIDELITY.md">Fidelity notes</a><a href="REPORT.json">Completion evidence</a>''','''<a href="../../QML-PORTING-NOTES.md">QML porting notes</a><a href="../../REPORT.json">Batch completion evidence</a>''')
s=s.replace('''function renderStage(){
 const d=device();''','''function renderStage(){
 if(window.BatchCollections?.inspect().open)BatchCollections.close();
 const sourceScroll=document.querySelector(".source-scroll")?.scrollTop||0;
 const tableScroll=document.querySelector(".source-table")?.scrollTop||0;
 const d=device();''')
s=s.replace('''+(st.collectionPopup?collectionsPopup():"")+sourceOverlay();''','''+sourceOverlay();''')
s=s.replace(''' document.getElementById("app-scroll").scrollTop=oldScroll;
 syncFrame();''',''' document.getElementById("app-scroll").scrollTop=oldScroll;
 document.getElementById("app-scroll").inert=!!st.sourceEdition;
 app.querySelector(".chrome").inert=!!st.sourceEdition;
 if(st.sourceEdition){app.querySelector(".source-scroll").scrollTop=sourceScroll;app.querySelector(".source-table").scrollTop=tableScroll;}
 if(st.confirmingWeak){app.querySelector(".source-scroll").inert=true;app.querySelector(".source-back").inert=true;}
 syncFrame();''')
s=s.replace('''st.sourceState="results";
 if(page==="series"){''','''st.sourceState="results";st.confirmingWeak=false;
 if(page==="series"){''')
s=s.replace('''  if(value==="long")st.showSynopsis=true;''','''  if(value==="long")st.showSynopsis=true;
  if(value.startsWith("source-")){st.sourceEdition=editions[3];st.sourceState=value.slice(7)==="weak"?"results":value.slice(7);st.confirmingWeak=value==="source-weak";}''')
s=s.replace(''' st.status="State selected for local review; no backend action occurred.";
 renderStage();''',''' st.status="State selected for local review; no backend action occurred.";
 renderStage();if(st.collectionPopup)openCollections();if(st.sourceEdition)focusSource(st.confirmingWeak?'.weak-dialog button':undefined);''')
s=s.replace(''' location.href=destination;''',''' const url=new URL(destination,location.href);
 for(const k of ["capture","font","top","bottom","host"])if(params.has(k))url.searchParams.set(k,params.get(k));
 if(!capture){url.searchParams.set("w",st.w);url.searchParams.set("h",st.h);url.searchParams.set("font",st.font);url.searchParams.set("top",st.top);url.searchParams.set("bottom",st.bottom);}
 location.href=url.href;''')
s=s.replace(''' const id=el.dataset.id||"";
 switch(name){''',''' const id=el.dataset.id||"";
 const oldFocus=document.activeElement,focusAction=oldFocus?.dataset?.action,focusId=oldFocus?.dataset?.id;
 switch(name){''')
s=s.replace('''   if(page==="archive"&&st.archiveView==="index")''','''   if(page==="archive"&&st.archiveView==="releases"){st.archiveView="index";st.status="Returned to the archive index.";break;}
   if(page==="archive"&&st.archiveView==="index")''')
s=s.replace(''' case "collections":st.collectionPopup=true;break;''',''' case "collections":openCollections(el);return;''')
s=s.replace(''' case "release-read":{
  if''',''' case "release-read":{
  st.pendingRead=id;
  if''')
s=s.replace('''st.status="Read would open Claude-owned reader; it is intentionally excluded."''','''st.status="Read would open the existing comic reader; reader UI is outside Batch 2."''')
s=s.replace(''' case "alt":st.sourceEdition=editions.find(e=>e.id===id)||editions[0];st.sourceState="results";st.sourceQuery="";st.status="Original alternate sources full-screen page shown. Fixture results only.";break;''',''' case "alt":st.sourceEdition=editions.find(e=>e.id===id)||editions[0];st.sourceState="results";st.sourceQuery="";st.sourceReturnId=id;st.status="Original alternate sources page; deterministic local pack fixtures only.";break;''')
s=s.replace(''' case "source-choose":st.sourceState="inspecting";st.status="Result selection demonstrated locally. No torrent chosen or downloaded.";break;''',''' case "source-choose":
   if(Number(el.dataset.index)===2){st.confirmingWeak=true;break;}
   inspectPack(Number(el.dataset.index));return;
 case "weak-cancel":st.confirmingWeak=false;break;
 case "weak-confirm":inspectPack(2);return;
 case "source-return":case "source-manual":st.sourceState="results";break;
 case "source-confirm":case "archive-choose":finishPack();return;''')
s=s.replace(''' case "archive-series":return openLocalRoute("comic-series.html");
 case "all-releases":return openLocalRoute("comic-series.html");''',''' case "archive-series":
   if(Number(el.dataset.index)===0&&st.archiveBox==="DC Comics")return openLocalRoute("comic-series.html");
   st.status="Selected archive route: "+(st.archiveBox==="Image Comics"?["Invincible","The Walking Dead"][Number(el.dataset.index)]:archiveSeries[Number(el.dataset.index)]?.title)+". This bounded fixture includes a runnable Batman detail; additional catalogue data is not bundled.";break;
 case "all-releases":st.archiveView="releases";st.status="Opened the archive release shelf with the eight bundled example releases.";break;''')
s=s.replace(''' renderStage();
}
renderAll();''',''' renderStage();
 if(st.sourceEdition)focusSource(st.confirmingWeak?'.weak-dialog button':name==="source-manual"?'#source-query':name==="weak-cancel"?'[data-action="source-choose"][data-index="2"]':undefined);
 else if(name==="close-sources")requestAnimationFrame(()=>document.querySelector('[data-action="alt"][data-id="'+st.sourceReturnId+'"]')?.focus({preventScroll:true}));
 else if(focusAction)requestAnimationFrame(()=>document.querySelector('[data-action="'+focusAction+'"]'+(focusId?'[data-id="'+focusId+'"]':""))?.focus({preventScroll:true}));
}
renderAll();''')
start=s.index('if(!capture){\n if(params.get("state")')
end=s.index('root.addEventListener("click"',start)
s=s[:start]+'''if(params.has("state"))setState(params.get("state"));
'''+s[end:]
s=s.replace(''' const ed=e.target.closest(".edition-row[data-edition]");''',''' const source=e.target.closest("[data-source-index]");
 if(source&&!e.target.closest("button")){appAction("source-choose",{dataset:{index:source.dataset.sourceIndex}});return;}
 const release=e.target.closest(".release-row[data-release]");
 if(release&&!e.target.closest("button")){appAction("release-read",{dataset:{id:release.dataset.release}});return;}
 const ed=e.target.closest(".edition-row[data-edition]");''')
a=s.index('root.addEventListener("keydown"');b=s.index('window.addEventListener("resize"',a)
s=s[:a]+'''root.addEventListener("keydown",e=>{
 if(e.key==="Escape"&&st.sourceEdition){e.preventDefault();if(st.confirmingWeak)appAction("weak-cancel",{dataset:{}});else appAction("close-sources",{dataset:{}});return;}
 if(e.key==="Enter"&&e.target.id==="source-query"){e.preventDefault();appAction("source-search",{dataset:{}});return;}
 if(st.sourceEdition&&e.key==="Tab"){
  const host=document.querySelector(st.confirmingWeak?".weak-dialog":".source-overlay"),items=[...host.querySelectorAll("button,input")].filter(x=>!x.disabled&&x.getClientRects().length),i=items.indexOf(document.activeElement);
  e.preventDefault();items[(i+items.length+(e.shiftKey?-1:1))%items.length]?.focus();return;
 }
 if(e.key!=="Enter"&&e.key!==" ")return;
 const ed=e.target.closest(".edition-row[data-edition]"),row=e.target.closest(".release-row[data-release]"),source=e.target.closest("[data-source-index]");
 if(ed&&e.target===ed){e.preventDefault();appAction("edition-open",{dataset:{id:ed.dataset.edition}});}
 if(row&&e.target===row){e.preventDefault();appAction("release-read",{dataset:{id:row.dataset.release}});}
 if(source&&e.target===source){e.preventDefault();appAction("source-choose",{dataset:{index:source.dataset.sourceIndex}});}
});
'''+s[b:]
p.write_text(s,encoding="utf-8")
p=p.with_name("style.css")
s=p.read_text(encoding="utf-8")+"""
/* Batch 2 fitting corrections; source recipes retained. */
.app.android-host .window-controls{display:none}
.app.phone.landscape .hero-actions .ghost-button{font-size:calc(13px * var(--font-scale,1));min-height:44px}
.app.android-host .release-title{white-space:normal;display:block;-webkit-line-clamp:unset;overflow:visible}
.app.android-host .read-action,.app.android-host .round-action,.app.android-host .edition-act,.app.android-host .alt-button{min-height:44px}
.app.android-host .round-action{width:44px;position:relative;left:-4px}
.app.android-host .alt-button{width:44px;border:0;background:radial-gradient(circle,rgba(255,255,255,.05) 18px,transparent 19px);position:relative}
.app.android-host .alt-button:before{content:"";position:absolute;width:38px;height:38px;border:1px solid var(--edge);border-radius:50%}
.release-row:focus-visible,.edition-row:focus-visible,.source-result:focus-visible{outline:2px solid var(--gold);outline-offset:-2px}
.app .chrome:before{content:"";position:absolute;left:calc(-1 * var(--margin));right:calc(-1 * var(--margin));top:-28px;height:92px;background:linear-gradient(#000b,#0000);z-index:-1;pointer-events:none}
.source-overlay{inset:var(--inset-top,0px) 0 var(--inset-bottom,0px);overflow:hidden}
.source-scroll{height:100%;overflow:auto;overscroll-behavior:contain}
.source-overlay>.source-back{top:28px;z-index:8}
.source-overlay:before{content:"";position:absolute;top:0;left:0;right:0;height:90px;background:linear-gradient(#000d,#0000);z-index:6;pointer-events:none}
.source-table{padding:0;max-height:none;overflow:visible;min-height:180px}
.source-table-head{min-height:52px;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 26px;border-bottom:1px solid var(--edge);flex-wrap:wrap}
.source-table-head h3{margin:0;font-size:calc(16px * var(--font-scale,1))}
.source-table-head>span{font-size:calc(12px * var(--font-scale,1));color:var(--muted)}
.source-result{padding:16px 26px;min-height:150px;gap:24px;cursor:pointer}
.source-result .confidence{font-size:calc(13px * var(--font-scale,1));font-weight:600;letter-spacing:.5px}
.source-result .confidence-1{color:#8ea3c0}.source-result .confidence-2{color:#c98a8a}
.source-result p{font-size:calc(14px * var(--font-scale,1))}
.source-result .evidence{display:inline-block;padding:3px 8px;border-radius:6px;border:1px solid var(--edge);background:#ffffff0d;color:var(--dim);font-size:calc(10px * var(--font-scale,1));font-weight:600;letter-spacing:.6px}
.source-result .coverage{background:#f0c44a1f;border-color:#f0c44a66;color:var(--gold)}
.source-choose,.pick-disc{width:56px;height:56px;border-radius:28px;display:grid;place-items:center;flex:none;background:var(--gold);color:#1a1306;font-size:18px}
.source-query input{font-size:calc(16px * var(--font-scale,1))}
.pack-state{padding:32px 40px;min-height:180px;display:flex;flex-direction:column;justify-content:center;gap:18px}
.pack-state p{margin:0;color:var(--ink);font-size:calc(17px * var(--font-scale,1));line-height:1.45}
.pack-state p.gold{color:var(--gold);font-size:calc(14px * var(--font-scale,1))}
.pack-state h3{font:600 calc(20px * var(--font-scale,1)) FrauncesLocal,serif;text-align:center;margin:0}
.pack-actions{display:flex;gap:14px;flex-wrap:wrap}
.pack-actions button{min-height:44px;max-width:100%;padding:10px 20px;border:1px solid var(--edge);border-radius:12px;background:#ffffff0d;color:var(--ink);font-size:calc(14px * var(--font-scale,1));white-space:normal}
.pack-actions button.gold-button{background:var(--gold);color:#1a1306;border:0}
.pack-empty{padding:36px;text-align:center;font-size:calc(16px * var(--font-scale,1))}
.archive-file{width:100%;min-height:92px;padding:16px 26px;display:flex;gap:24px;align-items:center;text-align:left;border:0;border-bottom:1px solid var(--edge);background:transparent;color:var(--ink)}
.archive-file .source-badge{font:600 calc(12px * var(--font-scale,1)) SegoeLocal,sans-serif}
.archive-file .result-copy{min-width:0;flex:1;overflow-wrap:anywhere}
.archive-file strong{font-size:calc(15px * var(--font-scale,1));font-weight:400}
.archive-file small{display:block;margin-top:6px;color:var(--dim);font-size:calc(13px * var(--font-scale,1))}
.weak-shade{position:absolute;inset:0;z-index:40;padding:20px;display:flex;align-items:center;justify-content:center}
.weak-dialog{width:min(560px,100%);max-height:100%;overflow:auto;border:1px solid var(--edge);border-radius:18px;padding:28px;background:rgba(20,20,23,.98);display:flex;flex-direction:column;gap:18px}
.weak-dialog p{font-size:calc(17px * var(--font-scale,1));line-height:1.4;margin:0}
.weak-dialog small{font-size:calc(13px * var(--font-scale,1));line-height:1.4;color:var(--dim)}
.app.desktop .source-heading h2{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
@container(max-width:600px){.source-result,.archive-file{padding:16px;gap:12px}.source-choose,.pick-disc{width:44px;height:44px}.source-badge{width:43px;height:43px}.pack-state{padding:24px 20px}.source-table-head{padding:14px 16px}.source-query{flex-wrap:wrap}.source-query input{flex:1 1 160px}.source-query .gold-button{flex:0 0 auto}.weak-dialog{padding:22px}}
"""
p.write_text(s,encoding="utf-8")
print("Finished-only Comics fixes and source outcome states written.")
