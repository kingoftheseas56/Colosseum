"use strict";
// Standalone HTML-only catalogue fitting study: no fetch, network, storage, providers or app operations.
const ROOT="../../../../reference/desktop/assets/";
const A=p=>ROOT+p;
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const slug=s=>String(s).toLowerCase().replace(/[^a-z0-9]+/g,"-").replace(/^-|-$/g,"");
const ICONS={Theatre:"projector-theatre.svg",Tankoban:"comic-book.svg",Biblio:"book-library.svg",Universes:"universes-atom.svg",Profile:"your-colosseum-laurel.svg"};
const MOVIE_ART=["wallpaper/sagas/dune.jpg","wallpaper/sagas/jurassic-park.jpg","wallpaper/sagas/harry-potter.jpg","wallpaper/sagas/lord-of-the-rings.jpg","wallpaper/sagas/sherlock-holmes.jpg","wallpaper/sagas/the-witcher.jpg","wallpaper/sagas/ice-and-fire.jpg","wallpaper/sagas/percy-jackson.jpg"];
const MANGA_ART=["universes/one-piece/east-blue/banners/romance.png","universes/one-piece/east-blue/banners/arlong.png","universes/one-piece/east-blue/banners/baratie.png","universes/one-piece/paradise/banners/arabasta.png","universes/one-piece/paradise/banners/enies-lobby.png","universes/one-piece/new-world/banners/wano-country.png","universes/one-piece/new-world/banners/dressrosa.png","universes/one-piece/final-sea/banners/egghead.png","universes/one-piece/final-sea/banners/elbaf.png","universes/one-piece/paradise/banners/skypiea.png"];
const COMIC_ART=["universes/dcau/portals/gotham.jpg","universes/dcau/portals/metropolis.jpg","universes/dcau/portals/watchtower.jpg","universes/dcau/portals/future-gotham.jpg","universes/dcau/environments/gotham-v11-bg.png","universes/dcau/environments/justice-v11-bg.png","extensions/house/dcau.jpg","universes/star-wars/coruscant.jpg","extensions/house/starwars-banner.jpg","universes/star-wars/mustafar.jpg"];
const MOVIES=["Dune: Part Two","Oppenheimer","The Batman","Spider-Man: Across the Spider-Verse","Godzilla Minus One","Mad Max: Fury Road","Interstellar","Everything Everywhere All at Once","John Wick: Chapter 4","The Lord of the Rings: The Return of the King"];
const SHOWS=["Shogun","The Bear","Fallout","The Last of Us","Succession","Andor","Severance","House of the Dragon","The Boys","True Detective"];
const ANIME=["One Piece","Jujutsu Kaisen","Attack on Titan","Fullmetal Alchemist","Naruto","Cowboy Bebop","Death Note","Demon Slayer","Vinland Saga","Monster"];
const MANGA=["One Piece","Berserk","Vinland Saga","Vagabond","Chainsaw Man","Monster","Slam Dunk","Solo Leveling","20th Century Boys","Jujutsu Kaisen"];
const COMICS=["Invincible","The Sandman","Saga","Watchmen","Sin City","Hellboy","Paper Girls","East of West","Black Hammer","Descender"];
const THEATRE_TITLES={
 movies:["Top 10","Recently Released","Top Rated","Hidden Gems","Cult Classics","Under Two Hours","Documentary Movies","Animated Movies","International Cinema","Japanese Cinema","Korean Cinema","French Cinema","2020s Movies","2010s Movies","2000s Movies","1990s Movies","1980s Movies","1970s Movies"],
 shows:["Top 10","Currently Airing","Recently Premiered","Top Rated","Hidden Gems","Cult Classics","Long-Running Series","Limited Series","Drama Series","Comedy Series","Crime and Mystery","Science Fiction and Fantasy","Documentary Series","Animated Series","Korean Drama"],
 anime:["Top 10","Trending","Airing Now","Top Airing","Upcoming Season","Top Series","Top Anime Movies","Most Popular","Top Rated","Hidden Gems","2020s Anime","2010s Anime","2000s Anime","1990s and Earlier","Action and Adventure","Romance","Slice of Life","Mecha","Fantasy","Science Fiction","Psychological","Horror and Supernatural"]
};
const MANGA_SHELVES=["Completed Series","Currently Publishing","Completed in Five Volumes or Fewer","One-shots","Manga That Began in the 1990s","Manga That Began in the 2000s","Manga That Began in the 2010s"];
const COMIC_SHELVES=[["Recently Available",true],["Complete Runs",true],["Near Complete",true],["Omnibuses",true],["Deluxe Editions",true],["Graphic Novels",true],["Community Collections",true],["Most Stocked",true],["Marvel",true],["DC",true],["Image",true],["The 2020s",false],["The 2010s",false],["Deep Shelves",false],["Fan-Made Shelf",false]];
const GENRES={
 theatre:["Action","Adventure","Comedy","Crime","Documentary","Drama","Fantasy","History","Horror","Mystery","Romance","Science Fiction","Thriller","Animation","Family"],
 manga:["Shounen","Seinen","Romance","Action","Comedy","Drama","Shoujo","Josei","Isekai","School","Magic"],
 comics:["Marvel","DC","Image","Batman","Superman","Spider-Man","X-Men","Avengers","Justice League","Star Wars"]
};
const PRESETS=[
 ["412x900","412 × 900 · phone portrait",412,900,"android"],["354x792","354 × 792 · narrow portrait",354,792,"android"],
 ["900x412","900 × 412 · wide landscape",900,412,"android"],["792x354","792 × 354 · short landscape",792,354,"android"],
 ["800x1280","800 × 1280 · tablet portrait",800,1280,"android"],["1280x800","1280 × 800 · tablet landscape",1280,800,"android"],
 ["1280x700","1280 × 700 · desktop reference",1280,700,"desktop"]];
