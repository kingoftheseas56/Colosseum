(function bootstrap(){
"use strict";
const AS="../../../reference/desktop/assets/";
const icons={simkl:"icons/trackers/simkl.svg",mal:"icons/trackers/myanimelist.svg",trakt:"icons/trackers/trakt.svg",anilist:"icons/trackers/anilist.svg",global:"icons/preferences.svg"};
const providers={
simkl:{name:"Simkl",subject:"Movies, series, and anime",caps:["History import","Progress import","Progress export","Completion export","Playback tracking"]},
mal:{name:"MyAnimeList",subject:"Anime and manga",caps:["History import","Progress import"]},
trakt:{name:"Trakt",subject:"Movies and series",caps:["History import","Playback tracking","Progress export"]},
anilist:{name:"AniList",subject:"Anime and manga",caps:[]},
global:{name:"Preferences",subject:"",caps:[]}
};
const sourceRows=[
{id:"one-piece",title:"One Piece",classification:"Progress differs",progress:"Tracker: Episode 1045 · Colosseum: Episode 1041",choices:["Use tracker progress","Keep Colosseum progress","Leave for later"]},
{id:"naruto",title:"Naruto",classification:"Tracker is ahead",progress:"Tracker: Episode 40 · Colosseum: Episode 26",choices:["Use tracker progress","Keep Colosseum progress","Leave for later"]},
{id:"aot",title:"Attack on Titan",classification:"Needs a title match",progress:"Tracker: Episode 12 · No Colosseum progress yet",choices:["Find match","Leave unmatched"]},
{id:"spirited-away",title:"Spirited Away",classification:"Already matches",progress:"Tracker: Completed · Colosseum: Completed",protected:true,choices:["Keep Colosseum progress"]}
];
const exportRows=[
{id:"one-piece",title:"One Piece",kind:"Progress",progress:1041,before:"Episode 1039",after:"Episode 1041",reason:"Colosseum has newer progress.",eligible:true},
{id:"naruto",title:"Naruto",kind:"Completion",progress:0,before:"In progress",after:"Completed",reason:"Colosseum has a completed entry.",eligible:true},
{id:"aot",title:"Attack on Titan",kind:"Progress",progress:12,before:"Episode 12",after:"Episode 12",reason:"No change needed.",eligible:false}
];
const seed={trackerSyncEnabled:true,checkOnLaunch:true,backgroundDelivery:false,completionMessages:true,pull:true,send:false,live:true};
const mode=document.body.dataset.start||"connections";
const query=new URLSearchParams(location.search);
const capture=query.get("capture")==="1"||document.body.classList.contains("capture");
if(capture)document.body.classList.add("capture");
const state={scenario:query.get("scenario")||"connected",panel:mode==="dossier"?"simkl":mode==="preferences"?"global":null,dialog:query.get("dialog")||document.body.dataset.dialog||null,selected:new Set(),chosen:{},selectedExports:new Set(["one-piece"]),toggles:{...seed},fontScale:1,provider:"simkl",platform:"android",scroll:{page:0,detail:0,dialog:0},search:"",lastFocus:null,connectedOverrides:{},removedImports:{},matches:{},importConfirmed:{},exportConfirmed:{},providerToggles:{},reviews:{},returnFocus:null};
if(query.has("provider")&&providers[query.get("provider")]){state.provider=query.get("provider");if(mode!=="connections")state.panel=state.provider;}
const windowEl=document.getElementById("appWindow"),app=document.getElementById("app"),labStatus=document.getElementById("labStatus");
const el=(id)=>document.getElementById(id);
const n=(x,fallback=0)=>{if(x===null||x===undefined||String(x).trim()==="")return fallback;const v=Number(x);return Number.isFinite(v)?v:fallback;};
const safe=(s)=>String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const asset=(p,alt,cls="")=>`<img class="${cls}" src="${AS+p}" alt="${safe(alt)}">`;
const icon=(p,alt,cls="")=>asset(icons[p]||icons.global,alt,cls);
const action=(text,act,cls="panel-btn",attrs="")=>`<button type="button" class="${cls}" data-action="${act}" ${attrs}>${text}</button>`;
function note(message){if(labStatus)labStatus.textContent=message;}
function connected(){const base=state.scenario==="disconnected"?[]:state.scenario==="attention"?["simkl","trakt"]:["simkl","mal"];return ["simkl","mal","trakt"].filter(k=>state.connectedOverrides[k]===undefined?base.includes(k):state.connectedOverrides[k]);}
function status(key){if(state.scenario==="attention"&&key==="simkl")return"Attention";return connected().includes(key)?"Confirmed":key==="anilist"?"Coming soon":"Connect";}
function avail(key){return key!=="anilist";}
function pageSummary(){const count=connected().length;if(!count)return"No third-party trackers connected";return `${count} connected${state.scenario==="attention"?" · 2 need attention":" · 2 waiting · 2 need review"}`;}
function setViewport(w,h,platform=state.platform){
  w=Math.max(320,Math.min(1920,Math.round(n(w,412))));
  h=Math.max(260,Math.min(1800,Math.round(n(h,900))));
  state.platform=platform;app.classList.toggle("keyboard",query.get("keyboard")==="1");app.style.setProperty("--keyboard",query.get("keyboard")==="1"?Math.min(280,Math.round(h*.36))+"px":"0px");
  state.viewWidth=w;state.viewHeight=h;app.classList.toggle("short450",h<=450);
  [640,700,760,900,1380].forEach(bp=>app.classList.toggle("w"+bp,w>=bp));
  app.classList.toggle("narrow430",w<=430);
  windowEl.style.setProperty("--viewport-width",w+"px");
  windowEl.style.setProperty("--viewport-height",h+"px");
  windowEl.dataset.platform=platform;
  app.style.setProperty("--page-inset",Math.min(54,Math.max(22,w*.05))+"px");
  app.style.setProperty("--f",String(state.fontScale));
  app.style.setProperty("--top-safe",Math.max(0,n(el("topInset")?.value,n(document.body.dataset.top,0)))+"px");
  app.style.setProperty("--bottom-safe",Math.max(0,n(el("bottomInset")?.value,n(document.body.dataset.bottom,0)))+"px");
  if(el("vw"))el("vw").value=w;
  if(el("vh"))el("vh").value=h;
  applyFitting();
}
function shell(){
const back=`<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 L8 12 L14.5 18.5"/></svg>`;
return `<div class="app-bg" aria-hidden="true"></div>
<header class="shell-head"><button class="back-action" data-action="back" type="button" aria-label="Back">${back}<span>Back</span></button>
<div class="window-buttons" aria-label="Desktop window controls">
${["minimize.svg","fullscreen-exit.svg","power.svg"].map((v,i)=>`<button aria-label="${["Minimize","Exit fullscreen","Quit Colosseum"][i]}" data-action="window-control">${asset("icons/"+v,"")}</button>`).join("")}</div></header>`;
}
function heading(){
return `<div class="page-heading"><div><h1 class="page-title">Connections</h1><p class="page-subtitle">Your external library connections</p></div>
<div class="page-actions"><button type="button" class="glass-btn" data-action="preferences">${asset("icons/preferences.svg","")}<span>Preferences</span></button>
<button type="button" class="glass-btn gold-btn" data-action="sync-all" ${connected().length?"":"disabled"}>${asset("icons/sync.svg","")}<span>Sync all</span></button></div></div>`;
}
function nativeEmpty(){
return `<p class="empty-summary">${pageSummary()}</p><div class="native-empty" id="trackerEmptyState">${asset("icons/colosseum.svg","Colosseum","native-logo")}
<div class="native-divider" aria-hidden="true"></div><div class="native-copy"><div class="kicker">NATIVE LIBRARY</div><div class="native-name">Colosseum</div><div class="native-message">Works without trackers.</div>
<p class="native-detail">Your native progress, History, and activity stay here.</p></div><div class="native-available">✓&nbsp; Always available</div></div>`;
}
function relay(){
return `<section class="relay" aria-label="Tracker library relation"><div class="relay-library">${asset("icons/colosseum.svg","")}
<div><strong>Your library stays in Colosseum</strong><p>Trackers connect to your native progress and History.</p></div></div>
<p class="relay-summary ${state.scenario==="attention"?"attention":""}">${pageSummary()}</p></section>`;
}
function attention(){
return state.scenario==="disconnected"?"":`<div class="attention-region" id="trackerAttentionRegion"><b class="attention-mark">!</b>
<strong class="attention-title">${state.scenario==="attention"?"2 trackers need attention":"2 tracker items need review"}</strong>
<span class="attention-note">Native History is unchanged.</span></div>`;
}
function connectedCards(){
if(!connected().length)return"";
return `<section class="section" id="trackerConnectedSection"><div class="section-head"><h2 class="section-title">Connected trackers</h2><span class="section-meta">This profile</span></div>
<div class="connected-grid" id="trackerConnectedGrid">${connected().map((key,i)=>{
const p=providers[key],s=status(key),warning=s==="Attention";
return `<button class="connected-card ${warning?"attention":""}" data-action="open-provider" data-key="${key}" aria-label="${p.name}, ${s}" type="button">
<div class="card-top"><span class="icon-tile">${icon(key,p.name)}</span><div class="card-identity"><div class="card-name">${p.name}</div>
<div class="card-account">Connected to this profile</div></div><span class="card-status ${warning?"warn":""}">${s}</span></div>
<div class="card-bottom"><span class="card-stat"><strong>${i===0?"2 h ago":"Never"}</strong><small>LAST SYNC</small></span>
<span class="card-stat"><strong>${i===0?"3":"1"}</strong><small>WAITING / REVIEW</small></span>
<span class="card-open">${warning?"Review ›":"Open ›"}</span></div></button>`;
}).join("")}</div></section>`;
}
function catalogue(){
return `<section class="section" id="trackerCatalogueSection"><div class="section-head"><h2 class="section-title">Available trackers</h2>
<span class="section-meta catalogue-meta">Capabilities are verified per provider</span></div><div class="catalogue-grid" id="trackerCatalogueGrid">
${["simkl","mal","trakt","anilist"].map(key=>{
const p=providers[key],s=connected().includes(key)?"Connected":key==="anilist"?"Coming soon":state.scenario==="attention"&&key==="simkl"?"Needs attention":"Connect";
return `<button class="catalogue-card" type="button" data-action="open-provider" data-key="${key}" aria-label="${p.name}, ${s}">
<div class="catalogue-row">${icon(key,p.name)}<div><div class="catalogue-name">${p.name}</div><div class="catalogue-subject">${p.subject}</div></div></div>
<span class="catalogue-state ${s==="Needs attention"?"warn":""}">${s}</span></button>`;
}).join("")}</div></section>`;
}
function external(){
return `<section class="section" id="trackerMainSyncSection"><h2 class="section-title">External services</h2>
<p class="external-intro">Manage connections to external libraries.</p><div class="stremio-card" id="trackerStremioCard">
${asset("icons/stremio-official.svg","Stremio","stremio-icon")}
<div><div class="stremio-name">Stremio</div><p class="stremio-description">Library, progress, and History</p>
<div class="capability-tags"><span>Library</span><span>Progress</span><span>History</span></div></div>
<div class="stremio-actions"><span class="status-pill">Not connected</span>
<button class="stremio-open" type="button" data-action="open-stremio">Open settings ›</button></div></div></section>`;
}
function bottomDock(){
return `<div class="desktop-dock" aria-label="Desktop Colosseum taskbar"><button type="button" class="dock-item" data-action="dock-route" aria-label="Colosseum taskbar">${asset("icons/colosseum.svg","Colosseum")}</button>
<button type="button" class="dock-item" data-action="dock-route" aria-label="Vault">${asset("icons/vault-door.svg","Vault")}</button>
<button type="button" class="dock-item" data-action="dock-route" aria-label="Your Collections"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#f7f7f5" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/></svg></button></div>`;
}
function basePage(){
return `${shell()}<main class="page-scroll" id="pageScroll" aria-label="Tracker Connections"><div class="page-column">
${heading()}${connected().length?relay():nativeEmpty()}${attention()}${connectedCards()}${catalogue()}${external()}</div></main>${bottomDock()}`;
}
function providerSeed(provider){return {...seed,live:provider!=="mal"};}
function canExport(provider){return providers[provider]?.caps.some(c=>c.endsWith("export"));}
function toggle(label,key){
 const disabled=(["send","live"].includes(key)&&state.panel==="mal")||(state.scenario==="attention"&&state.panel==="simkl");
 const value=(["pull","send","live"].includes(key)?(state.providerToggles[state.panel]||providerSeed(state.panel)):state.toggles)[key];
return `<button class="toggle-row" type="button" role="switch" aria-checked="${value}" data-action="toggle" data-key="${key}" ${disabled?"disabled":""}>
<span>${label}</span><span class="switch-track" aria-hidden="true"></span></button>`;
}
function accountGroup(){
return `<section class="content-group" id="trackerAccountProfile"><h3 class="group-title">Account and profile</h3>
<div class="account-label">Connected to this profile</div>
<div class="account-sub">Only this Colosseum profile can use this connection</div>
<div class="group-actions">${action("Disconnect from this profile","disconnect")}
${action("Remove imported data","remove-data")}</div></section>`;
}
function capabilities(key){
return `<section class="content-group" id="trackerCapabilitySection"><h3 class="group-title">Capabilities</h3>
${providers[key].caps.length?`<div class="cap-pills">${providers[key].caps.map(c=>`<span>${c}</span>`).join("")}</div>`:
`<p class="section-description">No capabilities are verified in this build.</p>`}</section>`;
}
function importGroup(){
return `<section class="content-group" id="trackerImportReviewSection"><h3 class="group-title">Import review</h3>
<div class="info-card"><div class="info-row"><div><div class="info-title">Initial import</div>
<div class="info-detail">2 items need a decision · 1 item left unchanged</div></div>
${action(state.importConfirmed[state.panel]?"View review":"Review import","review-import","review-btn")}</div></div></section>`;
}
function deliveryGroup(){
if(!canExport(state.panel))return "";
return `<section class="content-group" id="trackerDeliveryQueueSection"><h3 class="group-title">Delivery status</h3>
${[{title:"One Piece",status:"Checking delivery",reason:"The provider may have received this. Colosseum is checking before any retry."},
{title:"Attack on Titan",status:"Waiting to sync",reason:"Saved in Colosseum. Waiting for the tracker to sync."}].map(row=>
`<div class="info-card"><div class="delivery-title"><b>${row.title}</b><span>${row.status}</span></div><p class="delivery-reason">${row.reason}</p></div>`).join("")}</section>`;
}
function automaticGroup(){
return `<section class="content-group" id="trackerAutomaticSyncSummary"><h3 class="group-title">Automatic sync</h3>
${toggle("Pull changes automatically","pull")}${toggle("Send Colosseum progress","send")}
<div style="margin:10px 0 9px">${canExport(state.panel)&&!state.exportConfirmed[state.panel]?action("Review Colosseum updates before sending","review-export","review-btn gold"):""}</div>
${toggle("Live playback tracking","live")}</section>`;
}
function globalGroup(){
return `<section class="content-group" id="trackerGlobalPreferencesSummary"><h3 class="group-title">Current preferences</h3>
${toggle("Tracker sync","trackerSyncEnabled")}${toggle("Check connected trackers on launch","checkOnLaunch")}
${toggle("Deliver in the background","backgroundDelivery")}${toggle("Show completion messages","completionMessages")}</section>`;
}
function dossier(){
if(!state.panel)return"";
const key=state.panel,p=providers[key],on=connected().includes(key),isGlobal=key==="global";
const attentionState=state.scenario==="attention"&&key==="simkl";
const connectStatus=isGlobal?"Confirmed":on?(attentionState?"Attention":"Confirmed"):key==="anilist"?"Coming soon":"Connect";
const explanation=isGlobal?"Trackers are optional. Colosseum saves local changes first.":
attentionState?"Tracker work remains paused on this profile. Reconnect the same account to check uncertain delivery before any retry.":
on?"Provider status and actions follow the verified capability snapshot.":
key==="anilist"?"Coming soon.":key==="trakt"?
"Connect Trakt to this Colosseum profile to import history and track playback.":
"Connect only when this build has a verified provider authentication path.";
const connect=!on&&!isGlobal?`<div class="health-action">${action(key==="anilist"?"Coming soon":"Connect","connect-attempt","panel-btn",key==="anilist"?"disabled":"")}</div>`:"";
const noteTrakt=key==="trakt"?`<p>Stremio link: Not linked</p>`:"";
return `<div class="overlay-veil" id="dossierVeil" data-action="veil-close"><aside class="dossier" role="dialog" aria-modal="true" aria-label="${isGlobal?"Connection preferences":p.name+" tracker details"}">
<div class="dossier-head"><span class="icon-tile">${icon(key,isGlobal?"Preferences":p.name)}</span>
<div class="dossier-titles"><div class="dossier-kicker">${isGlobal?"ALL TRACKERS":on?"CONNECTED TRACKER":attentionState?"PAUSED TRACKER WORK":"AVAILABLE TRACKER"}</div>
<h2 class="dossier-title" id="trackerDossierTitle">${p.name}</h2></div>
<button class="icon-close" type="button" data-action="close-dossier" aria-label="Close tracker details">×</button></div>
<div class="dossier-scroll" id="dossierScroll"><div class="dossier-content">
<section class="health" id="trackerHealth"><div class="health-top"><span class="health-state ${attentionState?"warn":""}">${connectStatus}</span>
<span class="tinycaps">${on?"THIS PROFILE":"CAPABILITY GATE"}</span></div>
<p>${explanation}</p>${noteTrakt}${connect}</section>
${isGlobal?globalGroup():(on?accountGroup():"")+capabilities(key)+(on?(state.removedImports[key]?"":importGroup())+deliveryGroup()+automaticGroup():"")}
</div></div></aside></div>`;
}
function dialogHead(title){return `<header class="dialog-head"><h2>${title}</h2><button class="icon-close" type="button" data-action="dialog-close" aria-label="Close review">×</button></header>`;}
function importDialog(){
const selected=state.selected;
const bulk=selected.size?`<div class="bulk-row"><span>${selected.size} items selected</span>
<div class="choice-row">${action("Review shared choices","bulk-review","panel-btn",selected.size<2?"disabled":"")}
${action("Clear selection","clear-selection","panel-btn")}</div></div>`:"";
const reviewRows=sourceRows.map(row=>{
const choice=state.chosen[row.id];
return `<article class="import-item ${row.protected?"protected":""}">
<div class="import-item-row">${action(selected.has(row.id)?"Selected":"Select","select-import",`small-choice ${selected.has(row.id)?"selected":""}`,`data-id="${row.id}" aria-pressed="${selected.has(row.id)}"`)}
<strong>${row.title}</strong><em>${row.classification}</em></div>
<p>${row.progress}</p>${row.protected?`<p class="inline-warning">Native History protects this title. Keep Colosseum progress is the only available choice.</p>`:""}
${state.matches[row.id]?`<p class="info-detail">Matched in Colosseum: ${safe(state.matches[row.id])}</p>`:""}
${choice?`<p class="info-detail">${choice==="Keep Colosseum progress"?"Kept Colosseum progress":choice==="Leave unmatched"?"Left unmatched · no Colosseum progress was changed":choice==="Leave for later"?"Left for later · Colosseum progress was not changed":"Approved · waiting for Colosseum to apply progress"}</p>`:
`<div class="choice-row">${(state.matches[row.id]?["Use tracker progress","Keep Colosseum progress","Leave for later"]:row.choices).map(c=>action(c,"import-choice","panel-btn",`data-id="${row.id}" data-choice="${safe(c)}"`)).join("")}</div>`}
</article>`;
}).join("");
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog dialog-import" role="dialog" aria-modal="true" aria-label="Tracker import review">
${dialogHead("Initial import · "+providers[state.panel||"simkl"].name)}
<p>Review each item before importing. Colosseum’s native History and witnessed activity stay protected; tracker progress never creates a watched event by itself.</p>
${bulk}<div class="dialog-list" id="dialogList">${reviewRows}</div>
<div class="dialog-footer">${action(state.importConfirmed[state.panel]?"Review confirmed":"Confirm import review","confirm-import","review-btn gold",state.importConfirmed[state.panel]||sourceRows.some(r=>!state.chosen[r.id])?"disabled":"")}
${action("Close","dialog-close","panel-btn")}</div></section></div>`;
}
function exportDialog(){
const rows=exportRows.map(row=>`<label class="export-item" aria-label="${row.title}, ${row.kind}">
<input type="checkbox" data-action="export-item" value="${row.id}" ${state.selectedExports.has(row.id)?"checked":""} ${row.eligible?"":"disabled"}>
<span class="check-visual" aria-hidden="true"></span><span>${row.title} · ${row.kind}${row.kind==="Progress"?" "+row.progress:""}
<span class="export-reason">Tracker now: ${row.before} → after send: ${row.after} · ${row.reason}</span></span></label>`).join("");
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog dialog-export" role="dialog" aria-modal="true" aria-label="Review Colosseum updates before sending">
${dialogHead("Send to "+providers[state.panel||"simkl"].name)}
<p>Choose the Colosseum updates you want to send. This review is separate from importing tracker data.</p>
<div class="dialog-list" id="dialogList">${rows}</div>
<div class="dialog-footer"><span class="info-detail">${state.selectedExports.size} selected</span>
${action("Send "+state.selectedExports.size+" to "+providers[state.panel||"simkl"].name,"confirm-export","review-btn gold",state.selectedExports.size?"":"disabled")}</div></section></div>`;
}
function disconnectDialog(){
const provider=providers[state.panel||"simkl"].name;
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog" role="dialog" aria-modal="true" aria-label="Disconnect tracker from this profile">
${dialogHead("Disconnect "+provider+"?")}
<div class="dialog-scroll"><p class="dialog-text">This disconnects the tracker from this Colosseum profile. It does not delete the tracker account or Colosseum's own History, Progress, or Activity. Imported tracker data remains here until you remove it separately.</p>
<p class="dialog-text" style="margin-top:12px">Pending: 3 · Known unsent: 2 · Unknown outcome: 1</p>
<p class="dialog-text" style="margin-top:12px">After disconnecting, Colosseum will ask the tracker to revoke this app's access. That request is best-effort: the tracker always acknowledges it, so it cannot be confirmed from here. Check the service's connected-app settings if you want certainty.</p>
<p class="inline-warning" style="margin-top:12px">1 update may already have reached the tracker. It cannot be discarded or resent while uncertain; it stays paused until reconnect so Colosseum can check first.</p></div>
<div class="dialog-actions">${action("Disconnect and keep pending work","no-disconnect")}
${action("Disconnect and discard known-unsent work","no-disconnect")}${action("Cancel","dialog-close")}</div>
</section></div>`;
}
function removeDialog(){
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog" role="dialog" aria-modal="true" aria-label="Review removal of tracker-imported data">
${dialogHead("Remove imported data from "+providers[state.panel||"simkl"].name+"?")}
<div class="dialog-scroll"><p class="dialog-text">2 tracker-imported History items and 1 tracker-imported Progress item will be removed for this tracker account.</p>
<p class="inline-warning" style="margin-top:12px">Only this account's tracker-imported History evidence and eligible tracker-imported Progress are removed. Native History, Activity, statistics, local Progress, and another tracker's data are preserved. No remote tracker account or data is changed.</p>
<p class="info-title" style="margin-top:12px">Affected entries</p><div class="import-preview">
${["One Piece","Naruto","Attack on Titan"].map((title,i)=>`<div class="info-card"><div class="info-title">${title}</div><p class="info-detail">${i===2?"Tracker-imported Progress":"Tracker-imported History"} · Only this account's imported data is affected.</p></div>`).join("")}</div></div>
<div class="dialog-actions">${action("Cancel","dialog-close")}${action("Remove imported data","no-remove")}</div></section></div>`;
}
function bulkDialog(){
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog" role="dialog" aria-modal="true" aria-label="Review shared choices for selected tracker items">
${dialogHead("Review choices for "+state.selected.size+" selected items")}
<div class="dialog-scroll"><p class="dialog-text">${sourceRows.filter(r=>state.selected.has(r.id)).map(r=>r.title).join(" · ")}</p>
<p class="inline-warning" style="margin:12px 0">Only the selected items receive this decision. Other titles and any protected or unmatched exceptions remain visible for individual review. This saves decisions to the preview; no Progress or History is applied until the separate import confirmation.</p></div>
<div class="dialog-actions">${action("Keep Colosseum progress for "+state.selected.size+" selected items","bulk-keep")}
${action("Cancel","back-to-import")}</div></section></div>`;
}
function matchDialog(){
return `<div class="dialog-veil" data-action="dialog-close"><section class="dialog dialog-export" role="dialog" aria-modal="true" aria-label="Find a Colosseum title">
${dialogHead("Find a Colosseum title")}
<p>Tracker title: “Attack on Titan”</p><p>Choose a named title already in Colosseum Progress. A title match alone cannot apply tracker Progress or change History without a verified exact target.</p>
<input id="titleSearch" aria-label="Search Colosseum titles" type="search" placeholder="Search" value="${safe(state.search)}" style="background:var(--glass);color:var(--ink);border:1px solid var(--edge);border-radius:10px;padding:13px">
<div class="dialog-list" id="dialogList">${["Attack on Titan","One Piece","Naruto"].filter(v=>v.toLowerCase().includes(state.search.toLowerCase())).map(v=>action(v,"match-choose","panel-btn",`data-choice="${safe(v)}"`)).join("")}</div>
${action("Back to import review","back-to-import")}</section></div>`;
}
function dialog(){
switch(state.dialog){
case"import":return importDialog();
case"export":return exportDialog();
case"disconnect":return disconnectDialog();
case"remove":return removeDialog();
case"bulk":return bulkDialog();
case"match":return matchDialog();
default:return"";
}}
function applyFitting(){
 const w=state.viewWidth||412;
 const set=(sel,props)=>{for(const node of app.querySelectorAll(sel))for(const [key,val] of Object.entries(props))node.style[key]=val;};
 set(".page-heading",{gridTemplateColumns:w>=700?"minmax(0,1fr) auto":"minmax(0,1fr)"});
 set(".page-title",{fontSize:"calc("+(w>=700?42:36)+"px * var(--f))"});
 set(".relay",{gridTemplateColumns:w>=700?"minmax(0,1fr) auto":"1fr",gap:w>=700?"24px":"12px"});
 set(".relay-summary",{textAlign:w>=700?"right":"left"});
 set(".attention-region",{gridTemplateColumns:w>=700?"auto 1fr auto":"auto 1fr"});
 set(".attention-note",{gridColumn:w>=700?"auto":"1 / -1",textAlign:w>=700?"right":"left"});
 set(".connected-grid",{gridTemplateColumns:w>=900?"repeat(2,minmax(0,1fr))":"minmax(0,1fr)"});
 set(".catalogue-grid",{gridTemplateColumns:w>=1380?"repeat(3,minmax(0,1fr))":w>=760?"repeat(2,minmax(0,1fr))":"minmax(0,1fr)"});
 set(".catalogue-meta",{display:w>=900?"block":"none"});
 set(".capability-tags",{display:w>=760?"flex":"none"});
 set(".native-logo",{width:w>=900?"180px":w<=430?"50px":"72px"});
 set(".native-available",{display:w>=900?"block":"none"});
 set(".stremio-card",{gridTemplateColumns:w>=640?"50px minmax(0,1fr) auto":"50px minmax(0,1fr)",minHeight:w>=640?"116px":"190px"});
 set(".stremio-actions",{gridColumn:w>=640?"auto":"1 / -1"});
 if(w<=430){
   set(".dossier-head",{padding:"16px",gap:"10px",minHeight:"84px"});
   set(".dossier-head .icon-tile",{width:"44px",height:"44px",borderRadius:"12px"});
   set(".dossier-head .icon-tile img",{width:"28px",height:"28px"});
   set(".dossier-title",{fontSize:"calc(23px * var(--f))"});
   set(".dossier-scroll",{padding:"18px 18px 38px"});
   set(".native-empty",{padding:"16px",gap:"13px",minHeight:"162px"});
   set(".native-name",{fontSize:"calc(26px * var(--f))"});
   set(".native-message",{fontSize:"calc(13px * var(--f))"});
   set(".connected-card",{padding:"14px"});
   set(".catalogue-card",{padding:"13px"});
   set(".dialog",{padding:"17px"});
   set(".panel-btn, .review-btn",{minHeight:"44px"});
 }
}
function render(preserve=true){
const focused=document.activeElement;const focusId=focused?.id;const focusAct=focused?.dataset.action;const focusKey=focused?.dataset.key;
if(preserve){
state.scroll.page=el("pageScroll")?.scrollTop??state.scroll.page;
state.scroll.detail=el("dossierScroll")?.scrollTop??state.scroll.detail;
state.scroll.dialog=el("dialogList")?.scrollTop??state.scroll.dialog;
}
app.innerHTML=basePage()+dossier()+dialog();
const top=app.querySelector(".dialog")||app.querySelector(".dossier");for(const e of app.querySelectorAll(".shell-head,.page-scroll,.desktop-dock"))e.inert=!!top;const d=app.querySelector(".dossier");if(d)d.inert=!!state.dialog;
applyFitting();
if(el("pageScroll"))el("pageScroll").scrollTop=state.scroll.page;
if(el("dossierScroll"))el("dossierScroll").scrollTop=state.scroll.detail;
if(el("dialogList"))el("dialogList").scrollTop=state.scroll.dialog;
if(focused&&focused!==document.body){let next=focusId?el(focusId):[...app.querySelectorAll("[data-action]")].find(e=>e.dataset.action===focusAct&&e.dataset.key===focusKey&&!e.closest("[inert]"));if(!next||next.closest("[inert]"))next=top?.querySelector("button:not(:disabled),input:not(:disabled)")||app.querySelector("[data-action=preferences]");next?.focus({preventScroll:true});}
}
function openPanel(key){if(state.panel&&state.panel!=="global")state.reviews[state.panel]={chosen:{...state.chosen},matches:{...state.matches},selected:[...state.selected],exports:[...state.selectedExports]};const r=state.reviews[key]||{};state.chosen={...(r.chosen||{})};state.matches={...(r.matches||{})};state.selected=new Set(r.selected||[]);state.selectedExports=new Set(r.exports||["one-piece"]);state.returnFocus=document.activeElement?.dataset.action||null;state.panel=key;state.provider=key;state.dialog=null;state.scroll.detail=0;state.scroll.dialog=0;render();}
function openDialog(key){state.dialog=key;state.scroll.dialog=0;render();}
function reset(){
state.scenario="connected";state.panel=mode==="dossier"?"simkl":mode==="preferences"?"global":null;
state.provider="simkl";state.dialog=null;state.selected.clear();state.chosen={};state.selectedExports=new Set(["one-piece"]);
state.toggles={...seed};state.scroll={page:0,detail:0,dialog:0};state.search="";state.connectedOverrides={};state.removedImports={};state.matches={};state.importConfirmed={};state.exportConfirmed={};state.reviews={};state.providerToggles={};
if(el("scenario"))el("scenario").value=state.scenario;if(el("provider"))el("provider").value=state.provider;
render(false);note("Restored representative fixtures. No external state was changed.");
}
app.addEventListener("click",ev=>{
const node=ev.target.closest("[data-action]");if(!node)return;
const a=node.dataset.action,key=node.dataset.key;
if(ev.target.closest(".dossier")&&a==="veil-close")return;
if(a==="veil-close"){state.panel=null;state.dialog=null;render();return;}
if(a==="dialog-close"){if(ev.target.closest(".dialog")&&node===ev.target.closest(".dialog-veil"))return;state.dialog=null;render();return;}
switch(a){
case"back":if(state.dialog)state.dialog=null;else if(state.panel)state.panel=null;else note("Back returns to the existing Profile route in the app; this file stays standalone.");render();break;
case"close-dossier":state.panel=null;state.dialog=null;render();break;
case"preferences":openPanel("global");break;
case"open-provider":openPanel(key);break;
case"review-import":openDialog("import");break;
case"review-export":openDialog("export");break;
case"disconnect":openDialog("disconnect");break;
case"remove-data":openDialog("remove");break;
case"select-import":{const id=node.dataset.id;if(state.selected.has(id))state.selected.delete(id);else state.selected.add(id);render();break;}
case"clear-selection":state.selected.clear();render();break;
case"bulk-review":if(state.selected.size>1)openDialog("bulk");break;
case"bulk-keep":for(const id of state.selected)state.chosen[id]="Keep Colosseum progress";state.selected.clear();openDialog("import");break;
case"back-to-import":openDialog("import");break;
case"import-choice":{const id=node.dataset.id,c=node.dataset.choice;if(c==="Find match"){state.search="";openDialog("match");}
else{state.chosen[id]=c;render();}break;}
case"match-choose":state.matches["aot"]=node.dataset.choice;delete state.chosen["aot"];note("Local title-match preview: "+node.dataset.choice+". Review a separate progress decision.");openDialog("import");break;
case"toggle":{const values=["pull","send","live"].includes(key)?(state.providerToggles[state.panel]??=providerSeed(state.panel)):state.toggles;values[key]=!values[key];render();note("Local switch preview only: "+key+" is "+(values[key]?"On":"Off")+".");break;}
case"sync-all":note("Sync all is shown for fidelity. No tracker sync, account operation or network request occurred.");break;
case"connect-attempt":state.connectedOverrides[state.panel]=true;render();note("Connected-state fixture selected locally. Authentication and provider operations are outside this mock.");break;
case"open-stremio":note("Stremio settings are a separate existing route; this mock does not open or modify an account.");break;
case"window-control":note("Desktop chrome is illustrative. No actual window control is invoked.");break;
case"dock-route":note("Desktop taskbar route is shown for context only. No shell navigation occurred.");break;
case"confirm-import":state.importConfirmed[state.panel]=true;state.dialog=null;render();note("Local import-review fixture confirmed; native History and real Progress were untouched.");break;
case"confirm-export":state.exportConfirmed[state.panel]=true;state.dialog=null;render();note("Local export-review fixture confirmed; nothing was sent.");break;
case"no-disconnect":state.connectedOverrides[state.panel]=false;state.dialog=null;render();note("Only the local fixture was disconnected. No account or pending app work was changed.");break;
case"no-remove":state.removedImports[state.panel]=true;state.dialog=null;render();note("Only the local imported-data fixture was removed; real data remains untouched.");break;
}
});
app.addEventListener("change",ev=>{
if(ev.target.dataset.action==="export-item"){
if(ev.target.checked)state.selectedExports.add(ev.target.value);else state.selectedExports.delete(ev.target.value);
render();note("Updated only the mock export selection. Nothing was sent.");
}
});
app.addEventListener("input",ev=>{
if(ev.target.id==="titleSearch"){
state.search=ev.target.value;
const area=el("dialogList");
if(area)area.innerHTML=["Attack on Titan","One Piece","Naruto"].filter(v=>v.toLowerCase().includes(state.search.toLowerCase())).map(v=>action(v,"match-choose","panel-btn",`data-choice="${safe(v)}"`)).join("");
}
});
window.addEventListener("keydown",ev=>{
if(ev.key==="Tab"&&(state.dialog||state.panel)){const top=app.querySelector(".dialog")||app.querySelector(".dossier");const nodes=[...top.querySelectorAll("button:not(:disabled),input:not(:disabled),[tabindex]")].filter(e=>e.getClientRects().length&&!e.closest("[inert]"));if(nodes.length){const i=nodes.indexOf(document.activeElement);if(ev.shiftKey&&(i<=0)){ev.preventDefault();nodes.at(-1).focus();}else if(!ev.shiftKey&&(i===nodes.length-1||i===-1)){ev.preventDefault();nodes[0].focus();}}}
if(ev.key==="Escape"&&(state.dialog||state.panel)){ev.preventDefault();if(state.dialog)state.dialog=null;else state.panel=null;render();}
});
if(el("preset"))el("preset").addEventListener("change",e=>{
const [w,h]=e.target.value.split("x").map(Number);const platform=e.target.value==="1280x700"?"desktop":"android";
setViewport(w,h,platform);note("Viewport preset switched; Android dimensions remain provisional.");
});
for(const id of ["vw","vh","topInset","bottomInset","fontScale"]){
if(!el(id))continue;
el(id).addEventListener("input",()=>{
state.fontScale=Math.max(.8,Math.min(1.6,n(el("fontScale")?.value,1)));
setViewport(el("vw")?.value,el("vh")?.value);
});
}
if(el("scenario"))el("scenario").addEventListener("change",e=>{state.scenario=e.target.value;state.dialog=null;render();});
if(el("provider"))el("provider").addEventListener("change",e=>openPanel(e.target.value));
if(el("keyboardRaised")){el("keyboardRaised").checked=query.get("keyboard")==="1";el("keyboardRaised").addEventListener("change",e=>{query.set("keyboard",e.target.checked?"1":"0");setViewport(state.viewWidth,state.viewHeight);});}
if(el("reset"))el("reset").addEventListener("click",reset);
const initialW=n(query.get("w"),n(document.body.dataset.width,capture?window.innerWidth:412)),initialH=n(query.get("h"),n(document.body.dataset.height,capture?window.innerHeight:900));
const initialPlatform=query.get("platform")==="desktop"||document.body.dataset.platform==="desktop"||(capture&&initialW===1280&&initialH===700)?"desktop":"android";
if(query.has("fontScale"))state.fontScale=n(query.get("fontScale"),1);else state.fontScale=n(document.body.dataset.fontScale,1);
if(query.has("scenario")&&el("scenario"))el("scenario").value=state.scenario;
if(query.has("provider")&&el("provider"))el("provider").value=state.provider;
if(query.has("top")&&el("topInset"))el("topInset").value=n(query.get("top"));
if(query.has("bottom")&&el("bottomInset"))el("bottomInset").value=n(query.get("bottom"));
if(el("fontScale"))el("fontScale").value=state.fontScale;
if(el("preset")){
const p=initialW+"x"+initialH;let o=[...el("preset").options].find(o=>o.value===p);
if(o)el("preset").value=p;
}
if(query.get("long")==="1"){sourceRows.push(...Array.from({length:16},(_,i)=>({id:"long-"+i,title:"One Piece — The continuing voyage through the Grand Line, chapter "+(i+1),classification:"Tracker is ahead",progress:"Tracker: Episode "+(80+i)+" · Colosseum: Episode "+(70+i),choices:["Use tracker progress","Keep Colosseum progress","Leave for later"]})));}
if(state.dialog==="bulk")state.selected=new Set(["one-piece","naruto"]);
setViewport(initialW,initialH,initialPlatform);
render(false);
if(query.get("keyboard")==="1"&&state.dialog==="match"){el("titleSearch")?.focus();el("titleSearch")?.scrollIntoView({block:"nearest"});}
})();
