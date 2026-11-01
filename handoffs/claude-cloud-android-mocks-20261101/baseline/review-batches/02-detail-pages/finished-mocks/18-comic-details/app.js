(()=>{
"use strict";
const page = document.body.dataset.page || "series";
const params = new URLSearchParams(location.search);
const capture = params.get("capture")==="1";
if(capture)document.body.classList.add("capture");
const assets="../../../../reference/desktop/assets/";
const art={
  gotham:assets+"universes/dcau/portals/gotham.jpg",
  future:assets+"universes/dcau/portals/future-gotham.jpg",
  metropolis:assets+"universes/dcau/portals/metropolis.jpg",
  watchtower:assets+"universes/dcau/portals/watchtower.jpg",
  gothamWash:assets+"universes/dcau/environments/gotham-v11-bg.png",
  justiceWash:assets+"universes/dcau/environments/justice-v11-bg.png",
  marvel:assets+"universes/marks/marvel.png",
  dc:assets+"universes/marks/dc.png",
  download:assets+"icons/download.svg",
  books:assets+"icons/books.svg",
  search:assets+"icons/search.svg",
  minimize:assets+"icons/minimize.svg",
  fullscreen:assets+"icons/fullscreen.svg",
  power:assets+"icons/power.svg"
};
const editions=[
 {id:"7579956",title:"Batman Vol. 1: I Am Gotham (2024 Edition)",fmt:"Trade Paperback",year:"2024",date:"2024-08-27",pages:192,author:"Tom King",isbn:"9781779527677",collects:"Collects the pages of Batman: Rebirth #1 and issues #1-6.",description:"Batman must save Gotham from its newest threat... another vigilante hero! Batman is Gotham City's greatest protector. But now he's not alone. There are two ...",available:true,art:art.gotham},
 {id:"5099415",title:"Batman Vol. 2: I Am Suicide (2024 Edition)",fmt:"Trade Paperback",year:"2024",date:"2024-10-29",pages:168,author:"Tom King",description:"Still reeling from a cataclysmic battle, Batman finds himself up against some of the biggest (literally) threats he's ever faced within the city limits of Go...",available:false,art:art.future},
 {id:"2065135",title:"Batman Vol. 3: I Am Bane (2024 Edition)",fmt:"Trade Paperback",year:"2024",date:"2024-12-24",pages:176,author:"Tom King",description:"He is not a joke. He is not a riddle. He is not a bird or a cat or a penguin, not a scarecrow or a plant or a puppet. He is not a fairy tale or a circus act...",available:false,art:art.metropolis},
 {id:"3603805",title:"Batman: Detective Comics: The Rebirth Deluxe Edition Book 1",fmt:"Deluxe",year:"2017",date:"2017-11-07",pages:388,author:"James Tynion IV",isbn:"9781401276089",collects:"Collects the first two volumes of the critically acclaimed series, issues #934-949.",description:"Someone is targeting Batman’s allies in Gotham—young and untested heroes who model themselves after the Dark Knight but don’t yet have his ...",available:true,art:art.watchtower},
 {id:"4696839",title:"Batman Vol. 4: The War of Jokes and Riddles (2025 Edition)",fmt:"Trade Paperback",year:"2025",date:"2025-02-11",pages:200,author:"Tom King",description:"Early in Batman’s career, the Joker and the Riddler would seemingly be natural allies. But each man determined that he and he alone must be the one t...",available:false,art:art.gothamWash}
];
const releases=[
 {id:"4696839",name:editions[4].title,year:2025,collection:true,cover:art.gothamWash},
 {id:"2065135",name:editions[2].title,year:2024,collection:true,cover:art.metropolis},
 {id:"5099415",name:editions[1].title,year:2024,collection:true,cover:art.future},
 {id:"7579956",name:editions[0].title,year:2024,collection:true,cover:art.gotham},
 {id:"3603805",name:editions[3].title,year:2017,collection:true,cover:art.watchtower},
 {id:"issue3",name:"Batman #3",year:2016,collection:false,cover:art.gotham},
 {id:"issue2",name:"Batman #2",year:2016,collection:false,cover:art.future},
 {id:"issue1",name:"Batman #1",year:2016,collection:false,cover:art.metropolis}
];
const boxes=[
 {name:"DC Comics",count:3902,cover:art.gothamWash},
 {name:"Marvel Comics",count:4501,cover:art.marvel},
 {name:"Image Comics",count:1268,cover:art.watchtower},
 {name:"Batman",count:1425,cover:art.gotham},
 {name:"Spider-Man",count:1048,cover:art.metropolis},
 {name:"Superman",count:824,cover:art.metropolis},
 {name:"Justice League",count:451,cover:art.justiceWash},
 {name:"The Walking Dead",count:217,cover:art.future},
 {name:"Invincible",count:155,cover:art.watchtower},
 {name:"Ultimate Spider-Man",count:279,cover:art.gotham}
];
const archiveSeries=[
 {title:"Batman",count:231,cover:art.gotham},
 {title:"The Boys",count:82,cover:art.future},
 {title:"Superman",count:157,cover:art.metropolis},
 {title:"Justice League",count:73,cover:art.justiceWash},
 {title:"Detective Comics",count:198,cover:art.gothamWash},
 {title:"Wonder Woman",count:92,cover:art.watchtower},
 {title:"The Flash",count:119,cover:art.metropolis},
 {title:"Action Comics",count:131,cover:art.future}
];
const presets=[
 {id:"portrait",name:"412 × 900",w:412,h:900},
 {id:"compact",name:"354 × 792",w:354,h:792},
 {id:"landscape",name:"900 × 412",w:900,h:412},
 {id:"short",name:"792 × 354",w:792,h:354},
 {id:"desktop",name:"1280 × 700",w:1280,h:700},
 {id:"tablet",name:"800 × 1280",w:800,h:1280},
 {id:"tabletWide",name:"1280 × 800",w:1280,h:800}
];
const defaultPreset=presets.find(p=>p.id===(params.get("preset")||"portrait"))||presets[0];
const st={
  w:capture?window.innerWidth:Number(params.get("w"))||defaultPreset.w,
  h:capture?window.innerHeight:Number(params.get("h"))||defaultPreset.h,
  top:Number(params.get("top")||0),bottom:Number(params.get("bottom")||0),
  font:Number(params.get("font")||1),
  preset:params.get("preset")||"portrait",
  sort:"new",filter:"",saved:false,collections:[],collectionPopup:false,
  archiveView:params.get("state")==="index"?"index":"board",archiveBox:"DC Comics",
  editionDownloads:{"7579956":"done"},
  releaseDownloads:{"7579956":"done","issue2":"downloading"},
  sourceEdition:null,sourceQuery:"",sourceSelection:null,sourceState:"results",
  showSynopsis:false,status:"Interactions are local fixture state only; nothing is downloaded."
};
if(params.get("state")==="alt")st.sourceEdition=editions[3];
if(params.get("state")==="collections")st.collectionPopup=true;
if(params.get("state")==="downloads")st.releaseDownloads["2065135"]="queued";
const root=document.getElementById("review-root");
const safe=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const icon=(name,alt="")=>'<img src="'+safe(art[name])+'" alt="'+safe(alt)+'">';
const attr=(s)=>safe(s);
const device=()=>{
  let phone=Math.min(st.w,st.h)<600;
  let desktop=params.get("host")==="desktop" || (!phone && st.w>=1000 && st.h<=750);
  let landscape=st.w>st.h;
  return {phone,desktop,landscape};
};
const backButton=()=>'<button class="back-button" type="button" data-action="back" aria-label="Back"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 L8 12 L14.5 18.5"/></svg><span>Back</span></button>';
const chrome=()=>'<header class="chrome">'+backButton()+'<div class="window-controls" aria-label="Desktop window controls"><button aria-label="Minimize window" title="Minimize window" data-action="window">'+icon("minimize")+'</button><button aria-label="Toggle fullscreen" title="Toggle fullscreen" data-action="window">'+icon("fullscreen")+'</button><button aria-label="Close window" title="Close window" data-action="window">'+icon("power")+'</button></div></header>';
const libraryButtons=()=>'<div class="hero-actions"><button class="ghost-button '+(st.saved?'saved':'')+'" data-action="library"><span class="glyph">'+(st.saved?'✓':'+')+'</span>'+(st.saved?'In Library':'Library')+'</button><button class="ghost-button" data-action="collections" aria-haspopup="dialog"><svg class="collection-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/></svg>Add to Collections</button></div>';
function seriesItems(){
  let items=releases.filter(x=>x.name.toLowerCase().includes(st.filter.toLowerCase().trim()));
  if(st.sort==="old")items=items.slice().sort((a,b)=>(a.year-b.year)||a.name.localeCompare(b.name,undefined,{numeric:true}));
  if(st.sort==="az")items=items.slice().sort((a,b)=>a.name.localeCompare(b.name,undefined,{numeric:true}));
  return items;
}
function releaseStatus(r){
  let v=st.releaseDownloads[r.id]||"none";
  if(v==="done")return r.id==="7579956"?"37% read":"● Downloaded";
  if(v==="downloading")return "Downloading 51%";
  if(v==="queued")return "Queued…";
  if(v==="error")return "⚠ Failed — Read to retry";
  return r.year?String(r.year):"";
}
function releaseRow(r){
  let dl=st.releaseDownloads[r.id]||"none";
  let label=st.pendingRead===r.id && ["queued","downloading"].includes(dl)?"Reading when ready":dl==="done"?(r.id==="7579956"?"Continue · 37%":"Read"):dl==="queued"||dl==="downloading"?"Read when ready":"Read";
  let trailing=dl==="done"?"✓":dl==="downloading"||dl==="queued"?"✕":dl==="error"?"↻":"↓";
  return '<div class="release-row" role="button" tabindex="0" data-release="'+attr(r.id)+'"><div class="poster release-cover"><img src="'+attr(r.cover)+'" alt=""></div><div class="release-row-copy"><div class="release-title" title="'+attr(r.name)+'">'+safe(r.name)+'</div><div class="release-status '+(dl==="done"||dl==="downloading"?"active":"")+'">'+safe(releaseStatus(r))+'</div></div><button class="gold-button read-action '+(dl==="queued"||dl==="downloading"?"pending":"")+'" data-action="release-read" data-id="'+attr(r.id)+'">'+safe(label)+'</button><button class="round-action" title="'+(dl==="done"?"Remove download":dl==="downloading"?"Cancel download":"Download release")+'" aria-label="'+(dl==="done"?"Remove downloaded release":dl==="downloading"?"Cancel release download":"Download release")+'" data-action="release-download" data-id="'+attr(r.id)+'">'+trailing+'</button></div>';
}
function releaseSection(label,rows){
  if(!rows.length)return "";
  return '<section><div class="release-section-head"><span class="section-label">'+label+'</span><span class="count">'+rows.length+'</span></div>'+rows.map(releaseRow).join("")+'</section>';
}
function series(title="Batman"){
  const shown=seriesItems();
  const collected=shown.filter(r=>r.collection);
  const issues=shown.filter(r=>!r.collection);
  const match=st.filter?'<span class="search-match">'+shown.length+(shown.length===1?" match":" matches")+'</span>':"";
  return '<div class="page-content series-page"><div class="hero-series page-inset"><div class="hero-art" style="background-image:url(&quot;'+art.gothamWash+'&quot;)"></div><div class="hero-series-content"><div class="poster hero-poster"><img src="'+art.gotham+'" alt=""></div><div class="hero-series-copy"><div class="gold-eyebrow">Western Comics · Tankoban</div><h1 class="display">'+safe(title)+'</h1><div class="facts-row"><strong>8</strong><span>releases</span><span class="separator">·</span><span>5 collected editions</span><span class="separator">·</span><span>GetComics</span></div>'+libraryButtons()+'</div></div></div>'+
  '<div class="series-tools page-inset"><div class="search-inline"><input id="release-filter" type="search" placeholder="Search releases…" value="'+attr(st.filter)+'" aria-label="Search releases">'+(st.filter?'<button class="clear-search" aria-label="Clear release search" data-action="clear-search">✕</button>':"")+'</div>'+match+'<nav class="sort-controls" aria-label="Sort releases">'+[["new","Newest"],["old","Oldest"],["az","A–Z"]].map(([k,v])=>'<button data-action="sort" data-sort="'+k+'" class="'+(st.sort===k?"active":"")+'" aria-pressed="'+(st.sort===k)+'">'+v+'</button>').join("")+'</nav></div>'+
  '<div class="page-inset"><div class="glass-panel">'+releaseSection("COLLECTED EDITIONS",collected)+releaseSection("ISSUES",issues)+'</div>'+(shown.length?"":'<p class="empty-match">No releases match “'+safe(st.filter.trim())+'”.</p>')+'</div></div>';
}

function displayTitle(t){const p="Batman";return t.toLowerCase().startsWith(p.toLowerCase())?t.slice(p.length).replace(/^[\s:,./–—-]+/,""):t;}
function editionRow(ed){
  const state=st.editionDownloads[ed.id]||"none";
  const busy=state==="queued"||state==="downloading";
  const canAlt=!busy&&state!=="done";
  const canPrimary=ed.available||state==="done";
  let iconState=state==="done"?icon("books","Read downloaded edition"):busy?'<span class="state-percent">'+(state==="downloading"?"51%":"…")+'</span>':ed.available?icon("download","Download verified GetComics release"):"";
  const sources=st.sourceFixture&&ed.id==="7579956"?'<div class="edition-sources"><div>1 verified source</div><div class="source-line"><strong>Trade Paperback</strong><span>2017</span><span>Batman Vol. 1: I Am Gotham TPB</span></div></div>':"";
  return '<article class="edition-row" data-edition="'+attr(ed.id)+'" '+(canPrimary?'tabindex="0" role="button" aria-label="'+(state==="done"?"Read ":"Download ")+attr(ed.title)+'"':'')+'><div class="poster edition-poster"><img src="'+attr(ed.art)+'" alt=""></div>'+
  '<div class="edition-copy '+(canPrimary?'available':'')+'"><h3 title="'+attr(ed.title)+'">'+safe(displayTitle(ed.title))+'</h3><div class="edition-spec"><span>'+safe(ed.author)+'</span><span>·</span><span>'+ed.pages+' Pages</span><span>·</span><span>'+ed.year+'</span></div>'+
  '<p class="edition-description">'+safe(ed.description)+'</p></div>'+
  '<div class="edition-actions">'+(canAlt?'<button class="alt-button" data-action="alt" data-id="'+attr(ed.id)+'" aria-label="Find alternate sources for '+attr(ed.title)+'" title="Find alternate sources">'+icon("search")+'</button>':'')+
  '<button class="edition-act" data-action="edition-open" data-id="'+attr(ed.id)+'" '+(canPrimary?'':'disabled')+' aria-label="'+(state==="done"?"Read ":"Download ")+attr(ed.title)+'">'+iconState+'</button></div>'+sources+'</article>';
}
function edition(){
  const groups=["Compendium","Omnibus","Complete Library","Ultimate Collection","Epic Collection","Absolute","Deluxe","Hardcover","Trade Paperback","Graphic Novel","Other"].map(fmt=>({fmt,items:editions.filter(e=>e.fmt===fmt)})).filter(g=>g.items.length);
  const synopsis=editions[0].description;
  return '<div class="page-content edition-page page-inset"><div class="edition-ledger-hero"><div class="poster series-poster"><img src="'+art.gotham+'" alt=""></div><div class="edition-hero-copy">'+
  '<div class="gold-eyebrow">Comics</div><h1 class="display">Batman</h1><div class="creator-line">by Tom King, James Tynion IV</div><div class="edition-meta">#4 Most-Read&nbsp;&nbsp; · &nbsp;&nbsp;DC Comics&nbsp;&nbsp; · &nbsp;&nbsp;5 collected editions</div>'+
  '<p class="synopsis">'+safe(synopsis)+'</p>'+libraryButtons()+'</div></div>'+
  groups.map(g=>'<section class="edition-group"><div class="edition-group-head"><h2>'+safe(g.fmt)+'</h2><span class="count">'+g.items.length+'</span><span class="rule"></span></div>'+g.items.map(editionRow).join("")+'</section>').join("")+'</div>';
}
function archive(){
  if(st.archiveView==="releases")return series(st.archiveBox);
  if(st.archiveView==="board"){
    return '<div class="page-content archive-page page-inset"><div class="gold-eyebrow">GetComics · Tankoban</div><h1 class="display">GetComics Archives</h1><div class="archive-subtitle">Publishers &amp; franchises — whole-archive releases (TPBs, omnibuses, runs)</div>'+
    '<div class="archive-board-grid">'+boxes.map((b,i)=>'<button class="archive-board-tile" data-action="archive-box" data-index="'+i+'" aria-label="Open '+attr(b.name)+' archive"><span class="tile-art" style="background-image:url(&quot;'+attr(b.cover)+'&quot;)"></span><span class="tile-title">'+safe(b.name)+'</span><span class="tile-count">'+b.count+'</span></button>').join("")+'</div></div>';
  }
  const found=boxes.find(b=>b.name===st.archiveBox)||boxes[0];
  const indexItems=st.archiveBox==="DC Comics"?archiveSeries:st.archiveBox==="Image Comics"?
    [{title:"Invincible",count:155,cover:art.watchtower},{title:"The Walking Dead",count:217,cover:art.future}]:archiveSeries;
  return '<div class="page-content archive-page page-inset"><div class="gold-eyebrow">Western Comics · Tankoban</div><h1 class="display">'+safe(st.archiveBox)+'</h1>'+
  '<div class="archive-index-meta"><strong>'+indexItems.length+'</strong><span class="dim">series archives in the latest releases</span><span class="tiny-dot">·</span>'+
  '<button class="link-button" data-action="all-releases">All '+found.count+' releases ›</button></div>'+
  '<div class="archive-posters">'+indexItems.map((s,i)=>'<button class="archive-poster" data-action="archive-series" data-index="'+i+'" title="'+attr(s.title)+'"><div class="archive-poster-image"><img src="'+attr(s.cover)+'" alt=""></div><span class="archive-poster-title">'+safe(s.title)+'</span><span class="archive-poster-count">'+s.count+' releases</span></button>').join("")+'</div></div>';
}
function collectionsPopup(){
  const chosen=id=>st.collections.includes(id)?"✓":"";
  const mediaTitle="Batman";
  return '<div class="overlay" id="collections-overlay"><section class="collection-popup" role="dialog" aria-modal="true" aria-label="Add to Collections"><div class="popup-top"><h2>'+ (st.creating?"New collection":"Add to Collections") +'</h2><button class="close-popup" data-action="close-popup" aria-label="Close">×</button></div>'+
  '<p class="popup-help">'+(st.creating?"Give it a name. Make it yours over time.":"Keep this title in one collection, or a few.")+'</p>'+
  (!st.creating?'<div class="popup-media"><img src="'+art.gotham+'" alt=""><div><strong>'+mediaTitle+'</strong><small>Comic · DC Comics</small></div></div>'+
  '<input id="collection-filter" class="popup-search" placeholder="Find a collection" aria-label="Find a collection"><div class="popup-memberships">'+["Comics","Favorites","Reading"].map((name,i)=>'<button data-action="toggle-collection" data-id="'+i+'"><span class="tick">'+chosen(String(i))+'</span>'+name+'</button>').join("")+'</div>':
  '<input id="new-collection" class="popup-search" placeholder="New collection" aria-label="New collection name" value="'+attr(st.newCollection||"")+'">')+
  '<div class="popup-footer">'+(st.creating?'<button class="link-button" data-action="cancel-create">Cancel</button><button class="gold-button" data-action="create-collection">Create collection</button>':
  '<button class="link-button" data-action="begin-create">+ New collection</button><button class="link-button" data-action="close-popup">Cancel</button><button class="gold-button" data-action="save-collections">Done</button>')+'</div></section></div>';
}
function sourceRows(){
 const name=st.sourceEdition?st.sourceEdition.title:"Batman";
 return [
  {source:"Pirate Bay",confidence:"STRONG MATCH",evidence:"FORMAT RANGE",name:name+" TPB",size:""},
  {source:"ExtraTorrents",confidence:"POSSIBLE MATCH",evidence:"ISSUES",name:name+" (CBZ)",size:""},
  {source:"Torrents-CSV",confidence:"WEAK MATCH",evidence:"",name:name+" Collection",size:""}
 ];
}
function sourceTable(){
 const e=st.sourceEdition,results=sourceRows(),state=st.sourceState;
 if(state==="inspecting")return '<div class="pack-state"><h3>Inspecting pack…</h3><p>'+safe(e.title)+'</p></div>';
 if(state==="empty")return '<p class="pack-empty">No torrents matched this query. Try another title or ISBN.</p>';
 if(state==="incomplete")return '<div class="pack-state"><p>This pack is missing issues this edition needs.</p><p class="gold">#5 · #6</p><div class="pack-actions"><button data-action="source-return">Try another source</button><button data-action="source-manual">Search manually</button></div></div>';
 if(state==="combined")return '<div class="pack-state"><p>Only a combined archive covers this edition — it likely includes other editions too.</p><div class="pack-actions"><button data-action="source-return">Go back</button><button class="gold-button" data-action="source-confirm">Download whole archive anyway</button></div></div>';
 if(state==="ambiguous")return '<div class="source-table-head"><h3>2 comic archives</h3><span class="gold">CHOOSE ONE</span></div><div class="source-result-list">'+[["Batman - Detective Comics - Rebirth Deluxe Book 1.cbz","428.5 MB"],["Batman - Detective Comics - Rebirth Deluxe Book 2.cbz","512.2 MB"]].map((f,i)=>'<button class="archive-file" data-action="archive-choose" data-index="'+i+'"><span class="source-badge">CBZ</span><span class="result-copy"><strong>'+safe(f[0])+'</strong><small>'+f[1]+'</small></span><span class="pick-disc">↓</span></button>').join("")+'</div>';
 return '<div class="source-table-head"><h3>3 results</h3><span>Pirate Bay · ExtraTorrents · Torrents-CSV</span></div><div class="source-result-list">'+results.map((r,i)=>'<div class="source-result" tabindex="0" role="button" aria-label="Choose '+attr(r.source)+'" data-source-index="'+i+'"><div class="source-badge">'+r.source.charAt(0)+'</div><div class="result-copy"><strong>'+r.source+'</strong><span class="confidence confidence-'+i+'">'+r.confidence+'</span><p>'+safe(r.name)+'</p>'+(r.evidence?'<small class="evidence '+(i===0?'coverage':'')+'">'+r.evidence+'</small>':"")+'</div><button class="source-choose" data-action="source-choose" data-index="'+i+'" aria-label="Choose '+attr(r.source)+'">↓</button></div>').join("")+'</div>';
}
function sourceOverlay(){
 const e=st.sourceEdition;if(!e)return "";
 const identity=[e.title,e.isbn?"ISBN "+e.isbn:"",e.collects||""].filter(Boolean).join("      ·      ");
 const weak=st.confirmingWeak?'<div class="weak-shade"><section class="weak-dialog" role="dialog" aria-modal="true" aria-label="Weak match confirmation"><p>This release does not closely match the collected edition.</p><small>'+safe(e.title+' Collection')+'</small><div class="pack-actions"><button data-action="weak-cancel">Go back</button><button class="gold-button" data-action="weak-confirm">Choose anyway</button></div></section></div>':"";
 return '<section class="source-overlay" role="dialog" aria-modal="true" aria-label="Find alternate sources"><div class="source-back">'+backButton().replace('data-action="back"','data-action="close-sources"')+'</div><div class="source-scroll"><div class="source-banner" style="background-image:url(&quot;'+art.gothamWash+'&quot;)"><div class="source-heading"><div class="gold-eyebrow">ALTERNATE SOURCES · COLLECTED EDITION</div><h2>'+safe(e.title)+'</h2><div class="source-subhead">'+safe(identity)+'</div></div></div><div class="source-body">'+
 (["results","empty"].includes(st.sourceState)?'<div class="source-query"><input id="source-query" placeholder="Search another title or ISBN" aria-label="Search another title or ISBN" value="'+attr(st.sourceQuery)+'"><button class="gold-button" data-action="source-search">Search</button></div>':"")+
 '<div class="source-table glass-panel">'+sourceTable()+'</div></div></div>'+weak+'</section>';
}
function openCollections(trigger){
 st.collectionPopup=false;
 BatchCollections.open({host:document.getElementById("app"),trigger:trigger||document.querySelector('[data-action="collections"]'),key:"comic:"+(page==="archive"?st.archiveBox:"Batman"),title:page==="archive"?st.archiveBox:"Batman",kind:"comic",kindLabel:"Comic",year:"DC Comics",cover:art.gotham,onSaved:ids=>{st.collections=ids;st.status=ids.length+" memberships saved in this local fixture.";const n=document.getElementById("review-status");if(n)n.textContent=st.status;}});
}
function focusSource(selector){
 requestAnimationFrame(()=>document.querySelector(selector||'.source-overlay [data-action="close-sources"]')?.focus({preventScroll:true}));
}
function finishPack(){
 const id=st.sourceEdition.id;st.editionDownloads[id]="queued";st.sourceEdition=null;st.confirmingWeak=false;
 st.status="Pack choice accepted in this local fixture; no download or service operation.";renderStage();
 requestAnimationFrame(()=>document.querySelector('.edition-row[data-edition="'+id+'"]')?.focus({preventScroll:true}));
}
function inspectPack(index){
 st.confirmingWeak=false;st.sourceSelection=index;st.sourceState="inspecting";renderStage();focusSource();
 const identity=st.sourceEdition.id;
 window.setTimeout(()=>{if(st.sourceEdition?.id!==identity||st.sourceState!=="inspecting")return;st.sourceState=index===0?"combined":index===1?"incomplete":"ambiguous";renderStage();focusSource();},350);
}

function stateChoices(){
 if(page==="series")return [["mixed","Collected editions + issues"],["downloads","Download state"],["collections","Collections popup"],["no-match","Empty search"]];
 if(page==="edition")return [["ledger","Edition ledger"],["sources","Verified source rows"],["alt","Alternate sources open"],["collections","Collections popup"],["long","Expanded synopsis stress"],["source-inspecting","Pack: inspecting"],["source-incomplete","Pack: missing issues"],["source-combined","Pack: combined archive"],["source-ambiguous","Pack: choose archive"],["source-weak","Weak match confirmation"]];
 return [["board","Publishers / franchises board"],["index","Series archives index"]];
}
function reviewPanel(){
 const current=page==="series"?"Series":page==="edition"?"Editions":"Archives";
 const buttons=[["series","comic-series.html","Series"],["edition","comic-edition.html","Editions"],["archive","comic-archive.html","Archives"]];
 const note=page==="edition"?
   "Batman edition names, creators, years and descriptions are drawn from local comics_db.json. Covers are local DCAU artwork stand-ins, not actual cover reproductions. Pack fixtures: Pirate Bay shows combined; ExtraTorrents shows incomplete; Torrents-CSV asks for weak-match confirmation then shows archive choices. The active source page is parked in current routing.":
   page==="series"?"Release names draw from local comic database plus illustrative issue rows. Downloads, counts, and all displayed cover replacements are review fixtures.":
   "Publisher/series labels are representative. Counts and poster art assignments are illustrative fixture values, not a live GetComics response.";
 return '<aside class="review-panel" aria-label="External mock review controls"><div class="review-eyebrow">Standalone study · task 18</div>'+
  '<h1>'+current+' — phone fit</h1><p class="review-desc">Adjust layout measurements here, outside the product viewport. No backend calls or changes to Colosseum.</p>'+
  '<nav class="review-pages" aria-label="Study navigation">'+buttons.map(([id,url,label])=>'<a href="'+url+'" class="'+(page===id?"current":"")+'">'+label+'</a>').join("")+'</nav>'+
  '<label class="review-label">Viewport presets · logical CSS px</label><div class="preset-grid">'+presets.map(p=>'<button class="review-preset '+(st.preset===p.id?"selected":"")+'" data-preset="'+p.id+'">'+p.name+'</button>').join("")+'</div>'+
  '<div class="review-input-row"><label>Width<input id="review-width" class="review-input" type="number" min="320" max="2000" value="'+st.w+'"></label><label>Height<input id="review-height" class="review-input" type="number" min="240" max="2200" value="'+st.h+'"></label></div>'+
  '<label class="review-label">Illustrative safe-area inset (not device measured)</label><div class="review-input-row"><label>Top<input id="review-top" class="review-input" type="number" min="0" max="90" value="'+st.top+'"></label><label>Bottom<input id="review-bottom" class="review-input" type="number" min="0" max="90" value="'+st.bottom+'"></label></div>'+
  '<label class="review-label" for="review-font">Font scale</label><select class="review-input" id="review-font"><option value="1"'+(st.font===1?" selected":"")+'>1.0 ×</option><option value="1.3"'+(st.font===1.3?" selected":"")+'>1.3 ×</option></select>'+
  '<label class="review-label" for="review-state">Example state</label><select class="review-input" id="review-state">'+stateChoices().map(([id,label])=>'<option value="'+id+'"'+(id===(params.get("state")||stateChoices()[0][0])?" selected":"")+'>'+label+'</option>').join("")+'</select>'+
  '<div class="review-status" id="review-status" role="status">'+safe(st.status)+'</div>'+
  '<p class="review-note">'+note+' Provisional OnePlus measurements; no physical handset, reader implementation or running Qt application was tested.</p>'+
  '<div class="review-links"><a href="index.html">Study index</a><a href="../../QML-PORTING-NOTES.md">QML porting notes</a><a href="../../REPORT.json">Batch completion evidence</a></div></aside>';
}
function renderStage(){
 if(window.BatchCollections?.inspect().open)BatchCollections.close();
 const sourceScroll=document.querySelector(".source-scroll")?.scrollTop||0;
 const tableScroll=document.querySelector(".source-table")?.scrollTop||0;
 const d=device();
 const frame=document.getElementById("frame");
 const prev=document.getElementById("app-scroll");
 const oldScroll=prev?prev.scrollTop:0;
 const app=document.getElementById("app");
 app.className="app "+(d.phone?"phone ":"")+(d.desktop?"desktop ":"android-host ")+(d.landscape?"landscape ":"")+(st.showSynopsis?"show-synopsis":"");
 app.style.setProperty("--inset-top",st.top+"px");
 app.style.setProperty("--inset-bottom",st.bottom+"px");
 app.style.setProperty("--font-scale",st.font);
 app.style.setProperty("--margin",d.phone?"20px":"54px");
 app.innerHTML='<div class="app-wall"></div>'+chrome()+'<div id="app-scroll" class="app-scroll">'+(page==="series"?series():page==="edition"?edition():archive())+'</div>'+sourceOverlay();
 document.getElementById("app-scroll").scrollTop=st.nextScroll===undefined?oldScroll:st.nextScroll;delete st.nextScroll;
 document.getElementById("app-scroll").inert=!!st.sourceEdition;
 app.querySelector(".chrome").inert=!!st.sourceEdition;
 if(st.sourceEdition){app.querySelector(".source-scroll").scrollTop=sourceScroll;app.querySelector(".source-table").scrollTop=tableScroll;}
 if(st.confirmingWeak){app.querySelector(".source-scroll").inert=true;app.querySelector(".source-back").inert=true;}
 syncFrame();
 if(document.getElementById("review-status"))document.getElementById("review-status").textContent=st.status;
}
function syncFrame(){
 const stage=document.getElementById("stage"),frame=document.getElementById("frame"),space=document.getElementById("frame-space");
 if(!frame||!stage||!space)return;
 if(capture){st.w=window.innerWidth;st.h=window.innerHeight;}
 frame.style.width=st.w+"px";frame.style.height=st.h+"px";
 if(capture){space.style.width=st.w+"px";space.style.height=st.h+"px";frame.style.transform="none";return;}
 const scale=Math.max(.15,Math.min(1.1,(stage.clientWidth-36)/st.w,(stage.clientHeight-36)/st.h));
 space.style.width=Math.round(st.w*scale)+"px";space.style.height=Math.round(st.h*scale)+"px";
 frame.style.transform="scale("+scale+")";
}
function refreshReview(){
 if(capture)return;
 const panel=document.querySelector(".review-panel");
 if(panel){
  const scroll=panel.scrollTop;
  panel.outerHTML=reviewPanel();
  document.querySelector(".review-panel").scrollTop=scroll;
 }
}
function renderAll(){
 root.innerHTML='<div class="review-layout"><main id="stage" class="review-stage"><div id="frame-space" class="frame-space"><div id="frame" class="app-frame"><div id="app" class="app"></div></div></div></main>'+reviewPanel()+'</div>';
 renderStage();
}
function setState(value){
 st.filter="";st.sourceEdition=null;st.collectionPopup=false;st.showSynopsis=false;st.sourceFixture=false;st.sourceState="results";st.confirmingWeak=false;
 if(page==="series"){
  st.releaseDownloads={"7579956":"done","issue2":"downloading"};
  if(value==="downloads")st.releaseDownloads["2065135"]="queued";
  if(value==="collections")st.collectionPopup=true;
  if(value==="no-match")st.filter="ZZZ nonexistent series";
 }else if(page==="edition"){
  st.editionDownloads={"7579956":"done"};
  if(value==="sources")st.sourceFixture=true;
  if(value==="alt")st.sourceEdition=editions[3];
  if(value==="collections")st.collectionPopup=true;
  if(value==="long")st.showSynopsis=true;
  if(value.startsWith("source-")){st.sourceEdition=editions[3];st.sourceState=value.slice(7)==="weak"?"results":value.slice(7);st.confirmingWeak=value==="source-weak";}
 }else if(page==="archive"){
  st.archiveView=value==="index"?"index":"board";
 }
 st.status="State selected for local review; no backend action occurred.";
 renderStage();if(st.collectionPopup)openCollections();if(st.sourceEdition)focusSource(st.confirmingWeak?'.weak-dialog button':undefined);
}
function openLocalRoute(destination){
 const url=new URL(destination,location.href);
 for(const k of ["capture","font","top","bottom","host"])if(params.has(k))url.searchParams.set(k,params.get(k));
 if(!capture){url.searchParams.set("w",st.w);url.searchParams.set("h",st.h);url.searchParams.set("font",st.font);url.searchParams.set("top",st.top);url.searchParams.set("bottom",st.bottom);}
 location.href=url.href;
}
function appAction(name,el){
 const id=el.dataset.id||"";
 const oldFocus=document.activeElement,focusAction=oldFocus?.dataset?.action,focusId=oldFocus?.dataset?.id;
 switch(name){
 case "back":
   if(page==="archive"&&st.archiveView==="releases"){st.nextScroll=st.archiveIndexScroll||0;st.archiveView="index";st.status="Returned to the archive index.";break;}
   if(page==="archive"&&st.archiveView==="index"){st.nextScroll=st.archiveBoardScroll||0;st.archiveView="board";st.status="Returned to GetComics Archives board.";break;}
   if(page==="series")return openLocalRoute("comic-archive.html?state=index");
   if(page==="edition")return openLocalRoute("comic-series.html");
   return openLocalRoute("index.html");
 case "window":st.status="Desktop window control shown for fidelity; mock does not control the OS.";break;
 case "library":st.saved=!st.saved;st.status=st.saved?"Added to local mock Library.":"Removed from local mock Library.";break;
 case "collections":openCollections(el);return;
 case "close-popup":st.collectionPopup=false;st.creating=false;break;
 case "toggle-collection":{
  const i=st.collections.indexOf(id);
  if(i<0)st.collections.push(id);else st.collections.splice(i,1);
  break;
 }
 case "save-collections":st.collectionPopup=false;st.status=st.collections.length+" collection memberships selected locally.";break;
 case "begin-create":st.creating=true;break;
 case "cancel-create":st.creating=false;break;
 case "create-collection":{
  const v=document.getElementById("new-collection")?.value.trim();
  if(!v){st.status="Give your collection a name.";break;}
  st.status="Local mock collection created: "+v;st.creating=false;st.collectionPopup=false;break;
 }
 case "sort":st.sort=el.dataset.sort;break;
 case "clear-search":st.filter="";break;
 case "release-read":{
  st.pendingRead=id;
  if((st.releaseDownloads[id]||"none")==="done"){st.status="Read / Continue would open the existing comic reader; reader UI is out of scope.";}
  else if(["downloading","queued"].includes(st.releaseDownloads[id])){st.status="Read when ready — pending local fixture, no actual download.";}
  else{st.releaseDownloads[id]="queued";st.status="Read intent queued in mock only, without source or backend.";}
  break;
 }
 case "release-download":{
  const prev=st.releaseDownloads[id]||"none";
  st.releaseDownloads[id]=prev==="done"||prev==="queued"||prev==="downloading"?"none":"downloading";
  st.status="Local download state toggled for visual comparison; no download was started.";break;
 }
 case "edition-open":{
  const edition=editions.find(e=>e.id===id);
  if(!edition)return;
  if(st.editionDownloads[id]==="done"){st.status="Read would open the existing comic reader; reader UI is outside Batch 2.";}
  else if(edition.available){st.editionDownloads[id]="queued";st.status="Edition action queued locally without download.";}
  break;
 }
 case "alt":st.sourceEdition=editions.find(e=>e.id===id)||editions[0];st.sourceState="results";st.sourceQuery="";st.sourceReturnId=id;st.status="Original alternate sources page; deterministic local pack fixtures only.";break;
 case "close-sources":st.sourceEdition=null;st.status="Returned to the edition ledger.";break;
 case "source-search":st.sourceState=st.sourceQuery.trim()?"empty":"results";st.status="Search was simulated; no provider was queried.";break;
 case "source-choose":
   if(Number(el.dataset.index)===2){st.confirmingWeak=true;break;}
   inspectPack(Number(el.dataset.index));return;
 case "weak-cancel":st.confirmingWeak=false;break;
 case "weak-confirm":inspectPack(2);return;
 case "source-return":case "source-manual":st.sourceState="results";break;
 case "source-confirm":case "archive-choose":finishPack();return;
 case "archive-box":{
  const box=boxes[Number(el.dataset.index)]||boxes[0];
  st.archiveBoardScroll=document.getElementById("app-scroll").scrollTop;st.nextScroll=0;st.archiveView="index";st.archiveBox=box.name;st.status="Opened local "+box.name+" series archives.";break;
 }
 case "archive-series":
   if(Number(el.dataset.index)===0&&st.archiveBox==="DC Comics")return openLocalRoute("comic-series.html");
   st.status="Selected archive route: "+(st.archiveBox==="Image Comics"?["Invincible","The Walking Dead"][Number(el.dataset.index)]:archiveSeries[Number(el.dataset.index)]?.title)+". This bounded fixture includes a runnable Batman detail; additional catalogue data is not bundled.";break;
 case "all-releases":st.archiveIndexScroll=document.getElementById("app-scroll").scrollTop;st.nextScroll=0;st.archiveView="releases";st.status="Opened the archive release shelf with the eight bundled example releases.";break;
 default:return;
 }
 renderStage();
 if(st.sourceEdition)focusSource(st.confirmingWeak?'.weak-dialog button':name==="source-manual"?'#source-query':name==="weak-cancel"?'[data-action="source-choose"][data-index="2"]':undefined);
 else if(name==="close-sources")requestAnimationFrame(()=>document.querySelector('[data-action="alt"][data-id="'+st.sourceReturnId+'"]')?.focus({preventScroll:true}));
 else if(focusAction)requestAnimationFrame(()=>document.querySelector('[data-action="'+focusAction+'"]'+(focusId?'[data-id="'+focusId+'"]':""))?.focus({preventScroll:true}));
}
renderAll();
if(params.has("state"))setState(params.get("state"));
root.addEventListener("click",e=>{
 const p=e.target.closest("[data-preset]");
 if(p){
  const cfg=presets.find(v=>v.id===p.dataset.preset);if(!cfg)return;
  st.preset=cfg.id;st.w=cfg.w;st.h=cfg.h;
  st.status="Viewport changed to "+cfg.w+" × "+cfg.h+" provisional logical pixels.";
  refreshReview();renderStage();return;
 }
 const action=e.target.closest("[data-action]");
 if(action){appAction(action.dataset.action,action);return;}
 const source=e.target.closest("[data-source-index]");
 if(source&&!e.target.closest("button")){appAction("source-choose",{dataset:{index:source.dataset.sourceIndex}});return;}
 const release=e.target.closest(".release-row[data-release]");
 if(release&&!e.target.closest("button")){appAction("release-read",{dataset:{id:release.dataset.release}});return;}
 const ed=e.target.closest(".edition-row[data-edition]");
 if(ed&&e.target.closest("button")==null){
  appAction("edition-open",{dataset:{id:ed.dataset.edition}});return;
 }
});
root.addEventListener("input",e=>{
 const t=e.target;
 if(t.id==="release-filter"){
  const caret=t.selectionStart;st.filter=t.value;renderStage();
  const next=document.getElementById("release-filter");if(next){next.focus();try{next.setSelectionRange(caret,caret);}catch(_){}}
 }else if(t.id==="source-query")st.sourceQuery=t.value;
 else if(t.id==="new-collection")st.newCollection=t.value;
});
root.addEventListener("change",e=>{
 const t=e.target;
 if(t.id==="review-state"){setState(t.value);return;}
 const names={"review-width":"w","review-height":"h","review-top":"top","review-bottom":"bottom","review-font":"font"};
 if(names[t.id]){
  const k=names[t.id];const value=Number(t.value);
  if(Number.isFinite(value))st[k]=value;
  st.preset="custom";st.status="Custom viewport or inset measurement applied (review-only).";
  refreshReview();renderStage();
 }
});
root.addEventListener("keydown",e=>{
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
window.addEventListener("resize",()=>{if(capture){st.w=window.innerWidth;st.h=window.innerHeight;renderStage();}else syncFrame();});
})();

