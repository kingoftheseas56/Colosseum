"use strict";
(() => {
 const A="../../reference/desktop/assets/",p=new URLSearchParams(location.search);
 const page=document.body.dataset.page==="chapters"?"chapters":"series",capture=p.has("capture");
 const art=[
 ["universes/one-piece/east-blue/canon/romance-dawn.png","Romance Dawn"],
 ["universes/one-piece/east-blue/canon/orange-town.png","Orange Town"],
 ["universes/one-piece/east-blue/canon/syrup-village.png","Syrup Village"],
 ["universes/one-piece/east-blue/canon/baratie.png","Baratie"],
 ["universes/one-piece/east-blue/canon/arlong-park.png","Arlong Park"],
 ["universes/one-piece/east-blue/canon/loguetown.png","Loguetown"],
 ["universes/one-piece/paradise/badges/arabasta.png","Arabasta"],
 ["universes/one-piece/paradise/badges/skypiea.png","Skypiea"],
 ["universes/one-piece/paradise/badges/water-7.png","Water 7"],
 ["universes/one-piece/paradise/badges/enies-lobby.png","Enies Lobby"]
 ];
 const volumeNames=["Romance Dawn","Buggy the Clown","Don't Get Fooled Again","The Black Cat Pirates","For Whom the Bell Tolls","The Oath","The Crap-Geezer","I Won't Die","Tears","OK, Let's Stand Up!"];
 const chapterNames=["Romance Dawn","They Call Him Straw Hat Luffy","Enter Zoro: Pirate Hunter","The Great Captain Morgan","The Pirate King and the Master Swordsman","The First","Friends","Nami","The Devil's Daughter","The Black Cat Pirates","The World's Greatest Power","Dog","Treasure","Reckless","Gong","Versus Buggy's Crew","Difference in Level","Pirate Buggy","Devil Fruit","Thief"];
 const synopsis="Monkey D. Luffy sets sail in pursuit of the legendary treasure known as One Piece. With the Straw Hat Pirates, he crosses treacherous seas, meets unlikely allies, and challenges powerful enemies on the way to becoming King of the Pirates. Along the Grand Line and into the New World, every island brings new friendships and an adventure of its own.";
 const s={expanded:false,library:false,selecting:false,selected:new Set(),focusVol:3,lang:"Default · English",langCode:"",pageIndex:0,pageCount:113,popup:null};
 const svgBack='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 L8 12 L14.5 18.5"/></svg>';
 const collectionIcon='<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="5" width="13" height="14" rx="2"/><path d="M7 2h12a2 2 0 0 1 2 2v12M7 9h6M10 6v6"/></svg>';
 const icon=n=>A+"icons/"+n;
 function viewLink(which){return (which==="chapters"?"manga-chapters.html":"manga-series.html")+(p.has("preset")&&!capture?"?preset="+encodeURIComponent(p.get("preset")):"");}
 function masthead(){return `
 <header class="chrome">
 <button class="back-action" data-do="back" aria-label="Back">${svgBack}<span>Back</span></button>
 <div class="chrome-brand">COLOSSEUM · TANKOBAN</div>
 <div class="chrome-window">
 <button title="Minimize" data-do="window" aria-label="Minimize"><img src="${icon("minimize.svg")}" alt=""></button>
 <button title="Fullscreen" data-do="window" aria-label="Fullscreen"><img src="${icon("fullscreen.svg")}" alt=""></button>
 <button title="Close" data-do="window" aria-label="Close"><img src="${icon("power.svg")}" alt=""></button>
 </div></header>
 <section class="hero"><div class="hero-content">
 <div class="poster-frame"><img src="${A+art[0][0]}" alt="Local One Piece Romance Dawn project art; illustrative, not an exact manga-volume cover"></div>
 <div class="hero-text">
 <div class="identity"><div class="eyebrow">Manga &nbsp;·&nbsp; Tankoban</div>
 <div class="title-line"><h1 class="series-title">One Piece</h1>
 <button class="bookmark" id="bookmark" data-do="bookmark" title="Add to Library" aria-label="Add to Library"><img id="bookmarkImg" src="${icon("lucide/bookmark-plus.svg")}" alt=""></button></div>
 <button class="collection-btn" data-do="collections">${collectionIcon}Add to Collections</button></div>
 <div class="facts"><span class="author">Eiichiro Oda</span><span>Publishing</span><span>1997</span><span>★ 9.22</span><span>${page==="series"?"107 volumes · 3 on this device":"107 volumes"}</span></div>
 <div class="genre-row"><span class="genre">Action</span><span class="genre">Adventure</span><span class="genre">Fantasy</span></div>
 <div class="synopsis-wrap" id="synopsisWrap"><p class="synopsis-text">${synopsis}</p><button class="synopsis-more" id="synopsisMore" data-do="synopsis" aria-expanded="false">More</button></div>
 </div>
 <div class="header-controls">
 <div class="mode-switch" aria-label="Reading mode"><a href="${viewLink("series")}" class="${page==="series"?"active":""}" aria-current="${page==="series"?"page":"false"}">Tankoban Mode</a><a href="${viewLink("chapters")}" class="${page==="chapters"?"active":""}" aria-current="${page==="chapters"?"page":"false"}">Chapter Mode</a></div>
 <div class="language-box"><label>Language</label><button id="languageSelector" class="combo-button" aria-haspopup="listbox" aria-expanded="false" ${page==="series"?'disabled title="Available in Chapter Mode"':""}><span id="languageText">${page==="series"?"English":s.lang}</span><span class="chevron">${page==="series"?"":"▾"}</span></button></div>
 ${page==="chapters"?`<button class="settings-btn" data-do="settings"><img src="${icon("settings.svg")}" alt="">Language settings</button>`:""}
 </div></div></section>`;}
 function volumeMarkup(){return `
 <section class="device-progress" aria-label="On this device, 3 of 107 volumes"><div class="progress-title"><span>ON THIS DEVICE</span><strong>3 OF 107</strong></div><div class="progress-track"><div class="progress-fill"></div></div></section>
 <section class="volume-section" id="volumes"><div class="volume-head"><div class="volume-count"><span class="whisper">VOLUMES</span><strong>107</strong></div>
 <div class="select-actions"><button id="next10" class="text-action" data-do="next10" hidden>Download next 10</button><button class="text-action" id="selectToggle" data-do="select">Select</button></div></div>
 <div class="volume-stage"><div class="volume-flow" id="volumeFlow" role="list" aria-label="One Piece volume cover flow"></div><div class="flow-fade left"></div><div class="flow-fade right"></div></div>
 <div class="volume-action"><div class="volume-name"><strong id="focusVolumeLabel">Vol. 3</strong><span id="focusVolumeName">Don't Get Fooled Again</span></div>
 <div class="action-buttons"><button class="btn-outline" data-do="downloadVol">↓ &nbsp;Download</button><button class="btn-gold" id="readVolume" data-do="readVolume">Continue Vol. 3</button></div></div>
 <div class="selection-tray" id="selectionTray"><span id="selectedCount">0 selected</span><button class="btn-gold" data-do="downloadSelected">Download selected</button></div>
 </section>`;}
 function chapterMarkup(){return `
 <div class="continuation-wrap"><button class="chapter-continuation" data-do="continueChapter"><span class="emblem"><img src="${icon("comic-book.svg")}" alt=""></span><span><span class="main">Continue reading</span><span class="meta">Chapter 7 · Page 14</span></span></button></div>
 <section class="chapters-section"><div class="chapter-toolbar"><div class="chapter-stats"><div class="chapter-range" id="chapterRange">Chapters 1–10</div><div class="chapter-total">1,130 chapters</div></div>
 <div class="chapter-tools"><div class="pager" id="pager"><button aria-label="Previous page" id="pagePrev" data-do="prev">‹</button><button class="pager-label" id="pageSelector" data-do="pagePopup" aria-haspopup="listbox" aria-expanded="false">Page 1 of 113</button><button aria-label="Next page" id="pageNext" data-do="next">›</button></div>
 <input class="goto" id="goTo" type="text" inputmode="decimal" placeholder="Go to chapter" aria-label="Go to chapter" maxlength="6">
 <button class="download-page" data-do="downloadPage">↓ &nbsp;Download page</button></div></div>
 <div class="chapter-list" id="chapterList" role="list"></div></section>`;}
 const app=document.getElementById("app");
 app.innerHTML='<div class="app-scroll" id="appScroll">'+masthead()+(page==="series"?volumeMarkup():chapterMarkup())+'</div><div class="popup-host" id="popupHost"><div class="popup" id="menuPopup" role="listbox"><div class="popup-list" id="popupList"></div></div></div>';
 function say(msg){const el=document.getElementById("labStatus");if(el)el.textContent="Mock-only action: "+msg;}
 function focusVolume(n,center=true){
 s.focusVol=n;
 document.querySelectorAll(".volume-card").forEach(b=>b.classList.toggle("active",Number(b.dataset.vol)===n));
 document.getElementById("focusVolumeLabel").textContent="Vol. "+n;
 document.getElementById("focusVolumeName").textContent=volumeNames[n-1]||"";
 document.getElementById("readVolume").textContent=n===3?"Continue Vol. 3":"Read Vol. "+n;
 if(center){const card=document.querySelector(".volume-card[data-vol='"+n+"']"),flow=document.getElementById("volumeFlow");if(card&&flow)flow.scrollLeft=card.offsetLeft-(flow.clientWidth-card.offsetWidth)/2;}
 }
 function renderVolumes(){
 const flow=document.getElementById("volumeFlow");if(!flow)return;
 flow.innerHTML=Array.from({length:10},(_,i)=>{let n=i+1,a=art[i],owned=n<=3,selected=s.selected.has(n);
 return `<button class="volume-card" data-vol="${n}" aria-pressed="${selected}" aria-label="Volume ${n}, ${volumeNames[i]}, ${owned?"downloaded":"available"}">
 <span class="volume-cover ${owned?"owned":""}"><img src="${A+a[0]}" alt="Illustrative local One Piece ${a[1]} project art"><span class="selection-mark">${selected?"✓":""}</span></span>
 <span class="volume-caption"><span class="vol-label">VOL ${n}</span><span class="vol-name">${volumeNames[i]}</span><span class="vol-state ${owned?"ready":""}">${n===3?"Continue · p. 78":""}</span></span></button>`;}).join("");
 flow.querySelectorAll(".volume-card").forEach(b=>b.addEventListener("click",()=>{const n=Number(b.dataset.vol);
 if(s.selecting){if(s.selected.has(n))s.selected.delete(n);else s.selected.add(n);b.setAttribute("aria-pressed",String(s.selected.has(n)));b.querySelector(".selection-mark").textContent=s.selected.has(n)?"✓":"";updateSelection();}
 else focusVolume(n);}));
 focusVolume(s.focusVol,false);requestAnimationFrame(()=>focusVolume(s.focusVol,true));
 }
 function updateSelection(){const tray=document.getElementById("selectionTray");if(!tray)return;
 document.querySelector(".volume-section").classList.toggle("selecting",s.selecting);
 document.getElementById("selectToggle").textContent=s.selecting?"Done":"Select";
 document.getElementById("next10").hidden=!s.selecting;
 document.getElementById("selectedCount").textContent=s.selected.size+" selected";
 tray.classList.toggle("shown",s.selecting&&s.selected.size>0);
 }
 const chapterStatus=new Map([[1,"done"],[2,"done"],[4,"downloading"]]);
 function chapterRow(n){
 const title=chapterNames[n-1]||"",status=chapterStatus.get(n)||"none",image=art[(n-1)%art.length];
 return `<div class="chapter-row" role="listitem"><div class="chapter-num">${n}</div>
 <img class="chapter-thumb" src="${A+image[0]}" alt="Local One Piece ${image[1]} illustration, not an exact chapter thumbnail">
 <div class="chapter-detail"><button class="chapter-open" data-chapter="${n}">Chapter ${n}${title?" · "+title:""}</button>
 ${status==="none"?"":`<div class="chapter-status ${status==="done"?"done":""}">${status==="done"?"Downloaded":status==="downloading"?"Downloading 40%":status==="queued"?"Queued…":"Failed · press the retry button"}</div>`}
 </div><button class="chapter-download" data-download="${n}" aria-label="${status==="done"?"Delete download":status==="downloading"?"Cancel download":"Download"} Chapter ${n}">${status==="done"?"✓":status==="downloading"?"✕":"↓"}</button></div>`;
 }
 function renderChapters(){
 const el=document.getElementById("chapterList");if(!el)return;let start=s.pageIndex*10+1,finish=Math.min(start+9,1130);
 document.getElementById("chapterRange").textContent="Chapters "+start+"–"+finish;
 document.getElementById("pageSelector").textContent="Page "+(s.pageIndex+1)+" of "+s.pageCount;
 document.getElementById("pagePrev").disabled=s.pageIndex===0;
 document.getElementById("pageNext").disabled=s.pageIndex>=s.pageCount-1;
 el.innerHTML=Array.from({length:finish-start+1},(_,i)=>chapterRow(start+i)).join("");
 el.querySelectorAll("[data-chapter]").forEach(b=>b.addEventListener("click",()=>say("Read "+b.textContent+". No reader or provider was opened.")));
 el.querySelectorAll("[data-download]").forEach(b=>b.addEventListener("click",()=>{let n=Number(b.dataset.download);const old=chapterStatus.get(n)||"none";chapterStatus.set(n,old==="done"||old==="downloading"?"none":"done");renderChapters();say("Changed Chapter "+n+" mock download state.");}));
 }
 function closePopup(){const pop=document.getElementById("menuPopup");pop.classList.remove("open");pop.style.height="";s.popup=null;["languageSelector","pageSelector"].forEach(id=>{const el=document.getElementById(id);if(el)el.setAttribute("aria-expanded","false");});}
 function choosePage(i){s.pageIndex=Math.max(0,Math.min(s.pageCount-1,i));renderChapters();closePopup();
 const sc=document.getElementById("appScroll"),list=document.getElementById("chapterList");
 if(sc&&list)sc.scrollTo({top:Math.max(0,list.offsetTop-260),behavior:"instant"});}
 function positionPopup(kind,anchor,count){
 const popup=document.getElementById("menuPopup"),win=app.getBoundingClientRect(),rect=anchor.getBoundingClientRect();
 const w=kind==="language"?Math.min(224,win.width-20):Math.min(188,win.width-20);
 const h=Math.min(count*44+12,kind==="page"?320:232,Math.max(120,win.height-24));
 const x=Math.max(8,Math.min(win.width-w-8,rect.right-win.left-w));
 let y=rect.bottom-win.top+6;if(y+h>win.height-8)y=rect.top-win.top-h-6;y=Math.max(8,Math.min(win.height-h-8,y));
 popup.style.width=w+"px";popup.style.height=h+"px";popup.style.left=x+"px";popup.style.top=y+"px";
 }
 function openPopup(kind,anchor){
 if(s.popup===kind){closePopup();return;}
 const popup=document.getElementById("menuPopup"),list=document.getElementById("popupList");
 const choices=kind==="language"?[
 {label:"Use preferred language",code:"",current:s.langCode===""},{label:"English",code:"en",current:s.langCode==="en"},{label:"Português (Brasil)",code:"pt-BR",current:s.langCode==="pt-BR"},{label:"Español",code:"es",current:s.langCode==="es"}
 ]:Array.from({length:s.pageCount},(_,i)=>({label:"Page "+(i+1)+"  ·  "+(i*10+1)+"–"+Math.min(1130,i*10+10),code:String(i),current:i===s.pageIndex}));
 list.innerHTML=choices.map((o,i)=>`<button class="popup-option ${o.current?"current":""}" data-choice="${i}" role="option" aria-selected="${o.current}">${o.label}</button>`).join("");
 list.querySelectorAll("[data-choice]").forEach(b=>b.addEventListener("click",()=>{const chosen=choices[Number(b.dataset.choice)];if(kind==="page")choosePage(Number(chosen.code));else{s.langCode=chosen.code;s.lang=chosen.code?chosen.label:"Default · English";document.getElementById("languageText").textContent=s.lang;closePopup();say("Chapter language: "+s.lang);}}));
 positionPopup(kind,anchor,choices.length);popup.classList.add("open");
 anchor.setAttribute("aria-expanded","true");s.popup=kind;
 const current=list.querySelector(".current");if(current)current.scrollIntoView({block:"nearest",behavior:"instant"});
 }
 document.getElementById("languageSelector").addEventListener("click",e=>{if(page==="chapters")openPopup("language",e.currentTarget);});
 document.getElementById("appScroll").addEventListener("scroll",()=>{
   if(!s.popup)return;
   const kind=s.popup,anchor=document.getElementById(kind==="language"?"languageSelector":"pageSelector");
   if(anchor)positionPopup(kind,anchor,kind==="language"?4:s.pageCount);
 },{passive:true});
 document.addEventListener("keydown",e=>{if(e.key==="Escape")closePopup();});
 document.addEventListener("pointerdown",e=>{if(s.popup&&!e.target.closest("#menuPopup")&&!e.target.closest("#pageSelector")&&!e.target.closest("#languageSelector"))closePopup();});
 document.querySelectorAll("[data-do]").forEach(el=>el.addEventListener("click",()=>{
 const action=el.dataset.do;
 if(action==="synopsis"){s.expanded=!s.expanded;document.getElementById("synopsisWrap").classList.toggle("expanded",s.expanded);el.textContent=s.expanded?"Less":"More";el.setAttribute("aria-expanded",String(s.expanded));}
 if(action==="bookmark"){s.library=!s.library;const b=document.getElementById("bookmark");b.classList.toggle("saved",s.library);b.title=s.library?"Remove from Library":"Add to Library";b.setAttribute("aria-label",b.title);document.getElementById("bookmarkImg").src=icon("lucide/bookmark-"+(s.library?"check":"plus")+".svg");say("Library "+(s.library?"saved":"removed")+" locally.");}
 if(action==="select"){s.selecting=!s.selecting;if(!s.selecting)s.selected.clear();renderVolumes();updateSelection();}
 if(action==="prev")choosePage(s.pageIndex-1);if(action==="next")choosePage(s.pageIndex+1);
 if(action==="pagePopup")openPopup("page",el);
 if(action==="downloadPage")say("Download page "+(s.pageIndex+1)+" (no real download).");
 if(action==="downloadSelected")say("Download selected: "+Array.from(s.selected).join(", ")+" (no real download).");
 if(action==="next10")say("Download next 10 (no real download).");
 if(action==="downloadVol")say("Volume "+s.focusVol+" download (no real download).");
 if(action==="readVolume")say("Read volume "+s.focusVol+" (no reader launched).");
 if(action==="continueChapter")say("Continue Chapter 7 · Page 14 (no reader launched).");
 if(action==="collections")say("Add to Collections invoked; actual picker is outside this page mock.");
 if(action==="settings")say("Language settings invoked; destination outside this page mock.");
 if(action==="window")say(el.title+" requested; browser window unchanged.");
 if(action==="back"){if(!capture)location.href="index.html";else say("Back requested.");}
 }));
 if(page==="series")renderVolumes();else{renderChapters();document.getElementById("goTo").addEventListener("keydown",e=>{if(e.key==="Enter"){const v=Number(e.currentTarget.value);if(Number.isInteger(v)&&v>=1&&v<=1130){choosePage(Math.floor((v-1)/10));const target=document.querySelector('[data-chapter="'+v+'"]');if(target)target.focus({preventScroll:true});e.currentTarget.value="";}else say("Enter chapter number from 1 to 1130.");}if(e.key==="Escape")e.currentTarget.value="";});}
 const preset={portrait:[412,900],landscape:[900,412],compact:[354,792],short:[792,354],desktop:[1280,700],tablet:[800,1280],tabletLandscape:[1280,800]};
 const device=document.getElementById("device"),controls={preset:document.getElementById("preset"),w:document.getElementById("width"),h:document.getElementById("height"),top:document.getElementById("insetTop"),bottom:document.getElementById("insetBottom"),left:document.getElementById("insetLeft"),right:document.getElementById("insetRight"),scale:document.getElementById("fontScale")};
 function setSize(w,h,top=0,bottom=0,left=0,right=0,scale=1){
 device.style.setProperty("--vw",w+"px");device.style.setProperty("--vh",h+"px");
 for(const [k,v] of [["insetTop",top],["insetBottom",bottom],["insetLeft",left],["insetRight",right]])device.style.setProperty("--"+k,v+"px");
 device.style.setProperty("--fontScale",String(scale));
 const phone=Math.min(w,h)<600,landscape=w>h,desktop=!phone&&(controls.preset.value==="desktop"||(w>=1180&&h<=760)),tablet=!phone&&!desktop&&Math.min(w,h)<=900;
 device.classList.toggle("phone",phone);device.classList.toggle("landscape",landscape);device.classList.toggle("tablet",tablet);device.classList.toggle("desktop",desktop);
 device.classList.toggle("tight-chrome",phone&&w<390&&scale>1.15);
 if(page==="series")requestAnimationFrame(()=>focusVolume(s.focusVol,true));
 }
 function syncInputs(){const w=Number(controls.w.value),h=Number(controls.h.value);setSize(Math.max(320,Math.min(1920,w||412)),Math.max(240,Math.min(1600,h||900)),Number(controls.top.value)||0,Number(controls.bottom.value)||0,Number(controls.left.value)||0,Number(controls.right.value)||0,Number(controls.scale.value)||1);closePopup();}
 controls.preset.addEventListener("change",()=>{if(preset[controls.preset.value])[controls.w.value,controls.h.value]=preset[controls.preset.value];syncInputs();});
 [controls.w,controls.h,controls.top,controls.bottom,controls.left,controls.right,controls.scale].forEach(el=>el.addEventListener("input",()=>{if(el===controls.w||el===controls.h)controls.preset.value="custom";syncInputs();}));
 const starting=p.get("preset")&&preset[p.get("preset")]?p.get("preset"):"portrait";
 controls.preset.value=starting;[controls.w.value,controls.h.value]=preset[starting];
 if(capture){document.body.classList.add("capture");controls.w.value=window.innerWidth;controls.h.value=window.innerHeight;controls.top.value=p.get("top")||0;controls.bottom.value=p.get("bottom")||0;controls.left.value=p.get("left")||0;controls.right.value=p.get("right")||0;controls.scale.value=p.get("scale")||1;}
 syncInputs();
 if(p.get("state")==="expanded")document.getElementById("synopsisMore").click();
 if(p.get("state")==="selected"&&page==="series"){document.getElementById("selectToggle").click();document.querySelector(".volume-card[data-vol='3']").click();}
 if(p.get("state")==="language-open"&&page==="chapters")requestAnimationFrame(()=>document.getElementById("languageSelector").click());
 if(p.get("state")==="page-open"&&page==="chapters")requestAnimationFrame(()=>document.getElementById("pageSelector").click());
 window.addEventListener("resize",()=>{if(capture){controls.w.value=window.innerWidth;controls.h.value=window.innerHeight;syncInputs();}});
})();

