HELPERS="function dockGeometry(){\n const app=$(\"app\"),top=Number($(\"insetT\").value)||0,left=(parseFloat(getComputedStyle(app).getPropertyValue(\"--gutter\"))||24)+(Number($(\"insetL\").value)||0),right=(parseFloat(getComputedStyle(app).getPropertyValue(\"--gutter\"))||24)+(Number($(\"insetR\").value)||0;\n const track=document.querySelector(\".tabs-track\"),natural=track?Array.from(track.children).reduce((n,e)=>n+e.getBoundingClientRect().width,0)+Math.max(0,track.children.length-1)*2+8:0;\n const below=state.kind===\"phone\"&&state.dock!==\"original\"&&left+118+natural+right>app.clientWidth;\n return {top,left,right,natural,below,floor:top+(below?74:22)};\n}\nfunction syncTabs(){\n const tabbar=$(\"tabbar\"),page=$(\"pageScroll\"),hero=$(\"hero\"),app=$(\"app\");if(!tabbar||!page||!hero)return;\n const g=dockGeometry(),dock=hero.offsetHeight-page.scrollTop<=g.floor;\n tabbar.style.top=Math.max(g.floor,hero.offsetHeight-page.scrollTop)+\"px\";\n tabbar.style.left=g.left+(dock&&!g.below?118:0)+\"px\";\n tabbar.style.maxWidth=state.dock===\"original\"?\"none\":Math.max(60,app.clientWidth-g.left-g.right-(dock&&!g.below?118:0))+\"px\";\n tabbar.classList.toggle(\"original-attempt\",state.dock===\"original\");\n tabbar.dataset.docked=String(dock);tabbar.dataset.position=dock?(g.below?\"below-back\":\"beside-back\"):\"floating\";\n $(\"chromeFade\").style.opacity=page.scrollTop>120?Math.min(1,(page.scrollTop-120)/240):0;\n $(\"chromeFade\").style.height=(g.top+(g.below?140:112))+\"px\";\n let active=0;for(let i=0;i<currentTabs.length;i++){const el=$(currentTabs[i][1]);if(el&&el.getBoundingClientRect().top-page.getBoundingClientRect().top+page.scrollTop-(g.floor+62)<=page.scrollTop)active=i;}\n document.querySelectorAll(\".tab\").forEach((t,i)=>{t.classList.toggle(\"active\",i===active);t.setAttribute(\"aria-current\",i===active?\"true\":\"false\");});\n}\nfunction revealForKeyboard(el){\n const page=$(\"pageScroll\"),a=$(\"app\").getBoundingClientRect(),g=dockGeometry(),r=el.getBoundingClientRect();\n const top=a.top+g.floor+56,bottom=a.bottom-(Number($(\"insetB\").value)||0)-10;\n if(r.height>bottom-top||r.top<top)page.scrollTo({top:Math.max(0,page.scrollTop+r.top-top),behavior:\"instant\"});\n else if(r.bottom>bottom)page.scrollTo({top:page.scrollTop+r.bottom-bottom,behavior:\"instant\"});\n}\nfunction configureCollections(){\n document.querySelectorAll(\".matrix-window,.branch-shelf,.rail,.magazine-rows\").forEach((host,group)=>{\n  host.tabIndex=0;host.setAttribute(\"role\",\"group\");host.setAttribute(\"aria-label\",host.getAttribute(\"aria-label\")||host.closest(\"article\")?.querySelector(\"h2\")?.textContent||host.previousElementSibling?.textContent||\"Works\");\n  const items=[...host.querySelectorAll(\"[data-select]\")];host._index=0;\n  items.forEach((el,i)=>{el.tabIndex=-1;el.id=\"work-\"+group+\"-\"+i;});\n  host.onfocus=()=>{if(!items.length)return;const current=items[host._index||0];host.setAttribute(\"aria-activedescendant\",current.id);if(P===\"saga\")selectSaga(current.dataset.select);revealForKeyboard(current);};\n  host.onkeydown=e=>{\n   const arrows=[\"ArrowLeft\",\"ArrowRight\",\"ArrowUp\",\"ArrowDown\",\"Home\",\"End\"];if(!arrows.includes(e.key)&&e.key!==\"Enter\"&&e.key!==\" \")return;\n   let index=host._index||0;\n   if(e.key===\"Enter\"||e.key===\" \"){e.preventDefault();actionOf(state.items.get(items[index]?.dataset.select));return;}\n   if(host.classList.contains(\"matrix-window\")){\n    const key=items[index].dataset.select,book=key.startsWith(\"book\"),n=Number(key.replace(/\\D/g,\"\"));\n    let row=book?0:1,cell=n;\n    if(e.key===\"ArrowUp\")row=0,cell=Math.min(6,cell);else if(e.key===\"ArrowDown\")row=1;\n    else if(e.key===\"ArrowLeft\")cell--;else if(e.key===\"ArrowRight\")cell++;\n    else if(e.key===\"Home\")cell=0;else if(e.key===\"End\")cell=row===0?6:7;\n    const target=items.findIndex(x=>x.dataset.select===(row===0?\"book\":\"film\")+cell);if(target<0)return;index=target;\n   }else if(host.classList.contains(\"magazine-rows\")){\n    const two=getComputedStyle(host).display===\"grid\"&&getComputedStyle(host).gridTemplateColumns.split(\" \").length>1,per=Math.ceil(items.length/2);\n    if(e.key===\"Home\")index=0;else if(e.key===\"End\")index=items.length-1;\n    else index+=e.key===\"ArrowDown\"?1:e.key===\"ArrowUp\"?-1:e.key===\"ArrowRight\"&&two?per:e.key===\"ArrowLeft\"&&two?-per:0;\n    if(index<0||index>=items.length)return;\n   }else{\n    if(e.key===\"ArrowUp\"||e.key===\"ArrowDown\")return;\n    index=e.key===\"Home\"?0:e.key===\"End\"?items.length-1:index+(e.key===\"ArrowRight\"?1:-1);if(index<0||index>=items.length)return;\n   }\n   e.preventDefault();e.stopPropagation();host._index=index;const el=items[index];\n   host.setAttribute(\"aria-activedescendant\",el.id);items.forEach(x=>x.classList.toggle(\"keyboard-current\",x===el));\n   if(P===\"saga\")selectSaga(el.dataset.select);\n   if(host.classList.contains(\"rail\")||host.classList.contains(\"matrix-window\")){\n    const a=host.getBoundingClientRect(),r=el.getBoundingClientRect();\n    if(r.left<a.left)host.scrollTo({left:host.scrollLeft+r.left-a.left,behavior:\"instant\"});\n    else if(r.right>a.right)host.scrollTo({left:host.scrollLeft+r.right-a.right,behavior:\"instant\"});\n   }\n   revealForKeyboard(el);syncRailEdges();\n  };\n });\n}\n"
from pathlib import Path
root=Path(r"D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\02-detail-pages\finished-mocks\19-generic-universes")
p=root/"app.js";s=p.read_text(encoding="utf-8")
s=s.replace('progress:true,logs:[]','progress:true,dock:"proposed",logs:[]')
a=s.index('const actionOf=');b=s.index('\n',a)
s=s[:a]+'''const actionOf=item=>{if(!item)return;if(item.resolved===false){clickLog("Details pending for "+item.title+" (unresolved local fixture)");return;}clickLog("Open "+item.title+" in "+routeOf(item)+" (simulated)");};'''+s[b:]
s=s.replace('medium:"series",upcoming:true','medium:"series",upcoming:true,resolved:false')
s=s.replace('''(item.upcoming?'aria-disabled="true"':'')''','''(item.resolved===false?'aria-disabled="true"':'')''')
s=s.replace('''class="continue-title">''','''class="continue-title" title="'+safe(item.title)+'">''')
s=s.replace('''<button type="button" data-route="'+safe(item.key)+'" class="continue-resume" aria-label="Resume '+safe(item.title)+'">▶</button>''','''<button type="button" data-resume="'+safe(item.key)+'" class="continue-resume" aria-label="Resume '+safe(item.title)+'"><img src="'+image("icons/"+(item.medium==="book"?"books":item.medium==="manga"?"manga":"play")+".svg")+'" alt=""></button>''')
s=s.replace('''button("Read","book0")+button("Watch","film0")''','''button("Read","book0",state.progress?"Book 1":"")+button("Watch","film0")''')
s=s.replace('''if(item.upcoming)where+=" · UPCOMING";''','''if(item.medium==="film"&&item.story===7)where+=" · PART "+(item.key==="film7"?2:1);
if(item.key.startsWith("extraBook"))where="HOGWARTS LIBRARY · BOOK";if(item.key.startsWith("extraFilm"))where="FANTASTIC BEASTS · FILM";
if(item.upcoming)where+=" · UPCOMING";''')
s=s.replace('''cta=item.medium==="book"?"Read":"Watch",hint=item.upcoming?"Details pending":"Opens in "+routeOf(item);''','''fraction=state.progress?(item.progress||0):0,cta=item.medium==="book"?(fraction>=1?"Read again":fraction>0?"Continue reading":"Read"):(fraction>=1?"Watch again":fraction>0?"Continue watching":"Watch"),hint=item.resolved===false?"Details pending":"Opens in "+routeOf(item);''')
s=s.replace('''button(cta,item.key,"",item.upcoming)''','''button(cta,item.key,"",item.resolved===false)''')
s=s.replace('''style="grid-template-rows:repeat('+per+',78px)"''','''style="grid-template-rows:repeat('+per+',minmax(78px,auto))"''')
s=s.replace('''if(P==="saga")selectSaga(state.selected);attachEvents();''','''if(P==="saga")selectSaga(state.selected);attachEvents();configureCollections();''')
a=s.index('function syncTabs()');b=s.index('function syncRailEdges()',a)
s=s[:a]+HELPERS+"\n"+s[b:]
s=s.replace('''$("app").addEventListener("keydown",e=>{const card=e.target.closest(".continue-card[data-detail]");if(card&&(e.key==="Enter"||e.key===" ")){e.preventDefault();const item=state.items.get(card.dataset.detail);if(item)clickLog("Open "+item.title+" details (simulated)");}});''','''$("app").onkeydown=e=>{
const card=e.target.closest(".continue-card[data-detail]");
if(card&&e.target===card&&(e.key==="Enter"||e.key===" ")){e.preventDefault();const item=state.items.get(card.dataset.detail);if(item)clickLog("Open "+item.title+" details (simulated)");}
if(e.key==="Escape"){e.preventDefault();clickLog("Back requested");}
const tab=e.target.closest(".tab");if(tab&&["ArrowLeft","ArrowRight","Home","End"].includes(e.key)){
 const tabs=[...document.querySelectorAll(".tab")],i=tabs.indexOf(tab),n=e.key==="Home"?0:e.key==="End"?tabs.length-1:(i+tabs.length+(e.key==="ArrowRight"?1:-1))%tabs.length;
 e.preventDefault();tabs[n].focus({preventScroll:true});tabs[n].scrollIntoView({block:"nearest",inline:"nearest",behavior:"instant"});
}
};''')
s=s.replace('''$("app").addEventListener("click",event=>{''','''$("app").onclick=event=>{''')
s=s.replace('''if(action.dataset.tab){const el=$(action.dataset.tab);if(el)page.scrollTo({top:Math.max(0,el.offsetTop-(state.kind==="phone"?130:110)),behavior:"smooth"});return;}''','''if(action.dataset.tab){const el=$(action.dataset.tab),g=dockGeometry();if(el)page.scrollTo({top:Math.max(0,page.scrollTop+el.getBoundingClientRect().top-page.getBoundingClientRect().top-g.floor-56),behavior:"instant"});syncTabs();action.scrollIntoView({block:"nearest",inline:"nearest",behavior:"instant"});return;}''')
s=s.replace('''behavior:"smooth"''','''behavior:"instant"''')
s=s.replace('''if(action.dataset.route){const item=state.items.get(action.dataset.route);if(item)actionOf(item);return;}
});}''','''if(action.dataset.resume){const item=state.items.get(action.dataset.resume);if(item)clickLog("Resume "+item.title+" in "+routeOf(item)+" (simulated)");return;}
if(action.dataset.route){const item=state.items.get(action.dataset.route);if(item)actionOf(item);return;}
};}''')
s=s.replace('''setPreset(preset);
window.__mockAudit''','''state.dock=args.get("tabs")==="original"?"original":"proposed";
$("dockMode").value=state.dock;
$("dockMode").addEventListener("change",e=>{state.dock=e.target.value;syncTabs();});
for(const [key,id] of [["scale","fontScale"],["top","insetT"],["bottom","insetB"],["left","insetL"],["right","insetR"]])if(args.has(key))$(id).value=args.get(key);
setPreset(preset);
window.__mockState=()=>({selected:state.selected,dock:state.dock,kind:state.kind,logs:[...state.logs]});
window.__mockAudit''')
p.write_text(s,encoding="utf-8")
p=root/"style.css";s=p.read_text(encoding="utf-8")
if s.endswith("\\n"):s=s[:-2]+"\n"
s+="""
/* ContinueTile.qml home geometry and glyphs: resume belongs in the 112px art slot. */
@font-face{font-family:UniverseSegoe;src:url("../../../../reference/local-fonts/segoeui.ttf");font-weight:400}
@font-face{font-family:UniverseSegoe;src:url("../../../../reference/local-fonts/seguisb.ttf");font-weight:600}
.back-action,.continue-body{font-family:UniverseSegoe,"Segoe UI",sans-serif}
.continue-kind{font-family:UniverseSegoe,"Segoe UI",sans-serif}
.continue-title{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.continue-sub{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.continue-resume{left:33px;top:50%;transform:translateY(-50%);border-width:1.5px;border-color:rgba(255,255,255,.9);background:rgba(0,0,0,.55)}
.continue-resume img{width:21px;height:21px;object-fit:contain}
.continue-card:has(.continue-kind:first-child) .continue-bar{height:4px}
.work-tile[aria-disabled=true]{cursor:default}
.work-tile:has(.watched) img{opacity:.72}
.keyboard-current{outline:2px solid var(--gold);outline-offset:-2px}
.matrix-window:focus-visible,.branch-shelf:focus-visible,.rail:focus-visible,.magazine-rows:focus-visible{outline:none}
.tabbar.original-attempt{width:max-content;max-width:none;overflow:visible}
.tabbar.original-attempt .tabs-track{max-width:none;overflow:visible}
.page-scroll,.matrix-window,.rail{scroll-behavior:auto}
/* Allow the original numbered table to grow for actual 1.3 text; retain down-first columns. */
.app[data-kind="phone"] .magazine-row,.app[data-kind="tablet"] .magazine-row{height:auto;min-height:78px;grid-template-rows:auto auto;row-gap:3px;padding:8px 0;overflow:visible}
.app[data-kind="phone"] .magazine-details,.app[data-kind="tablet"] .magazine-details{overflow:visible}
.app[data-kind="phone"] .magazine-name,.app[data-kind="tablet"] .magazine-name{font-size:calc(14px * var(--font-scale));line-height:1.2;white-space:normal;overflow:visible;overflow-wrap:anywhere;text-overflow:clip}
.app[data-kind="phone"] .magazine-by,.app[data-kind="tablet"] .magazine-by{font-size:calc(12px * var(--font-scale));line-height:1.2;white-space:normal;overflow:visible;overflow-wrap:anywhere;text-overflow:clip}
.app[data-kind="phone"] .magazine-years,.app[data-kind="tablet"] .magazine-years{font-size:calc(12px * var(--font-scale));line-height:1.2;white-space:normal;overflow:visible}
.app[data-kind="phone"] .magazine-index,.app[data-kind="tablet"] .magazine-index{font-size:calc(24px * var(--font-scale))}
.magazine-row:nth-child(-n+3) .magazine-index{color:var(--ink)}
.creator-name{font-size:calc(13px * var(--font-scale))}.creator-role{font-size:calc(12px * var(--font-scale))}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important;transition:none!important}}
"""
p.write_text(s,encoding="utf-8")
for name in ["saga-universe.html","shelves-universe.html"]:
 p=root/name;s=p.read_text(encoding="utf-8")
 s=s.replace('declared in FIDELITY.md','declared in the batch report')
 s=s.replace('<label class="check">','<label>Phone tab docking — decision pending <select id="dockMode"><option value="proposed">Proposed: original position where it fits; below Back on narrow phones</option><option value="original">Original: always beside Back (source geometry attempt)</option></select></label><label class="check">')
 p.write_text(s,encoding="utf-8")
print("Finished-only Universe fidelity, keyboard, scaling and comparison controls written.")
