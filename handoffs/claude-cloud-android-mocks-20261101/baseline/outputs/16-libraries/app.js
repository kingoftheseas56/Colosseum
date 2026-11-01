/* Standalone mock only. Every item is an illustrative local fixture; no persistence or provider calls. */
(function(){
"use strict";
const ASSET="../../reference/desktop/assets/";
const worlds=["Theatre","Tankoban","Biblio","Universes","Profile"];
const iconNames=["projector-theatre.svg","comic-book.svg","book-library.svg","universes-atom.svg","your-colosseum-laurel.svg"];
const presets={portrait:[412,900,"android"],small:[354,792,"android"],landscape:[900,412,"android"],"small-landscape":[792,354,"android"],tablet:[800,1280,"android"],"tablet-landscape":[1280,800,"android"],desktop:[1280,700,"desktop"]};
const theatre=[
{id:"theatre-dune",title:"Dune",art:"wallpaper/sagas/dune.jpg",year:2021,type:"movie",state:"inProgress",progress:.42,downloaded:true,sub:"01:12:08"},
{id:"theatre-witcher",title:"The Witcher",art:"wallpaper/sagas/the-witcher.jpg",year:2019,type:"series",airing:"ended",state:"watched"},
{id:"theatre-jurassic",title:"Jurassic Park",art:"wallpaper/sagas/jurassic-park.jpg",year:1993,type:"movie",state:"unwatched"},
{id:"theatre-onepiece",title:"One Piece",art:"universes/one-piece/east-blue/banners/romance.png",year:1999,type:"series",airing:"ongoing",state:"inProgress",progress:.57,newCount:3,sub:"S02E17"},
{id:"theatre-starwars",title:"Star Wars",art:"extensions/house/starwars-banner.jpg",year:1977,type:"movie",state:"watched"},
{id:"theatre-harry",title:"Harry Potter",art:"wallpaper/sagas/harry-potter.jpg",year:2001,type:"movie",state:"unwatched",downloaded:true},
{id:"theatre-lotr",title:"The Lord of the Rings",art:"wallpaper/sagas/lord-of-the-rings.jpg",year:2001,type:"movie",state:"unwatched"},
{id:"theatre-gotham",title:"Gotham",art:"universes/dcau/portals/gotham.jpg",year:2014,type:"series",airing:"ended",state:"inProgress",progress:.29,sub:"S01E05"},
{id:"theatre-mandalorian",title:"The Mandalorian",art:"universes/star-wars/nevarro.jpg",year:2019,type:"series",airing:"ongoing",state:"unwatched",newCount:2},
{id:"theatre-percy",title:"Percy Jackson and the Olympians",art:"wallpaper/sagas/percy-jackson.jpg",year:2023,type:"series",airing:"ongoing",state:"unwatched"}];
const tankoban=[
{id:"tanko-romance",title:"One Piece — Romance Dawn",art:"universes/one-piece/east-blue/canon/romance-dawn.png",state:"inProgress",progress:.56,kind:"manga"},
{id:"tanko-arlong",title:"One Piece — Arlong Park",art:"universes/one-piece/east-blue/canon/arlong-park.png",state:"unread",kind:"manga"},
{id:"tanko-baratie",title:"One Piece — Baratie",art:"universes/one-piece/east-blue/canon/baratie.png",state:"unread",kind:"manga"},
{id:"tanko-wano",title:"One Piece — Wano Country",art:"universes/one-piece/new-world/banners/wano-country.png",state:"inProgress",progress:.43,downloaded:true,kind:"manga"},
{id:"tanko-dressrosa",title:"One Piece — Dressrosa",art:"universes/one-piece/new-world/banners/dressrosa.png",state:"unread",kind:"manga"},
{id:"tanko-skypiea",title:"One Piece — Skypiea",art:"universes/one-piece/paradise/banners/skypiea.png",state:"inProgress",progress:.73,kind:"manga"},
{id:"tanko-water7",title:"One Piece — Water 7",art:"universes/one-piece/paradise/banners/water-7.png",state:"unread",kind:"manga"},
{id:"tanko-marineford",title:"One Piece — Marineford",art:"universes/one-piece/paradise/banners/marineford.png",state:"unread",kind:"manga"},
{id:"tanko-egghead",title:"One Piece — Egghead",art:"universes/one-piece/final-sea/banners/egghead.png",state:"unread",kind:"manga"},
{id:"tanko-dc",title:"Gotham",art:"universes/dcau/portals/gotham.jpg",state:"unread",kind:"comic"}];
const biblio=[
{id:"biblio-dune",title:"Dune",author:"Frank Herbert",art:"wallpaper/sagas/dune.jpg",state:"inProgress",progress:.23,canResume:true},
{id:"biblio-lotr",title:"The Lord of the Rings",author:"J. R. R. Tolkien",art:"wallpaper/sagas/lord-of-the-rings.jpg",state:"unread"},
{id:"biblio-ice",title:"A Song of Ice and Fire",author:"George R. R. Martin",art:"wallpaper/sagas/ice-and-fire.jpg",state:"inProgress",progress:.61,canResume:true},
{id:"biblio-potter",title:"Harry Potter and the Philosopher's Stone",author:"J. K. Rowling",art:"wallpaper/sagas/harry-potter.jpg",state:"unread"},
{id:"biblio-witcher",title:"The Witcher: The Last Wish",author:"Andrzej Sapkowski",art:"wallpaper/sagas/the-witcher.jpg",state:"inProgress",progress:.11,canResume:true},
{id:"biblio-sherlock",title:"The Adventures of Sherlock Holmes",author:"Arthur Conan Doyle",art:"wallpaper/sagas/sherlock-holmes.jpg",state:"unread"},
{id:"biblio-percy",title:"Percy Jackson and the Lightning Thief",author:"Rick Riordan",art:"wallpaper/sagas/percy-jackson.jpg",state:"unread"},
{id:"biblio-jurassic",title:"Jurassic Park",author:"Michael Crichton",art:"wallpaper/sagas/jurassic-park.jpg",state:"unread"}];
const datasets={theatre:theatre,tankoban:tankoban,biblio:biblio};
const tabs={theatre:["Discover","Movies","Shows","Anime","Library"],tankoban:["Discover","Manga","Comics","Library"],biblio:["Discover","Explore","Library"]};
const state={world:document.documentElement.dataset.world||"theatre",sort:"",filter:"",type:"",airing:"",query:"",menu:null,selected:new Set(),confirm:false,visible:[],layout:{},holdTimer:0,holdFired:false};
state.sort=state.world==="theatre"?"lastWatched":state.world==="biblio"?"added":"lastRead";
const params=new URLSearchParams(location.search);
const capture=params.has("capture");
let preset=params.get("preset")||"portrait";
if(!presets[preset])preset="portrait";
const firstPreset=presets[preset];
const els={device:document.getElementById("device"),wrap:document.getElementById("deviceWrap"),status:document.getElementById("reviewStatus"),preset:document.getElementById("preset"),width:document.getElementById("vw"),height:document.getElementById("vh"),top:document.getElementById("safeTop"),bottom:document.getElementById("safeBottom"),scale:document.getElementById("fontScale"),demo:document.getElementById("demoState")};
if(capture){document.body.classList.add("capture");document.body.style.setProperty("--cap-w",(Number(params.get("width"))||innerWidth)+"px");document.body.style.setProperty("--cap-h",(Number(params.get("height"))||innerHeight)+"px");}
els.preset.value=preset;
els.width.value=firstPreset[0];els.height.value=firstPreset[1];
els.demo.value=params.get("state")||"normal";
function esc(v){return String(v==null?"":v).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;")}
function localIcon(name){return ASSET+"icons/"+name}
function art(name){return ASSET+name}
function note(message){if(els.status)els.status.textContent=message||"Illustrative saved entries and local project art. No library or device data is changed."}
function platform(){return capture?(params.get("platform")|| (dimensions()[0]>=1050&&dimensions()[1]>=600?"desktop":"android")):presets[els.preset.value][2]}
function dimensions(){return capture?[Number(params.get("width"))||innerWidth,Number(params.get("height"))||innerHeight]:[Math.max(280,+els.width.value||412),Math.max(300,+els.height.value||900)]}
function iconButton(name,label,cls){return '<button type="button" class="system-action '+(cls||"")+'" data-action="shell" data-label="'+esc(label)+'" aria-label="'+esc(label)+'"><img alt="" src="'+localIcon(name)+'"></button>'}
function shellHTML(){
const worldName=state.world==="theatre"?"Theatre":state.world==="tankoban"?"Tankoban":"Biblio";
const t=worldName==="Theatre"?' '+iconButton("stremio-official.svg","Stremio Sync","stremio"):"";
const nav=worlds.map((w,i)=>'<button class="world-entry '+(w===worldName?"active":"")+'" data-action="shell" data-label="'+esc(w)+'" aria-label="'+esc(w)+'"><img alt="" src="'+localIcon(iconNames[i])+'"><span>'+esc(w)+'</span></button>').join("");
const desktopWorldPills=["Tankoban","Biblio","Theatre","Universes","Feria","Vinyl"].map(w=>'<button class="world-pill '+(w===worldName?"is-active":"")+'" data-action="shell" data-label="'+esc(w)+'" '+(w==="Feria"||w==="Vinyl"?"disabled":"")+'>'+w+(w==="Feria"||w==="Vinyl"?'<span class="soon">SOON</span>':"")+'</button>').join("");
const time=new Date();const h=(time.getHours()%12)||12;const min=String(time.getMinutes()).padStart(2,"0");
const clock=h+":"+min;const ampm=time.getHours()>=12?"PM":"AM";
const date=time.toLocaleDateString("en-US",{weekday:"long",month:"long",day:"numeric"});
const topbar='<header class="topbar" aria-label="World header"><div class="clock-area"><div class="clock-back-row"><button class="back" data-action="shell" data-label="Home">‹ Home</button><div><div class="clock-big">'+clock+' <small>'+ampm+'</small></div><div class="clock-date">'+esc(date)+'</div></div></div></div><div class="world-pills" aria-label="Worlds">'+desktopWorldPills+'</div><div class="system-actions">'+iconButton("search.svg","Search")+t+iconButton("trackers.svg","Trackers")+'<button class="system-action account" data-action="shell" data-label="Account" aria-label="Account"><span></span></button>'+iconButton("preferences.svg","Preferences","desktop-only")+'</div></header>';
const max=state.world==="theatre"?"760px":state.world==="tankoban"?"640px":"480px";
const tabbar='<nav class="world-tabs" style="--tabs-max:'+max+'" aria-label="'+worldName+' tabs"><div class="tabs-glass">'+tabs[state.world].map(t=>'<button class="tab-pill '+(t==="Library"?"active":"")+'" data-action="tab" data-label="'+t+'">'+t+'</button>').join("")+'</div></nav>';
return '<div class="world-rail">'+nav+'</div><div class="app-main">'+topbar+'<main id="viewport" class="library-viewport">'+tabbar+'<section class="library-block" aria-label="'+worldName+' library"><div id="libraryBody"></div><div id="galleryRoot"></div></section></main></div><nav class="phone-bottom" aria-label="World navigation">'+nav+'</nav><div id="overlays"></div>';
}
function fitViewport(){
const [w,h]=dimensions();const android=platform()==="android";const phone=android&&Math.min(w,h)<600;const wide=android&&w>=840;const portrait=w<h;const short=phone&&h<500&&w>h;const worldWidth=w-(wide?92:0);
const margin=worldWidth<=430?18:worldWidth<600?24:worldWidth<840?32:54;
const top=worldWidth<840?12:30;
const barHeight=android&&worldWidth<840?56:56;
const selected=phone&&state.selected.size>0;
if(!phone)state.selected.clear();
const reserve=selected?(short?72:128):(android&&!wide?72:0);
state.layout={w,h,android,phone,wide,portrait,short,margin,top,barHeight,worldWidth,reserve};
els.device.className="screen world-"+state.world+(android?" android":" desktop")+(phone?" phone":"")+(wide?" wide":"")+(portrait?" portrait":"")+(short?" short":"")+(selected?" selection-active":"")+(state.confirm?" confirm-active":"");
els.device.style.width=w+"px";els.device.style.height=h+"px";
els.device.style.setProperty("--margin",margin+"px");els.device.style.setProperty("--top-inset",top+"px");
els.device.style.setProperty("--topbar-h",barHeight+"px");els.device.style.setProperty("--selection-bar-height",short?"72px":"128px");
els.device.style.setProperty("--reserved-bottom",reserve+"px");
els.device.style.setProperty("--inset-top",(capture?(+params.get("top")||0):(+els.top.value||0))+"px");
els.device.style.setProperty("--inset-bottom",(capture?(+params.get("bottom")||0):(+els.bottom.value||0))+"px");
els.device.style.setProperty("--s",capture?(+params.get("scale")||1):(+els.scale.value||1));
let factor=1;
if(!capture){factor=Math.min(1,Math.max(.25,(innerWidth-70)/w),(Math.max(520,innerHeight-185))/h);}
els.device.style.transform=factor===1?"none":"scale("+factor+")";els.device.style.transformOrigin="top left";
els.wrap.style.width=Math.round(w*factor)+"px";els.wrap.style.height=Math.round(h*factor)+"px";
}
function rowState(row){return row.state==="inProgress"?"inProgress":row.state==="watched"?"watched":"unwatched"}
function visibleRows(){
let rows=datasets[state.world].slice();
const q=state.query.trim().toLowerCase();
if(q)rows=rows.filter(r=>(r.title+" "+(r.author||"")).toLowerCase().includes(q));
if(state.world==="theatre"){
if(state.filter)rows=rows.filter(r=>state.filter==="newEpisodes"?!!r.newCount:state.filter==="downloaded"?!!r.downloaded:rowState(r)===state.filter);
if(state.type)rows=rows.filter(r=>r.type===state.type);
if(state.airing)rows=rows.filter(r=>r.type==="series"&&r.airing===state.airing);
if(state.sort==="year")rows.sort((a,b)=>(b.year||0)-(a.year||0));
}else if(state.world==="tankoban"){
if(state.filter)rows=rows.filter(r=>state.filter==="downloaded"?!!r.downloaded:r.state==="inProgress");
}else if(state.filter)rows=rows.filter(r=>r.state==="inProgress");
if(state.sort==="az")rows.sort((a,b)=>a.title.localeCompare(b.title));
if(state.sort==="added")rows.reverse();
return rows;
}
function filterPill(label,key,kind,active){return '<button class="filter-pill '+(active?"active":"")+'" data-action="filter" data-kind="'+kind+'" data-key="'+esc(key)+'" aria-pressed="'+(active?"true":"false")+'">'+esc(label)+'</button>'}
function filterGroup(items,kind,current){return '<div class="pill-group">'+items.map(x=>filterPill(x[0],x[1],kind,x[1]===current)).join("")+'</div>'}
function ledgerHTML(){
const all=datasets.theatre;
const segments=[["saved","",all.length],["in progress","inProgress",all.filter(r=>rowState(r)==="inProgress").length],["unwatched","unwatched",all.filter(r=>rowState(r)==="unwatched").length],["watched","watched",all.filter(r=>rowState(r)==="watched").length],["new episodes","newEpisodes",all.filter(r=>r.newCount).length],["downloaded","downloaded",all.filter(r=>r.downloaded).length]];
return '<div class="ledger">'+segments.map((s,i)=> (i?'<i aria-hidden="true" class="ledger-dot"></i>':"")+'<button class="ledger-frag '+(state.filter===s[1]?"active":"")+'" data-action="filter" data-kind="filter" data-key="'+s[1]+'" aria-pressed="'+(state.filter===s[1])+'"><strong>'+s[2]+'</strong> '+s[0]+'</button>').join("")+'</div>';
}
function headerHTML(){
const search='<input class="search-box" data-role="search" type="search" autocomplete="off" spellcheck="false" aria-label="'+(state.world==="biblio"?"Search by title or author":"Search your library")+'" placeholder="'+(state.world==="biblio"?"Search by title or author":"Search your library")+'" value="'+esc(state.query)+'">';
let out="";
if(state.world==="theatre"){
out='<header class="header-row theatre-header">'+ledgerHTML()+search+'</header>';
out+='<div class="filter-bar"><div class="pill-scroll">'+filterGroup([["Last watched","lastWatched"],["Recently added","added"],["A–Z","az"],["Year","year"]],"sort",state.sort)+'<i class="pill-divider"></i>'+filterGroup([["All",""],["Movies","movie"],["Series","series"]],"type",state.type)+'<i class="pill-divider"></i>'+filterGroup([["All",""],["Ongoing","ongoing"],["Ended","ended"]],"airing",state.airing)+'</div></div>';
}else if(state.world==="tankoban"){
out='<div class="tanko-toolbar">'+search+'<div class="pill-scroll">'+filterGroup([["All",""],["In Progress","inProgress"],["Downloaded","downloaded"]],"filter",state.filter)+'<i class="pill-divider"></i>'+filterGroup([["Last Read","lastRead"],["Recently Added","added"],["A-Z","az"]],"sort",state.sort)+'</div></div>';
}else{
out='<header class="header-row biblio-header">'+search+'</header>';
out+='<div class="filter-bar"><div class="pill-scroll">'+filterGroup([["Recently added","added"],["Last read","lastRead"],["A–Z","az"]],"sort",state.sort)+'<i class="pill-divider"></i>'+filterGroup([["All",""],["In Progress","inProgress"]],"filter",state.filter)+'</div></div>';
}
return out;
}
function galleryGeometry(){
const view=document.getElementById("viewport");
const width=view?view.clientWidth:Math.max(300,state.layout.worldWidth-2*state.layout.margin);
const phone=state.layout.phone,portrait=state.layout.portrait;
const cols=phone?(portrait?Math.max(1,Math.floor(width/106)):Math.max(1,Math.floor(width/128))):(state.world==="tankoban"?Math.max(2,Math.floor(width/178)):Math.max(2,Math.floor((width+20)/168)));
const cellW=width/cols;
const pw=phone?(portrait?Math.min(94,cellW-12):Math.min(148,cellW-16)):(state.world==="tankoban"?cellW-16:148);
return {cols,poster:Math.max(65,Math.floor(pw))};
}
function coverHTML(r){
const watched=rowState(r)==="watched";
const progress=r.progress>0?'<div class="hairline" style="--progress:'+r.progress+'"><i></i></div>':"";
const badge=state.world==="theatre"&&r.newCount?'<span class="new-chip">+'+r.newCount+'</span>':"";
const dl=state.world==="theatre"&&r.downloaded?'<span class="download-chip"><img alt="" src="'+localIcon("download.svg")+'"></span>':"";
const checked=state.world==="theatre"&&watched?'<span class="watched-chip">✓</span>':"";
const classic=state.world==="tankoban"?'<div class="classic-hover"><span class="classic-play">▶</span></div>':"";
const marked='<span class="selection-mark '+(state.selected.has(r.id)?"selected":"")+'">'+(state.selected.has(r.id)?"✓":"")+'</span>';
return '<div tabindex="0" role="'+(state.selected.size?"checkbox":"button")+'" aria-checked="'+(state.selected.has(r.id)?"true":"false")+'" aria-label="'+esc(r.title)+'" class="poster '+(watched?"watched":"")+'" data-action="card" data-id="'+r.id+'"><img class="poster-image" alt="" src="'+art(r.art)+'">'+classic+progress+badge+dl+checked+'<span class="poster-border"></span>'+marked+'<button class="dots" type="button" data-action="menu" data-id="'+r.id+'" aria-label="More actions for '+esc(r.title)+'">⋮</button></div>';
}
function cardHTML(r){
const watched=rowState(r)==="watched";
const meta=state.world==="theatre"?'<div class="card-meta">'+r.year+' · '+(r.type==="series"?"Series":"Movie")+(r.type==="series"&&r.airing?" · "+r.airing[0].toUpperCase()+r.airing.slice(1):"")+'</div>':state.world==="biblio"?'<div class="card-author">'+esc(r.author)+'</div>':"";
return '<article class="gallery-item '+(watched?"watched":"")+'" data-identity="'+r.id+'">'+coverHTML(r)+'<div class="card-details"><div class="card-title" title="'+esc(r.title)+'">'+esc(r.title)+'</div>'+meta+'</div></article>';
}
function emptyHTML(){
if(!state.query&&!state.filter&&!state.type&&!state.airing){
const copy=state.world==="theatre"?"Press play on anything, or tap + Library — it lands here.":state.world==="tankoban"?"Save a manga or comic series — it lands here.":"Save a book with + Library — it lands here.";
return '<div class="empty-view"><h2>Your library is empty</h2><p>'+copy+'</p></div>';
}
return '<div class="empty-view"><h2>'+(state.world==="theatre"?"Nothing matches these filters":state.world==="tankoban"?"No matches":"Nothing matches")+'</h2>'+(state.world==="tankoban"?"<p>Try a different filter or search.</p>":"")+'</div>';
}
function drawGallery(){
const viewport=document.getElementById("viewport");
if(!viewport)return;
const previousScroll=viewport.scrollTop;
document.getElementById("libraryBody").innerHTML=headerHTML();
state.visible=visibleRows();
const dims=galleryGeometry();
const root=document.getElementById("galleryRoot");
root.innerHTML=state.visible.length?'<div class="gallery '+(state.world==="tankoban"?"tanko-gallery":"")+'" style="--columns:'+dims.cols+';--poster-width:'+dims.poster+'px">'+state.visible.map(cardHTML).join("")+'</div>':emptyHTML();
viewport.scrollTop=previousScroll;
els.device.classList.toggle("selection-active",state.layout.phone&&state.selected.size>0);
els.device.classList.toggle("confirm-active",!!state.confirm);
renderOverlay();
}
function menuOptions(r){
if(state.world==="tankoban")return [["remove","Remove from Library","",true],...(state.layout.phone?[["select","Select","",false]]:[])];
if(state.world==="biblio"){
const actions=[];
if(r.canResume)actions.push(["resume","Resume","",false]);
actions.push(["details","Details","",false]);
if(state.layout.phone)actions.push(["select","Select","",false]);
actions.push(["separator"]);
actions.push(["remove","Remove from Library","",true]);return actions;
}
const label=r.progress?"Resume"+(r.sub?" "+r.sub:""):"Play";
const actions=[["resume",label,"play",false],["details","Details","info",false]];
if(state.layout.phone)actions.push(["select","Select","circle-check",false]);
actions.push(["separator"]);
if(r.progress)actions.push(["dismiss","Dismiss progress","rotate-ccw",false]);
actions.push(["watch",r.state==="watched"?"Mark unwatched":"Mark watched","circle-check",false],["separator"],["remove","Remove from Library","x",true]);return actions;
}
function menuHTML(r){
const options=menuOptions(r);
return '<div class="menu-scrim" data-action="close-menu"></div><div class="card-menu '+(state.world==="tankoban"?"tanko-menu":state.world==="biblio"?"biblio-menu":"")+'" id="cardMenu" role="menu" aria-label="Card actions">'+options.map(o=>o[0]==="separator"?'<div class="menu-separator" role="separator"></div>':'<button class="menu-item '+(o[3]?"warning":"")+'" data-action="menu-item" data-key="'+o[0]+'" role="menuitem">'+(o[2]?'<img alt="" src="'+ASSET+'icons/lucide/'+o[2]+'.svg">':"")+'<span>'+esc(o[1])+'</span></button>').join("")+'</div>';
}
function selectionHTML(){
if(!state.layout.phone||!state.selected.size)return "";
return '<div class="selection-bar" role="toolbar" aria-label="Library selection"><div class="selection-summary"><span class="selection-count" id="selectedCount">'+state.selected.size+' selected</span><button class="selection-action" data-action="batch" data-key="cancel">Cancel</button></div><div class="selection-actions">'+["Select all","Invert","Remove",...(state.world==="theatre"?["Mark watched","Mark unwatched"]:[])].map(s=>'<button class="selection-action" data-action="batch" data-key="'+esc(s)+'">'+s+'</button>').join("")+'</div></div>';
}
function confirmHTML(){
return '<div class="confirm-scrim" data-action="dismiss-confirm"><div class="confirm-box" role="dialog" aria-modal="true" aria-label="Remove from library"><div class="confirm-title">Remove '+state.selected.size+' from library?</div><div class="confirm-copy">Downloaded files and reading/watch progress stay on this device.</div><div class="confirm-actions"><button class="selection-action" data-action="confirm" data-key="keep">Keep</button><button class="selection-action" data-action="confirm" data-key="remove">Remove</button></div></div></div>';
}
function renderOverlay(){
const overlay=document.getElementById("overlays");if(!overlay)return;
const row=state.menu?datasets[state.world].find(r=>r.id===state.menu.id):null;
overlay.innerHTML=(row?menuHTML(row):"")+selectionHTML()+(state.confirm?confirmHTML():"");
if(row){
const menu=document.getElementById("cardMenu");const sw=state.layout.w;const sh=state.layout.h;
const top=state.layout.top+(+els.device.style.getPropertyValue("--inset-top").replace("px","")||0)+state.layout.barHeight+8;
const bottom=Math.max(8,state.layout.reserve+ (+els.device.style.getPropertyValue("--inset-bottom").replace("px","")||0));
menu.style.maxHeight=Math.max(74,sh-top-bottom-16)+"px";
const desiredX=state.menu.x;const desiredY=state.menu.y;
const mw=200;const mh=Math.min(menu.scrollHeight,sh-top-bottom-16);
menu.style.left=Math.max(8,Math.min(desiredX-mw,sw-mw-8))+"px";
menu.style.top=Math.max(top,Math.min(desiredY,sh-bottom-mh-8))+"px";
}
}
function redraw(){
fitViewport();els.device.innerHTML=shellHTML();drawGallery();
}
function fullReset(){
state.filter="";state.type="";state.airing="";state.query="";
state.sort=state.world==="theatre"?"lastWatched":state.world==="biblio"?"added":"lastRead";
state.menu=null;state.selected.clear();state.confirm=false;redraw();
const demo=capture?(params.get("state")||"normal"):els.demo.value;
if(demo==="menu"){
const row=state.visible[0];
if(row){const btn=els.device.querySelector(".dots");if(btn)openMenu(row.id,btn);}
}else if(demo==="selection"&&state.layout.phone){
state.selected=new Set(state.visible.slice(0,2).map(r=>r.id));fitViewport();drawGallery();
}else if(demo==="confirm"&&state.layout.phone){
state.selected=new Set(state.visible.slice(0,2).map(r=>r.id));state.confirm=true;fitViewport();drawGallery();
}
}
function openMenu(id,button){
const bounds=els.device.getBoundingClientRect();const b=button.getBoundingClientRect();
const scale=bounds.width/state.layout.w||1;
state.menu={id,x:(b.right-bounds.left)/scale,y:(b.bottom-bounds.top)/scale};state.confirm=false;
const row=els.device.querySelector('[data-id="'+id+'"].poster');if(row)row.classList.add("menu-target");
renderOverlay();note("Original "+(state.world==="tankoban"?"Tankoban":"library")+" card menu open; all actions are simulated.");
}
function closeMenu(){state.menu=null;els.device.querySelectorAll(".menu-target").forEach(el=>el.classList.remove("menu-target"));renderOverlay()}
function actionCard(id){
const row=datasets[state.world].find(x=>x.id===id);if(!row)return;
if(state.selected.size&&state.layout.phone){
if(state.selected.has(id))state.selected.delete(id);else state.selected.add(id);
state.confirm=false;fitViewport();drawGallery();return;
}
const action=state.world==="theatre"?"Details":state.world==="tankoban"&&row.state==="inProgress"?"Resume":state.world==="biblio"&&row.canResume?"Resume":"Details";
note(action+" requested for "+row.title+" (mock only; no real navigation).");
}
function actionMenu(key){
const row=state.menu&&datasets[state.world].find(r=>r.id===state.menu.id);
if(!row)return;
closeMenu();
if(key==="select"&&state.layout.phone){
state.selected.add(row.id);fitViewport();drawGallery();note("Selection started using the original phone menu. Nothing saved or changed.");return;
}
note((key==="remove"?"Remove from Library":key==="watch"?"Mark watched/unwatched":key==="dismiss"?"Dismiss progress":key==="details"?"Details":key==="resume"?"Resume / Play":key)+" requested for "+row.title+" (mock only; no library data changed).");
}
function batch(key){
if(key==="cancel"){state.selected.clear();state.confirm=false;}
else if(key==="Select all")state.selected=new Set(state.visible.map(r=>r.id));
else if(key==="Invert")state.selected=new Set(state.visible.filter(r=>!state.selected.has(r.id)).map(r=>r.id));
else if(key==="Remove"){state.confirm=true;}
else if(key==="Mark watched"||key==="Mark unwatched"){state.selected.clear();note(key+" simulated. No watch-history data changed.");}
fitViewport();drawGallery();
}
els.device.addEventListener("click",function(e){
const b=e.target.closest("[data-action]");if(!b)return;
const act=b.dataset.action;
if(act==="dismiss-confirm"){if(e.target===b){state.confirm=false;fitViewport();drawGallery();}return;}
if(act==="confirm"){state.confirm=false;if(b.dataset.key==="remove"){note("Remove confirmed in the mock; no entry was actually removed.");state.selected.clear();}fitViewport();drawGallery();return;}
if(act==="close-menu"){closeMenu();return;}
if(act==="menu-item"){actionMenu(b.dataset.key);return;}
if(act==="menu"){const current=state.menu;const id=b.dataset.id;if(current&&current.id===id)closeMenu();else openMenu(id,b);return;}
if(act==="card"){if(state.holdFired){state.holdFired=false;return;}actionCard(b.dataset.id);return;}
if(act==="batch"){batch(b.dataset.key);return;}
if(act==="filter"){
const kind=b.dataset.kind;const key=b.dataset.key;
state[kind]=state[kind]===key&&(kind!=="sort")?"":key;
state.menu=null;state.selected.clear();state.confirm=false;
fitViewport();drawGallery();note("Illustrative filter or sort applied locally.");return;
}
if(act==="tab"||act==="shell"){
note(b.dataset.label+" opens its existing Colosseum route in the app. This standalone page does not navigate or call a backend.");
}
});
els.device.addEventListener("input",function(e){
if(!e.target.matches('[data-role="search"]'))return;
const pos=e.target.selectionStart;const value=e.target.value;state.query=value;
state.menu=null;state.selected.clear();state.confirm=false;drawGallery();
const next=els.device.querySelector('[data-role="search"]');if(next){next.focus();next.setSelectionRange(pos,pos)}
});
els.device.addEventListener("pointerdown",function(e){
const p=e.target.closest(".poster");if(!p||e.target.closest(".dots")||!state.layout.phone)return;
state.holdFired=false;const id=p.dataset.id;const x=e.clientX,y=e.clientY;
clearTimeout(state.holdTimer);
state.holdTimer=setTimeout(function(){
const current=els.device.querySelector('[data-id="'+id+'"].dots');if(current){state.holdFired=true;openMenu(id,current)}
},500);
const cancel=function(m){if(m&&m.type==="pointermove"&&Math.hypot(m.clientX-x,m.clientY-y)<8)return;clearTimeout(state.holdTimer);els.device.removeEventListener("pointermove",cancel);els.device.removeEventListener("pointerup",cancel);els.device.removeEventListener("pointercancel",cancel);};
els.device.addEventListener("pointermove",cancel);els.device.addEventListener("pointerup",cancel);els.device.addEventListener("pointercancel",cancel);
});
els.device.addEventListener("keydown",function(e){
if(e.key==="Escape"||e.key==="Backspace"&&state.confirm){
if(state.confirm){state.confirm=false;fitViewport();drawGallery();e.preventDefault();return}
if(state.menu){closeMenu();e.preventDefault();return}
if(state.selected.size){state.selected.clear();fitViewport();drawGallery();e.preventDefault();return}
}
if((e.key==="Enter"||e.key===" ")&&e.target.classList.contains("poster")){actionCard(e.target.dataset.id);e.preventDefault()}
if(state.menu&&(e.key==="ArrowDown"||e.key==="ArrowUp")){
const buttons=[...els.device.querySelectorAll(".card-menu .menu-item")];if(buttons.length){
let pos=buttons.indexOf(document.activeElement);pos=(pos+(e.key==="ArrowDown"?1:-1)+buttons.length)%buttons.length;buttons[pos].focus();e.preventDefault();
}
}
});
els.preset.addEventListener("change",function(){const p=presets[this.value];els.width.value=p[0];els.height.value=p[1];redraw()});
for(const el of [els.width,els.height,els.top,els.bottom,els.scale])el.addEventListener("change",redraw);
els.demo.addEventListener("change",fullReset);
window.addEventListener("resize",function(){if(capture)redraw();else fitViewport()});
fullReset();note("");
})();