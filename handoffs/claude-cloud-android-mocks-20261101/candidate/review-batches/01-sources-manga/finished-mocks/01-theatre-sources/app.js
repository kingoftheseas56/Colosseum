"use strict";
// Local HTML study. All media rows below are illustrative, not fetched or live availability.
const FILM = {
  title:"Star Wars: A New Hope", meta:"1977 - Science Fiction, Adventure",
  art:"../../../../../baseline/reference/desktop/assets/extensions/house/starwars-banner.jpg",
  rows:[
    ["Torrentio","4K","4K · HDR","Star.Wars.Episode.IV.A.New.Hope.1977.REMASTERED.2160p.UHD.BluRay.REMUX.HEVC.TrueHD.Atmos.7.1",744,"64.8 GB","1337x","TRUEHD ATMOS 7.1",["ENG","HIN","ESP","FRA"],["REMUX","HDR10","HEVC"],"Torrent","P2P stream",false],
    ["MediaFusion","4K","4K · Dolby Vision","Star.Wars.A.New.Hope.1977.2160p.DV.HDR10.BluRay.REMUX.HEVC.MULTI-AUDIO.RELEASE-GROUP",182,"52.4 GB","TorrentGalaxy","DTS-HD MA 7.1",["ENG","JPN","DEU"],["DOLBY VISION","REMUX","BLURAY"],"Torrent","P2P stream",false],
    ["Torrentio","1080p","1080p · BluRay","Star.Wars.Episode.IV.A.New.Hope.1977.1080p.BluRay.x264.DTS.MULTi.LONG-RELEASE-NAME-WITH-MANY-PARTS",1286,"12.6 GB","YTS","DTS 5.1",["ENG","ESP","POR","HIN","FRA","TAM"],["BLURAY","H.264","MULTI"],"Torrent","P2P stream",false],
    ["NoTorrent","1080p","1080p · WEB-DL","Star.Wars.A.New.Hope.1977.1080p.WEB-DL.H264.AAC2.0",null,"4.2 GB","NoTorrent","AAC 2.0",["ENG","ESP"],["WEB-DL","DIRECT"],"Direct","HTTP stream",false],
    ["Comet","1080p","1080p · BluRay","Star.Wars.1977.DESPECIALIZED.EDITION.1080p.BluRay.x264.FLAC-RESTORATION",58,"18.1 GB","1337x","FLAC 2.0",["ENG"],["RESTORED","BLURAY"],"Torrent","P2P stream",false],
    ["MediaFusion","720p","720p · WEB","Star.Wars.Episode.IV.A.New.Hope.1977.720p.WEBRip.x264",316,"2.1 GB","BitSearch","AAC 5.1",["ENG","HIN"],["WEBRIP"],"Torrent","P2P stream",false],
    ["NoTorrent","720p","720p · Direct","Star.Wars.A.New.Hope.1977.720p.STREAM.H264.AAC", -1,"","NoTorrent","AAC 2.0",["ENG","FRA"],["DIRECT","H.264"],"Direct","HTTP stream",false],
    ["Torrentio","SD","SD · DVD","Star.Wars.Episode.IV.A.New.Hope.1977.DVDRip.XviD.MP3",63,"1.4 GB","YTS","MP3",["ENG"],["DVDRIP"],"Torrent","P2P stream",false]
  ]
};
const EPISODE = {
  title:"The Mandalorian", meta:"S1E1 - Chapter 1: The Mandalorian",
  art:"../../../../../baseline/reference/desktop/assets/universes/star-wars/nevarro.jpg",
  rows:[
    ["Torrentio","4K","4K · HDR","The.Mandalorian.S01E01.Chapter.One.2160p.DSNP.WEB-DL.DDP5.1.Atmos.HDR10.HEVC.MULTi",587,"9.7 GB","TorrentGalaxy","DDP ATMOS 5.1",["ENG","ESP","HIN","TAM"],["HDR10","WEB-DL"],"Torrent","P2P stream",false],
    ["MediaFusion","4K","4K · Dolby Vision","The.Mandalorian.S01E01.2160p.DV.HDR.WEB-DL.H265.EAC3.MULTI-EXTENDED-CUT",112,"8.3 GB","BitSearch","EAC3 5.1",["ENG","FRA","DEU","JPN"],["DOLBY VISION","HEVC"],"Torrent","P2P stream",false],
    ["NoTorrent","1080p","1080p · Direct","The.Mandalorian.S01E01.1080p.DSNP.WEB-DL.AAC.2.0",null,"1.9 GB","NoTorrent","AAC 2.0",["ENG","ESP","POR"],["DIRECT","WEB-DL"],"Direct","HTTP stream",false],
    ["Torrentio","1080p","1080p · WEB-DL","The.Mandalorian.S01E01.1080p.DSNP.WEB-DL.DDP5.1.H264-RELEASE",1073,"3.4 GB","1337x","DDP 5.1",["ENG","HIN"],["WEB-DL","H.264"],"Torrent","P2P stream",false],
    ["Comet","720p","720p · WEB","The.Mandalorian.S01E01.720p.WEBRip.x264.AAC",87,"1.1 GB","YTS","AAC 5.1",["ENG"],["WEBRIP"],"Torrent","P2P stream",false],
    ["Torrentio","4K","4K · Season Pack","The.Mandalorian.S01.COMPLETE.2160p.DSNP.WEB-DL.DDP5.1.Atmos.HDR10.HEVC-ALL-EIGHT-EPISODES",331,"77.2 GB","TorrentGalaxy","DDP ATMOS 5.1",["ENG","FRA","ESP"],["FULL SEASON","HDR10","HEVC"],"Torrent","P2P stream",true],
    ["MediaFusion","1080p","1080p · Season Pack","The.Mandalorian.Season.01.Complete.1080p.BluRay.x264.DTS.5.1.MULTi-LONG-EDITION",188,"27.6 GB","1337x","DTS 5.1",["ENG","JPN","DEU"],["FULL SEASON","BLURAY"],"Torrent","P2P stream",true],
    ["Comet","720p","720p · Season Pack","The.Mandalorian.S01.COMPLETE.720p.WEBRip.AAC2.0.H264",76,"11.3 GB","BitSearch","AAC 2.0",["ENG","POR"],["FULL SEASON"],"Torrent","P2P stream",true],
    ["Torrentio","SD","SD · Season Pack","The.Mandalorian.S01.COMPLETE.SD.WEBRip.MP3.XviD",14,"3.7 GB","YTS","MP3",["ENG"],["FULL SEASON","SD"],"Torrent","P2P stream",true]
  ]
};
const page = document.body.dataset.page === "episode" ? "episode" : "film";
const config = page === "episode" ? EPISODE : FILM;
const $ = id => document.getElementById(id);
const rows = config.rows.map((r,i)=>({
  id:i,addonName:r[0],quality:r[1],qualityLine:r[2],release:r[3],seeders:r[4],
  size:r[5],sourceName:r[6],audio:r[7],languages:r[8],tags:r[9],
  streamKind:r[10],streamLabel:r[11],seasonPack:r[12]
}));
const state={w:412,h:900,insets:{top:0,right:0,bottom:0,left:0},scale:1,mode:"play",quality:"all",
  provider:"all",menu:false,selected:null,copied:null,queued:false,open:true,context:null};
