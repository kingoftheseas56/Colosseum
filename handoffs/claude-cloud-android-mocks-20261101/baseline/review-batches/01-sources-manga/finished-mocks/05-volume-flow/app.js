"use strict";
// Standalone review-only reconstruction of source QML. No native/network integrations.
(() => {
  const $ = id => document.getElementById(id);
  const frame = $("appFrame"), view = $("flowViewport"), scroll = $("seriesScroll");
  const variant = document.body.dataset.variant;
  const qs = new URLSearchParams(location.search);
  const presets = {
    portrait:[412,900], "portrait-small":[354,792],
    landscape:[900,412], "landscape-small":[792,354],
    desktop:[1280,700], "tablet-portrait":[800,1280], "tablet-landscape":[1280,800]
  };
  const artBase = "../../../../reference/desktop/assets/universes/one-piece/";
  const imageSources = [
    "east-blue/noncanon/01-ganzack.png","east-blue/noncanon/02-one-piece-movie.png",
    "east-blue/noncanon/03-oceans-navel.png","east-blue/noncanon/04-jangos-dance-carnival.png",
    "east-blue/noncanon/05-clockwork-island.png","east-blue/canon/romance-dawn.png",
    "east-blue/canon/baratie.png","east-blue/canon/arlong-park.png",
    "paradise/banners/arabasta.png","paradise/banners/skypiea.png",
    "paradise/banners/water-7.png","paradise/banners/enies-lobby.png",
    "paradise/banners/marineford.png","new-world/banners/dressrosa.png",
    "new-world/banners/whole-cake-island.png","new-world/banners/wano-country.png",
    "final-sea/banners/egghead.png"
  ];
  // Deliberately illustrative model: local project art is NOT the actual manga-volume jacket.
  const volumes = Array.from({length:48}, (_, i) => ({
    number:i+1,
    art:artBase+imageSources[i%imageSources.length],
    name:i===0?"Romance Dawn — Dawn of the Adventure":i===5?"The Oath":i===10?"The Grand Line":i===22?"The Pirate King and the Master Swordsman":"",
    state:i===0||i===1||i===5?"ready":i===3?"downloading":i===4?"failed":"none"
  }));
  const synopsisBase=$("synopsisText").textContent;
  const synopsisStress=synopsisBase+" Their story continues across countless islands, taking them through the perilous New World, intricate alliances, and battles that shape a generation. Each voyage reveals more of the Grand Line and those who hope to find its treasure.";
  const state = {focused:2, selecting:false, selected:new Set(), expanded:false,
    saved:false, fontScale:1, width:412, height:900, top:0,bottom:0,pending:false,noCover:false};
  let suppressClickUntil=0;
  const format = n => String(n);
  const clamp = (v,a,b) => Math.min(b,Math.max(a,v));
  function log(message){ $("eventOutput").textContent = message; }
  function syncFlags() {
    const phone = Math.min(state.width,state.height)<600;
    const landscape = state.width>state.height;
    frame.className = "app-frame";
    if(phone) frame.classList.add(landscape?"phone-landscape":"phone-portrait");
    else if($("viewportPreset").value.startsWith("tablet")){
      frame.classList.add("tablet");
      if(state.height>state.width) frame.classList.add("tablet-portrait");
    } else frame.classList.add("desktop");
    if(phone&&landscape&&state.height<=400) frame.classList.add("short-landscape");
    if(phone&&!landscape&&state.width<=360) frame.classList.add("narrow-phone");
    frame.dataset.variant = variant;
    frame.style.setProperty("--frame-w",state.width+"px");
    frame.style.setProperty("--frame-h",state.height+"px");
    frame.style.setProperty("--inset-top",state.top+"px");
    frame.style.setProperty("--inset-bottom",state.bottom+"px");
    frame.style.setProperty("--font-scale",String(state.fontScale));
    $("customWidth").value=String(state.width);$("customHeight").value=String(state.height);
    $("topOutput").textContent=String(state.top);$("bottomOutput").textContent=String(state.bottom);
  }
  function applyPreset(name) {
    const pair = presets[name] || presets.portrait;
    state.width=pair[0];state.height=pair[1];
    $("viewportPreset").value = name;
    syncFlags(); resizeLater();
  }
  function coverScale(offset){
    const d=Math.abs(offset);
    return d===0?1.10:d===1?1.00:d===2?.95:d===3?.90:.86;
  }
  function renderCards() {
    const height=view.clientHeight, width=view.clientWidth;
    if(height<1||width<1)return;
    const isLandscape=frame.classList.contains("phone-landscape");
    const caption=Math.ceil(54*state.fontScale); // Scale the original caption budget with user text.
    const budget=Math.floor(Math.min(276,(height-16)/1.1-caption));
    // Unlike the QML 190px floor, a true viewport-fit must allow the book to shrink.
    const bookH=clamp(budget,variant==="alternative"?190:16,276), bookW=Math.round(bookH*2/3);
    const baseH=bookH+caption, slot=bookW+36;
    const visible=frame.classList.contains("phone-portrait")?5:9;
    const limit=Math.ceil(visible/2)+1;
    const start=Math.max(0,state.focused-limit), end=Math.min(volumes.length-1,state.focused+limit);
    const keepFocus=view.contains(document.activeElement);frame.dataset.bookHeight=String(bookH);frame.dataset.sourceCoverMinimum=String(bookH>=190);const frag=document.createDocumentFragment();
    for(let i=start;i<=end;i++){
      const vol=volumes[i], delta=i-state.focused, scale=coverScale(delta);
      const card=document.createElement("button");
      card.type="button";card.tabIndex=-1;card.className="volume-card"+(delta===0?" focused":"");
      card.setAttribute("aria-label","Volume "+vol.number);
      card.setAttribute("aria-pressed",String(state.selected.has(vol.number)));
      card.title="Volume "+vol.number;
      card.style.cssText="--card-w:"+(bookW+18)+"px;--card-h:"+baseH+"px;--book-w:"+bookW+"px;--book-h:"+bookH+"px;"+
        "left:"+(width/2+delta*slot)+"px;top:"+((height-baseH)/2+(scale-1)*baseH/2)+"px;"+
        "transform:translateX(-50%) scale("+scale+");z-index:"+(delta===0?6:Math.max(0,4-Math.abs(delta)))+";";
      const cover=document.createElement("div");
      cover.className="cover-frame"+(vol.state==="ready"?" owned":"")+(vol.state==="downloading"?" loading":"")+(delta===0?" focus-cover":"");
      const img=document.createElement("img");img.src=vol.art;img.alt="Local One Piece art used for illustrative volume "+vol.number;img.loading="eager";
      if(state.noCover&&delta===0){const fallback=document.createElement("div");fallback.className="cover-fallback";fallback.innerHTML="<b>"+vol.number+"</b><span>NO COVER</span>";cover.append(fallback);}else cover.append(img);
      const captionEl=document.createElement("div");captionEl.className="volume-caption";
      const number=document.createElement("span");number.className="volume-num";number.textContent="VOL "+vol.number;
      const name=document.createElement("span");name.className="volume-name";name.textContent=vol.name;name.hidden=!vol.name;
      const status=document.createElement("span");status.className="volume-state"+(vol.state==="downloading"?" loading-state":"");
      status.textContent=vol.state==="downloading"?"73% | downloading":vol.state==="failed"?"failed":"";
      captionEl.append(number,name,status);card.append(cover,captionEl);
      card.addEventListener("click",()=>onVolumeTap(i));
      frag.append(card);
    }
    $("flowTrack").replaceChildren(frag);
    if(keepFocus)view.focus({preventScroll:true});updateAction();
    requestAnimationFrame(checkGeometry);
  }
  function updateAction() {
    const vol=volumes[state.focused], unavailable=vol.state==="ready"||vol.state==="downloading";
    $("currentVolume").textContent="Vol. "+vol.number;
    $("currentName").textContent=vol.name;
    $("readAction").textContent=state.pending&&vol.state==="downloading"?"Reading when ready":vol.state==="failed"?"Retry Read":vol.state==="ready"&&vol.number===2?"Continue Vol. 2":"Read Vol. "+vol.number;
    $("downloadAction").disabled=unavailable;
    $("readAction").classList.toggle("pending",state.pending&&vol.state==="downloading");
    $("progressLabel").textContent=state.pending&&vol.state==="downloading"?"73%":"";
    $("volumeCount").textContent=String(volumes.length);
    $("selectToggle").textContent=state.selecting?"Done":"Select";
  }
  function syncSelection() {
    $("selectionFloat").hidden=!(state.selecting && state.selected.size>0);
    $("selectedCount").textContent=state.selected.size+" selected";
    $("selectToggle").textContent=state.selecting?"Done":"Select";
    frame.querySelector(".volume-library").classList.toggle("selecting",state.selecting);frame.querySelector(".action-buttons").inert=state.selecting&&state.selected.size>0;reserveReadableFlow();
    for(const el of $("flowTrack").children){
      const match=/Volume (\d+)/.exec(el.getAttribute("aria-label"));
      if(match)el.setAttribute("aria-pressed",String(state.selected.has(+match[1])));
    }
  }
  function onVolumeTap(index) {
    if(Date.now()<suppressClickUntil)return;
    const vol=volumes[index];
    if(state.selecting){state.selected.has(vol.number)?state.selected.delete(vol.number):state.selected.add(vol.number);syncSelection();log("Selected volume set: "+[...state.selected].sort((a,b)=>a-b).join(", ")+". No batch started.");return;}
    if(index!==state.focused){state.focused=index;state.pending=false;renderCards();log("Volume "+vol.number+" centred. Tap focused cover to request Read.");return;}
    log("readVolumeRequested(volume "+vol.number+") — preview only; no download or reader launched.");
  }
  function focusOffset(n){state.focused=clamp(state.focused+n,0,volumes.length-1);state.pending=false;renderCards();}
  function setState(name){
    state.selected.clear();state.selecting=false;state.expanded=false;state.focused=2;state.pending=false;state.noCover=false;
    if(name==="select"){state.selecting=true;state.selected.add(2);state.selected.add(3);}
    if(name==="synopsis") state.expanded=true;
    if(name==="downloading"){state.focused=3;state.pending=true;}
    if(name==="failed") state.focused=4;
    if(name==="end") state.focused=47;
    if(name==="no-cover"){state.focused=47;state.noCover=true;}
    $("synopsisText").textContent=state.expanded?synopsisStress:synopsisBase;
    $("synopsisMore").textContent=state.expanded?"less":"more";$("synopsisMore").setAttribute("aria-expanded",String(state.expanded));
    $("synopsisText").parentElement.classList.toggle("expanded",state.expanded);
    frame.querySelector(".hero").classList.toggle("synopsis-expanded",state.expanded);
    syncSelection();resizeLater();
  }
  function checkGeometry(){
    const focused=frame.querySelector(".volume-card.focused");
    if(!focused)return;
    const fr=focused.getBoundingClientRect(), vr=view.getBoundingClientRect();
    const bar=frame.querySelector(".action-bar").getBoundingClientRect();
    const fit=fr.top>=vr.top-1&&fr.bottom<=vr.bottom+1;
    const hero=frame.querySelector(".hero").getBoundingClientRect();
    const overlap=hero.bottom > vr.top+5;
    $("geometryStatus").textContent="Inside "+state.width+"×"+state.height+" · hero "+Math.round(hero.height)+"px · flow "+Math.round(vr.height)+"px · focused card "+Math.round(fr.height)+"px · "+(fit?"entire focused card visible":"focused card clipped")+" · "+(overlap?"hero/gallery overlap":"no hero/gallery overlap");
    frame.dataset.focusFits=String(fit);frame.dataset.geometryOverlap=String(overlap);
    frame.dataset.flowHeight=String(Math.round(vr.height));
  }
  function reserveReadableFlow(){const caption=Math.ceil(54*state.fontScale),minFlow=Math.ceil((190+caption)*1.10+16),lane=frame.querySelector(".lane-head").getBoundingClientRect().height,bar=frame.querySelector(".action-bar").getBoundingClientRect().height;frame.style.setProperty("--readable-flow",minFlow+"px");frame.style.setProperty("--readable-library",Math.ceil(lane+bar+minFlow)+"px");frame.style.setProperty("--attempt-library-min",Math.ceil(lane+bar+(16+caption)*1.10+16)+"px");const chrome=frame.querySelector(".chrome"),screen=frame.querySelector(".app-screen"),releaseChrome=variant==="alternative"&&Math.min(state.width,state.height)<600&&(state.height-state.top-state.bottom-chrome.offsetHeight<minFlow);frame.classList.toggle("scrolling-chrome",releaseChrome);if(releaseChrome&&chrome.parentElement!==scroll)scroll.prepend(chrome);else if(!releaseChrome&&chrome.parentElement===scroll)screen.prepend(chrome);}
  function resizeLater(){
    requestAnimationFrame(()=>requestAnimationFrame(()=>{
      reserveReadableFlow();renderCards();if(qs.get("capture")==="1" && qs.get("scene")==="gallery" && variant==="alternative"){
        scroll.scrollTop+=view.getBoundingClientRect().top-scroll.getBoundingClientRect().top;
        requestAnimationFrame(checkGeometry);
      }
    }));
  }
  $("viewportPreset").addEventListener("change",e=>{applyPreset(e.target.value);scroll.scrollTop=0;});
  for(const id of ["customWidth","customHeight"]){$(id).addEventListener("change",()=>{state.width=clamp(+$("customWidth").value,320,1920);state.height=clamp(+$("customHeight").value,240,1600);syncFlags();resizeLater();});}
  $("topInset").addEventListener("input",e=>{state.top=+e.target.value;syncFlags();resizeLater();});
  $("bottomInset").addEventListener("input",e=>{state.bottom=+e.target.value;syncFlags();resizeLater();});
  $("fontScale").addEventListener("change",e=>{state.fontScale=+e.target.value;syncFlags();resizeLater();});
  $("reviewState").addEventListener("change",e=>setState(e.target.value));
  $("selectToggle").addEventListener("click",()=>{state.selecting=!state.selecting;if(!state.selecting)state.selected.clear();syncSelection();log(state.selecting?"Select mode enabled. Tap volumes to select.":"Selection cleared.");});
  $("downloadNext").addEventListener("click",()=>log("batchRequested("+volumes.filter(v=>!["ready","downloading"].includes(v.state)).slice(0,10).map(v=>v.number).join(", ")+", Download next 10) — local fixture only."));
  $("downloadSelected").addEventListener("click",()=>log("batchRequested("+[...state.selected].sort((a,b)=>a-b).join(", ")+", Download selected) — no backend."));
  $("downloadAction").addEventListener("click",()=>log("sourcesRequested(volume "+volumes[state.focused].number+", acquire) — approved native picker not opened in static demo."));
  $("readAction").addEventListener("click",()=>log("readVolumeRequested(volume "+volumes[state.focused].number+") — no backend."));
  $("backAction").addEventListener("click",()=>log("backRequested() — local preview."));
  $("tankobanMode").addEventListener("click",()=>log("Tankoban Mode remains selected."));
  $("chapterMode").addEventListener("click",()=>log("chapterModeRequested() — separate approved series mode, not navigated in this mock."));
  $("bookmark").addEventListener("click",()=>{state.saved=!state.saved;$("bookmarkIcon").src="../../../../reference/desktop/assets/icons/lucide/bookmark-"+(state.saved?"check":"plus")+".svg";$("bookmark").classList.toggle("saved",state.saved);$("bookmark").title=state.saved?"Remove from Library":"Add to Library";$("bookmark").setAttribute("aria-label",$("bookmark").title);log(state.saved?"Local library state: saved.":"Local library state: not saved.");});
  $("collections").addEventListener("click",()=>log("openNamedCollectionsPicker() requested — full picker not invented here."));
  document.querySelectorAll("[data-event]").forEach(el=>el.addEventListener("click",()=>log(el.dataset.event+" requested — preview only.")));
  view.addEventListener("keydown",e=>{
    let amount=0;
    if(e.key==="ArrowRight")amount=1;if(e.key==="ArrowLeft")amount=-1;
    if(e.key==="PageDown")amount=10;if(e.key==="PageUp")amount=-10;
    if(e.key==="Home")amount=-48;if(e.key==="End")amount=48;
    if(amount){e.preventDefault();focusOffset(amount);}
    if(["Enter"," "].includes(e.key)){e.preventDefault();onVolumeTap(state.focused);}
  });
  view.addEventListener("wheel",e=>{if(e.deltaY!==0||e.deltaX!==0){e.preventDefault();focusOffset((e.deltaY||e.deltaX)>0?(e.shiftKey?10:1):(e.shiftKey?-10:-1));}}, {passive:false});
  let touchStartX=null,touchStartY=null;
  view.addEventListener("touchstart",e=>{touchStartX=e.touches[0]?.clientX??null;touchStartY=e.touches[0]?.clientY??null;},{passive:true});
  view.addEventListener("touchend",e=>{if(touchStartX===null)return;const change=e.changedTouches[0].clientX-touchStartX,dy=e.changedTouches[0].clientY-touchStartY;touchStartX=null;if(Math.abs(change)>30&&Math.abs(change)>Math.abs(dy)*1.2){suppressClickUntil=Date.now()+500;focusOffset(change<0?1:-1);}},{passive:true});
  new ResizeObserver(()=>resizeLater()).observe(view);
  $("synopsisMore").addEventListener("click",()=>{state.expanded=!state.expanded;$("synopsisText").textContent=state.expanded?synopsisStress:synopsisBase;$("synopsisText").parentElement.classList.toggle("expanded",state.expanded);frame.querySelector(".hero").classList.toggle("synopsis-expanded",state.expanded);$("synopsisMore").textContent=state.expanded?"less":"more";$("synopsisMore").setAttribute("aria-expanded",String(state.expanded));resizeLater();});
  const requestedPreset=qs.get("preset")||"portrait";
  if(qs.get("capture")==="1")document.body.classList.add("capture-mode");
  state.top=clamp(+(qs.get("top")||0),0,48);state.bottom=clamp(+(qs.get("bottom")||0),0,48);
  state.fontScale=+(qs.get("font")||1)===1.3?1.3:1;
  $("topInset").value=state.top;$("bottomInset").value=state.bottom;$("fontScale").value=String(state.fontScale);
  applyPreset(requestedPreset);
  const startingState=qs.get("state")||"default";$("reviewState").value=startingState;setState(startingState);
  document.fonts.ready.then(resizeLater);
})();