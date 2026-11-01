(function(){
"use strict";
var AS="../../reference/desktop/assets/";
function asset(path){return AS+path;}
function safe(x){return String(x==null?"":x).replace(/[&<>"']/g,function(c){return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c];});}
function im(src,cls,alt){return '<img class="'+(cls||"")+'" src="'+safe(asset(src))+'" alt="'+safe(alt||"")+'">';}
function t(title,art,blurb,ghost,c1,c2,kind){return {title:title,art:art,blurb:blurb,ghost:ghost||"",c1:c1||"#181820",c2:c2||"#0a0c12",kind:kind||""};}
function item(id,title,art,sub,kind,progress,watched){return {id:id,title:title,art:art,sub:sub,kind:kind||"video",progress:progress==null?0:progress,watched:!!watched};}
var ip="universes/one-piece/east-blue/banners/";
var ut="universes/";
var wall="wallpaper/sagas/";
var D={
 theatre:{
  name:"Theatre",tabs:["Discover","Movies","Shows","Anime","Library"],
  slides:[
   t("Dune: Part Two",wall+"dune.jpg","Paul Atreides unites with Chani and the Fremen while war spreads across Arrakis.","T","#5d4633","#18110c"),
   t("Shogun",wall+"ice-and-fire.jpg","A shipwrecked pilot, a lord's dangerous gambit, and a country on the edge of war.","S","#4c2f2a","#160d0b"),
   t("The Bear",wall+"the-witcher.jpg","A kitchen turns into family, pressure, grief, and service at full speed.","S","#33445d","#0c1118"),
   t("Fallout",wall+"jurassic-park.jpg","Vaults, wasteland, and old-world power plays collide above ground.","S","#3f5640","#111b12")
  ],
  next:[item("t-next-shogun","Shogun",wall+"ice-and-fire.jpg","Next episode","video",0)],
  continuing:[
   item("t-dune","Dune: Part Two",wall+"dune.jpg","1h 12m remaining","video",.62),
   item("t-fallout","Fallout",wall+"jurassic-park.jpg","S1 · E3","video",.32),
   item("t-bear","The Bear",wall+"the-witcher.jpg","S2 · E6","video",.73)
  ],
  ranked:[
   t("Dune: Part Two",wall+"dune.jpg",""),t("Oppenheimer",wall+"ice-and-fire.jpg",""),
   t("The Batman",ut+"dcau/portals/gotham.jpg",""),t("Spider-Man: Across the Spider-Verse",ut+"marks/marvel.png",""),
   t("Godzilla Minus One",wall+"jurassic-park.jpg",""),t("Mad Max: Fury Road",wall+"dune.jpg",""),
   t("Interstellar",wall+"ice-and-fire.jpg",""),t("Everything Everywhere All at Once",wall+"sherlock-holmes.jpg",""),
   t("John Wick: Chapter 4",wall+"the-witcher.jpg",""),t("The Lord of the Rings: The Return of the King",wall+"lord-of-the-rings.jpg","")
  ]
 },
 tankoban:{
  name:"Tankoban",tabs:["Discover","Manga","Comics","Library"],
  slides:[
   t("One Piece",ip+"romance.png","Rubber-bodied Monkey D. Luffy and his Straw Hat crew sail the seas hunting the legendary treasure that crowns the next King of the Pirates.","M","#7a2f49","#1d121b"),
   t("Saga","extensions/house/cosmere.jpg","Alana and Marko, lovers from opposite sides of a galactic war, flee both armies to raise their newborn daughter in hiding.","C","#7a4a2f","#241813","poster"),
   t("Berserk",wall+"the-witcher.jpg","Guts, a lone swordsman in a brutal dark-fantasy world, hunts the demons who damned his comrades — and the friend who betrayed them.","M","#5a2f3f","#180f14"),
   t("Invincible",ut+"dcau/portals/watchtower.jpg","Mark Grayson inherits superpowers from his father, Earth's greatest hero — then learns the man is an alien sent to conquer the planet.","C","#5a3f2f","#1a120b","poster")
  ],
  next:[item("m-next-piece","One Piece",ip+"baratie.png","Next chapter","manga",0)],
  continuing:[
   item("m-piece","One Piece",ip+"romance.png","Chapter 1089","manga",.72),
   item("m-saga","Saga","extensions/house/cosmere.jpg","Vol. 1 · Page 34","comic",.49),
   item("m-berserk","Berserk",wall+"the-witcher.jpg","Chapter 27","manga",.23)
  ],
  ranked:[
   t("One Piece",ip+"romance.png",""),t("Berserk",wall+"the-witcher.jpg",""),
   t("Vinland Saga",wall+"ice-and-fire.jpg",""),t("Vagabond",wall+"sherlock-holmes.jpg",""),
   t("Chainsaw Man",ut+"one-piece/paradise/banners/impel-down.png",""),t("Monster",ut+"dcau/portals/gotham.jpg",""),
   t("Slam Dunk",wall+"jurassic-park.jpg",""),t("Solo Leveling",ut+"one-piece/new-world/banners/wano-country.png",""),
   t("20th Century Boys",wall+"percy-jackson.jpg",""),t("Jujutsu Kaisen",ut+"one-piece/paradise/banners/enies-lobby.png","")
  ],
  comics:[
   t("Invincible",ut+"dcau/portals/watchtower.jpg",""),t("The Sandman","extensions/house/cosmere.jpg",""),
   t("Saga","extensions/house/cosmere.jpg",""),t("Watchmen",ut+"dcau/portals/gotham.jpg",""),
   t("Sin City",ut+"dcau/portals/metropolis.jpg",""),t("Hellboy",wall+"the-witcher.jpg",""),
   t("Paper Girls",wall+"percy-jackson.jpg",""),t("East of West",wall+"dune.jpg",""),
   t("Black Hammer",wall+"the-witcher.jpg",""),t("Descender","extensions/house/starwars-banner.jpg","")
  ]
 },
 universes:{
  name:"Universes",tabs:[],slides:[
   t("One Piece",ip+"romance.png","One Piece is a Japanese manga series written and illustrated by Eiichiro Oda. It follows the adventures of Monkey D. Luffy and his crew, the Straw Hats, as he searches for the legendary treasure known as the One Piece to become the next King of the Pirates.","","#14161d","#0c0e11"),
   t("Star Wars","extensions/house/starwars-banner.jpg","A galaxy far, far away — the nine-episode Skywalker Saga, the standalone stories, and the age of The Mandalorian.","","#14161d","#0c0e11"),
   t("DC Animated Universe","extensions/house/dcau.jpg","The Timmverse — one continuous animated world, from the noir rooftops of Gotham to the neon future of Neo-Gotham.","","#14161d","#0c0e11"),
   t("Cosmere","extensions/house/cosmere.jpg","","","#14161d","#0c0e11")
  ],
  roster:[
   {name:"DC Animated Universe",art:"extensions/house/dcau.jpg",logo:ut+"marks/dc.png",blurb:"The Timmverse — one continuous animated world, from the noir rooftops of Gotham to the neon future of Neo-Gotham.",chips:["Series","Films"],facets:["series"]},
   {name:"Harry Potter",art:wall+"harry-potter.jpg",logo:ut+"marks/harry-potter.png",blurb:"",chips:["Books","Films","Series"],facets:["books","films"]},
   {name:"Naruto",art:ut+"one-piece/new-world/banners/wano-country.png",logo:ut+"marks/naruto.svg",blurb:"Naruto is a Japanese manga series written and illustrated by Masashi Kishimoto. It tells the story of Naruto Uzumaki, a young, socially isolated ninja who seeks recognition from his peers and dreams of becoming the Hokage, the leader of his village.",chips:["Manga","Anime","Films"],facets:["manga","anime"]},
   {name:"One Piece",art:ut+"one-piece/east-blue/banners/arlong.png",logo:ut+"marks/one-piece.png",blurb:"One Piece is a Japanese manga series written and illustrated by Eiichiro Oda. It follows the adventures of Monkey D. Luffy and his crew, the Straw Hats, as he searches for the legendary treasure known as the One Piece to become the next King of the Pirates.",chips:["Manga","Anime","Films","Series"],facets:["manga","anime"]},
   {name:"Star Wars",art:"extensions/house/starwars-banner.jpg",logo:ut+"marks/star-wars.png",blurb:"A galaxy far, far away — the nine-episode Skywalker Saga, the standalone stories, and the age of The Mandalorian.",chips:["Films","Series","Books","Comics"],facets:["films","series"]}
  ]
 }
};
var world=document.body.getAttribute("data-world")||"theatre";
var capture=new URLSearchParams(location.search).get("capture")==="1";
if(capture)document.body.classList.add("capture");
var presetData={
 portrait:{w:412,h:900,mode:"phone"},
 landscape:{w:900,h:412,mode:"phone"},
 compact:{w:354,h:792,mode:"phone"},
 compactLandscape:{w:792,h:354,mode:"phone"},
 desktop:{w:1280,h:700,mode:"desktop"},
 tabletPortrait:{w:800,h:1280,mode:"tablet"},
 tabletLandscape:{w:1280,h:800,mode:"tablet"}
};
var s={w:412,h:900,mode:"phone",top:0,bottom:0,left:0,right:0,scale:1,slide:0,tab:world==="universes"?"all":"Discover",facet:"all",subType:world==="tankoban"?"Manga":"Movies",category:"Popular",filter:"All",picker:"",sourceOpen:false,indexOpen:false,contextId:null,removed:[],hideProgress:false,longTitles:false};
if(capture){
 s.w=window.innerWidth;s.h=window.innerHeight;
 s.mode=(Math.min(s.w,s.h)<600)?"phone":s.w>=1100&&s.h<=750?"desktop":"tablet";
}
var stage=document.getElementById("stage");
var appHost=document.getElementById("app");
function currentMode(){
 if(s.mode==="desktop")return "desktop";
 if(s.mode==="phone"||Math.min(s.w,s.h)<600)return "phone";
 return "tablet";
}
function bottomNav(){return currentMode()!=="desktop" && s.w<840 || currentMode()==="phone";}
function wideNav(){return currentMode()==="tablet" && s.w>=840;}
function msg(txt){var target=document.getElementById("reviewActivity");if(target)target.textContent="Mock action: "+txt;}
function icon(src,name,action){
 return '<button class="icon-action" type="button" title="'+safe(name)+'" aria-label="'+safe(name)+'" data-action="'+safe(action||"shell")+'" data-value="'+safe(name)+'">'+im("icons/"+src,"",name)+'</button>';
}
function topbar(){
 var now=new Date(),h=now.getHours(),minute=now.getMinutes(),hh=h%12||12,amp=h>=12?"PM":"AM",mon=now.toLocaleDateString("en-US",{weekday:"long",month:"long",day:"numeric"});
 var clock='<div class="clock"><div class="clock-inline"><span class="clock-big">'+hh+':'+String(minute).padStart(2,"0")+'</span><span class="clock-ampm">'+amp+'</span></div><div class="clock-date">'+safe(mon)+'</div></div>';
 var back=currentMode()==="desktop"?'<button type="button" class="back-to-home" data-action="shell" data-value="Home">‹ Home</button>':"";
 var left='<div class="left-chrome">'+back+clock+'</div>';
 var opts=["Tankoban","Biblio","Theatre","Universes","Feria","Vinyl"];
 var pills=currentMode()==="desktop"?'<div class="world-pills">'+opts.map(function(o){
  var coming=o==="Feria"||o==="Vinyl";return '<button type="button" class="world-pill'+(D[world].name===o?" active":"")+(coming?" soon":"")+'" data-action="'+(coming?"noop":"world")+'" data-value="'+safe(o)+'">'+safe(o)+(coming?'<span class="soon-tag">SOON</span>':"")+'</button>';
 }).join("")+'</div>':"";
 var isPhone=currentMode()!=="desktop";
 var right='<div class="action-row">';
 if(isPhone||world!=="universes")right+=icon("search.svg","Search","shell");
 if(world==="theatre")right+=icon("stremio-official.svg","Stremio Sync","shell");
 right+=icon("trackers.svg","Trackers","shell");
 right+='<button type="button" class="icon-action" aria-label="Account" title="Account" data-action="shell" data-value="Account"><span class="account-medal" aria-hidden="true"><i></i></span></button>';
 right+=icon("settings.svg","Wallpaper","shell");
 if(!isPhone){right+=icon("minimize.svg","Minimize","shell")+icon("fullscreen.svg","Enter fullscreen","shell")+icon("power.svg","Quit Colosseum","shell");}
 return '<header class="topbar">'+left+pills+right+'</div></header>';
}
var worlds=[
 ["Theatre","projector-theatre.svg","theatre-home.html"],
 ["Tankoban","comic-book.svg","tankoban-home.html"],
 ["Biblio","book-library.svg",""],
 ["Universes","universes-atom.svg","universes-home.html"],
 ["Profile","your-colosseum-laurel.svg",""]
];
function worldNav(wide){
 var klass=wide?"wide-world-nav":"bottom-world-nav";
 return '<nav class="'+klass+'" aria-label="Worlds">'+worlds.map(function(e){
 var active=e[0]===D[world].name;
 return '<button class="'+(active?"active":"")+'" type="button" data-action="world" data-value="'+e[0]+'" aria-current="'+(active?"page":"false")+'"><span class="tab-glyph" aria-hidden="true">'+im("icons/"+e[1],"","")+'</span><span>'+safe(e[0])+'</span></button>';
 }).join("")+'</nav>';
}
function hero(){
 var slides=D[world].slides,slide=slides[s.slide%slides.length],phone=currentMode()==="phone";
 var kind=slide.kind==="poster" && !phone?" inset":"";
 var second=world==="universes"?(slide.title==="Cosmere"?"":"Browse universe"):"Details";
 var first=world==="theatre"?"Watch":world==="tankoban"?"Read":"Enter tribute";
 var kicker=world==="theatre"?"Featured in Theatre":world==="tankoban"?"Featured in Tankoban":"Tribute";
 var actions='<div class="hero-actions"><button type="button" class="hero-action primary" data-action="hero-primary">'+first+'</button>';
 if(second)actions+='<button type="button" class="hero-action" data-action="hero-secondary">'+second+'</button>';
 actions+='</div>';
 var dots='<div class="carousel-indicators" role="group" aria-label="Featured slides">'+slides.map(function(d,i){return '<button type="button" aria-label="Slide '+(i+1)+': '+safe(d.title)+'" class="carousel-dot'+(s.slide===i?" active":"")+'" data-action="slide" data-index="'+i+'"></button>';}).join("")+'</div>';
 return '<section class="section featured" tabindex="0" aria-label="'+safe(kicker)+'" data-hero="true" style="--hero-c1:'+safe(slide.c1)+';--hero-c2:'+safe(slide.c2)+'">'+im(slide.art,"hero-art"+kind,slide.title)+'<div class="hero-shade"></div><div class="hero-ghost" aria-hidden="true">'+safe(slide.ghost)+'</div><div class="hero-copy"><span class="hero-kicker">'+safe(kicker)+'</span><h1 class="hero-title">'+safe(s.longTitles?(world==="theatre"?"The Lord of the Rings: The Return of the King":world==="tankoban"?"One Piece: The Story of Monkey D. Luffy and the Straw Hat Pirates":slide.title):slide.title)+'</h1>'+(slide.blurb?'<p class="hero-blurb">'+safe(slide.blurb)+'</p>':"")+actions+'</div>'+dots+'</section>';
}
function widgetHead(title,verb,action){
 return '<div class="widget-head"><h2>'+safe(title)+'</h2>'+(verb?'<button type="button" data-action="'+safe(action||"see-all")+'" data-value="'+safe(title)+'">'+safe(verb)+' <span>›</span></button>':"")+'</div>';
}
function continueCard(d){
 var context=s.contextId===d.id;
 var inContext=context?'<div class="tile-context" role="menu"><button data-action="tile-detail" data-value="'+safe(d.id)+'">Open details</button><button data-action="tile-resume" data-value="'+safe(d.id)+'">Resume</button><button data-action="tile-remove" data-value="'+safe(d.id)+'">Remove</button></div>':"";
 return '<article class="continue-tile'+(context?" touch-focus":"")+'" role="button" tabindex="0" aria-label="Open details for '+safe(d.title)+'" data-action="tile-detail" data-value="'+safe(d.id)+'" data-tile="'+safe(d.id)+'">'
 +im(d.art,"tile-art",d.title)+'<span class="tile-badge">'+safe(d.kind==="manga"?"MANGA":d.kind==="comic"?"COMIC":"VIDEO")+'</span>'
 +(d.watched?'<span class="tile-watched">✓</span>':"")
 +'<div class="tile-copy"><strong>'+safe(d.title)+'</strong><small>'+safe(d.sub)+'</small></div>'
 +'<div class="tile-progress"><span style="width:'+Math.round(d.progress*100)+'%"></span></div>'
 +'<button type="button" class="resume-circle" aria-label="Resume '+safe(d.title)+'" data-action="tile-resume" data-value="'+safe(d.id)+'">'+im("icons/"+(d.kind==="video"?"play.svg":"manga.svg"),"", "Resume")+'</button>'
 +'<button type="button" class="tile-remove" aria-label="Remove '+safe(d.title)+' from Continue" data-action="tile-remove" data-value="'+safe(d.id)+'">✕</button>'+inContext+'</article>';
}
function continueRow(title,all){
 var shown=all.filter(function(d){return s.removed.indexOf(d.id)<0;});
 if(!shown.length||s.hideProgress)return "";
 return '<section class="section continue-section" data-section="'+safe(title)+'" aria-label="'+safe(title)+'">'+widgetHead(title,title==="Your Collection"?"":"See all","see-all")+'<div class="poster-rail" role="list" aria-label="'+safe(title)+'">'+shown.map(continueCard).join("")+'</div></section>';
}
function poster(d){
 return '<button class="poster-card" type="button" data-action="poster" data-value="'+safe(d.title)+'">'+im(d.art,"",d.title)+'<span class="caption">'+safe(d.title)+'</span></button>';
}
function posterRail(title,items){
 return '<section class="section" data-section="'+safe(title)+'">'+widgetHead(title,"See all","see-all")+'<div class="poster-rail">'+items.map(poster).join("")+'</div></section>';
}
function rankRail(title,items){
 var cards=items.map(function(d,i){return '<div class="rank-card"><span class="huge-rank" aria-hidden="true">'+(i+1)+'</span>'+poster(d)+'</div>';}).join("");
 return '<section class="section" data-section="'+safe(title)+'">'+widgetHead(title,"Explore","see-all")+'<div class="rank-stage"><div class="rank-strip" tabindex="0" aria-label="'+safe(title)+'" id="rankStrip">'+cards+'</div><button class="rank-arrow previous" aria-label="Show earlier items" data-action="rank-previous">‹</button><button class="rank-arrow following" aria-label="Show later items" data-action="rank-next">›</button></div></section>';
}
function worldTabs(tabs,barKind){
 var isFacet=barKind==="facets",active=isFacet?s.facet:s.tab;
 return '<nav class="world-tabs-wrap'+(s.docked?" docked":"")+'" aria-label="'+(isFacet?"Universe filters":"World categories")+'" id="worldTabs"><div class="glass-tabs" style="--tab-count:'+tabs.length+'">'+tabs.map(function(tab){
  var label=typeof tab==="string"?tab:tab.label;var key=isFacet?tab.key:label;
  return '<button class="world-tab'+(active===key?" active":"")+'" type="button" data-action="'+(isFacet?"facet":"tab")+'" data-value="'+safe(key)+'" aria-current="'+(active===key?"page":"false")+'">'+safe(label)+'</button>';
 }).join("")+'</div></nav>';
}
function discovery(){
 var typeTabs=world==="theatre"?["Movies","Shows","Anime"]:["Manga","Manhwa","Comics"];
 var categories=["Popular","Top Rated","Recently Added"];
 var catalog=s.sourceSelected||(world==="theatre"?"IMDb":"MAL");
 var titles=world==="theatre"?D.theatre.ranked:D.tankoban.ranked;
 if(s.subType==="Comics")titles=D.tankoban.comics;
 if(s.subType==="Shows")titles=[D.theatre.slides[1],D.theatre.slides[2],D.theatre.slides[3]].concat(D.theatre.ranked.slice(0,3));
 var rails=world==="theatre"?[["IMDb","addon-logos/omdb.png"],["Stremio","icons/stremio-official.svg"]]:[["MAL","addon-logos/jikan.png"],["GetComics","addon-logos/getcomics.png"],["WeebCentral","addon-logos/weebcentral.png"]];
 var rail='<aside class="source-rail'+(s.sourceOpen?" open":"")+'" aria-label="Source sidebar"><button class="rail-toggle" aria-label="'+(s.sourceOpen?"Collapse sources":"Expand sources")+'" data-action="source-toggle">'+(s.sourceOpen?"‹":"›")+'</button><div class="rail-brand"><span>WORLD</span><strong>'+safe(D[world].name)+'</strong></div>'+rails.map(function(e,i){return '<button class="rail-item'+(e[0]===catalog?" active":"")+'" data-action="source" data-value="'+safe(e[0])+'">'+im(e[1],"",e[0])+'<span>'+safe(e[0])+'</span></button>';}).join("")+'</aside>';
 var types='<div class="type-switch">'+typeTabs.map(function(x){return '<button class="type-tab'+(s.subType===x?" active":"")+'" data-action="type" data-value="'+safe(x)+'">'+safe(x)+'</button>';}).join("")+'</div>';
 var cMenu=s.picker==="category"?'<div class="picker-menu" role="menu">'+categories.map(function(c){return '<button class="'+(c===s.category?"current":"")+'" data-action="category" data-value="'+safe(c)+'">'+safe(c)+'</button>';}).join("")+'</div>':"";
 var summary='<div class="browse-summary"><span class="kicker">NOW BROWSING</span><div class="popup-anchor"><button class="catalog-button" data-action="picker" data-value="category">'+safe(s.category)+' <small>▾</small></button>'+cMenu+'</div><span class="byline">'+safe(catalog)+'</span></div>';
 var fopts=["All","Action","Adventure","Drama","Fantasy","Romance","Sci-Fi"];
 var fMenu=s.picker==="filter"?'<div class="picker-menu" role="menu">'+fopts.map(function(c){return '<button class="'+(c===s.filter?"current":"")+'" data-action="filter-option" data-value="'+safe(c)+'">'+safe(c)+'</button>';}).join("")+'</div>':"";
 var filter='<div class="filter-row"><span>FILTER</span><div class="popup-anchor"><button class="filter-trigger" data-action="picker" data-value="filter">Genre · '+safe(s.filter)+' &nbsp;▾</button>'+fMenu+'</div></div>';
 return '<section class="section discover'+(s.sourceOpen?" source-expanded":"")+'">'+rail+'<div class="discover-main"><div class="discover-masthead">'+types+summary+'</div>'+filter+'<div class="poster-wall">'+titles.map(poster).join("")+'</div></div></section>';
}
function rowIcon(label){
 var title=label.toLowerCase(),paths='<path d="M4 6h16M4 12h16M4 18h16"/>';
 if(/collection|library|shelf|shelves/.test(title))paths='<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>';
 else if(/top|rated|popular|best/.test(title))paths='<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>';
 else if(/recent|new|available|release|continue/.test(title))paths='<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>';
 else if(/explore|discover|genre/.test(title))paths='<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>';
 else if(/edition|omnibus|omnibuses|complete|run/.test(title))paths='<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 7h6M9 11h6M9 15h4"/>';
 return '<svg class="rail-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+paths+'</svg>';
}
function rowIndex(labels){
 return '<aside class="index-rail'+(s.indexOpen?" open":"")+'"><div class="rail-brand"><span>WORLD</span><strong>'+safe(D[world].name)+'</strong></div>'+labels.map(function(l,i){return '<button class="row-index-entry'+(i===0?" active":"")+'" type="button" data-action="jump" data-value="'+safe(l)+'">'+rowIcon(l)+'<span>'+safe(l)+'</span></button>';}).join("")+'<button class="index-toggle" type="button" data-action="index-toggle" aria-label="'+(s.indexOpen?"Collapse row index":"Expand row index")+'">'+(s.indexOpen?"‹":"›")+'</button></aside>';
}
function genreRow(label){
 var genres=world==="theatre"?["In Theaters","Prestige TV","Sci-Fi","Action","Drama","Animation","Fantasy","Horror","Crime","Anime"]:["Shounen","Seinen","Romance","Action","Comedy","Drama","Shoujo","Josei","Isekai","School","Magic"];
 var art=world==="theatre"?D.theatre.ranked:D.tankoban.ranked;
 return '<section class="section" data-section="'+safe(label)+'">'+widgetHead(label,"Explore","see-all")+'<div class="poster-rail">'+genres.map(function(g,i){var d=art[i%art.length];return '<button class="genre-cell" data-action="genre" data-value="'+safe(g)+'">'+im(d.art,"",g)+'<span>'+safe(g)+'</span></button>';}).join("")+'</div></section>';
}
function catalogue(){
 var active=s.tab;
 if(active==="Discover")return discovery();
 if(active==="Library"){
  var label="Your Collection";
  return '<div class="row-index-shell'+(s.indexOpen?" expanded":"")+'">'+rowIndex([label,"Continue"])+'<div class="row-index-main">'+posterRail(label,(world==="theatre"?D.theatre.ranked:D.tankoban.ranked).slice(0,7))+'</div></div>';
 }
 var labels=world==="theatre"?["Top 10 This Week","Popular","Explore by Genre"]:["Your Collection","Top in Tankoban — "+active,"Explore by Genre — "+active];
 var ranked=(world==="theatre"?D.theatre.ranked:active==="Comics"?D.tankoban.comics:D.tankoban.ranked);
 if(world==="theatre"&&active==="Shows")ranked=D.theatre.slides.slice(1).concat(D.theatre.ranked);
 var top=world==="tankoban"?(active==="Comics"?"Top in Tankoban — Comics":"Top in Tankoban — Manga"):"Top 10 This Week";
 var content='';
 if(world==="tankoban")content+=continueRow("Your Collection",D.tankoban.continuing.slice(0,2));
 content+=rankRail(top,ranked.slice(0,10));
 content+=posterRail("Popular",ranked.slice(1));
 content+=genreRow(world==="theatre"?"Explore by Genre":"Explore by Genre — "+active);
 return '<div class="row-index-shell'+(s.indexOpen?" expanded":"")+'">'+rowIndex(labels)+'<div class="row-index-main">'+content+'</div></div>';
}
function universeSection(){
 var facets=[{key:"all",label:"All"},{key:"books",label:"Books"},{key:"manga",label:"Manga"},{key:"anime",label:"Anime"},{key:"films",label:"Films"},{key:"series",label:"Series"},{key:"comics",label:"Comics"}];
 facets=facets.filter(function(f){return f.key==="all"||D.universes.roster.some(function(d){return d.facets.indexOf(f.key)>=0;});});
 var shown=D.universes.roster.filter(function(d){return s.facet==="all"||d.facets.indexOf(s.facet)>=0;});
 var head='<div class="universes-heading"><h2>Explore Universes</h2><span>'+shown.length+' '+(shown.length===1?"universe":"universes")+'</span></div>';
 var banners=shown.length?shown.map(function(d){
  return '<article class="universe-banner" role="button" tabindex="0" data-action="universe" data-value="'+safe(d.name)+'">'+im(d.art,"banner-art",d.name)+'<div class="banner-wash"></div><div class="banner-mark">'+im(d.logo,"",d.name+" mark")+'</div><div class="banner-copy"><h3>'+safe(d.name)+'</h3>'+(d.blurb?'<p>'+safe(d.blurb)+'</p>':"")+'<small>'+safe(d.chips.join("  ·  "))+'</small></div><button class="banner-enter" data-action="universe" data-value="'+safe(d.name)+'">Explore ›</button></article>';
 }).join(""):'<p style="color:var(--dim)">No universes hold this yet.</p>';
 return '<section class="section">'+head+worldTabs(facets,"facets")+'<div class="universe-list">'+banners+'</div></section>';
}
function board(){
 var out=hero();
 if(world==="universes")return out+universeSection();
 out+=continueRow("Next Up",D[world].next);
 out+=continueRow(world==="theatre"?"Continue Watching":"Continue Reading",D[world].continuing);
 out+=worldTabs(D[world].tabs,"world");
 out+=catalogue();
 return out;
}
function configure(){
 if(!stage||!appHost)return;
 var mode=currentMode(),wide=wideNav(),bottom=bottomNav(),leftRail=wide?92:0;
 var margin=mode==="desktop"?54:wide?32:mode==="tablet"?32:(s.w<=430?18:24);
 var top=mode==="desktop"?30:12;top+=s.top;
 var navHeight=bottom?(72+s.bottom):0;
 var heroHeight=mode==="desktop"?340:mode==="tablet"?300:260;
 var gap=mode==="desktop"?36:mode==="tablet"?28:22;
 stage.style.width=s.w+"px";stage.style.height=s.h+"px";
 var cls="app mode-"+mode+(s.h<530?" short-viewport":"")+(s.scale>1.05?" font-large":"");
 var styles="--page-margin:"+(margin+s.left)+"px;--right-margin:"+(margin+s.right)+"px;--left-rail:"+leftRail+"px;--top-inset:"+top+"px;--content-top:"+(top+56+8)+"px;--nav-height:"+navHeight+"px;--safe-bottom:"+s.bottom+"px;--safe-left:"+s.left+"px;--safe-right:"+s.right+"px;--hero-h:"+heroHeight+"px;--section-gap:"+gap+"px;--font-scale:"+s.scale+";";
 var oldScroll=document.querySelector(".world-scroll");
 var keep=s.keepScroll&&oldScroll?oldScroll.scrollTop:0;
 appHost.innerHTML='<div class="'+cls+'" style="'+styles+'"><div class="app-wall"></div>'+topbar()+'<main class="world-scroll" id="worldScroll"><div class="board">'+board()+'</div></main>'+(bottom?worldNav(false):"")+(wide?worldNav(true):"")+'</div>';
 var sc=document.querySelector("#worldScroll");
 sc.scrollTop=keep;
 sc.addEventListener("scroll",function(){
  var tabs=sc.querySelector("#worldTabs");if(!tabs)return;
  var isDocked=tabs.getBoundingClientRect().top<=sc.getBoundingClientRect().top+1 && sc.scrollTop>0;
  if(s.docked!==isDocked){s.docked=isDocked;tabs.classList.toggle("docked",isDocked);}
 },{passive:true});
 s.keepScroll=true;
 syncReview();
}
function findItem(id){
 return D[world].next.concat(D[world].continuing).find(function(x){return x.id===id;});
}
function selectWorld(name){
 var mapping={Theatre:"theatre-home.html",Tankoban:"tankoban-home.html",Universes:"universes-home.html"};
 if(mapping[name]){
  if(D[world].name===name){msg(name+" is active");return;}
  location.href=mapping[name]+(capture?"?capture=1":"");return;
 }
 msg(name+" route outside this three-world mock assignment");
}
document.addEventListener("click",function(ev){
 var button=ev.target.closest("[data-action]");
 if(!button||!appHost.contains(button))return;
 var action=button.dataset.action,val=button.dataset.value||"",idx=Number(button.dataset.index||0);
 ev.preventDefault();ev.stopPropagation();
 if(action==="world"){selectWorld(val);return;}
 if(action==="noop")return;
 if(action==="shell"){msg(val+" — existing shell route; backend not invoked");return;}
 if(action==="slide"){s.slide=idx;configure();return;}
 if(action==="hero-primary"||action==="hero-secondary"){
  var d=D[world].slides[s.slide];
  msg((action==="hero-primary"?"Primary":"Secondary")+" action for "+d.title+" (no provider call)");
  return;
 }
 if(action==="tile-resume"||action==="tile-detail"||action==="tile-remove"){
  var obj=findItem(val);
  if(action==="tile-remove"){s.removed.push(val);s.contextId=null;configure();}
  msg((action==="tile-remove"?"Remove":action==="tile-resume"?"Resume":"Details")+" "+(obj?obj.title:val)+" — local fixture");return;
 }
 if(action==="tab"){s.tab=val;s.contextId=null;s.picker="";s.indexOpen=false;s.keepScroll=true;configure();msg("Selected "+val+" tab");return;}
 if(action==="facet"){s.facet=val;configure();msg("Universes filter: "+val);return;}
 if(action==="picker"){s.picker=s.picker===val?"":val;configure();return;}
 if(action==="category"){s.category=val;s.picker="";configure();return;}
 if(action==="filter-option"){s.filter=val;s.picker="";configure();return;}
 if(action==="type"){s.subType=val;configure();return;}
 if(action==="source-toggle"){s.sourceOpen=!s.sourceOpen;configure();return;}
 if(action==="source"){s.sourceSelected=val;s.sourceOpen=false;s.category="Popular";configure();msg("Source "+val+" selected (illustrative catalogue only)");return;}
 if(action==="index-toggle"){s.indexOpen=!s.indexOpen;configure();return;}
 if(action==="jump"){s.indexOpen=false;configure();var target=Array.from(document.querySelectorAll("[data-section]")).find(function(x){return x.dataset.section===val;});var view=document.getElementById("worldScroll");if(target&&view)view.scrollTo({top:view.scrollTop+target.getBoundingClientRect().top-view.getBoundingClientRect().top-12,behavior:"smooth"});msg("Row index: "+val);return;}
 if(action==="rank-next"||action==="rank-previous"){
  var strip=document.getElementById("rankStrip");if(strip)strip.scrollBy({left:action==="rank-next"?260:-260,behavior:"smooth"});return;
 }
 if(action==="poster"||action==="universe"||action==="genre"||action==="see-all"){msg(val+" — original navigation intent, mock-only");return;}
});
document.addEventListener("contextmenu",function(ev){
 var tile=ev.target.closest(".continue-tile");if(!tile||!appHost.contains(tile))return;
 ev.preventDefault();s.contextId=tile.dataset.tile;configure();
});
document.addEventListener("keydown",function(ev){
 if(ev.key==="Escape"&&(s.picker||s.contextId||s.sourceOpen||s.indexOpen)){
  s.picker="";s.contextId=null;s.sourceOpen=false;s.indexOpen=false;configure();return;
 }
 var heroElement=ev.target.closest(".featured");
 if(heroElement&&(ev.key==="ArrowRight"||ev.key==="ArrowLeft")){
  s.slide=(s.slide+(ev.key==="ArrowRight"?1:-1)+D[world].slides.length)%D[world].slides.length;
  configure();ev.preventDefault();return;
 }
 if(ev.key==="Enter"||ev.key===" "){
  var tile=ev.target.closest(".continue-tile");
  if(tile&&ev.target===tile){ev.preventDefault();msg("Details "+(findItem(tile.dataset.tile)||{}).title+" — mock-only");}
  var uni=ev.target.closest(".universe-banner");
  if(uni&&ev.target===uni){ev.preventDefault();msg("Enter "+uni.dataset.value+" — mock-only");}
 }
});
var downX=null;
document.addEventListener("pointerdown",function(ev){
 if(ev.target.closest(".featured")&&!ev.target.closest("button"))downX=ev.clientX;
});
document.addEventListener("pointerup",function(ev){
 if(downX===null)return;var diff=ev.clientX-downX;downX=null;
 if(Math.abs(diff)<45)return;
 s.slide=(s.slide+(diff<0?1:-1)+D[world].slides.length)%D[world].slides.length;configure();
});
function setPreset(key){
 var p=presetData[key];if(!p)return;
 s.w=p.w;s.h=p.h;s.mode=p.mode;s.slide=0;s.keepScroll=false;s.docked=false;
 configure();
}
function syncReview(){
 var ids={width:s.w,height:s.h,insetTop:s.top,insetBottom:s.bottom,insetLeft:s.left,insetRight:s.right,fontScale:s.scale};
 Object.keys(ids).forEach(function(k){var elm=document.getElementById(k);if(elm&&document.activeElement!==elm)elm.value=ids[k];});
 var marker=document.getElementById("fixtureState");
 if(marker)marker.textContent=world==="theatre"?"Illustrative local progress; bundled Dune saga art is not a Dune: Part Two poster. Other titles without matching local art use declared substituted assets.":world==="tankoban"?"Illustrative local reading progress; One Piece uses real One Piece saga artwork. Manga and comic titles without matching local covers use declared substituted project art.":"Illustrative installed Tribute and roster set; banners reuse real titled universe assets. Naruto banner is substituted One Piece art. Cosmere has Tribute-only treatment.";
}
var preset=document.getElementById("preset");
if(preset)preset.addEventListener("change",function(e){setPreset(e.target.value);});
["width","height","insetTop","insetBottom","insetLeft","insetRight","fontScale"].forEach(function(k){
 var node=document.getElementById(k);if(!node)return;
 node.addEventListener("change",function(){var n=Number(node.value);if(!Number.isFinite(n))return;
  var dest={width:"w",height:"h",insetTop:"top",insetBottom:"bottom",insetLeft:"left",insetRight:"right",fontScale:"scale"}[k];
  if(dest==="scale")n=Math.min(1.8,Math.max(0.8,n));else n=Math.max(0,Math.round(n));
  if(dest==="w")n=Math.max(280,n);if(dest==="h")n=Math.max(300,n);
  s[dest]=n;if(dest==="w"||dest==="h")s.mode=Math.min(s.w,s.h)<600?"phone":s.w>=1100&&s.h<=750?"desktop":"tablet";s.keepScroll=false;configure();
 });
});
var selState=document.getElementById("reviewState");
if(selState)Array.from(selState.options).forEach(function(option){
 if(world==="universes"&&["source-open","category-open","filter-open","context-open","empty-progress"].indexOf(option.value)>=0)option.remove();
 if(world!=="universes"&&option.value==="facets-books")option.remove();
});
if(selState)selState.addEventListener("change",function(e){
 var x=e.target.value;
 s.slide=0;s.picker="";s.sourceOpen=false;s.indexOpen=false;s.contextId=null;s.hideProgress=false;s.longTitles=false;s.keepScroll=false;
 if(x==="carousel-two")s.slide=1;
 else if(x==="source-open"){s.sourceOpen=true;s.tab="Discover";}
 else if(x==="category-open"){s.tab="Discover";s.picker="category";}
 else if(x==="filter-open"){s.tab="Discover";s.picker="filter";}
 else if(x==="context-open")s.contextId=D[world].continuing[0]?D[world].continuing[0].id:null;
 else if(x==="long-title"){s.longTitles=true;if(world==="universes")s.slide=2;}
 else if(x==="empty-progress")s.hideProgress=true;
 else if(x==="facets-books")s.facet="books";
 configure();
 if(x==="source-open"||x==="category-open"||x==="filter-open"){var sc=document.getElementById("worldScroll"),tab=document.getElementById("worldTabs");if(sc&&tab)sc.scrollTop=tab.offsetTop-30;}
 if(x==="context-open"){var c=document.querySelector(".continue-section");if(c)c.scrollIntoView({block:"center"});}
});
configure();
})();

