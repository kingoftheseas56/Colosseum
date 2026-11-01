from pathlib import Path
import hashlib,json,datetime
base=Path(r'D:\Colosseum-Android-Phone-Mocks-20261101\review-batches\01-sources-manga')
p=base/'finished-mocks/01-theatre-sources/app.js'; s=p.read_text(encoding='utf-8')
repls=[
('function renderRows(){\n  const current=shownRows();','function renderRows(){\n  const focused=document.activeElement, focusId=focused?.dataset?.row, focusAct=focused?.dataset?.act, focusInRows=$("rows").contains(focused), scroll=$("rows").scrollTop;\n  const current=shownRows();'),
('  }).join("")||\'<div class="empty-state">No sources found.</div>\';\n}','  }).join("")||\'<div class="empty-state">No sources found.</div>\';\n  $("rows").scrollTop=scroll;\n  if(focusInRows&&focusId!==undefined){const target=focusAct?$("rows").querySelector(\'[data-act="\'+focusAct+\'"][data-row="\'+focusId+\'"]:not([disabled])\'):$("rows").querySelector(\'.source-row[data-row="\'+focusId+\'"]\'); (target||$("rows").querySelector(\'.source-row[data-row="\'+focusId+\'"]\'))?.focus({preventScroll:true});}\n}'),
('state.menu=!!show;$("providerMenu").hidden=!state.menu;','state.menu=!!show;$("providerMenu").hidden=!state.menu;$("providerScrim").hidden=!state.menu;'),
('if(!open){state.menu=false;$("providerMenu").hidden=true;closeContext()}','if(!open){state.menu=false;$("providerMenu").hidden=true;$("providerScrim").hidden=true;closeContext()}'),
('state.context=r.id;state.menu=false;$("providerMenu").hidden=true;','state.context=r.id;state.menu=false;$("providerMenu").hidden=true;$("providerScrim").hidden=true;'),
('function closeContext(){\n  state.context=null;','function closeContext(){\n  const old=state.context;if(old!==null)$("rows").querySelector(\'.source-row[data-row="\'+old+\'"]\')?.focus({preventScroll:true});\n  state.context=null;'),
('state.mode=mode;state.provider="all";','state.mode=mode;$("viewport").dataset.mode=mode;state.provider="all";'),
('  $("providerTrigger").addEventListener("click",()=>showMenu(!state.menu));','  const menuScrim=document.createElement("div");menuScrim.id="providerScrim";menuScrim.className="provider-scrim";menuScrim.hidden=true;$("sourceSheet").append(menuScrim);menuScrim.addEventListener("click",e=>{e.stopPropagation();showMenu(false)});\n  $("providerTrigger").addEventListener("click",()=>showMenu(!state.menu));'),
('state.provider=b.dataset.provider;state.quality="all";showMenu(false);','state.provider=b.dataset.provider;showMenu(false);'),
('if(e.key==="ArrowDown")next=Math.min(btn.length-1,i+1);','if(e.key==="ArrowDown")next=(i+1)%btn.length;'),
('else if(e.key==="ArrowUp")next=Math.max(0,i-1);','else if(e.key==="ArrowUp")next=(i+btn.length-1)%btn.length;'),
('else if(e.key==="Escape"){showMenu(false);e.preventDefault();return}','else if(e.key==="Escape"){showMenu(false);e.preventDefault();e.stopPropagation();return}\n    else if(e.key==="Tab"){e.preventDefault();e.stopPropagation();return}'),
('if(state.menu && !e.target.closest(".provider-wrap"))showMenu(false);','if(state.menu && !e.target.closest(".provider-wrap") && e.target.id!=="providerScrim"){e.preventDefault();e.stopPropagation();showMenu(false);}'),
('if(params.get("insetTop")){state.insets.top=Number(params.get("insetTop"));$("inset-top").value=state.insets.top;setInsets()}','for(const side of ["top","right","bottom","left"]){const q="inset"+side[0].toUpperCase()+side.slice(1);if(params.has(q)){state.insets[side]=Math.max(0,Number(params.get(q))||0);$("inset-"+side).value=state.insets[side];}}setInsets();')
]
before=hashlib.sha256(p.read_bytes()).hexdigest()
for a,b in repls:
 if a not in s: raise Exception('Missing replacement '+a[:90])
 s=s.replace(a,b,1)
p.write_text(s,encoding='utf-8')
css=base/'finished-mocks/01-theatre-sources/style.css'
with css.open('a',encoding='utf-8') as f:f.write('\n/* Finished review: preserve the approved click-away overlay and right-aligned pick actions. */\n.provider-scrim{position:absolute;inset:0;z-index:29;background:transparent}\n#viewport:not(.is-phone)[data-mode="download"] .source-row,#viewport:not(.is-phone)[data-mode="season"] .source-row{grid-template-columns:54px minmax(0,1fr) 40px 56px}\n.is-phone .lang-chip{color:var(--dim);letter-spacing:0;border-radius:5px}\n')
(base/'evidence/theatre-fix-authored.json').write_text(json.dumps({'at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'appBefore':before,'appAfter':hashlib.sha256(p.read_bytes()).hexdigest(),'changes':['click-away overlay catches input','Escape stops propagation; Tab constrained; arrows wrap','row/context focus restoration','download/season desktop gold action right aligned','all four safe inset parameters supported','phone language chip tokens match frozen original']},indent=2),encoding='utf-8')
print('THEATRE_FIXED_AUTHORED')
