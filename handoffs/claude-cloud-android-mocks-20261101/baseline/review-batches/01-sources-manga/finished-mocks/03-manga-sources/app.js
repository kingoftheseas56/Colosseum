"use strict";
// Standalone, local-only renderer of the approved QML source picker. No service/provider calls.
const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const capture = params.get("capture") === "1";
const controls = {
  preset: $("preset"), width: $("vpWidth"), height: $("vpHeight"),
  top: $("topInset"), bottom: $("bottomInset"), left: $("leftInset"),
  right: $("rightInset"), scale: $("fontScale"), scene: $("scene"), context: $("context")
};
const fixtures = [
  {id:"fixture-1",uploader:"Fixture Archive",kind:"nyaa",standalone:true,tier:1,digital:true,coverageLo:1,coverageHi:1,
   releaseTitle:"One Piece — Vol. 01: Romance Dawn — Complete Digital Edition (English)",
   longTitle:"One Piece — Volume 01 Romance Dawn — Complete Revised Digital Edition — English translated pages — archival high-resolution scan with complete extras and bonus material",
   sizeBytes:119537664,seeders:24,enabled:true},
  {id:"fixture-2",uploader:"Another Archive",kind:"nyaa",standalone:false,tier:3,digital:true,coverageLo:1,coverageHi:3,
   releaseTitle:"One Piece — Collected Digital Volumes 01–03 (English)",
   longTitle:"One Piece — Volumes 01–03 — Complete archival East Blue collection (English translated, digital pages)",
   sizeBytes:1825361101,seeders:12,enabled:true},
  {id:"fixture-3",uploader:"Fixture Archive",kind:"nyaa",standalone:false,tier:99,digital:false,coverageLo:1,coverageHi:12,
   releaseTitle:"One Piece — East Blue, Volumes 01–12 — Collected Edition",
   longTitle:"One Piece — East Blue, complete collection of Volumes 01–12 — scanned edition — extended release filename illustrating edge-case text overflow",
   sizeBytes:7516192768,seeders:5,enabled:true},
  {id:"fixture-4",uploader:"Another Archive",kind:"nyaa",standalone:false,tier:99,digital:true,coverageLo:1,coverageHi:105,
   releaseTitle:"One Piece — Vols. 01–105 — Complete Digital Archive",
   longTitle:"One Piece — Vols. 01–105 — Complete digital archive in English — long coverage range that should remain visible before deliberate selection",
   sizeBytes:62277025792,seeders:2,enabled:true}
];
const longSynopsis = "Monkey D. Luffy sets sail across the East Blue after inheriting a straw hat from Shanks. On his voyage he meets new allies, faces hostile pirates and begins the search for the legendary One Piece.";
const S = {
  width: Number(params.get("w")) || (capture ? window.innerWidth : 412),
  height: Number(params.get("h")) || (capture ? window.innerHeight : 900),
  top: Number(params.get("top")) || 0,
  bottom: Number(params.get("bottom")) || 0,
  left: Number(params.get("left")) || 0,
  right: Number(params.get("right")) || 0,
  scale: Number(params.get("scale")) || 1,
  scene: params.get("scene") || "sources",
  context: params.get("context") || "single",
  picked: params.get("context")==="batch" ? "fixture-2" : "fixture-1",
  sourcePickedByUser: false
};
if (capture) document.body.classList.add("capture");
let phaseTimer=0, toastTimer=0, timedScene="";
$("app").append($("toast"));
function capInt(value,min,max){const n=Number(value);return Math.max(min,Math.min(max,Number.isFinite(n)?n:min));}
function setReviewMessage(message){$("reviewMessage").textContent=message;}
function renderDimensions(){
  const app=$("app");
  S.width=capInt(S.width,320,2560);S.height=capInt(S.height,240,2560);
  S.scale=Number(S.scale)===1.3?1.3:1;
  app.style.width=S.width+"px";app.style.height=S.height+"px";
  app.style.setProperty("--font-scale",S.scale);
  for (const k of ["top","bottom","left","right"]){
    S[k]=capInt(S[k],0,k==="top"||k==="bottom"?140:80);
    app.style.setProperty("--"+k+"-inset",S[k]+"px");
  }
  const phone=Math.min(S.width,S.height)<600;
  const landscape=S.width>S.height;
  app.classList.toggle("phone",phone);
  app.classList.toggle("portrait",phone&&!landscape);
  app.classList.toggle("landscape",phone&&landscape);
  let hero = phone ? (landscape?151:258) : 300;
  hero += (S.scale-1)*(phone?(landscape?60:110):100);
  if (S.scene==="long") hero += phone?(landscape?16:32):0;
  const usableHeight=S.height-S.top-S.bottom;
  hero=Math.min(hero,Math.max(100,usableHeight-95));
  app.style.setProperty("--hero-h",Math.round(hero)+"px");
  app.classList.toggle("scroll-fit",phone&&landscape&&usableHeight-hero-17<180);
  if(!capture){
    controls.width.value=S.width;controls.height.value=S.height;
    controls.top.value=S.top;controls.bottom.value=S.bottom;
    controls.left.value=S.left;controls.right.value=S.right;
    controls.scale.value=String(S.scale);controls.scene.value=S.scene;
    controls.context.value=S.context;
    const key=S.width+"x"+S.height;
    controls.preset.value=[...controls.preset.options].some(o=>o.value===key)?key:"custom";
  }
}
function make(tag,className,text){
  const node=document.createElement(tag);
  if(className)node.className=className;
  if(text!==undefined)node.textContent=text;
  return node;
}
function fmtSize(bytes){
  const mb=(Number(bytes)||0)/1048576;
  if(mb<=0)return "";
  return mb>=1024?(mb/1024).toFixed(1)+" GB":(mb>=100?mb.toFixed(0):mb.toFixed(1))+" MB";
}
function chipsFor(row){
  const chips=[];
  if(row.digital)chips.push("DIGITAL");
  chips.push(row.standalone?"SINGLE VOLUME":"VOLS "+(row.coverageLo||"?")+"–"+(row.coverageHi||"?"));
  if(Number(row.tier)<99)chips.push("TRUSTED");
  return chips;
}
function availableRows(){
  let rows=fixtures.filter(row=>row.kind==="nyaa");
  if(S.scene==="metadata")rows=rows.map((r,i)=>({...r,seeders:[24,undefined,-1,null][i]}));
  if(S.scene==="metadata-zero")rows=rows.map((r,i)=>({...r,seeders:i===3?0:r.seeders}));
  if(S.context!=="batch")return rows;
  return rows.filter(row=>[1,2,3].every(n=>n>=row.coverageLo&&n<=row.coverageHi))
    .sort((a,b)=>(a.coverageHi-a.coverageLo)-(b.coverageHi-b.coverageLo)||b.seeders-a.seeders);
}
function statusText(){
  if(S.scene==="resolving")return "Resolving torrent…";
  if(S.scene==="downloading")return "Downloading · 47%";
  if(S.scene==="done")return "Done — added to your shelf";
  if(S.scene==="failed")return "Failed — pick another source";
  return "";
}
function isLive(){return ["resolving","downloading","done","failed"].includes(S.scene);}
function renderHero(){
  const isBatch=S.context==="batch",isSeries=S.context==="series";
  $("heroTitle").textContent=isBatch?"3 volumes":isSeries?"Sources":"Vol. 1";
  const identity=$("heroIdentity");identity.replaceChildren();
  const parts=isSeries?["One Piece"]:isBatch?["One Piece"]:["One Piece","Vol. 1"];
  for(const [i,part] of parts.entries()){
    if(i)identity.append(make("span","","·"));
    identity.append(document.createTextNode(part));
  }
  const synopsis=$("heroSynopsis");
  synopsis.textContent=S.scene==="long"?longSynopsis:"No synopsis for this volume yet.";
  synopsis.classList.toggle("is-empty",S.scene!=="long");
}
function renderRow(row,index){
  const live=isLive(),selected=live&&S.picked===row.id;
  const wrapper=make("div","source-row"+(live&&!selected?" dimmed inactive":""));
  wrapper.setAttribute("role","button");
  wrapper.setAttribute("tabindex",live?"-1":"0");
  wrapper.setAttribute("aria-label","Choose "+row.uploader+" source: "+row.releaseTitle);
  wrapper.setAttribute("aria-current",selected?"true":"false");
  wrapper.setAttribute("aria-disabled",live?"true":"false");
  wrapper.dataset.rowId=row.id;wrapper.dataset.rowIndex=String(index);
  wrapper.append(make("div","source-tile",String(row.uploader||"?").charAt(0).toUpperCase()));
  const body=make("div","source-body");
  const name=make("div","uploader-line");
  name.append(make("span","uploader",row.uploader||"Nyaa"));
  const tight=row.standalone===true||Number(row.tier)===1;
  name.append(make("span","confidence "+(tight?"strong":"possible"),tight?"STRONG":"POSSIBLE"));
  body.append(name);
  const release=make("div","release-title",S.scene==="long"?row.longTitle:row.releaseTitle);release.title=release.textContent;body.append(release);
  wrapper.setAttribute("aria-label","Choose "+row.uploader+" source: "+release.textContent);
  const chipContainer=make("div","evidence");
  for(const value of chipsFor(row))chipContainer.append(make("span","evidence-chip",value));
  body.append(chipContainer);
  if(selected){
    body.append(make("div","source-meta live",statusText()));
  }else{
    const meta=make("div","source-meta");
    const size=fmtSize(row.sizeBytes),raw=row.seeders,n=Number(raw),known=(typeof raw==="number"||(typeof raw==="string"&&raw.trim().length>0))&&Number.isFinite(n)&&n>=0;
    if(size)meta.append(make("span","",size));
    if(known){if(size)meta.append(make("span","meta-bullet","·"));meta.append(make("span","","👤 "+n));}
    body.append(meta);
  }
  wrapper.append(body);
  const action=make("div","source-action");
  action.setAttribute("aria-hidden","true");
  if(!selected){
    action.classList.add("down");action.textContent="↓";
  }else{
    action.classList.add("disc");
    if(S.scene==="resolving")action.append(make("span","disc-spinner"));
    else if(S.scene==="downloading"){action.classList.add("percent");action.textContent="47%";}
    else if(S.scene==="done"){action.classList.add("done");action.textContent="✓";}
    else {action.classList.add("failed");action.textContent="!";}
  }
  wrapper.append(action);
  const choose=()=>{
    if(isLive()||!row.enabled)return;
    S.picked=row.id;S.scene="resolving";S.sourcePickedByUser=true;
    render();controls.scene.value="resolving";
    setReviewMessage("Local demo: source selected; resolving state shown. No download or provider call took place.");
  };
  wrapper.addEventListener("click",choose);
  wrapper.addEventListener("keydown",(event)=>{
    if(event.key==="Enter"||event.key===" "){event.preventDefault();choose();}
    if(["ArrowDown","ArrowUp","Home","End"].includes(event.key)&&!isLive()){event.preventDefault();const a=[...$("rows").children],i=a.indexOf(wrapper);const n=event.key==="Home"?0:event.key==="End"?a.length-1:Math.max(0,Math.min(a.length-1,i+(event.key==="ArrowDown"?1:-1)));a[n].focus();a[n].scrollIntoView({block:"nearest"});}
  });
  return wrapper;
}
function renderRows(){
  const hasRows=!["disabled","loading","empty","search-error","closed"].includes(S.scene);
  const rows=hasRows?availableRows():[];
  const rowHost=$("rows");rowHost.replaceChildren();
  $("tableHead").hidden=!hasRows;
  $("sourceCount").textContent=rows.length+(rows.length===1?" source":" sources");
  if(hasRows)rows.forEach((row,i)=>rowHost.append(renderRow(row,i)));
  $("emptyState").hidden=hasRows;
  $("emptyText").textContent=S.scene==="disabled"
    ?"No sources enabled — enable Nyaa in Extensions to search."
    :S.scene==="loading"?"Searching Nyaa releases…"
    :S.scene==="search-error"?"Some sources didn’t answer — provider unavailable.":"No releases matched this volume yet.";
  $("emptyText").classList.toggle("search-error",S.scene==="search-error");
  $("extensionsRoute").hidden=S.scene!=="disabled";
  const toast=$("toast");
  toast.hidden=!isLive();
  toast.classList.toggle("error",S.scene==="failed");
  if(S.scene==="failed")toast.textContent="Couldn’t get it from that source — source unavailable. Pick another one.";
  else toast.textContent=S.context==="batch"?"3 volumes are downloading — follow the shelf":S.context==="series"?"Downloading — follow it on the volume shelf":"Vol. 1 is downloading — follow it on the volume shelf";
  $("app").classList.toggle("picker-closed",S.scene==="closed");
}
function render(){const oldScroll=$("resultViewport").scrollTop;const focusId=document.activeElement?.dataset.rowId;renderDimensions();renderHero();renderRows();$("resultViewport").scrollTop=oldScroll;if(focusId)document.querySelector('[data-row-id="'+focusId+'"]')?.focus({preventScroll:true});controls.scene.value=S.scene;controls.context.value=S.context;document.documentElement.dataset.mockReady="true";syncTimers();}
function syncTimers(){if(timedScene===S.scene)return;clearTimeout(phaseTimer);clearTimeout(toastTimer);timedScene=S.scene;if(S.scene==="failed")phaseTimer=setTimeout(()=>{S.scene="sources";render();setReviewMessage("Failure beat ended; another source may be chosen.");},1600);if(S.scene==="done")phaseTimer=setTimeout(()=>{S.scene="closed";render();setReviewMessage("Completion beat ended; picker closed locally.");},1400);if(isLive())toastTimer=setTimeout(()=>{$("toast").hidden=true;},3800);}
$("back").addEventListener("click",()=>{
  S.scene="closed";render();
  setReviewMessage("Back: picker closed locally. Underlying MangaSeries is intentionally not fabricated.");
});
$("extensionsRoute").addEventListener("click",()=>{
  setReviewMessage("Mock openExtensionsRequested() signal only; no real Extensions navigation or account operation.");
});
$("reopen").addEventListener("click",()=>{
  S.scene="sources";S.sourcePickedByUser=false;render();setReviewMessage("Picker reopened locally.");
});
controls.preset.addEventListener("change",()=>{
  if(controls.preset.value==="custom")return;
  const [w,h]=controls.preset.value.split("x").map(Number);
  S.width=w;S.height=h;render();
});
for(const [name,key] of [["width","width"],["height","height"],["top","top"],["bottom","bottom"],["left","left"],["right","right"]]){
  controls[name].addEventListener("change",()=>{S[key]=Number(controls[name].value);render();});
}
controls.scale.addEventListener("change",()=>{S.scale=Number(controls.scale.value);render();});
controls.scene.addEventListener("change",()=>{S.scene=controls.scene.value;S.picked=S.context==="batch"?"fixture-2":"fixture-1";S.sourcePickedByUser=false;render();});
controls.context.addEventListener("change",()=>{S.context=controls.context.value;S.picked="fixture-2";S.scene="sources";render();});
render();
