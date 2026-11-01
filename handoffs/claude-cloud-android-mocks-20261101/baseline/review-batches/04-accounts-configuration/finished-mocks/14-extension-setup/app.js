(() => {
"use strict";
const $ = (s, root=document) => root.querySelector(s);
const $$ = (s, root=document) => Array.from(root.querySelectorAll(s));
const query = new URLSearchParams(location.search);
const page = document.body.dataset.page;
const viewport = $("#viewport");
const presetSizes = {portrait:[412,900],small:[354,792],landscape:[900,412],smallLandscape:[792,354],desktop:[1280,700],tablet:[800,1280],tabletLandscape:[1280,800]};
const clamp = (n,min,max) => Math.min(max,Math.max(min,Number.isFinite(+n) ? +n : min));
const setMessage = (msg) => { const el=$("#lab-message"); if(el)el.textContent=msg; };
const sizeInput = id => Number($("#"+id)?.value || 0);
let mode = query.get("preset") in presetSizes ? query.get("preset") : "portrait";
if(query.get("capture")==="1")document.body.classList.add("capture");
function preset(name) {
 if(!(name in presetSizes))return;
 mode=name; const [w,h]=presetSizes[name];
 $("#preset").value=name; $("#view-width").value=w; $("#view-height").value=h;
 updateViewport();
}
function updateViewport() {
 const w=clamp(sizeInput("view-width"),320,2000),h=clamp(sizeInput("view-height"),240,2000);
 viewport.style.width=w+"px";viewport.style.height=h+"px";
 const insetNames=["top","right","bottom","left"];
 for(const k of insetNames)viewport.style.setProperty("--safe-"+k,clamp(sizeInput("inset-"+k),0,120)+"px");
 const fs=Number($("#font-scale").value);
 viewport.style.setProperty("--font-scale",Number.isFinite(fs)?fs:1);
 const requestedIme=$("#ime-visible").checked?clamp(sizeInput("ime-height"),0,500):0;
 const safeHeight=h-clamp(sizeInput("inset-top"),0,120)-clamp(sizeInput("inset-bottom"),0,120);
 const appliedIme=Math.min(requestedIme,Math.max(0,safeHeight-160));
 viewport.style.setProperty("--ime",appliedIme+"px");
 viewport.dataset.imeRequested=requestedIme;viewport.dataset.imeApplied=appliedIme;
 viewport.classList.toggle("ime-cramped",appliedIme>0&&safeHeight-appliedIme<260);
 const imeStatus=$("#ime-status");if(imeStatus)imeStatus.textContent=requestedIme?"IME geometry only: requested "+requestedIme+"px; reserved "+appliedIme+"px; "+(safeHeight-appliedIme)+"px available.":"";
 viewport.classList.toggle("as-desktop",mode==="desktop");
 viewport.classList.toggle("as-tablet",mode.startsWith("tablet"));
 viewport.classList.toggle("as-narrow",w<620);
 viewport.classList.toggle("as-low",h<450);
 viewport.classList.toggle("as-wide-layout",w>=1050);
 viewport.classList.toggle("header-compact",w-(mode==="desktop"?108:mode.startsWith("tablet")?64:32)-sizeInput("inset-left")-sizeInput("inset-right")<720);
 viewport.classList.toggle("is-landscape",w>h);
}
$("#preset").addEventListener("change",e=>preset(e.target.value));
for(const id of ["view-width","view-height","inset-top","inset-right","inset-bottom","inset-left","font-scale","ime-visible","ime-height"]){
 $("#"+id).addEventListener("input",()=>{updateViewport();});
}
$("#preset-insets").addEventListener("click",()=>{$("#inset-top").value=24;$("#inset-bottom").value=24;updateViewport();});
$("#clear-insets").addEventListener("click",()=>{for(const x of ["top","right","bottom","left"])$("#inset-"+x).value=0;updateViewport();});
const imeStatus=document.createElement("span");imeStatus.id="ime-status";imeStatus.setAttribute("role","status");$("#lab-message").after(imeStatus);
preset(mode);
for(const k of ["top","right","bottom","left"])if(query.has("inset"+k[0].toUpperCase()+k.slice(1)))$("#inset-"+k).value=query.get("inset"+k[0].toUpperCase()+k.slice(1));
if(query.has("scale"))$("#font-scale").value=query.get("scale");
if(query.get("keyboard")==="1")$("#ime-visible").checked=true;
if(query.has("ime"))$("#ime-height").value=query.get("ime");
updateViewport();
if(page==="setup") {
 const shade=$("#setup-shade"),field=$("#paste-input"),desktopField=$("#desktop-paste");
 const addonChoice=$("#addon-choice");
 const syncAddon=()=>$$(".setup-heading h2,.desktop-head h2").forEach(el=>el.textContent="Set up "+addonChoice.value.trim());
 if(query.has("addon") && [...addonChoice.options].some(o=>o.value===query.get("addon")))addonChoice.value=query.get("addon");
 addonChoice.addEventListener("change",()=>{syncAddon();field.value="";desktopField.value="";setStatus("");updateButton();});syncAddon();
 const status=$("#setup-status"),desktopStatus=$("#desktop-status"),button=$("#paste-button"),desktopInstall=$("#desktop-install");
 function setSetupState(state){const isOpen=state!=="closed";shade.hidden=!isOpen;$("#setup-state").value=isOpen?"open":"closed";$(".store-context").inert=isOpen;if(isOpen){field.value="";desktopField.value="";setStatus("");updateButton();requestAnimationFrame(()=>{const panel=$$(".setup-panel,.setup-desktop-panel").find(Batch4.visible);panel?.querySelector("button")?.focus({preventScroll:true});});}else $("#setup-state").focus({preventScroll:true});}
 function setStatus(msg){status.textContent=msg;status.hidden=!msg;desktopStatus.textContent=msg;}
 function updateButton(){button.textContent=field.value.trim()?"Install":"Paste link";desktopInstall.disabled=!desktopField.value.trim();desktopInstall.classList.toggle("light",!!desktopField.value.trim());}
 function runInstall(value){if(!String(value).trim()){setStatus("The clipboard is empty.");return;}setStatus("Adding…");Batch4.emit("installRequested",{fixture:true,hasLink:true});setMessage("Local mock only: installRequested emitted; no add-on installed. Host completion is outside this static mock.");}
 $("#setup-state").addEventListener("change",e=>setSetupState(e.target.value));
 $("#setup-required").addEventListener("change",e=>$$("[data-skip-setup]").forEach(x=>x.hidden=e.target.checked));
 $$("[data-close-setup]").forEach(x=>x.addEventListener("click",()=>setSetupState("closed")));
 shade.addEventListener("click",e=>{if(e.target===shade||e.target.classList.contains("setup-area"))setSetupState("closed");});
 $$("[data-skip-setup]").forEach(x=>x.addEventListener("click",()=>runInstall("illustrative-original-manifest")));
 $$("[data-open-browser]").forEach(x=>x.addEventListener("click",()=>(Batch4.emit("openSetupUrl",{fixture:true}),setMessage("Open setup page: external browser navigation suppressed in this local mock."))));
 $$("[data-try-again]").forEach(x=>x.addEventListener("click",()=>(Batch4.emit("reloadSetupPage",{fixture:true}),setMessage("Try again: external setup page reload suppressed in this local mock."))));
 field.addEventListener("input",updateButton);desktopField.addEventListener("input",updateButton);
 field.addEventListener("keydown",e=>{if(e.key==="Enter")runInstall(field.value)});
 desktopField.addEventListener("keydown",e=>{if(e.key==="Enter")runInstall(desktopField.value)});
 button.addEventListener("click",()=>runInstall(field.value));
 desktopInstall.addEventListener("click",()=>runInstall(desktopField.value));
 $("#desktop-clip").addEventListener("click",()=>runInstall(""));
 setSetupState(query.get("state")==="closed"?"closed":"open");
 $("#setup-required").checked=query.get("required")==="1";
 $$("[data-skip-setup]").forEach(x=>x.hidden=$("#setup-required").checked);
 if(query.has("link")){field.value=query.get("link");desktopField.value=query.get("link");updateButton();}
 document.addEventListener("keydown",e=>{if(shade.hidden)return;if(e.key==="Escape"){e.preventDefault();setSetupState("closed");}else if(e.key==="Tab")Batch4.contain(e,$$(".setup-panel,.setup-desktop-panel").find(Batch4.visible));});
 $("#ime-visible").addEventListener("change",()=>{if($("#ime-visible").checked&&!shade.hidden){const input=mode==="desktop"?desktopField:field;input.focus();input.scrollIntoView({block:"nearest"});}});
 if(query.get("keyboard")==="1")requestAnimationFrame(()=>{const input=mode==="desktop"?desktopField:field;input.focus();input.scrollIntoView({block:"nearest"});});
}
if(page==="tankoyomi") {
 // Sample language/provider values do not originate from the native Manga bridge.
 const langs=[{code:"en",country:"gb",label:"English",providers:["WeebCentral"]},
 {code:"es",country:"es",label:"Español",providers:["ZonaTMO","NiAdd","Mangasins","leercapitulo"]},
 {code:"pt",country:"br",label:"Português (Brasil)",providers:[]},
 {code:"fr",country:"fr",label:"Français",providers:[]}];
 const original=Object.fromEntries(langs.map(x=>[x.code,x.providers.map((name,i)=>({id:name.toLowerCase().replace(/\W/g,""),name,enabled:true}))]));
 const providers=Object.fromEntries(langs.map(x=>[x.code,original[x.code].map(p=>({...p}))]));
 let selected=query.get("language")||"es", defaultCode="es",masterOn=true,menuOpen=false;
 if(!langs.some(x=>x.code===selected))selected="es";
 const flag=(code,classes="")=>'<span class="flag '+code+' '+classes+'" role="img" aria-label="'+code.toUpperCase()+' flag"></span>';
 const langByCode=code=>langs.find(l=>l.code===code)||langs[0];
 function renderLanguages() {
  $("#language-list").innerHTML=langs.map(l=>{const active=providers[l.code].filter(p=>p.enabled).length,def=l.code===defaultCode;return '<button type="button" class="language-item '+(l.code===selected?"selected":"")+'" data-language="'+l.code+'" aria-pressed="'+(l.code===selected)+'">'+flag(l.country)+'<span class="language-meta"><strong>'+l.label+'</strong><small>'+l.code+" · "+l.providers.length+" source"+(l.providers.length===1?"":"s")+'</small></span><span class="language-status '+(def?"default":"")+'">'+(def?"DEFAULT":active+" active")+"</span></button>";}).join("");
  $$("[data-language]").forEach(b=>b.addEventListener("click",()=>selectLanguage(b.dataset.language)));
  $("#default-name").textContent=langByCode(defaultCode).label;
  $("#default-options").innerHTML=langs.map(l=>'<button type="button" role="option" data-default="'+l.code+'" aria-selected="'+(defaultCode===l.code)+'">'+l.label+"</button>").join("");
  $$("[data-default]").forEach(b=>b.addEventListener("click",()=>{defaultCode=b.dataset.default;selectLanguage(defaultCode);setMenuOpen(false);$("#default-selector").focus();Batch4.emit("setChapterDefaultLanguage",{language:defaultCode});}));
 }
 function renderProviders() {
  const l=langByCode(selected),items=providers[selected];
  const f=$("#route-flag");f.className="flag "+l.country;f.setAttribute("aria-label",l.country.toUpperCase()+" flag");
  $("#route-language").textContent=l.label;$("#route-description").textContent=items.length+" configured source"+(items.length===1?"":"s")+" · same-language fallback only";
  $("#provider-list").innerHTML=items.length?items.map((p,i)=>'<article class="provider-item '+(p.enabled?"":"disabled")+'" data-provider="'+p.id+'"><div class="rank">'+(i+1)+'</div><div class="letter" aria-hidden="true">'+p.name.charAt(0).toUpperCase()+'</div><div class="provider-copy"><div class="provider-title">'+p.name+' <span class="active-label">'+(p.enabled?"ACTIVE":"DISABLED")+'</span></div><div class="rank-detail">Rank '+(i+1)+' · '+p.id+'</div></div><div class="provider-controls"><button class="switch" type="button" role="switch" aria-label="'+(p.enabled?"Disable ":"Enable ")+p.name+'" aria-checked="'+p.enabled+'" data-toggle="'+p.id+'"><span class="track"><span class="knob"></span></span></button><button type="button" class="arrow" aria-label="Move '+p.name+' up" data-move="'+p.id+'" data-dir="-1" '+(i===0?"disabled":"")+'>↑</button><button type="button" class="arrow" aria-label="Move '+p.name+' down" data-move="'+p.id+'" data-dir="1" '+(i===items.length-1?"disabled":"")+'>↓</button></div></article>').join(""):'<div class="muted small" style="padding-top:22px">No providers are enabled for this language.</div>';
  $$("[data-toggle]").forEach(b=>b.addEventListener("click",()=>{const p=providers[selected].find(p=>p.id===b.dataset.toggle);p.enabled=!p.enabled;Batch4.emit("setChapterProviderEnabled",{language:selected,id:p.id,enabled:p.enabled});renderAll();}));
  $$("[data-move]").forEach(b=>b.addEventListener("click",()=>{const arr=providers[selected],from=arr.findIndex(p=>p.id===b.dataset.move),to=from+Number(b.dataset.dir);if(from<0||to<0||to>=arr.length)return;arr.splice(to,0,...arr.splice(from,1));renderProviders();Batch4.emit("moveChapterProvider",{language:selected,id:b.dataset.move,to});const moved=$$('[data-move]').find(x=>x.dataset.move===b.dataset.move&&x.dataset.dir===b.dataset.dir&&!x.disabled)||$('[data-toggle="'+b.dataset.move+'"]');moved?.focus();}));
 }
 function renderAll(){renderLanguages();renderProviders();}
 function selectLanguage(code){if(!langs.some(x=>x.code===code))return;selected=code;$("#selected-language").value=code;setMenuOpen(false);renderAll();}
 function revealDefaultMenu(){if(!menuOpen)return;const scroller=$("#tank-scroll"),menu=$("#default-options"),selector=$("#default-selector");const a=scroller.getBoundingClientRect(),m=menu.getBoundingClientRect(),s=selector.getBoundingClientRect();const group=m.bottom-s.top;if(group<=a.height-16){if(m.bottom>a.bottom-8)scroller.scrollTop+=m.bottom-a.bottom+8;const top=selector.getBoundingClientRect().top;if(top<a.top+8)scroller.scrollTop-=a.top+8-top;}else menu.scrollIntoView({block:"nearest",inline:"nearest"});}
 function setMenuOpen(open){menuOpen=!!open;$("#default-options").hidden=!menuOpen;$("#default-selector").setAttribute("aria-expanded",String(menuOpen));$("#selector-arrow").textContent=menuOpen?"▲":"▼";$("#menu-state").value=menuOpen?"open":"closed";if(menuOpen)requestAnimationFrame(revealDefaultMenu);}
 $("#default-selector").addEventListener("click",()=>setMenuOpen(!menuOpen));
 $("#menu-state").addEventListener("change",e=>{setMenuOpen(e.target.value==="open");if(menuOpen)requestAnimationFrame(revealDefaultMenu);});
 $("#selected-language").addEventListener("change",e=>selectLanguage(e.target.value));
 $("#reset-order").addEventListener("click",()=>{const current=providers[selected];providers[selected]=original[selected].map(p=>current.find(x=>x.id===p.id)||({...p}));renderAll();Batch4.emit("resetChapterProviderOrder",{language:selected});});
 $("#master-switch").addEventListener("click",()=>{masterOn=!masterOn;$("#master-switch").setAttribute("aria-checked",String(masterOn));$("#master-description").textContent=masterOn?"Language-aware manga chapter router":"Tankoyomi is off";Batch4.emit("setMasterEnabled",{enabled:masterOn});});
 function selectTab(name){const tab=name==="about"?"about":"configuration";$("#config-tab").value=tab;$("#configuration-panel").hidden=tab!=="configuration";$("#about-panel").hidden=tab!=="about";$$("[data-tab]").forEach(b=>b.setAttribute("aria-selected",String(b.dataset.tab===tab)));$("#tank-scroll").scrollTop=0;}
 $("#config-tab").addEventListener("change",e=>selectTab(e.target.value));
 $$("[data-tab]").forEach(b=>b.addEventListener("click",()=>selectTab(b.dataset.tab)));
 $$("[data-local-back],[data-local-search]").forEach(b=>b.addEventListener("click",()=>setMessage(b.getAttribute("aria-label")+": navigation suppressed in this local mock.")));
 renderAll();selectTab(query.get("tab")||"configuration");setMenuOpen(query.get("menu")==="open");
 if(menuOpen)requestAnimationFrame(revealDefaultMenu);
 document.addEventListener("keydown",e=>{
 if(e.key==="Escape"&&menuOpen){e.preventDefault();setMenuOpen(false);$("#default-selector").focus();return;}
 if(e.target.matches("[data-tab]")&&["ArrowLeft","ArrowRight","Home","End"].includes(e.key)){e.preventDefault();const name=["ArrowRight","End"].includes(e.key)?"about":"configuration";selectTab(name);$('[data-tab="'+name+'"]').focus();return;}
 if(menuOpen&&e.target.closest(".default-field,#default-options")&&["ArrowUp","ArrowDown","Home","End"].includes(e.key)){
 e.preventDefault();const items=$$("[data-default]"),i=items.indexOf(document.activeElement),j=e.key==="Home"?0:e.key==="End"?items.length-1:Math.max(0,Math.min(items.length-1,i+(e.key==="ArrowUp"?-1:1)));items[j]?.focus();
 }
});
 }
window.colosseumMockAudit=() => {
 const r=viewport.getBoundingClientRect(), scroll=$("#tank-scroll"), shade=$("#setup-shade");
 const outside=$$("button,input",viewport).filter(el=>{if(el.getClientRects().length===0)return false;const b=el.getBoundingClientRect();return b.left<r.left-1||b.right>r.right+1;}).map(el=>el.getAttribute("aria-label")||el.id||el.textContent.slice(0,20));
 return {viewport:{width:r.width,height:r.height},horizontalOverflows:outside,scrollWidth:scroll?.scrollWidth,scrollClientWidth:scroll?.clientWidth,setupOpen:shade?!shade.hidden:null,defaultMenuOpen:$("#default-options")?.hidden===false};
};
})();