const qs=new URLSearchParams(location.search);
const page=document.body.dataset.page||"theatre";
const initial=PRESETS.find(p=>p[0]===qs.get("vp"))||PRESETS[0];
const S={page,tab:page==="theatre"?(["movies","shows","anime"].includes(qs.get("tab"))?qs.get("tab"):"movies"):page,rail:qs.get("rail")==="open",edit:qs.get("edit")==="1",w:initial[2],h:initial[3],mode:qs.get("mode")||initial[4],topInset:Number(qs.get("topInset")||qs.get("top")||0),bottomInset:Number(qs.get("bottomInset")||qs.get("bottom")||0),scale:Number(qs.get("font")||1),names:{},hidden:new Set(),orders:{},selected:0,slide:0,editName:null};
const world=()=>S.page==="theatre"?"Theatre":"Tankoban";
const nav=["Theatre","Tankoban","Biblio","Universes","Profile"];
const worldNav=n=>`<button class="world-nav-btn ${n===world()?"active":""}" style="--nav-mask:url('${A("icons/"+ICONS[n])}')" data-world="${n}" aria-label="${n}" title="${n}"><span class="nav-symbol" aria-hidden="true"></span><span>${n}</span></button>`;
const sys=(name,path,desktop)=>`<button class="sys-button ${desktop?"desktop-icon":""}" data-action="topbar-${slug(name)}" title="${name}" aria-label="${name}"><img src="${A("icons/"+path)}" alt=""></button>`;
function bar(){
 const now=new Date(),hour=now.getHours(),clock=(hour%12||12)+":"+String(now.getMinutes()).padStart(2,"0"),date=now.toLocaleDateString("en-US",{weekday:"long",month:"long",day:"numeric"});
 return `<header class="topbar"><div class="topbar-left"><button class="back-home" data-action="home" aria-label="Home"><svg viewBox="0 0 24 24"><path d="m14 6-6 6 6 6"/></svg>Home</button>
 <div class="clock-block"><div class="clock-line"><span class="clock-time">${clock}</span><span class="clock-am">${hour>=12?"PM":"AM"}</span></div><div class="clock-date">${date}</div></div></div>
 <div class="topbar-modes">${["Tankoban","Biblio","Theatre","Universes","Feria","Vinyl"].map(n=>`<button class="mode-pill ${n===world()?"active":""}" data-world="${n}" ${["Feria","Vinyl"].includes(n)?"disabled":""}>${n}${["Feria","Vinyl"].includes(n)?' <small>SOON</small>':""}</button>`).join("")}</div>
 <div class="sys-icons">${sys("Search","search.svg")}${world()==="Theatre"?sys("Stremio Sync","stremio-official.svg"):""}${sys("Trackers","trackers.svg")}
 <button class="sys-button account" title="Account" aria-label="Account" data-action="topbar-account"><span class="account-head"></span><span class="account-body"></span></button>
 ${sys("Wallpaper","settings.svg")}${sys("Minimize","minimize.svg",true)}${sys("Fullscreen","fullscreen.svg",true)}${sys("Quit Colosseum","power.svg",true)}</div></header>`;
}
function slides(){
 return world()==="Theatre"?
 [{title:"Dune: Part Two",blurb:"Paul Atreides unites with Chani and the Fremen while war spreads across Arrakis.",art:MOVIE_ART[0]},
 {title:"Shogun",blurb:"A shipwrecked pilot, a lord's dangerous gambit, and a country on the edge of war.",art:MOVIE_ART[2]}]:
 [{title:"One Piece",blurb:"Rubber-bodied Monkey D. Luffy and his Straw Hat crew sail the seas hunting the legendary treasure that crowns the next King of the Pirates.",art:MANGA_ART[0]},
 {title:"Saga",blurb:"Alana and Marko, lovers from opposite sides of a galactic war, flee both armies to raise their newborn daughter in hiding.",art:COMIC_ART[0]}];
}
function hero(){
 const s=slides()[S.slide];
 return `<section class="featured" style="--feature-image:url('${A(s.art)}')" aria-label="Featured in ${world()}">
 <div class="kicker">Featured in ${world()}</div><h1>${esc(s.title)}</h1><p>${esc(s.blurb)}</p>
 <div class="hero-actions"><button class="primary" data-action="hero-primary">${world()==="Theatre"?"Watch":"Read"}</button><button data-action="hero-details">Details</button></div>
 <div class="hero-dots">${slides().map((x,i)=>`<button class="${i===S.slide?"active":""}" data-slide="${i}" aria-label="Featured slide ${i+1}"></button>`).join("")}</div></section>`;
}
function allRows(){
 if(S.page==="theatre"){
  const names=THEATRE_TITLES[S.tab],order=S.orders[S.tab]||names.map(slug);
  return order.map(k=>names.find(n=>slug(n)===k)).filter(Boolean)
  .filter(n=>S.edit||!S.hidden.has(S.tab+"/"+slug(n)))
  .map(n=>({key:slug(n),name:S.names[S.tab+"/"+slug(n)]||n,ranked:n==="Top 10",navigable:true,hidden:S.hidden.has(S.tab+"/"+slug(n)),labels:S.tab==="shows"?SHOWS:S.tab==="anime"?ANIME:MOVIES,arts:S.tab==="anime"?MANGA_ART:MOVIE_ART}))
  .concat([{key:"genres",name:S.tab==="movies"?"Movie Genres":S.tab==="shows"?"Show Genres":"Anime Genres",genre:true,navigable:true}]);
 }
 if(S.page==="manga")return [{key:"collection",name:"Your Collection",collection:true},
 {key:"popular",name:"Popular Manga",ranked:true,navigable:true,labels:MANGA,arts:MANGA_ART}]
 .concat(MANGA_SHELVES.map((n,i)=>({key:slug(n),name:n,navigable:true,labels:MANGA.slice(i%3).concat(MANGA.slice(0,i%3)),arts:MANGA_ART})))
 .concat([{key:"genres",name:"Explore by Genre — Manga",genre:true,navigable:true}]);
 return [{key:"collection",name:"Your Collection",collection:true},
 {key:"top",name:"Top in Tankoban — Comics",ranked:true,navigable:true,labels:COMICS,arts:COMIC_ART}]
 .concat(COMIC_SHELVES.map(([n,navigable],i)=>({key:"comics-"+i,name:n,navigable,labels:COMICS.slice(i%4).concat(COMICS.slice(0,i%4)),arts:COMIC_ART})))
 .concat([{key:"explore",name:"Explore Comics",genre:true,navigable:false}]);
}
function glyph(name){
 let path='<path d="M4 6h16M4 12h16M4 18h16"/>';
 if(/collection|shelf/.test(name.toLowerCase()))path='<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>';
 else if(/top|rated|popular/.test(name.toLowerCase()))path='<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>';
 else if(/recent|new|available|release/.test(name.toLowerCase()))path='<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>';
 else if(/explore|genre/.test(name.toLowerCase()))path='<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>';
 else if(/edition|omnibus|omnibuses|complete|run/.test(name.toLowerCase()))path='<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 7h6M9 11h6M9 15h4"/>';
 return `<svg class="rail-glyph" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${path}</svg>`;
}
function rail(rows){
 return `<aside class="index-rail" aria-label="Catalogue row index">
 <div class="rail-brand"><div class="rail-context">WORLD</div><div class="rail-world">${world()}</div></div>
 <nav class="index-links" id="indexLinks">${rows.map((r,i)=>`<button class="rail-row ${i===S.selected?"active":""}" data-go-row="${r.key}" title="${esc(r.name)}" aria-label="Jump to ${esc(r.name)}" aria-current="${i===S.selected?"true":"false"}">${glyph(r.name)}<span class="rail-label">${esc(r.name)}</span></button>`).join("")}</nav>
 <button id="railToggle" class="rail-toggle ${S.rail?"expanded":""}" aria-expanded="${S.rail}" aria-label="${S.rail?"Retract":"Expand"} row index" title="${S.rail?"Retract":"Expand"} row index"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="m6 4 4 4-4 4"/></svg></button></aside>`;
}
function heading(title,more,action){
 return `<div class="widget-header"><span class="widget-title">${esc(title)}</span>${more?`<button class="widget-more" data-action="${action}" data-title="${esc(title)}">${esc(more)} <b>›</b></button>`:""}</div>`;
}
function tile(title,art,i,{ranked=false,inset=false,rating=false}={}){
 return `<div class="poster-cell ${ranked?"ranked":""}" title="${esc(title)}">${ranked?`<span class="rank-number">${i+1}</span>`:""}
 <div class="poster-art"><img src="${A(art)}" alt="" loading="${i>4?"lazy":"eager"}">
 ${inset?`<div class="poster-caption-inset">${esc(title)}</div>`:""}
 ${rating&&i%3===0?`<div class="poster-reveal"><span class="rating"><img src="${A("icons/rating-star.svg")}" alt="">8.4</span><span class="origin">IMDb</span></div>`:""}
 <button class="poster-click" data-action="open-title" data-title="${esc(title)}" aria-label="Open ${esc(title)}"></button></div>
 ${inset?"":`<div class="poster-title">${esc(title)}</div>`}</div>`;
}
function editBar(r){
 const list=S.orders[S.tab]||THEATRE_TITLES[S.tab].map(slug),index=list.indexOf(r.key),isName=!!S.names[S.tab+"/"+r.key];
 return `<div class="edit-controls"><button data-edit="up" data-key="${r.key}" title="Move up" ${index===0?"disabled":""}>▲</button>
 <button data-edit="down" data-key="${r.key}" title="Move down" ${index===list.length-1?"disabled":""}>▼</button>
 <button data-edit="visibility" data-key="${r.key}" title="${r.hidden?"Show":"Hide"}">${r.hidden?"+":"–"}</button>
 ${S.editName===r.key?`<input class="rename-input" data-key="${r.key}" value="${esc(r.name)}" aria-label="Row name">`:`<button data-edit="rename" data-key="${r.key}">Rename</button>`}
 <button data-edit="reset" data-key="${r.key}" title="Reset name" ${!isName?"disabled":""}>↺</button></div>`;
}
function genre(r){
 const names=GENRES[S.page],arts=S.page==="theatre"?MOVIE_ART:S.page==="manga"?MANGA_ART:COMIC_ART;
 return `<section class="shelf genre" id="shelf-${r.key}" data-row="${r.key}">${heading(r.name,r.navigable?"Explore":null,"genre-index")}
 <div class="genre-grid">${names.map((n,i)=>`<button class="genre-card" data-action="genre" data-title="${esc(n)}" aria-label="Open ${esc(n)}"><img src="${A(arts[i%arts.length])}" alt=""><span class="genre-name">${esc(n)}</span></button>`).join("")}</div></section>`;
}
function collection(){
 const names=S.page==="manga"?MANGA:COMICS,arts=S.page==="manga"?MANGA_ART:COMIC_ART;
 return `<section class="shelf" id="shelf-collection" data-row="collection">${heading("Your Collection",null,null)}
 <div class="strip">${names.slice(0,7).map((n,i)=>`<div class="resume-cell"><div class="poster-art"><img src="${A(arts[i])}" alt="">
 <div class="poster-caption-inset">${esc(n)}</div><div class="resume-progress"><span style="width:${20+i*9}%"></span></div>
 <button class="poster-click" data-action="collection-entry" data-title="${esc(n)}" aria-label="Open ${esc(n)}"></button>
 <button class="resume-round" data-action="collection-entry" data-title="${esc(n)}" aria-label="Open ${esc(n)} details"><img src="${A('icons/manga.svg')}" alt=""></button></div></div>`).join("")}</div></section>`;
}
function shelf(r,i){
 if(r.genre)return genre(r);
 if(r.collection)return collection();
 const classic=S.page!=="theatre",ranked=r.ranked||S.page==="comics",more=r.navigable?(S.page==="theatre"?"See all":ranked?"Explore":"See all"):null;
 return `<section class="shelf ${classic?"classic":"gallery"} ${ranked?"ranked":""} ${r.hidden?"is-hidden":""}" id="shelf-${r.key}" data-row="${r.key}">
 ${S.page==="theatre"&&S.edit?editBar(r):""}${heading(r.name,more,"see-all")}
 <div class="strip-wrap"><button class="strip-chev left" data-shift="-1" aria-label="Show earlier items" disabled>‹</button>
 <div class="strip">${r.labels.map((n,j)=>tile(n,r.arts[(j+i)%r.arts.length],j,{ranked,inset:classic&&ranked,rating:!classic})).join("")}</div>
 <button class="strip-chev right" data-shift="1" aria-label="Show later items">›</button></div></section>`;
}
function catalogue(){
 const r=allRows();
 return `<div id="catalogue" class="catalogue-layout ${S.rail?"open":""}">${rail(r)}
 <div class="shelf-stack ${S.page!=="theatre"?"classic":""}">${S.page==="theatre"?`<div class="customize"><button data-action="customize">${S.edit?"Done":"Customize rows"}</button></div>`:""}
 ${r.map(shelf).join("")}</div></div>`;
}
function prelude(){
 const items=S.page==="theatre"?MOVIES:S.page==="manga"?MANGA:COMICS,arts=S.page==="theatre"?MOVIE_ART:S.page==="manga"?MANGA_ART:COMIC_ART;
 return `<section class="shelf prelude">${heading("Next Up","See all","prelude-nav")}<div class="strip">${items.slice(0,6).map((n,i)=>tile(n,arts[i],i,{inset:true})).join("")}</div></section>
 <section class="shelf prelude">${heading(S.page==="theatre"?"Continue Watching":"Continue Reading","See all","prelude-nav")}
 <div class="strip">${items.slice(3,9).map((n,i)=>tile(n,arts[(i+3)%arts.length],i,{inset:true})).join("")}</div></section>`;
}
function tabs(){
 const model=S.page==="theatre"?[["discover","Discover"],["movies","Movies"],["shows","Shows"],["anime","Anime"],["library","Library"]]:
 [["discover","Discover"],["manga","Manga"],["comics","Comics"],["library","Library"]];
 return `<nav class="tab-zone" id="tabZone" aria-label="${world()} tabs"><div class="tab-glass">${model.map(([k,n])=>`<button data-tab="${k}" class="${S.tab===k?"active":""}" aria-current="${S.tab===k?"page":"false"}">${n}</button>`).join("")}</div></nav>`;
}
function bench(){
 const options=PRESETS.map(p=>`<option value="${p[0]}" ${p[2]===S.w&&p[3]===S.h?"selected":""}>${p[1]}</option>`).join("");
 return `<div id="bench"><h1 class="bench-title">09 / ${S.page==="theatre"?"Theatre Catalogue":S.page==="manga"?"Tankoban Manga":"Tankoban Comics"}</h1>
 <p class="bench-note">Standalone <strong>layout study</strong>, not the QML app. Representative titles and <strong>substitute artwork from actual local Colosseum assets</strong>; not live catalogue results, ownership or progress. These settings are outside Colosseum.</p>
 <div class="bench-fields"><label>Viewport preset<select id="preset">${options}</select></label>
 <label>Width (logical px)<input type="number" id="vw" min="320" max="1600" value="${S.w}"></label>
 <label>Height (logical px)<input type="number" id="vh" min="300" max="1800" value="${S.h}"></label>
 <label>Shell<select id="mode"><option value="android" ${S.mode==="android"?"selected":""}>Android</option><option value="desktop" ${S.mode==="desktop"?"selected":""}>Desktop reference</option></select></label>
 <label>Top inset<input type="number" id="top-inset" min="0" max="90" value="${S.topInset}"></label>
 <label>Bottom inset<input type="number" id="bottom-inset" min="0" max="90" value="${S.bottomInset}"></label>
 <label>Font scale<input type="number" id="font-scale" min=".8" max="1.5" step=".05" value="${S.scale}"></label>
 <button id="railControl">${S.rail?"Retract":"Expand"} index</button>
 <span class="bench-status" id="benchStatus" aria-live="polite">Local fitting study</span></div>
 <nav class="bench-links"><a href="index.html">All studies</a><a href="theatre-catalogue.html">Theatre</a><a href="manga-catalogue.html">Manga</a><a href="comics-catalogue.html">Comics</a></nav></div>`;
}
function layout(){
 const d=document.getElementById("device");if(!d)return;
 const android=S.mode==="android",wide=android&&S.w>=840,inner=S.w-(wide?92:0),phone=Math.min(S.w,S.h)<600,portrait=phone&&S.w<S.h&&inner<600;
 const margin=inner<=430?18:inner<600?24:inner<840?32:54,top=(android?12:30)+S.topInset;
 const boardTop=top+56+(android&&inner<840?8:10),bottom=android&&!wide?72+S.bottomInset:S.bottomInset;
 d.style.width=S.w+"px";d.style.height=S.h+"px";
 for(const [k,v] of Object.entries({"--page-margin":margin+"px","--font-scale":S.scale,"--topbar-top":top+"px","--topbar-h":"56px","--board-top":boardTop+"px","--bottom-bar":bottom+"px","--scroller-h":Math.max(140,S.h-boardTop-bottom)+"px"}))d.style.setProperty(k,v);
 d.className=[android?"android":"desktop",android?(wide?"android-wide":"android-narrow"):"large-desktop",android&&inner<840?"android-compact":"",portrait?"phone-portrait":phone?"phone-landscape":inner<840?"tablet":""].filter(Boolean).join(" ");
 const b=document.getElementById("railControl");if(b)b.textContent=S.rail?"Retract index":"Expand index";
 const out=document.getElementById("benchStatus");if(out)out.textContent=`${S.w}×${S.h} · ${android?(wide?"92 px wide world rail":"72 px bottom world tabs"):"desktop capsule"} · ${phone?"phone reach":"tablet/desktop reach"} · ${S.rail?"expanded":"collapsed"} index`;
}
function render(){
 const capture=qs.get("capture")==="1";document.body.classList.toggle("capture",capture);
 document.getElementById("app").innerHTML=(capture?"":bench())+`<div id="stage"><div id="device" data-page="${S.page}">
 <div class="wallpaper"></div><aside class="world-rail" aria-label="Wide world navigation">${nav.map(worldNav).join("")}</aside>
 <div class="world-stage">${bar()}<main class="world-scroll" id="worldScroll"><div class="world-board">
 ${hero()}${prelude()}${tabs()}${catalogue()}</div></main><nav class="bottom-tabs" aria-label="Android world navigation">${nav.map(worldNav).join("")}</nav></div></div></div>`;
 layout();refreshStrips();
 requestAnimationFrame(()=>{const sc=document.getElementById("worldScroll"),bar=document.getElementById("tabZone");if(sc&&bar){sc.scrollTop=bar.offsetTop;highlight();}});
}
function report(text){
 const out=document.getElementById("benchStatus");if(out)out.textContent=text;
}
function refreshStrips(){
 document.querySelectorAll(".strip-wrap").forEach(w=>{
  const sc=w.querySelector(".strip"),left=w.querySelector(".strip-chev.left"),right=w.querySelector(".strip-chev.right");
  if(!sc||!left||!right)return;
  const upd=()=>{left.disabled=sc.scrollLeft<2;right.disabled=sc.scrollLeft>=sc.scrollWidth-sc.clientWidth-2;};
  sc.addEventListener("scroll",upd,{passive:true});upd();
 });
}
function highlight(){
 const sc=document.getElementById("worldScroll"),rows=allRows();
 if(!sc)return;
 const y=sc.getBoundingClientRect().top+110;let active=0;
 rows.forEach((r,i)=>{const el=document.getElementById("shelf-"+r.key);if(el&&el.getBoundingClientRect().top<=y)active=i;});
 S.selected=active;
 const links=document.querySelectorAll(".rail-row");
 links.forEach((link,i)=>{link.classList.toggle("active",i===active);link.setAttribute("aria-current",i===active?"true":"false");});
 const activeLink=links[active],list=document.getElementById("indexLinks");
 if(activeLink&&list&&list.scrollHeight>list.clientHeight){
  const yy=activeLink.offsetTop-list.offsetTop;
  if(yy<list.scrollTop)list.scrollTop=yy;
  else if(yy+activeLink.offsetHeight>list.scrollTop+list.clientHeight)list.scrollTop=yy+activeLink.offsetHeight-list.clientHeight;
 }
}
function redraw(){
 const c=document.getElementById("catalogue");if(!c)return;
 c.outerHTML=catalogue();layout();refreshStrips();highlight();
 if(S.editName){const input=document.querySelector(`.rename-input[data-key="${S.editName}"]`);if(input){input.focus();input.select();}}
}
function changeTab(tab){
 if(S.page==="theatre"&&["movies","shows","anime"].includes(tab)){
  S.tab=tab;S.selected=0;S.edit=false;S.editName=null;
  document.getElementById("tabZone").outerHTML=tabs();redraw();
  report(`${tab} catalogue selected locally. No network calls.`);return;
 }
 if(S.page!=="theatre"&&["manga","comics"].includes(tab)){if(tab!==S.page)location.href=tab+"-catalogue.html";return;}
 report(`${tab} is an existing route outside this assigned catalogue mock. No backend action.`);
}
function editAction(kind,key){
 const id=S.tab+"/"+key;
 if(kind==="visibility")S.hidden.has(id)?S.hidden.delete(id):S.hidden.add(id);
 if(kind==="reset")delete S.names[id];
 if(kind==="rename")S.editName=key;
 if(kind==="up"||kind==="down"){
  const arr=(S.orders[S.tab]||THEATRE_TITLES[S.tab].map(slug)).slice(),i=arr.indexOf(key),j=i+(kind==="up"?-1:1);
  if(i>=0&&j>=0&&j<arr.length){[arr[i],arr[j]]=[arr[j],arr[i]];S.orders[S.tab]=arr;}
 }
 redraw();
}
function saveName(input){
 if(!input)return;const key=input.dataset.key,id=S.tab+"/"+key,n=input.value.trim(),original=THEATRE_TITLES[S.tab].find(t=>slug(t)===key);
 if(n&&n!==original)S.names[id]=n;else delete S.names[id];
 S.editName=null;redraw();
}
function events(){
 document.addEventListener("click",e=>{
  const b=e.target.closest("button");if(!b)return;
  if(b.id==="railControl"||b.id==="railToggle"){
   S.rail=!S.rail;
   document.getElementById("catalogue").classList.toggle("open",S.rail);
   const t=document.getElementById("railToggle");t.classList.toggle("expanded",S.rail);
   t.setAttribute("aria-expanded",String(S.rail));t.setAttribute("aria-label",(S.rail?"Retract":"Expand")+" row index");
   layout();requestAnimationFrame(highlight);return;
  }
  if(b.dataset.goRow){
   const sc=document.getElementById("worldScroll"),t=document.getElementById("shelf-"+b.dataset.goRow);
   if(sc&&t)sc.scrollTo({top:sc.scrollTop+t.getBoundingClientRect().top-sc.getBoundingClientRect().top-81,behavior:"smooth"});
   report("Jump to "+b.title);return;
  }
  if(b.dataset.shift){
   const sc=b.parentElement.querySelector(".strip");
   if(sc)sc.scrollBy({left:Number(b.dataset.shift)*Math.max(130,sc.clientWidth*.82),behavior:"smooth"});return;
  }
  if(b.dataset.tab){changeTab(b.dataset.tab);return;}
  if(b.dataset.edit){editAction(b.dataset.edit,b.dataset.key);return;}
  if(b.dataset.slide!==undefined){S.slide=Number(b.dataset.slide);document.querySelector(".featured").outerHTML=hero();return;}
  if(b.dataset.world){report(b.dataset.world===world()?world()+" is already selected":"Existing "+b.dataset.world+" route outside this assignment");return;}
  switch(b.dataset.action){
   case "customize":S.edit=!S.edit;S.editName=null;redraw();return;
   case "open-title":case "collection-entry":report("Existing detail entry: "+b.dataset.title+" · no backend");return;
   case "genre":report("Existing genre route: "+b.dataset.title+" · no backend");return;
   case "genre-index":report("Existing full genre index route · outside mock");return;
   case "see-all":report("Existing See all/Discover pin: "+b.dataset.title+" · outside mock");return;
   case "prelude-nav":report("Existing See all destination · outside mock");return;
   default:if(b.dataset.action)report("Existing "+b.dataset.action+" action · not invoked");
  }
 });
 document.addEventListener("keydown",e=>{if(e.target.matches(".rename-input")){
  if(e.key==="Enter"){e.preventDefault();saveName(e.target);}
  if(e.key==="Escape"){S.editName=null;redraw();}
 }});
 document.addEventListener("focusout",e=>{if(e.target.matches(".rename-input")&&S.editName)saveName(e.target);});
 document.getElementById("worldScroll").addEventListener("scroll",highlight,{passive:true});
 if(document.body.classList.contains("capture"))return;
 document.getElementById("preset").addEventListener("change",e=>{
  const p=PRESETS.find(p=>p[0]===e.target.value);if(!p)return;
  [S.w,S.h,S.mode]=[p[2],p[3],p[4]];
  document.getElementById("vw").value=S.w;document.getElementById("vh").value=S.h;document.getElementById("mode").value=S.mode;layout();refreshStrips();
 });
 for(const [id,key,min,max] of [["vw","w",320,1600],["vh","h",300,1800],["top-inset","topInset",0,90],["bottom-inset","bottomInset",0,90],["font-scale","scale",.8,1.5]]){
  document.getElementById(id).addEventListener("change",e=>{S[key]=Math.min(max,Math.max(min,Number(e.target.value)||min));layout();refreshStrips();});
 }
 document.getElementById("mode").addEventListener("change",e=>{S.mode=e.target.value;layout();});
}
render();events();