const escapeHtml=s=>String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const allowedModes=page==="episode"?["play","download","season"]:["play","download"];
const basename=page==="episode"?"episode-sources.html":"film-sources.html";
function baseRows(){return state.mode==="season"?rows.filter(r=>r.seasonPack):rows}
function extensionRows(){return state.provider==="all"?baseRows():baseRows().filter(r=>r.addonName===state.provider)}
function shownRows(){return state.quality==="all"?extensionRows():extensionRows().filter(r=>r.quality===state.quality)}
function extOptions(){
  const m=new Map();baseRows().forEach(r=>m.set(r.addonName,(m.get(r.addonName)||0)+1));
  return [["all",baseRows().length],...m.entries()];
}
function qualityOptions(){
  const m=new Map();extensionRows().forEach(r=>m.set(r.quality,(m.get(r.quality)||0)+1));
  return [["all",extensionRows().length],...["4K","1080p","720p","SD"].filter(q=>m.has(q)).map(q=>[q,m.get(q)])];
}
function metaPieces(r){
  const p=[];
  if(typeof r.seeders==="number" && r.seeders>=0)p.push(r.seeders+" seeders");
  if(r.size)p.push(r.size);
  if(r.sourceName && r.sourceName!=="P2P")p.push(r.sourceName);
  return p;
}
function tell(msg){if($("feedback")) $("feedback").textContent=msg}
function setView(w,h){
  state.w=Math.max(320,Math.min(1920,Number(w)||412));
  state.h=Math.max(320,Math.min(1300,Number(h)||900));
  $("vw").value=state.w;$("vh").value=state.h;
  const v=$("viewport");v.style.width=state.w+"px";v.style.height=state.h+"px";
  v.style.setProperty("--vh",state.h+"px");v.style.setProperty("--vw",state.w+"px");
  const phone=Math.min(state.w,state.h)<600,landscape=state.w>state.h;
  v.classList.toggle("is-phone",phone);v.classList.toggle("is-landscape",landscape);
  v.classList.toggle("is-short",phone&&state.h<385);
  document.querySelectorAll("[data-size]").forEach(b=>{
    b.classList.toggle("active",b.dataset.size===state.w+"x"+state.h);
  });
  setInsets();positionMenu();
  $("viewInfo").textContent=state.w+" × "+state.h+" logical px · "+(phone?"phone fitting":"desktop/tablet fitting");
}
function setInsets(){
  const v=$("viewport");
  for(const k of ["top","right","bottom","left"])v.style.setProperty("--inset-"+k,state.insets[k]+"px");
  v.style.setProperty("--scale",state.scale);
  positionMenu();
}
function positionMenu(){
  if(!$("providerMenu")||$("providerMenu").hidden)return;
  const frame=$("viewport").getBoundingClientRect(),trigger=$("providerTrigger").getBoundingClientRect();
  const room=frame.bottom-trigger.bottom-state.insets.bottom-14;
  $("providerMenu").style.maxHeight=Math.max(52,room)+"px";
}
function showMenu(show){
  state.menu=!!show;$("providerMenu").hidden=!state.menu;$("providerScrim").hidden=!state.menu;
  $("providerTrigger").setAttribute("aria-expanded",String(state.menu));
  if(state.menu){renderMenu();positionMenu();$("providerMenu").querySelector("[aria-selected=true]")?.focus()}
  else $("providerTrigger").focus();
}
function renderMenu(){
  $("providerLabel").textContent=state.provider==="all"?"All":state.provider;
  $("providerLabel").classList.toggle("selected",state.provider!=="all");
  $("providerMenu").innerHTML=extOptions().map(([name,count])=>
    '<button class="provider-option" role="option" aria-selected="'+(state.provider===name)+'" data-provider="'+escapeHtml(name)+'">'+
    '<span>'+escapeHtml(name==="all"?"All":name)+'</span><span>'+count+' '+(count===1?"source":"sources")+'</span></button>').join("");
}
function renderPills(){
  const opts=qualityOptions();
  if(state.quality!=="all"&&!opts.some(([q])=>q===state.quality))state.quality="all";
  $("quality").innerHTML=opts.map(([name,count])=>'<button type="button" class="quality-pill '+(name===state.quality?"active":"")+
    '" data-quality="'+name+'" aria-pressed="'+(name===state.quality)+'"><span>'+escapeHtml(name==="all"?"All":name)+
    '</span><small>'+count+'</small></button>').join("");
}
function renderRows(){
  const focused=document.activeElement, focusId=focused?.dataset?.row, focusAct=focused?.dataset?.act, focusInRows=$("rows").contains(focused), scroll=$("rows").scrollTop;
  const current=shownRows();
  $("tableCount").textContent=current.length+" "+(current.length===1?"source":"sources");
  const asking=extOptions().length-1;
  $("tableVia").textContent=asking>2?"via "+asking+" extensions":"via "+extOptions().slice(1).map(x=>x[0]).join(" · ");
  $("rows").innerHTML=current.map(r=>{
    const meta=metaPieces(r).map(p=>'<span class="meta-piece">'+escapeHtml(p)+'</span>').join("");
    const languages=r.languages.map(x=>'<span class="lang-chip">'+escapeHtml(x)+'</span>').join("");
    const tags=r.tags.map(x=>'<span class="tag-chip">'+escapeHtml(x)+'</span>').join("");
    const cop=state.copied===r.id,play=state.mode==="play";
    const queued=state.queued;
    return '<article class="source-row '+(state.selected===r.id?"chosen":"")+'" role="button" tabindex="0" aria-label="'+
      escapeHtml((play?"Play ":"Choose source ")+r.addonName+" "+r.qualityLine)+" " +escapeHtml(r.release)+'" data-row="'+r.id+'">'+
      '<div class="provider-tile" aria-hidden="true">'+escapeHtml(r.addonName.charAt(0))+'</div>'+
      '<div class="row-main"><div class="provider-quality"><span class="row-provider">'+escapeHtml(r.addonName)+
      '</span><span class="row-quality">'+escapeHtml(r.qualityLine||r.quality)+'</span></div>'+
      '<div class="row-release" title="'+escapeHtml(r.release)+'">'+escapeHtml(r.release)+'</div>'+
      '<div class="row-meta">'+meta+'</div>'+
      '<div class="audio-langs"><span class="audio-label">'+escapeHtml(r.audio)+'</span>'+languages+'</div>'+
      '<div class="tags">'+tags+'</div>'+
      '<div class="stream-line">'+escapeHtml((r.streamKind||"Torrent")+" · "+(r.streamLabel||"P2P stream"))+'</div></div>'+
      '<div class="row-actions">'+(play?'<button type="button" class="circle '+(queued?"queued":"")+
      '" data-act="download" data-row="'+r.id+'" aria-label="'+(queued?"Already queued":"Download this source")+
      '" title="Download this source" '+(queued?"disabled":"")+'>'+(queued?"✓":"↓")+'</button>':"")+
      '<button type="button" class="circle '+(cop?"copied":"")+'" data-act="copy" data-row="'+r.id+
      '" aria-label="Copy link" title="Copy link">'+(cop?"✓":"⧉")+'</button>'+
      '<button type="button" class="circle play" data-act="activate" data-row="'+r.id+
      '" aria-label="'+(play?"Play source":"Choose source")+'" title="'+(play?"Play":"Choose source")+'">'+
      (play?"▶":"↓")+'</button></div></article>';
  }).join("")||'<div class="empty-state">No sources found.</div>';
  $("rows").scrollTop=scroll;
  if(focusInRows&&focusId!==undefined){const target=focusAct?$("rows").querySelector('[data-act="'+focusAct+'"][data-row="'+focusId+'"]:not([disabled])'):$("rows").querySelector('.source-row[data-row="'+focusId+'"]'); (target||$("rows").querySelector('.source-row[data-row="'+focusId+'"]'))?.focus({preventScroll:true});}
}
function renderAll(resetScroll=false){
  if(state.provider!=="all"&&!extOptions().some(([n])=>n===state.provider))state.provider="all";
  renderMenu();renderPills();renderRows();
  if(resetScroll)$("rows").scrollTop=0;
}
function useRow(id,action){
  const r=rows.find(x=>x.id===Number(id));if(!r)return;
  if(action==="copy"){
    state.copied=r.id;renderRows();tell("Copy link selected · confirmation tick is simulated; no clipboard/provider operation.");
    setTimeout(()=>{if(state.copied===r.id){state.copied=null;renderRows()}},1200);return;
  }
  if(action==="download" && state.mode==="play"){
    if(state.queued)return;
    state.queued=true;renderRows();tell("downloadRequested("+r.addonName+") simulated; title queued for this visit only.");return;
  }
  state.selected=r.id;renderRows();
  if(state.mode!=="play"){
    tell("downloadRequested("+r.addonName+") simulated · "+(state.mode==="season"?"full-season ":"")+"source chosen; picker closed.");
    setOpen(false);
  }else{
    tell("playRequested("+r.addonName+", "+r.quality+") simulated · no player or network request.");
  }
}
function setOpen(open){
  state.open=!!open;
  $("sourceSheet").hidden=!open;$("underlay").hidden=open;
  $("toggleSheet").textContent=open?"Close picker":"Reopen picker";
  if(!open){state.menu=false;$("providerMenu").hidden=true;$("providerScrim").hidden=true;closeContext()}
}
function closeContext(){
  const old=state.context;if(old!==null)$("rows").querySelector('.source-row[data-row="'+old+'"]')?.focus({preventScroll:true});
  state.context=null;$("contextScrim").hidden=true;$("contextMenu").hidden=true;
}
function openContext(id){
  const r=rows.find(x=>x.id===Number(id));if(!r)return;
  state.context=r.id;state.menu=false;$("providerMenu").hidden=true;$("providerScrim").hidden=true;
  $("providerTrigger").setAttribute("aria-expanded","false");
  $("contextScrim").hidden=false;$("contextMenu").hidden=false;
  $("contextMenu").innerHTML=[["activate",state.mode==="play"?"Play":"Choose source"],["copy","Copy link"],
    ...(state.mode==="play"?[["download","Download"]]:[])].map(([a,label])=>
    '<button data-context-action="'+a+'" type="button">'+label+'</button>').join("");
  $("contextMenu").querySelector("button")?.focus();
}
function setMode(mode){
  if(!allowedModes.includes(mode))return;
  state.mode=mode;$("viewport").dataset.mode=mode;state.provider="all";state.quality="all";state.queued=false;state.selected=null;setOpen(true);
  const modeHeader=mode==="season"?"DOWNLOAD SEASON — FULL-SEASON TORRENTS":mode==="download"?"DOWNLOAD — PICK A SOURCE":"";
  $("heroMeta").textContent=modeHeader?(modeHeader+" · "+config.meta):config.meta;
  $("pageMode").value=mode;renderAll(true);
  tell("Study mode: "+mode+". "+(mode==="season"?"Only illustrative complete-season torrent rows are listed.":""));
}
function setup(){
  document.documentElement.style.setProperty("--art",'url("'+config.art+'")');
  $("heroTitle").textContent=config.title;$("detailTitle").textContent=config.title;
  $("heroMeta").textContent=config.meta;$("detailMeta").textContent=config.meta;
  $("artCredit").textContent=page==="film"?"Star Wars franchise banner (local house art; not a verified title backdrop)":
    "Nevarro scene (local universe art; not a verified episode backdrop)";
  $("pageName").textContent=page==="film"?"Film sources":"Episode sources";
  $("pageMode").innerHTML=allowedModes.map(m=>'<option value="'+m+'">'+(m==="play"?"Play":m==="download"?"Download":"Full season")+'</option>').join("");
  $("pageMode").addEventListener("change",e=>setMode(e.target.value));
  const menuScrim=document.createElement("div");menuScrim.id="providerScrim";menuScrim.className="provider-scrim";menuScrim.hidden=true;$("sourceSheet").append(menuScrim);menuScrim.addEventListener("click",e=>{e.stopPropagation();showMenu(false)});
  $("providerTrigger").addEventListener("click",()=>showMenu(!state.menu));
  $("providerMenu").addEventListener("click",e=>{
    const b=e.target.closest("[data-provider]");if(!b)return;
    state.provider=b.dataset.provider;showMenu(false);renderAll(true);tell("Provider filter: "+(state.provider==="all"?"All":state.provider));
  });
  $("providerMenu").addEventListener("keydown",e=>{
    const btn=[...$("providerMenu").querySelectorAll(".provider-option")];const i=btn.indexOf(document.activeElement);
    let next=i;
    if(e.key==="ArrowDown")next=(i+1)%btn.length;
    else if(e.key==="ArrowUp")next=(i+btn.length-1)%btn.length;
    else if(e.key==="Home")next=0;else if(e.key==="End")next=btn.length-1;
    else if(e.key==="Escape"){showMenu(false);e.preventDefault();e.stopPropagation();return}
    else if(e.key==="Tab"){e.preventDefault();e.stopPropagation();return}
    else return;
    btn[next]?.focus();e.preventDefault();
  });
  $("quality").addEventListener("click",e=>{const b=e.target.closest("[data-quality]");if(!b)return;
    state.quality=b.dataset.quality;renderAll(true);tell("Quality filter: "+state.quality);
  });
  $("rows").addEventListener("click",e=>{const btn=e.target.closest("[data-act]");if(btn){useRow(btn.dataset.row,btn.dataset.act);return}
    const r=e.target.closest("[data-row]");if(r)useRow(r.dataset.row,"activate");
  });
  $("rows").addEventListener("keydown",e=>{
    if(e.target.matches(".source-row")&&(e.key==="Enter"||e.key===" ")){e.preventDefault();useRow(e.target.dataset.row,"activate")}
    if(e.target.matches(".source-row")&&(e.key==="F10"&&e.shiftKey)){e.preventDefault();openContext(e.target.dataset.row)}
  });
  $("rows").addEventListener("contextmenu",e=>{const r=e.target.closest(".source-row");if(r){e.preventDefault();openContext(r.dataset.row)}});
  $("contextMenu").addEventListener("click",e=>{const b=e.target.closest("[data-context-action]");if(!b)return;
    const id=state.context;closeContext();useRow(id,b.dataset.contextAction);
  });
  $("contextScrim").addEventListener("click",closeContext);
  $("contextMenu").addEventListener("keydown",e=>{
    const buttons=[...$("contextMenu").querySelectorAll("button")],i=buttons.indexOf(document.activeElement);
    let n=i;
    if(e.key==="ArrowDown")n=(i+1)%buttons.length;
    else if(e.key==="ArrowUp")n=(i+buttons.length-1)%buttons.length;
    else if(e.key==="Home")n=0;else if(e.key==="End")n=buttons.length-1;
    else if(e.key==="Tab"){e.preventDefault();return}
    else return;
    buttons[n]?.focus();e.preventDefault();
  });
  $("quality").addEventListener("keydown",e=>{
    const buttons=[...$("quality").querySelectorAll("button")],i=buttons.indexOf(document.activeElement);
    let n=i;
    if(e.key==="ArrowRight")n=(i+1)%buttons.length;
    else if(e.key==="ArrowLeft")n=(i+buttons.length-1)%buttons.length;
    else if(e.key==="Home")n=0;else if(e.key==="End")n=buttons.length-1;
    else return;
    buttons[n]?.focus();e.preventDefault();
  });
  $("backAction").addEventListener("click",()=>{setOpen(false);tell("Back · source picker closed; parent Theatre detail remains.")});
  $("reopen").addEventListener("click",()=>setOpen(true));
  $("toggleSheet").addEventListener("click",()=>setOpen(!state.open));
  document.addEventListener("keydown",e=>{if(e.key==="Escape"){
    if(state.context!==null){closeContext();return}
    if(state.menu){showMenu(false);return}
    if(state.open)setOpen(false);
  }});
  document.addEventListener("pointerdown",e=>{
    if(state.menu && !e.target.closest(".provider-wrap") && e.target.id!=="providerScrim"){e.preventDefault();e.stopPropagation();showMenu(false);}
  });
  document.querySelectorAll("[data-size]").forEach(b=>b.addEventListener("click",()=>{
    const [w,h]=b.dataset.size.split("x").map(Number);setView(w,h);
  }));
  for(const id of ["vw","vh"])$(id).addEventListener("change",()=>setView($("vw").value,$("vh").value));
  $("fontScale").addEventListener("change",e=>{state.scale=Number(e.target.value)||1;setInsets()});
  for(const key of ["top","right","bottom","left"]){
    $("inset-"+key).addEventListener("change",e=>{state.insets[key]=Math.max(0,Number(e.target.value)||0);setInsets()});
  }
  $("presetInsets").addEventListener("click",()=>{
    state.insets={top:24,right:0,bottom:24,left:0};
    for(const k of Object.keys(state.insets))$("inset-"+k).value=state.insets[k];
    setInsets();tell("Illustrative 24px top and bottom insets; device insets remain unmeasured.");
  });
  $("zeroInsets").addEventListener("click",()=>{
    state.insets={top:0,right:0,bottom:0,left:0};
    for(const k of Object.keys(state.insets))$("inset-"+k).value=0;
    setInsets();
  });
  const params=new URLSearchParams(location.search);
  const w=Number(params.get("w"))||412,h=Number(params.get("h"))||900;
  if(params.get("render")==="1")document.body.classList.add("render");
  setView(w,h);setMode(params.get("mode")||"play");
  if(params.get("provider")){state.provider=params.get("provider");renderAll()}
  if(params.get("quality")){state.quality=params.get("quality");renderAll()}
  if(params.get("menu")==="1")showMenu(true);
  if(params.get("context")==="1")openContext(rows[0].id);
  if(params.get("closed")==="1")setOpen(false);
  if(params.get("scale")){$("fontScale").value=params.get("scale");state.scale=Number(params.get("scale"));setInsets()}
  for(const side of ["top","right","bottom","left"]){const q="inset"+side[0].toUpperCase()+side.slice(1);if(params.has(q)){state.insets[side]=Math.max(0,Number(params.get(q))||0);$("inset-"+side).value=state.insets[side];}}setInsets();
}
document.addEventListener("DOMContentLoaded",setup);

