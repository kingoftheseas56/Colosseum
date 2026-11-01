"use strict";
const presets = Object.freeze({
  portrait:[412,900], portraitSmall:[354,792], landscape:[900,412],
  landscapeSmall:[792,354], desktop:[1280,700],
  tabletPortrait:[800,1280], tabletLandscape:[1280,800]
});
const sources = {
  torrents:[
    {title:"The Way of Kings · Brandon Sanderson · EPUB",seeders:82,size:"4.3 MB",pack:false},
    {title:"The Stormlight Archive Book One · The Way of Kings · English",seeders:56,size:"8.7 MB",pack:false},
    {title:"Brandon Sanderson · The Way of Kings · EPUB + MOBI",seeders:29,size:"9.2 MB",pack:true},
    {title:"The Way of Kings · Stormlight Archive · Ebook Edition",seeders:24,size:"6.1 MB",pack:false},
    {title:"The Way of Kings · Brandon Sanderson · 2010 Edition",seeders:18,size:"5.6 MB",pack:false},
    {title:"Stormlight Archive Books 1–3 · English Ebook Bundle",seeders:12,size:"26.1 MB",pack:true},
    {title:"The Way of Kings · Fantasy Ebook · Alternate Source",seeders:7,size:"5.2 MB",pack:false}
  ],
  editions:[
    {format:"EPUB",size:"4.3 MB",source:"LIBGEN",year:"2010",language:"English",best:true},
    {format:"PDF",size:"9.8 MB",source:"LIBGEN",year:"2014",language:"English",best:false},
    {format:"MOBI",size:"6.1 MB",source:"LIBGEN",year:"2010",language:"English",best:false}
  ],
  audio:[
    {format:"M4B",size:"1.21 GB",language:"English",posted:"2020"},
    {format:"MP3",size:"950 MB",language:"English",posted:"2017"}
  ]
};
const fullSynopsis = document.getElementById("synopsis").textContent.trim();
const shortSynopsis = fullSynopsis.split(" In a distant city,")[0];
const $ = id => document.getElementById(id);
const state = {saved:false,torrentsExpanded:false,sourceStates:{},collections:new Set(),open:"",creating:false};
const list = [{id:"favourites",name:"Favorites"},{id:"reading",name:"Reading Later"},{id:"epics",name:"Epic Fantasy"}];
function escapeHTML(value){return String(value).replace(/[&<>"']/g,x=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[x]));}
function note(message){$("review-result").textContent=message;}
function readState(key){return state.sourceStates[key]||"idle";}
function indicator(key){const s=readState(key);return s==="done"?"✓":s==="resolving"?"…":s==="downloading"?"35%":"↓";}
function renderLibrary(){
  $("library-glyph").textContent=state.saved?"✓":"+";
  $("library-label").textContent=state.saved?"In Library":"Library";
  $("library-button").classList.toggle("is-saved",state.saved);
  $("library-button").setAttribute("aria-pressed",String(state.saved));
}
function renderTorrents(){
  const data=state.torrentsExpanded?sources.torrents:sources.torrents.slice(0,5);
  let html=data.map((r,i)=>{
    const key="t"+i, s=readState(key);
    return '<button type="button" class="source-row torrent-row'+(i===0?' is-preferred':'')+'" data-source="'+key+'" aria-label="Choose torrent: '+escapeHTML(r.title)+'"><span class="row-copy"><span class="row-title">'+escapeHTML(r.title)+'</span><span class="row-meta">▲ '+r.seeders+' &nbsp; '+escapeHTML(r.size)+(r.pack?' &nbsp; · &nbsp; PACK':'')+'</span></span><span class="row-indicator '+(s==="done"?"done":"")+'">'+indicator(key)+'</span></button>';
  }).join("");
  if(sources.torrents.length>5)html+='<button type="button" class="source-more" id="torrent-more">'+(state.torrentsExpanded?"See less":"See "+(sources.torrents.length-5)+" more")+'</button>';
  $("torrent-list").innerHTML=html;
  $("torrent-heading").textContent="TORRENTS · "+sources.torrents.length;
  $("torrent-more").onclick=()=>{state.torrentsExpanded=!state.torrentsExpanded;renderTorrents();note("Torrent row display expanded/collapsed locally; source list remains an illustrative fixture.");};
}
function renderEditions(){
  $("edition-list").innerHTML=sources.editions.map((r,i)=>{
    const key="e"+i,s=readState(key);
    return '<button type="button" class="source-row edition-row'+(r.best?' is-preferred':'')+'" data-source="'+key+'" aria-label="Choose '+r.format+' edition"><span class="format-pill">'+escapeHTML(r.format)+'</span><span class="row-copy"><span class="row-meta">'+escapeHTML(r.size)+' &nbsp; · &nbsp; '+escapeHTML(r.source)+' &nbsp; · &nbsp; '+r.year+' &nbsp; · &nbsp; '+r.language+'</span></span><span class="row-indicator '+(s==="done"?"done":"")+'">'+indicator(key)+'</span></button>';
  }).join("");
  $("edition-heading").textContent="EDITIONS · "+sources.editions.length;
}
function renderAudiobook(){
  $("audiobook-list").innerHTML=sources.audio.map((r,i)=>{
    const key="a"+i,s=readState(key);
    return '<button type="button" class="source-row audiobook-row" data-source="'+key+'" aria-label="Choose '+r.format+' audiobook"><span class="format-pill">'+r.format+'</span><span class="row-copy"><span class="row-meta">'+escapeHTML(r.size)+' &nbsp; · &nbsp; '+r.language+' &nbsp; · &nbsp; '+r.posted+'</span></span><span class="row-indicator '+(s==="done"?"done":"")+'">'+indicator(key)+'</span></button>';
  }).join("");
  $("audiobook-heading").textContent="AUDIOBOOK · "+sources.audio.length;
}
function renderSources(){renderTorrents();renderEditions();renderAudiobook();}
function openModal(kind){
  closeModals(false);
  if(kind==="collections"){
    BatchCollections.open({host:"#safe-zone",trigger:$("collections-button"),key:"book-way-of-kings",title:document.querySelector(".book-title").textContent,kind:"book",kindLabel:"Book",year:"2010",cover:document.querySelector(".book-jacket img").getAttribute("src"),onSaved:()=>note("Collection membership saved in this local mock only.")});
    return;
  }
  state.open=kind;
  const shade=$(kind==="read"?"read-shade":"collections-shade");
  shade.hidden=false;
  $("book-scroll").setAttribute("aria-hidden","true");
  if(kind==="read"){renderReadChoice();const row=shade.querySelector(".choice-row");if(row)row.focus();}
  else {renderCollections();$("collection-search").focus();}
}
function closeModals(restore=true){
  $("read-shade").hidden=true;$("collections-shade").hidden=true;
  state.open="";$("book-scroll").removeAttribute("aria-hidden");
  if(restore)$("read-button").focus();
}
function renderReadChoice(){
  const rows=[
    ...sources.editions.map((r,i)=>({key:"e"+i,label:r.format,meta:[r.size,r.source,r.year,r.language].join("   ·   ")})),
    ...sources.torrents.slice(0,5).map((r,i)=>({key:"t"+i,label:"EBOOK",meta:r.title}))
  ];
  $("read-choices").innerHTML=rows.map(r=>'<button type="button" class="choice-row" data-choice="'+r.key+'"><span class="choice-copy"><strong>'+escapeHTML(r.label)+'</strong><small>'+escapeHTML(r.meta)+'</small></span><span class="choice-read">Read</span></button>').join("");
}
function renderCollections(){
  const q=$("collection-search").value.toLowerCase().trim();
  $("picker-memberships").innerHTML=list.filter(c=>c.name.toLowerCase().includes(q)).map(c=>'<button type="button" class="membership-row" aria-pressed="'+state.collections.has(c.id)+'" data-membership="'+c.id+'"><span class="membership-mark">'+(state.collections.has(c.id)?"✓":"")+'</span><span>'+escapeHTML(c.name)+'</span></button>').join("") || '<p class="picker-note">No collections match that name.<br>Create one below, or clear your search.</p>';
  $("picker-create").hidden=!state.creating;
  $("new-collection").hidden=state.creating;
  $("picker-done").textContent=state.creating?"Create":"Done";
}
function setViewport(){
  const w=Math.max(300,Math.min(1920,Number($("viewport-width").value)||412));
  const h=Math.max(300,Math.min(1600,Number($("viewport-height").value)||900));
  const preset=$("preset").value;
  let mode;
  if(preset.startsWith("tablet"))mode="tablet";
  else if(Math.min(w,h)<600)mode=w>h?"phone-landscape":"phone-portrait";
  else if(w<=1000||h>750)mode="tablet";
  else mode="desktop";
  const app=$("app-screen");
  app.dataset.mode=mode;
  app.style.setProperty("--vw",w+"px");
  app.style.setProperty("--vh",h+"px");
  app.style.setProperty("--fs",$("font-scale").value);
  app.style.setProperty("--safe-top",(Number($("inset-top").value)||0)+"px");
  app.style.setProperty("--safe-bottom",(Number($("inset-bottom").value)||0)+"px");
  const coverH=Math.max(144,Math.min(402,h-132));
  app.style.setProperty("--land-cover-h",coverH+"px");
  app.style.setProperty("--land-cover-w",Math.round(coverH*2/3)+"px");
  $("measure").textContent=w+" × "+h+" · "+$("inset-top").value+"px / "+$("inset-bottom").value+"px safe insets · "+mode;
}
function applyScenario(){
  BatchCollections.close();
  state.saved=false;state.torrentsExpanded=false;state.sourceStates={};state.collections.clear();state.creating=false;
  $("collection-search").value="";$("collection-name").value="";
  $("synopsis").textContent=$("scenario").value==="short"?shortSynopsis:fullSynopsis;
  $("read-status").textContent="Read will choose a tracked edition";
  closeModals(false);
  if($("scenario").value==="saved")state.saved=true;
  if($("scenario").value==="expanded")state.torrentsExpanded=true;
  renderLibrary();renderSources();
  if($("scenario").value==="read")openModal("read");
  if($("scenario").value==="collections")openModal("collections");
  note("Local illustrative states only. No provider request or application operation has occurred.");
}
$("preset").addEventListener("change",()=>{
  const v=presets[$("preset").value];
  if(v){$("viewport-width").value=v[0];$("viewport-height").value=v[1];}
  setViewport();
});
for(const id of ["viewport-width","viewport-height"]){
  $(id).addEventListener("input",()=>{$("preset").value="custom";setViewport();});
}
for(const id of ["inset-top","inset-bottom","font-scale"])$(id).addEventListener("input",setViewport);
$("scenario").addEventListener("change",applyScenario);
$("reset-fixture").onclick=()=>{$("scenario").value="full";applyScenario();};
$("read-button").onclick=()=>{openModal("read");note("Read source choice opened locally (no acquisition started).");};
$("read-cancel").onclick=()=>{closeModals();note("Read source choice cancelled locally.");};
$("library-button").onclick=()=>{state.saved=!state.saved;renderLibrary();note(state.saved?"Library button now shows the local saved state.":"Library button now shows its original unsaved state.");};
$("collections-button").onclick=()=>{openModal("collections");note("Approved Add to Collections picker mock opened. Memberships are local fixtures.");};
$("collections-close").onclick=()=>closeModals();
$("picker-cancel").onclick=()=>closeModals();
$("collection-search").oninput=renderCollections;
$("new-collection").onclick=()=>{state.creating=true;renderCollections();$("collection-name").focus();};
$("picker-done").onclick=()=>{
  if(state.creating){
    const name=$("collection-name").value.trim();
    if(!name){note("Collection name required in the local fixture.");$("collection-name").focus();return;}
    const id="fixture"+list.length;list.push({id,name});state.collections.add(id);state.creating=false;
  }
  closeModals();note("Collection selection saved within this mock only.");
};
$("book-back").onclick=()=>note("Back would return to the Biblio world. Standalone mock keeps this page in view.");
document.querySelectorAll("[data-window]").forEach(b=>b.onclick=()=>note("Desktop window action is visual-only in this mock: "+b.getAttribute("aria-label")));
$("read-choices").addEventListener("click",e=>{
  const btn=e.target.closest("[data-choice]");if(!btn)return;
  const key=btn.dataset.choice;state.sourceStates[key]="resolving";closeModals();
  $("read-status").textContent="Preparing your edition…";
  renderSources();note("Illustrative resolving state shown. No download, read session or backend action started.");
});
document.querySelectorAll(".source-list").forEach(el=>el.addEventListener("click",e=>{
  const btn=e.target.closest("[data-source]");if(!btn)return;
  const key=btn.dataset.source;state.sourceStates[key]=readState(key)==="done"?"idle":"done";
  renderSources();note("Source icon state changed only in HTML; not a download or provider operation.");
}));
$("picker-memberships").addEventListener("click",e=>{
  const btn=e.target.closest("[data-membership]");if(!btn)return;
  const id=btn.dataset.membership;if(state.collections.has(id))state.collections.delete(id);else state.collections.add(id);
  renderCollections();
});
document.addEventListener("keydown",e=>{
  if(state.open==="read"&&["ArrowDown","ArrowUp","Home","End"].includes(e.key)){
    const rows=[...document.querySelectorAll("#read-choices .choice-row")];
    const i=rows.indexOf(document.activeElement);
    const next=e.key==="Home"?0:e.key==="End"?rows.length-1:(i+(e.key==="ArrowDown"?1:-1)+rows.length)%rows.length;
    rows[next]?.focus();e.preventDefault();return;
  }
  if(e.key==="Escape"&&state.open){closeModals();e.preventDefault();}
  if(e.key==="Tab"&&state.open){
    const area=$(state.open==="read"?"read-shade":"collections-shade");
    const focusable=Array.from(area.querySelectorAll("button,input")).filter(el=>el.offsetParent!==null&&!el.disabled);
    const ix=focusable.indexOf(document.activeElement);
    if(e.shiftKey&&ix<=0){focusable[focusable.length-1].focus();e.preventDefault();}
    else if(!e.shiftKey&&ix===focusable.length-1){focusable[0].focus();e.preventDefault();}
  }
});
const params=new URLSearchParams(location.search);
if(params.has("preset")&&presets[params.get("preset")])$("preset").value=params.get("preset");
if(params.has("width")||params.has("height"))$("preset").value="custom";
const selected=presets[$("preset").value];
if(selected){$("viewport-width").value=selected[0];$("viewport-height").value=selected[1];}
if(params.has("width"))$("viewport-width").value=params.get("width");
if(params.has("height"))$("viewport-height").value=params.get("height");
if(params.has("insetTop"))$("inset-top").value=params.get("insetTop");
if(params.has("insetBottom"))$("inset-bottom").value=params.get("insetBottom");
if(params.has("scale"))$("font-scale").value=params.get("scale");
if(params.has("scenario"))$("scenario").value=params.get("scenario");
setViewport();applyScenario();
