"use strict";
(() => {
 const A="../../../../../baseline/reference/desktop/assets/",p=new URLSearchParams(location.search);
 const sourceRoute=p.get("route")==="reading-room"?"reading-room":"rebuilt";
 // Cover-flow allocation under the chosen header: "scroll" = page scroll as in the Hemanth-viewed
 // rebuilt preview; "fit" = source desktop allocation (masthead fixed, flow takes the rest, 190px floor kept).
 const flowAlloc=(document.body.dataset.flow||p.get("flow"))==="fit"?"fit":"scroll";
 const pageBase=document.body.dataset.base||"";
 const hostParam=p.get("host");
 const page=document.body.dataset.page==="chapters"?"chapters":"series",capture=p.has("capture");
 const art=[
 ["universes/one-piece/east-blue/canon/romance-dawn.png","Romance Dawn"],
 ["universes/one-piece/east-blue/canon/orange-town.png","Orange Town"],
 ["universes/one-piece/east-blue/canon/syrup-village.png","Syrup Village"],
 ["universes/one-piece/east-blue/canon/baratie.png","Baratie"],
 ["universes/one-piece/east-blue/canon/arlong-park.png","Arlong Park"],
 ["universes/one-piece/east-blue/canon/loguetown.png","Loguetown"],
 ["universes/one-piece/east-blue/noncanon/01-ganzack.png","Ganzack film poster"],
 ["universes/one-piece/east-blue/noncanon/02-one-piece-movie.png","One Piece film poster"],
 ["universes/one-piece/east-blue/noncanon/05-clockwork-island.png","Clockwork Island film poster"],
 ["universes/one-piece/east-blue/noncanon/04-jangos-dance-carnival.png","Jango Dance Carnival art"]
 ];
 const volumeNames=["Romance Dawn","Buggy the Clown","Don't Get Fooled Again","The Black Cat Pirates","For Whom the Bell Tolls","The Oath","The Crap-Geezer","I Won't Die","Tears","OK, Let's Stand Up!"];
 const chapterNames=["Romance Dawn","They Call Him Straw Hat Luffy","Enter Zoro: Pirate Hunter","The Great Captain Morgan","The Pirate King and the Master Swordsman","The First","Friends","Nami","The Devil's Daughter","The Black Cat Pirates","The World's Greatest Power","Dog","Treasure","Reckless","Gong","Versus Buggy's Crew","Difference in Level","Pirate Buggy","Devil Fruit","Thief"];
 const synopsis="Monkey D. Luffy sets sail in pursuit of the legendary treasure known as One Piece. With the Straw Hat Pirates, he crosses treacherous seas, meets unlikely allies, and challenges powerful enemies on the way to becoming King of the Pirates. Along the Grand Line and into the New World, every island brings new friendships and an adventure of its own.";
 const s={expanded:false,library:false,selecting:false,selected:new Set(),focusVol:3,lang:"Default · English",langCode:"",pageIndex:0,pageCount:113,popup:null,fixture:p.get("fixture")||"populated"};
 const svgBack='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 L8 12 L14.5 18.5"/></svg>';
 const collectionIcon='<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/></svg>';
 const icon=n=>A+"icons/"+n;
 function viewLink(which){const q=new URLSearchParams(p);q.delete("state");q.delete("fixture");return pageBase+(which==="chapters"?"manga-chapters.html":"manga-series.html")+"?"+q.toString();}
 function masthead(){return `
 ${sourceRoute==="rebuilt"?'<div class="title-bar" id="titleBar"></div>':`<header class="chrome">
 <button class="back-action" data-do="back" aria-label="Back">${svgBack}<span>Back</span></button>
 <div class="chrome-brand">COLOSSEUM · TANKOBAN</div>
 <div class="chrome-window" id="chromeWindow"></div></header>`}
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
 function chapterMarkup(){if(sourceRoute==="reading-room")return `<div class="chapter-controls"><span class="chapter-total">CHAPTERS&nbsp; 1,130</span><div class="chapter-controls-right"><button class="chapter-continuation" data-do="continueChapter"><span class="emblem"><img src="${icon("comic-book.svg")}" alt=""></span><span><span class="main">Continue reading</span><span class="meta">Chapter 7 · Page 14</span></span></button><div class="pager" id="pager"><button aria-label="Previous page" id="pagePrev" data-do="prev" hidden>‹</button><button class="pager-label" id="pageSelector" data-do="pagePopup" aria-haspopup="listbox" aria-expanded="false">Page 1 ▾</button><button aria-label="Next page" id="pageNext" data-do="next" hidden>›</button></div><input id="goTo" type="text" hidden></div></div><section class="chapters-section"><div class="chapter-toolbar"><div class="chapter-stats"><div class="chapter-range" id="chapterRange">Chapters 1–10</div><div class="chapter-total" id="activeChapterCount">10 chapters</div></div><button class="download-page" data-do="downloadPage">↓ &nbsp;Download page</button></div><div class="chapter-list" id="chapterList" role="list"></div></section>`;return `
 <div class="continuation-wrap"><button class="chapter-continuation" data-do="continueChapter"><span class="emblem"><img src="${icon("comic-book.svg")}" alt=""></span><span><span class="main">Continue reading</span><span class="meta">Chapter 7 · Page 14</span></span></button></div>
 <section class="chapters-section"><div class="chapter-toolbar"><div class="chapter-stats"><div class="chapter-range" id="chapterRange">Chapters 1–10</div><div class="chapter-total">1,130 chapters</div></div>
 <div class="chapter-tools"><div class="pager" id="pager"><button aria-label="Previous page" id="pagePrev" data-do="prev">‹</button><button class="pager-label" id="pageSelector" data-do="pagePopup" aria-haspopup="listbox" aria-expanded="false">Page 1 of 113</button><button aria-label="Next page" id="pageNext" data-do="next">›</button></div>
 <input class="goto" id="goTo" type="text" inputmode="decimal" placeholder="Go to chapter" aria-label="Go to chapter" maxlength="6">
 <button class="download-page" data-do="downloadPage">↓ &nbsp;Download page</button></div></div>
 <div class="chapter-list" id="chapterList" role="list"></div></section>`;}
 const app=document.getElementById("app");
 app.dataset.route=sourceRoute;app.dataset.page=page;app.dataset.flow=flowAlloc;
 app.innerHTML='<div class="app-scroll" id="appScroll">'+masthead()+'<div class="content-scroll" id="contentScroll">'+(page==="series"?volumeMarkup():chapterMarkup())+'</div></div><div class="popup-host" id="popupHost"><button class="popup-guard" id="popupGuard" aria-label="Dismiss dropdown" tabindex="-1"></button><div class="popup" id="menuPopup" role="listbox"><div class="popup-list" id="popupList"></div></div></div>';
 function say(msg){const el=document.getElementById("labStatus");if(el)el.textContent="Mock-only action: "+msg;}
 function focusVolume(n,center=true){
 s.focusVol=n;
 document.querySelectorAll(".volume-card").forEach(b=>{const d=Math.abs(Number(b.dataset.vol)-n),scale=d===0?1.10:d===1?1:d===2?.95:d===3?.90:.86;b.classList.toggle("active",d===0);b.style.setProperty("--item-scale",scale);const h=parseFloat(getComputedStyle(document.getElementById("volumeFlow")).getPropertyValue("--coverH"))||255;b.style.transform="translateY("+((scale-1)*(h+54)/2)+"px) scale("+scale+")";b.setAttribute("aria-current",String(d===0));});
 document.querySelector("[data-do=downloadVol]").disabled=n<=3;
 document.getElementById("volumeFlow").setAttribute("aria-label","One Piece volumes. Current volume "+n+". Arrow keys move; Page Up or Down jumps ten; Enter reads.");
 document.getElementById("focusVolumeLabel").textContent="Vol. "+n;
 document.getElementById("focusVolumeName").textContent=volumeNames[n-1]||"";
 document.getElementById("readVolume").textContent=n===3?"Continue Vol. 3":"Read Vol. "+n;
 if(center){const card=document.querySelector(".volume-card[data-vol='"+n+"']"),flow=document.getElementById("volumeFlow");if(card&&flow)flow.scrollLeft=card.offsetLeft-(flow.clientWidth-card.offsetWidth)/2;}
 }
 function renderVolumes(){
 const flow=document.getElementById("volumeFlow");if(!flow)return;
 flow.innerHTML=Array.from({length:107},(_,i)=>{let n=i+1,a=art[i%art.length],owned=n<=3,selected=s.selected.has(n);
 return `<button class="volume-card" tabindex="-1" data-vol="${n}" aria-pressed="${selected}" aria-label="Volume ${n}, ${volumeNames[i]||""}, ${owned?"downloaded":"available"}">
 <span class="volume-cover ${owned?"owned":""}"><img src="${A+a[0]}" alt="Illustrative local One Piece ${a[1]} project art"><span class="selection-mark">${selected?"✓":""}</span></span>
 <span class="volume-caption"><span class="vol-label">VOL ${n}</span><span class="vol-name">${volumeNames[i]||""}</span><span class="vol-state ${owned?"ready":""}"></span></span></button>`;}).join("");
 flow.querySelectorAll(".volume-card").forEach(b=>b.addEventListener("click",()=>{const n=Number(b.dataset.vol);
 if(s.selecting){if(s.selected.has(n))s.selected.delete(n);else s.selected.add(n);b.setAttribute("aria-pressed",String(s.selected.has(n)));b.querySelector(".selection-mark").textContent=s.selected.has(n)?"✓":"";updateSelection();}
 else if(n===s.focusVol)say("readVolumeRequested(Vol. "+n+") — local fixture signal.");else focusVolume(n);}));
 focusVolume(s.focusVol,false);requestAnimationFrame(()=>{fitVolumeFlow();focusVolume(s.focusVol,true);});
 }
 function updateSelection(){const tray=document.getElementById("selectionTray");if(!tray)return;
 document.querySelector(".volume-section").classList.toggle("selecting",s.selecting);
 document.getElementById("selectToggle").textContent=s.selecting?"Done":"Select";
 document.getElementById("next10").hidden=!s.selecting;
 document.getElementById("selectedCount").textContent=s.selected.size+" selected";
 tray.classList.toggle("shown",s.selecting&&s.selected.size>0);
 }
 const chapterStatus=new Map([[1,"done"],[2,"done"],[4,"downloading"],[6,"error"]]);
 const chapterTimers=new Map();
 function chapterRow(n){
 const title=(s.fixture==="long"?chapterNames[4]:(chapterNames[n-1]||"")),status=chapterStatus.get(n)||"none",image=art[(n-1)%art.length];
 return `<div class="chapter-row" role="listitem"><div class="chapter-num">${n}</div>
 <img class="chapter-thumb" src="${A+image[0]}" alt="Local One Piece ${image[1]} illustration, not an exact chapter thumbnail">
 <div class="chapter-detail"><button class="chapter-open" data-chapter="${n}">Chapter ${n}${title?" · "+title:""}</button>
 ${`<div class="chapter-status ${status==="done"?"done":status==="error"?"failed":""}">${status==="done"?"Downloaded":status==="downloading"?"Downloading 40%":status==="queued"?"Queued…":status==="error"?"Failed · Retry":"Available"}</div>`}
 </div><button class="chapter-download" data-download="${n}" aria-label="${status==="done"?"Delete download":["downloading","queued"].includes(status)?"Cancel download":status==="error"?"Retry":"Download"} Chapter ${n}">${status==="done"?"✓":["downloading","queued"].includes(status)?"✕":status==="error"?"↻":"↓"}</button></div>`;
 }
 function renderChapters(){
 const focused=document.activeElement?.dataset.download;const scrollNode=document.getElementById("contentScroll"),sy=scrollNode.scrollTop;
 const el=document.getElementById("chapterList");if(!el)return;let start=s.pageIndex*10+1,finish=Math.min(start+9,1130);
 document.getElementById("chapterRange").textContent="Chapters "+start+"–"+finish;
 document.getElementById("pageSelector").textContent=sourceRoute==="reading-room"?"Page "+(s.pageIndex+1)+" ▾":"Page "+(s.pageIndex+1)+" of "+s.pageCount;
 document.getElementById("pagePrev").disabled=s.pageIndex===0;
 document.getElementById("pageNext").disabled=s.pageIndex>=s.pageCount-1;
 const noRows=["loading","empty","error","disabled"].includes(s.fixture);const totalNode=document.querySelector(".chapter-controls>.chapter-total");if(totalNode)totalNode.textContent="CHAPTERS  "+(noRows?"0":"1,130");el.innerHTML=noRows?`<div class="chapter-empty"><p>${s.fixture==="loading"?"Loading chapters...":s.fixture==="error"?"Chapter source unavailable.":"No chapters available"}</p>${s.fixture==="disabled"?'<button class="btn-outline" id="chapterExtensions">Open Extensions</button>':""}</div>`:Array.from({length:finish-start+1},(_,i)=>chapterRow(start+i)).join("");
 document.getElementById("pageSelector").disabled=noRows;document.querySelector("[data-do=downloadPage]").disabled=noRows;document.getElementById("chapterRange").textContent=noRows?"":("Chapters "+start+"–"+finish);const count=document.getElementById("activeChapterCount");if(count)count.textContent=noRows?"0 chapters":(finish-start+1)+" chapters";document.getElementById("chapterExtensions")?.addEventListener("click",()=>say("openExtensionsRequested() — local route signal."));
 el.querySelectorAll("[data-chapter]").forEach(b=>b.addEventListener("click",e=>{e.stopPropagation();say("Read "+b.textContent+". No reader or provider was opened.");}));el.querySelectorAll(".chapter-row").forEach(row=>row.addEventListener("click",e=>{if(!e.target.closest("[data-download]"))say("readChapterRequested("+row.querySelector("[data-chapter]").dataset.chapter+") — local fixture signal.");}));
 el.querySelectorAll("[data-download]").forEach(b=>b.addEventListener("click",()=>{changeChapter(Number(b.dataset.download));}));
 if(focused)el.querySelector('[data-download="'+focused+'"]')?.focus({preventScroll:true});scrollNode.scrollTop=sy;
 }
 function changeChapter(n){const old=chapterStatus.get(n)||"none";clearTimeout(chapterTimers.get(n));if(["done","downloading","queued"].includes(old)){chapterStatus.set(n,"none");}else{chapterStatus.set(n,"queued");chapterTimers.set(n,setTimeout(()=>{chapterStatus.set(n,"downloading");renderChapters();chapterTimers.set(n,setTimeout(()=>{chapterStatus.set(n,"done");renderChapters();},1200));},350));}renderChapters();say("Local fixture: Chapter "+n+" "+chapterStatus.get(n)+". No download service.");}
 function closePopup(restore=true){const oldKind=s.popup;const pop=document.getElementById("menuPopup");pop.classList.remove("open");pop.style.height="";document.getElementById("popupHost").classList.remove("open");s.popup=null;["languageSelector","pageSelector"].forEach(id=>{const el=document.getElementById(id);if(el)el.setAttribute("aria-expanded","false");});if(restore&&oldKind)document.getElementById(oldKind==="language"?"languageSelector":"pageSelector")?.focus({preventScroll:true});}
 function choosePage(i){s.pageIndex=Math.max(0,Math.min(s.pageCount-1,i));renderChapters();closePopup();
 const sc=document.getElementById("contentScroll");if(sc)sc.scrollTop=0;}
 function positionPopup(kind,anchor,count){
 const popup=document.getElementById("menuPopup"),win=app.getBoundingClientRect(),rect=anchor.getBoundingClientRect();
 const w=kind==="language"?Math.min(224,win.width-20):Math.min(sourceRoute==="reading-room"?150:180,win.width-20);
 const nativeDesktopPage=kind==="page"&&sourceRoute==="reading-room"&&!document.getElementById("device").classList.contains("phone");const h=Math.min(count*(nativeDesktopPage?36:44)+12,kind==="page"?(nativeDesktopPage?300:320):232,Math.max(120,win.height-24));popup.dataset.kind=kind;
 const x=Math.max(8,Math.min(win.width-w-8,rect.right-win.left-w));
 let y=rect.bottom-win.top+6;if(y+h>win.height-8)y=rect.top-win.top-h-6;y=Math.max(8,Math.min(win.height-h-8,y));
 popup.style.width=w+"px";popup.style.height=h+"px";popup.style.left=x+"px";popup.style.top=y+"px";
 }
 function openPopup(kind,anchor){
 if(s.popup===kind){closePopup();return;}
 const popup=document.getElementById("menuPopup"),list=document.getElementById("popupList");
 const choices=kind==="language"?[
 {label:"Use preferred language",code:"",current:s.langCode===""},{label:"English",code:"en",current:s.langCode==="en"},{label:"Português (Brasil)",code:"pt-BR",current:s.langCode==="pt-BR"},{label:"Español",code:"es",current:s.langCode==="es"}
 ]:Array.from({length:s.pageCount},(_,i)=>({label:sourceRoute==="reading-room"?"Page "+(i+1):"Page "+(i+1)+"  ·  "+(i*10+1)+"–"+Math.min(1130,i*10+10),code:String(i),current:i===s.pageIndex}));
 list.innerHTML=choices.map((o,i)=>`<button class="popup-option ${o.current?"current":""}" data-choice="${i}" role="option" aria-selected="${o.current}">${o.label}</button>`).join("");
 list.querySelectorAll("[data-choice]").forEach(b=>b.addEventListener("click",()=>{const chosen=choices[Number(b.dataset.choice)];if(kind==="page")choosePage(Number(chosen.code));else{s.langCode=chosen.code;s.lang=chosen.code?chosen.label:"Default · English";document.getElementById("languageText").textContent=s.lang;closePopup();say("Chapter language: "+s.lang);}}));
 anchor.scrollIntoView({block:"nearest"});positionPopup(kind,anchor,choices.length);popup.classList.add("open");document.getElementById("popupHost").classList.add("open");
 anchor.setAttribute("aria-expanded","true");s.popup=kind;
 const current=list.querySelector(".current")||list.firstElementChild;if(current){list.scrollTop=Math.max(0,current.offsetTop-list.clientHeight/2);current.focus({preventScroll:true});}
 }
 document.getElementById("languageSelector").addEventListener("click",e=>{if(page==="chapters")openPopup("language",e.currentTarget);});
 document.getElementById("appScroll").addEventListener("scroll",()=>{
   if(!s.popup)return;
   const kind=s.popup,anchor=document.getElementById(kind==="language"?"languageSelector":"pageSelector");
   if(anchor)positionPopup(kind,anchor,kind==="language"?4:s.pageCount);
 },{passive:true});
 document.getElementById("popupGuard").addEventListener("click",()=>closePopup());
 document.addEventListener("keydown",e=>{if(!s.popup)return;const a=[...document.querySelectorAll(".popup-option")],i=a.indexOf(document.activeElement);if(e.key==="Escape"){e.preventDefault();e.stopPropagation();closePopup();}else if(["ArrowDown","ArrowUp","Home","End","Tab"].includes(e.key)){e.preventDefault();const n=e.key==="Home"?0:e.key==="End"?a.length-1:Math.max(0,Math.min(a.length-1,i+((e.key==="ArrowUp"||(e.key==="Tab"&&e.shiftKey))?-1:1)));a[n].focus({preventScroll:true});a[n].scrollIntoView({block:"nearest"});}});
 document.addEventListener("pointerdown",e=>{if(s.popup&&!e.target.closest("#menuPopup")&&!e.target.closest("#pageSelector")&&!e.target.closest("#languageSelector")&&!e.target.closest("#popupGuard"))closePopup();});
 document.querySelectorAll("[data-do]").forEach(el=>el.addEventListener("click",()=>{
 const action=el.dataset.do;
 if(action==="synopsis"){s.expanded=!s.expanded;document.getElementById("synopsisWrap").classList.toggle("expanded",s.expanded);el.textContent=s.expanded?"Less":"More";el.setAttribute("aria-expanded",String(s.expanded));requestAnimationFrame(fitHeader);}
 if(action==="bookmark"){s.library=!s.library;const b=document.getElementById("bookmark");b.classList.toggle("saved",s.library);b.title=s.library?"Remove from Library":"Add to Library";b.setAttribute("aria-label",b.title);document.getElementById("bookmarkImg").src=icon("lucide/bookmark-"+(s.library?"check":"plus")+".svg");say("Library "+(s.library?"saved":"removed")+" locally.");}
 if(action==="select"){s.selecting=!s.selecting;if(!s.selecting)s.selected.clear();renderVolumes();updateSelection();}
 if(action==="prev")choosePage(s.pageIndex-1);if(action==="next")choosePage(s.pageIndex+1);
 if(action==="pagePopup")openPopup("page",el);
 if(action==="downloadPage"){for(let n=s.pageIndex*10+1;n<=Math.min(1130,s.pageIndex*10+10);n++){if(!["done","queued","downloading"].includes(chapterStatus.get(n)))changeChapter(n);}say("Current page queued in the local fixture only.");}
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
 const android=hostParam==="android"?true:hostParam==="desktop"?false:(phone||tablet);device.classList.toggle("android-host",android);renderWindowChrome(android);
 fitHeader();
 if(page==="series")requestAnimationFrame(()=>{fitVolumeFlow();focusVolume(s.focusVol,true);});
 }
 function syncInputs(){const w=Number(controls.w.value),h=Number(controls.h.value);setSize(Math.max(320,Math.min(1920,w||412)),Math.max(240,Math.min(1600,h||900)),Number(controls.top.value)||0,Number(controls.bottom.value)||0,Number(controls.left.value)||0,Number(controls.right.value)||0,Number(controls.scale.value)||1);closePopup();}
 controls.preset.addEventListener("change",()=>{if(preset[controls.preset.value])[controls.w.value,controls.h.value]=preset[controls.preset.value];syncInputs();});
 [controls.w,controls.h,controls.top,controls.bottom,controls.left,controls.right,controls.scale].forEach(el=>el.addEventListener("input",()=>{if(el===controls.w||el===controls.h)controls.preset.value="custom";syncInputs();}));

 function fitHeader(){if(sourceRoute!=="reading-room")return;const hero=document.querySelector(".hero"),text=document.querySelector(".hero-text"),facts=document.querySelector(".facts"),identity=document.querySelector(".identity"),phone=document.getElementById("device").classList.contains("phone");document.querySelector(".eyebrow").textContent="MANGA";document.querySelector(".genre-row").hidden=true;if(phone){text.insertBefore(facts,document.querySelector(".genre-row"));hero.style.height="";}else{identity.insertBefore(facts,document.querySelector(".collection-btn"));hero.style.height=Math.max(205,text.scrollHeight+34)+"px";}}
 function fitVolumeFlow(){const flow=document.getElementById("volumeFlow");if(!flow)return;const unclamped=Math.floor((flow.clientHeight-54-16)/1.1),h=Math.max(190,Math.min(276,unclamped));flow.dataset.unclampedBookHeight=unclamped;flow.dataset.bookHeight=h;flow.style.setProperty("--coverH",h+"px");flow.style.setProperty("--coverW",Math.round(h*2/3)+"px");flow.tabIndex=0;focusVolume(s.focusVol,true);}
 function setupFinishedControls(){const box=document.querySelector(".labfields"),label=document.createElement("label");label.textContent="Source composition";const sel=document.createElement("select");sel.id="sourceRoute";sel.innerHTML='<option value="rebuilt">Rebuilt title page · Hemanth-selected main (TankobanSeriesView + TitlePageBar)</option><option value="reading-room">Older reading room · comparison (MangaSeries shared header)</option>';sel.value=sourceRoute;sel.addEventListener("change",()=>{const q=new URLSearchParams(location.search);q.set("route",sel.value);location.search=q.toString();});label.append(sel);box.append(label);const stateLabel=document.createElement("label");stateLabel.textContent="Fixture state";const st=document.createElement("select");st.id="fixtureState";st.innerHTML=(page==="chapters"?["populated","long","loading","empty","error","disabled","saved-unavailable"]:["populated","long"]).map(v=>'<option value="'+v+'">'+v+'</option>').join("");st.value=s.fixture;st.addEventListener("change",()=>{s.fixture=st.value;if(page==="chapters"){renderChapters();const unavailable=s.fixture==="saved-unavailable";document.querySelectorAll(".chapter-continuation").forEach(e=>e.hidden=unavailable);let msg=document.getElementById("savedUnavailable");if(!msg){msg=document.createElement("p");msg.id="savedUnavailable";msg.className="saved-unavailable";document.querySelector(".chapter-controls,.continuation-wrap")?.append(msg);}msg.hidden=!unavailable;msg.textContent="Saved chapter unavailable on this device · choose a source below";}else{document.querySelector(".synopsis-text").textContent=s.fixture==="long"?synopsis+" "+synopsis:synopsis;fitHeader();}});stateLabel.append(st);box.append(stateLabel);st.dispatchEvent(new Event("change"));const note=document.createElement("p");note.className="labnote";note.textContent=sourceRoute==="reading-room"?"Comparison only: older Main.qml default route (MangaSeries → MangaSeriesSharedHeader / MangaChapterSeriesView). Hemanth selected the rebuilt route on 2026-11-01.":"Main mock (Hemanth-selected 2026-11-01): TankobanSeriesView masthead + TankobanChapterList under the real TitlePageBar — Back pill naming its destination, Search and the one system menu (desktop only; omitted on Android phones and tablets). Declared COLOSSEUM · TANKOBAN wordmark, device progress and synopsis More retained.";document.querySelector(".lab").append(note);if(page==="series"){const flow=document.getElementById("volumeFlow");flow.addEventListener("keydown",e=>{let n=s.focusVol;if(e.key==="ArrowRight")n++;else if(e.key==="ArrowLeft")n--;else if(e.key==="PageDown")n+=10;else if(e.key==="PageUp")n-=10;else if(e.key==="Home")n=1;else if(e.key==="End")n=107;else if(["Enter"," "].includes(e.key)){e.preventDefault();document.querySelector('.volume-card[data-vol="'+s.focusVol+'"]').click();return;}else return;e.preventDefault();focusVolume(Math.max(1,Math.min(107,n)));});flow.addEventListener("wheel",e=>{if(e.deltaX||e.deltaY){e.preventDefault();focusVolume(Math.max(1,Math.min(107,s.focusVol+((e.deltaY||e.deltaX)>0?1:-1)*(e.shiftKey?10:1))));}},{passive:false});new ResizeObserver(()=>fitVolumeFlow()).observe(flow);}}

 // ---- TitlePageBar (qml/TitlePageBar.qml at the fixed pin) for the Hemanth-selected rebuilt route ----
 // Left: Back pill naming its destination (Main.qml binds backLabel to the current world: "Tankoban").
 // Right: Search and ONE system menu (Minimize / Fullscreen / Quit… with confirmation).
 // Android phones AND tablets: the system-menu invoker and its panel are not rendered at all, so they have
 // no pointer, keyboard or accessibility path (precedent: qml/+android/TopBar.qml desktopWindowControlsVisible:
 // !androidHost). Back and Search remain; no replacement mobile menu. The declared COLOSSEUM · TANKOBAN
 // wordmark (classification item 7) is retained as non-interactive centred text; it is not part of
 // TitlePageBar.qml itself (see QML-PORTING.md, conflict C1).
 const tb={menu:false,quitArmed:false,android:false};
 const svgSearch=icon("search.svg");
 function titleBarMarkup(){
  const menuOpen=tb.menu&&!tb.android;
  const menu=!menuOpen?"":`<div class="tb-menu" id="tbMenu" role="menu" aria-label="Window and app">${tb.quitArmed
   ?`<div class="tb-quit-title">Quit Colosseum?</div><button role="menuitem" data-tb="keep">Keep watching</button><button role="menuitem" class="danger" data-tb="quit">Quit</button>`
   :`<button role="menuitem" data-tb="minimize">Minimize</button><button role="menuitem" data-tb="fullscreen">${tb.windowed?"Fullscreen":"Exit fullscreen"}</button><hr><button role="menuitem" class="danger" data-tb="arm-quit">Quit Colosseum…</button>`}</div>`;
  return `<div class="tb-row"><button class="back-action tb-back" id="tbBack" data-tb="back">${svgBack}<span>Tankoban</span></button>
  <div class="tb-wordmark" id="tbWordmark">COLOSSEUM · TANKOBAN</div>
  <div class="tb-right" id="tbRight"><button class="tb-round" id="tbSearch" data-tb="search" aria-label="Search"><img src="${svgSearch}" alt=""></button>${tb.android?"":`<button class="tb-round" id="tbMenuButton" data-tb="menu" aria-label="Window and app" aria-haspopup="menu" aria-expanded="${menuOpen}"><span class="tb-dots"><i></i><i></i><i></i></span></button>`}</div></div>${menu}`;
 }
 function renderTitleBar(focusId){
  const bar=document.getElementById("titleBar");if(!bar)return;
  bar.innerHTML=titleBarMarkup();
  fitWordmark();
  if(focusId)requestAnimationFrame(()=>document.getElementById(focusId)?.focus({preventScroll:true})||document.querySelector(focusId)?.focus({preventScroll:true}));
 }
 function fitWordmark(){
  const bar=document.getElementById("titleBar");if(!bar)return;bar.classList.remove("tb-wrap");
  const b=document.getElementById("tbBack").getBoundingClientRect(),r=document.getElementById("tbRight").getBoundingClientRect(),m=document.getElementById("tbWordmark").getBoundingClientRect();
  if(m.left<b.right+12||m.right>r.left-12)bar.classList.add("tb-wrap");
  app.style.setProperty("--tbH",bar.classList.contains("tb-wrap")?"98px":"76px");
 }
 function openTbMenu(){tb.menu=true;tb.quitArmed=false;renderTitleBar('[data-tb="minimize"]');}
 function closeTbMenu(restore=true){tb.menu=false;tb.quitArmed=false;renderTitleBar(restore?"tbMenuButton":null);}
 function renderWindowChrome(android){
  tb.android=android;if(android){tb.menu=false;tb.quitArmed=false;}
  if(sourceRoute==="rebuilt"){renderTitleBar();return;}
  const box=document.getElementById("chromeWindow");if(!box)return;
  box.innerHTML=android?"":`<button title="Minimize" data-chrome="window" aria-label="Minimize"><img src="${icon("minimize.svg")}" alt=""></button><button title="Fullscreen" data-chrome="window" aria-label="Fullscreen"><img src="${icon("fullscreen.svg")}" alt=""></button><button title="Close" data-chrome="window" aria-label="Close"><img src="${icon("power.svg")}" alt=""></button>`;
 }
 tb.windowed=false;
 document.getElementById("chromeWindow")?.addEventListener("click",e=>{const b=e.target.closest("[data-chrome]");if(b)say(b.title+" requested; browser window unchanged.");});
 document.getElementById("titleBar")?.addEventListener("click",e=>{
  const b=e.target.closest("[data-tb]");if(!b)return;const a=b.dataset.tb;
  if(a==="back"){if(!capture)location.href="index.html";else say("backRequested() → Main.qml closeSeries(); returns to Tankoban.");}
  else if(a==="search")say("searchRequested() → Main.qml closes the series, opens Tankoban and its Search surface (not opened in this mock).");
  else if(a==="menu"){tb.menu?closeTbMenu(true):openTbMenu();}
  else if(a==="minimize"){closeTbMenu(false);say("minimizeRequested() → page.leave(\"minimize\"); browser window unchanged.");}
  else if(a==="fullscreen"){closeTbMenu(false);tb.windowed=!tb.windowed;say("fullscreenRequested() → toggleFullscreenShell(); browser window unchanged.");}
  else if(a==="arm-quit"){tb.quitArmed=true;renderTitleBar('[data-tb="keep"]');}
  else if(a==="keep")closeTbMenu(true);
  else if(a==="quit"){closeTbMenu(false);say("quitRequested() → page.leave(\"close\") → requestApplicationQuit(); nothing closed.");}
 });
 document.addEventListener("keydown",e=>{if(e.key==="Escape"&&tb.menu&&!s.popup){e.preventDefault();closeTbMenu(true);}});
 document.getElementById("appScroll").addEventListener("scroll",e=>{document.getElementById("titleBar")?.classList.toggle("solid",e.currentTarget.scrollTop>4);},{passive:true});
 // Geometry probe for the cover-flow allocation re-evaluation (read by candidate/tools/batch1-*.mjs).
 window.__flowGeometry=()=>{const flow=document.getElementById("volumeFlow");if(!flow)return null;const stage=flow.getBoundingClientRect(),dev=document.getElementById("app").getBoundingClientRect();
  const card=document.querySelector(".volume-card.active");const cover=card?.querySelector(".volume-cover").getBoundingClientRect();const cr=card?.getBoundingClientRect();
  const scales=[...document.querySelectorAll(".volume-card")].map(c=>[Number(c.dataset.vol),Number(c.style.getPropertyValue("--item-scale"))]).filter(([n])=>Math.abs(n-s.focusVol)<=4).sort((a,b)=>a[0]-b[0]);
  const scroller=document.getElementById("appScroll"),actions=document.querySelector(".volume-action").getBoundingClientRect();
  return {flowAlloc,route:sourceRoute,appHeight:Math.round(dev.height),mastheadBottom:Math.round(document.querySelector(".hero").getBoundingClientRect().bottom-dev.top),
   flowViewportHeight:Math.round(stage.height),unclampedBookHeight:Number(flow.dataset.unclampedBookHeight),bookHeight:Number(flow.dataset.bookHeight),
   focusedDrawnCoverHeight:cover?Math.round(cover.height*10)/10:null,focusedScale:cover?Math.round(cover.height/Number(flow.dataset.bookHeight)*1000)/1000:null,neighbourScales:scales,
   focusedCardFullyInsideFlow:cr?cr.top>=stage.top-0.5&&cr.bottom<=stage.bottom+0.5:false,
   focusedCardFullyOnFirstScreen:cr?(cr.top-dev.top+scroller.scrollTop)>=0&&(cr.bottom-dev.top+scroller.scrollTop)<=dev.height:false,
   scrollToSeeFocusedAndActions:Math.max(0,Math.round(Math.max(cr?cr.bottom:0,actions.bottom)-dev.top+scroller.scrollTop-dev.height)),
   pageScrollable:scroller.scrollHeight>scroller.clientHeight+1};};

 setupFinishedControls();
 const starting=p.get("preset")&&preset[p.get("preset")]?p.get("preset"):"portrait";
 controls.preset.value=starting;[controls.w.value,controls.h.value]=preset[starting];
 if(capture){document.body.classList.add("capture");controls.w.value=window.innerWidth;controls.h.value=window.innerHeight;controls.top.value=p.get("top")||0;controls.bottom.value=p.get("bottom")||0;controls.left.value=p.get("left")||0;controls.right.value=p.get("right")||0;controls.scale.value=p.get("scale")||1;}
 syncInputs();
 if(p.get("state")==="expanded")document.getElementById("synopsisMore").click();
 if(p.get("state")==="selected"&&page==="series"){document.getElementById("selectToggle").click();document.querySelector(".volume-card[data-vol='3']").click();}
 if(p.get("state")==="language-open"&&page==="chapters")requestAnimationFrame(()=>document.getElementById("languageSelector").click());
 if(p.get("state")==="page-open"&&page==="chapters")requestAnimationFrame(()=>document.getElementById("pageSelector").click());
 document.fonts.ready.then(()=>{fitHeader();if(page==="series")fitVolumeFlow();});
 window.addEventListener("resize",()=>{if(capture){controls.w.value=window.innerWidth;controls.h.value=window.innerHeight;syncInputs();}});
})();

