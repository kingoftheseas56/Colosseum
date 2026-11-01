"use strict";
(function(){
  const ref = "../../../../../baseline/reference/desktop/assets/";
  const icon = function(name){return ref+"icons/"+name+".svg";};
  function rowIndexGlyph(title){
    const name=String(title).toLowerCase();
    let paths='<path d="M4 6h16M4 12h16M4 18h16"/>';
    if(/collection|library|shelf|shelves/.test(name)) paths='<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>';
    else if(/top|rated|popular|best/.test(name)) paths='<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>';
    else if(/recent|new|available|release|continue/.test(name)) paths='<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>';
    else if(/explore|discover|genre/.test(name)) paths='<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>';
    const svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#c9c8d0" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">'+paths+'</svg>';
    return "data:image/svg+xml,"+encodeURIComponent(svg);
  }
  const books = [
    {title:"Dune",author:"Frank Herbert",asset:"wallpaper/sagas/dune.jpg",rating:"4.8",source:"Open Library"},
    {title:"The Lord of the Rings",author:"J. R. R. Tolkien",asset:"wallpaper/sagas/lord-of-the-rings.jpg",rating:"4.9",source:"Open Library"},
    {title:"Harry Potter and the Deathly Hallows",author:"J. K. Rowling",asset:"wallpaper/sagas/harry-potter.jpg",rating:"4.7",source:"Open Library"},
    {title:"A Game of Thrones",author:"George R. R. Martin",asset:"wallpaper/sagas/ice-and-fire.jpg",rating:"4.6",source:"Open Library"},
    {title:"The Witcher: The Last Wish",author:"Andrzej Sapkowski",asset:"wallpaper/sagas/the-witcher.jpg",rating:"4.5",source:"Open Library"},
    {title:"Percy Jackson and the Olympians: The Lightning Thief",author:"Rick Riordan",asset:"wallpaper/sagas/percy-jackson.jpg",rating:"4.6",source:"Open Library"},
    {title:"The Adventures of Sherlock Holmes",author:"Arthur Conan Doyle",asset:"wallpaper/sagas/sherlock-holmes.jpg",rating:"4.8",source:"Open Library"},
    {title:"Jurassic Park",author:"Michael Crichton",asset:"wallpaper/sagas/jurassic-park.jpg",rating:"4.4",source:"Open Library"},
    {title:"The Way of Kings",author:"Brandon Sanderson",asset:"extensions/house/cosmere.jpg",rating:"4.9",source:"Open Library"},
    {title:"Heir to the Empire",author:"Timothy Zahn",asset:"extensions/house/starwars-banner.jpg",rating:"4.5",source:"Open Library"}
  ];
  const shelfSpecs = [
    {key:"top10",title:"Top 10",ranked:true,offset:0},
    {key:"popular",title:"Popular",offset:1},
    {key:"top-rated",title:"Top Rated",offset:2},
    {key:"new-releases",title:"New Releases",offset:3},
    {key:"trending",title:"Trending",offset:4},
    {key:"most-read",title:"Most Read",offset:5},
    {key:"classics",title:"Classics",offset:6}
  ];
  const catalogNames = shelfSpecs.filter(function(x){return !x.ranked;});
  const sourceFor = function(name){return name==="Most Read"||name==="Classics"?"Open Library":"Apple Books";};
  const genres = ["Fiction & Literature","Mysteries & Thrillers","Sci-Fi & Fantasy","Romance"];
  const mosaicSpecs = [{name:"Fiction",key:"fiction"},{name:"Nonfiction",key:"nonfiction"},{name:"Audience",key:"audience"}];
  const worldNames = [
    {name:"Theatre",icon:"projector-theatre"},
    {name:"Tankoban",icon:"comic-book"},
    {name:"Biblio",icon:"book-library"},
    {name:"Universes",icon:"universes-atom"},
    {name:"Profile",icon:"your-colosseum-laurel"}
  ];
  const $ = function(id){return document.getElementById(id);};
  const app = $("mock-app"), scroll = $("world-scroll"), board = $("world-board");
  const params = new URLSearchParams(window.location.search);
  const capturing = params.get("capture")==="1";
  let scene = params.get("scene")==="home"?"home":"world";
  let tab = ["discover","explore","library"].indexOf(params.get("tab"))>=0?params.get("tab"):"discover";
  let category = params.get("category") || "Popular";
  let currentSource = sourceFor(category);
  let selectedFilter = params.get("filter")||"";let tabScroll={};
  function matchesFilter(i){if(!selectedFilter||selectedFilter==="Fiction"||selectedFilter==="Fiction & Literature")return true;const g=[["Fantasy","Sci-Fi & Fantasy"],["Fantasy","Sci-Fi & Fantasy"],["Fantasy","Young Adult"],["Fantasy"],["Fantasy"],["Fantasy","Young Adult"],["Mystery","Mysteries & Thrillers"],["Sci-Fi & Fantasy"],["Fantasy"],["Sci-Fi & Fantasy"]];return g[i].includes(selectedFilter);}
  let genreMenu = params.get("menu")==="filter", catalogMenu = params.get("menu")==="category", sidebarOpen=params.get("sidebar")==="open", rowIndexOpen=params.get("index")==="open";
  let editMode = params.get("edit")==="1", heroIndex=0;
  let focusBook = -1, pinReturn = params.get("return")==="explore";
  let shelfOrder = shelfSpecs.map(function(s){return s.key;});
  let hiddenShelves = new Set();
  let continueEntries = [0,2,4,5].map(function(i,idx){return {book:books[i],progress:[.63,.28,.76,.35][idx],sub:["Chapter 12","Chapter 4","Chapter 17","Chapter 6"][idx]};});
  let width=412,height=900,platform="android",safeTop=0,safeBottom=0,fontScale=1;
  let categoryOpenFromSeeAll=false,pickerOpenedY=0;
  const esc = function(v){return String(v).replace(/[&<>"']/g,function(c){return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c];});};
  const asset = function(book){return ref+book.asset;};
  const attr = function(text){return esc(text);};
  const notify = function(message){
    let status=$("review-status");
    if(!status && $("inspector")){
      status=document.createElement("p");status.id="review-status";status.setAttribute("role","status");
      status.style.cssText="border:1px solid #5c5137;border-radius:8px;padding:10px;background:#24232b;color:#f3d48c;font-size:12px";
      $("inspector").appendChild(status);
    }
    if(status) status.textContent="Mock interaction: "+message;
  };
  function iconButton(file,label,action,classes){
    return '<button class="sys-glyph '+(classes||"")+'" aria-label="'+attr(label)+'" title="'+attr(label)+'" data-action="'+attr(action)+'"><img alt="" src="'+icon(file)+'"></button>';
  }
  function shell(){
    const android=platform==="android";
    let top='';
    if(android){
      top='<div class="clock-block"><div><div class="clock-main">8:29 <em>PM</em></div><div class="clock-date">Wednesday, June 24</div></div></div>';
    }else{
      top='<div class="clock-block"><button class="home-back" data-action="home">‹ Home</button><div><div class="clock-main">8:29 <em>PM</em></div><div class="clock-date">Wednesday, June 24</div></div></div>';
    }
    const pills=["Tankoban","Biblio","Theatre","Universes","Feria","Vinyl"].map(function(n){
      const soon=n==="Feria"||n==="Vinyl";
      return '<button class="world-pill '+(n==="Biblio"?"current":"")+'" data-action="other-world" data-name="'+n+'" '+(soon?'aria-disabled="true"':"")+'>'+n+(soon?'<span class="soon-token">SOON</span>':'')+'</button>';
    }).join("");
    let icons=iconButton("search","Search","search")+
      iconButton("trackers","Trackers","trackers")+
      '<button class="sys-glyph" aria-label="Account" title="Account" data-action="account"><span class="sys-account"></span></button>'+
      iconButton("settings","Wallpaper","wallpaper");
    if(!android) icons+=iconButton("minimize","Minimize","window","window-icon")+iconButton("fullscreen-exit","Exit fullscreen","window","window-icon")+iconButton("power","Quit Colosseum","window","window-icon");
    $("top-bar").innerHTML=top+'<nav class="world-picker">'+pills+'</nav><div class="bar-icons">'+icons+'</div>';
    let worlds=worldNames.map(function(w){return '<button class="phone-tab '+(w.name==="Biblio"?"current":"")+'" data-action="other-world" data-name="'+attr(w.name)+'" aria-label="'+attr(w.name)+'"><img src="'+icon(w.icon)+'" alt=""><span>'+w.name+'</span></button>';}).join("");
    $("phone-navigation").innerHTML=worlds;$("wide-shell").innerHTML=worlds;
  }
  function hero(){
    const b=books[heroIndex%4];
    const kicker="FEATURED IN BIBLIO";const tones=[["#6a4a2c","#1d1209"],["#5a3a3f","#180d10"],["#3f5a4a","#101a14"],["#4a4063","#13101f"]][heroIndex%4];
    return '<section id="featured" aria-label="Featured in Biblio" style="--hero-c1:'+tones[0]+';--hero-c2:'+tones[1]+'">'+
      '<img class="featured-image" src="'+asset(b)+'" alt="">'+
      '<img class="featured-image inset" src="'+asset(b)+'" alt="">'+
      '<div class="featured-dim"></div><div class="featured-ghost">B</div>'+
      '<div class="featured-copy"><span class="featured-kicker">'+kicker+'</span>'+
      '<div class="featured-title">'+esc(b.title)+'</div>'+
      '<div class="featured-blurb">By '+esc(b.author)+'.</div>'+
      '<div class="featured-actions"><button class="gold-button" data-action="hero-read">Read</button><button class="outline-button" data-action="hero-detail">Details</button></div></div>'+
      '<div class="slide-dots" aria-label="Featured slides">'+books.slice(0,4).map(function(_,i){return '<button class="slide-dot '+(i===heroIndex?"active":"")+'" aria-label="Featured slide '+(i+1)+'" data-action="slide" data-index="'+i+'"></button>';}).join("")+'</div></section>';
  }
  function continueShelf(){
    if(continueEntries.length===0) return '';
    return '<section id="continue-section" class="continue-row">'+
      '<div class="section-heading widget"><div class="heading-title">Continue Reading</div><button class="widget-see-all" data-action="continue-all">See all <span class="chevron">›</span></button></div>'+
      '<div class="horizontal-books">'+continueEntries.map(function(e,i){
        return '<article class="continue-tile" role="group" aria-label="'+attr(e.book.title)+'">'+
        '<img src="'+asset(e.book)+'" alt="">'+
        '<div class="tile-scrim"></div>'+
        '<button class="resume-fullhit" aria-label="Details: '+attr(e.book.title)+'" data-action="book-detail" data-book="'+books.indexOf(e.book)+'"></button>'+
        '<span class="kind-tag">BOOK</span>'+
        '<button class="resume-circle" aria-label="Resume '+attr(e.book.title)+'" data-action="resume" data-book="'+books.indexOf(e.book)+'"><img src="'+icon('books')+'" alt="" style="width:21px;height:21px"></button>'+
        '<button class="remove-progress" title="Remove progress" aria-label="Remove '+attr(e.book.title)+' from Continue Reading" data-action="remove-continue" data-index="'+i+'">×</button>'+
        '<div class="book-label">'+esc(e.book.title)+'</div><div class="sub-label">'+esc(e.sub)+'</div>'+
        '<div class="progress-bg"><div class="progress-bar" style="width:'+(e.progress*100)+'%"></div></div>'+
        '</article>';
      }).join("")+'</div></section>';
  }
  function worldTabs(){
    return '<div class="world-tabs-wrapper" id="sticky-tabs"><nav class="world-tabs" role="tablist" aria-label="Biblio sections">'+
      [{key:"discover",label:"Discover"},{key:"explore",label:"Explore"},{key:"library",label:"Library"}].map(function(t){
        return '<button role="tab" aria-selected="'+(tab===t.key)+'" class="world-tab '+(tab===t.key?"active":"")+'" data-action="tab" data-tab="'+t.key+'">'+t.label+'</button>';
      }).join("")+'</nav></div>';
  }
  function sideRail(type){
    const isExplore=type==="explore";
    const opened=isExplore?rowIndexOpen:sidebarOpen;
    const label=isExplore?"Shelf index":"Discover sources";
    const entries=isExplore?shelfOrder.filter(function(k){return editMode||!hiddenShelves.has(k);}).map(function(k){const x=shelfSpecs.find(function(s){return s.key===k;});return {name:x.title,key:x.key};}).concat(mosaicSpecs.map(function(x){return {name:x.name,key:x.key};})):
      [{name:"Apple Books",key:"apple-books"},{name:"Open Library",key:"open-library"}];
    return '<aside class="side-rail '+(opened?"open":"")+'" aria-label="'+label+'">'+
      '<div class="rail-brand"><span>WORLD</span><strong>Biblio</strong></div>'+
      '<div class="sidebar-list">'+entries.map(function(x,i){
        const active=isExplore?false:x.name===currentSource;
        const a=isExplore?' data-action="jump-row" data-row="'+x.key+'"':' data-action="source" data-name="'+attr(x.name)+'"';
        return '<button class="rail-item '+(active?"active":"")+'" '+a+' aria-label="'+attr(x.name)+'">'+
          (isExplore?'<img src="'+rowIndexGlyph(x.name)+'" alt="">':
           x.name==="Apple Books"?'<img src="../../../../pin-assets/assets/addon-logos/applebooks.ico" alt="">':
           '<span class="addon-letter" aria-hidden="true">O</span>')+
          '<span class="rail-label">'+esc(x.name)+'</span></button>';
      }).join("")+'</div>'+
      '<button class="rail-toggle" data-action="'+(isExplore?"toggle-index":"toggle-sidebar")+'" aria-label="'+(opened?"Collapse ":"Expand ")+label+'" aria-expanded="'+opened+'">'+(opened?"‹":"›")+'</button></aside>';
  }
  function bookTile(book,idx,ranked,wall){
    if(wall) {
      return '<div class="wall-book" tabindex="0" role="button" aria-label="Details: '+attr(book.title)+'" data-action="book-detail" data-book="'+idx+'">'+
        '<div class="wall-image-holder"><img src="'+asset(book)+'" alt="">'+
        '<div class="cover-reveal"><span class="rating">★ '+book.rating+'</span>'+esc(book.source)+'</div></div>'+
        '<div class="title">'+esc(book.title)+'</div><div class="author">'+esc(book.author)+'</div></div>';
    }
    return '<div class="book-card '+(ranked?"ranked":"")+'" role="button" tabindex="0" data-action="book-detail" data-book="'+idx+'" aria-label="Details: '+attr(book.title)+'">'+
      (ranked?'<span class="rank">'+(idx+1)+'</span>':'')+
      '<div class="cover-area"><img class="book-cover" src="'+asset(book)+'" alt="">'+
      '<div class="cover-reveal"><span class="rating">★ '+book.rating+'</span>'+esc(book.source)+'</div></div>'+
      '<div class="book-name" title="'+attr(book.title)+'">'+esc(book.title)+'</div>'+
      '<div class="book-author" title="'+attr(book.author)+'">'+esc(book.author)+'</div></div>';
  }
  function displayBooks(offset,count){
    const out=[];for(let i=0;i<count;i++)out.push((offset+i)%books.length);return out;
  }
  function seeAllMenu(){
    const groups=[{label:"Popular",key:"Popular"},{label:"Top Rated",key:"Top Rated"},{label:"New Releases",key:"New Releases"},{label:"Trending",key:"Trending"},{label:"Most Read",key:"Most Read"},{label:"Classics",key:"Classics"}].filter(function(g){return sourceFor(g.label)===currentSource;});
    return '<div class="original-menu" '+(!catalogMenu?"hidden":"")+' role="menu" aria-label="Catalogue">'+groups.map(function(g){
      return '<button role="menuitem" class="'+(g.label===category?"selected":"")+'" data-action="choose-category" data-name="'+g.label+'">'+g.label+'</button>';
    }).join("")+'</div>';
  }
  function filterOptions(){
    const options=["All","Fiction","Nonfiction","Fantasy","Mystery","Romance","Young Adult"];
    return '<div class="original-menu filter-menu" '+(!genreMenu?"hidden":"")+' role="menu" aria-label="Genre filter">'+options.map(function(n){
      return '<button role="menuitem" class="'+(n===selectedFilter||(n==="All"&&!selectedFilter)?"selected":"")+'" data-action="choose-filter" data-name="'+n+'">'+n+'</button>';
    }).join("")+'</div>';
  }
  function discover(){
    return '<section class="biblio-page" id="discover-content">'+
      '<div class="browse-layout">'+sideRail("discover")+
      '<div class="rail-main"><div class="catalog-header">'+
      '<div class="type-lens">'+(pinReturn?'<button data-action="back-explore">‹ Back</button>':'')+
      '<button class="active" data-action="lens">Books</button></div>'+
      '<div class="catalog-summary"><div class="catalog-kicker">NOW BROWSING</div>'+
      '<button class="category-chooser" data-action="toggle-category" aria-expanded="'+catalogMenu+'"><span>'+esc(category)+'</span><span class="down">▾</span></button>'+
      '<div class="catalog-source">'+esc(currentSource)+'</div></div>'+
      seeAllMenu()+'</div>'+
      '<div class="filter-line"><span class="filter-title">FILTER</span>'+
      '<button class="filter-pill '+(selectedFilter?"has-value":"")+'" data-action="toggle-filter" aria-expanded="'+genreMenu+'">'+esc(selectedFilter||"Genre")+'<span '+(selectedFilter?'data-action="clear-filter" role="button" tabindex="0" aria-label="Clear genre"':'')+'>'+(selectedFilter?"✕":"▾")+'</span></button>'+
      filterOptions()+'</div>'+
      '<div class="poster-wall">'+(displayBooks(Math.max(0,catalogNames.findIndex(function(c){return c.title===category;})),10).filter(matchesFilter).map(function(index){return bookTile(books[index],index,false,true);}).join("") || '<p class="empty-filter">No books match this filter.</p>')+'</div>'+
      '</div></div></section>';
  }
  function editStrip(spec, index){
    if(!editMode) return '';
    const currentlyHidden=hiddenShelves.has(spec.key);
    return '<div class="edit-strip" aria-label="Customize '+attr(spec.title)+'">'+
    '<button title="Drag to reorder" aria-label="Reorder '+attr(spec.title)+'" draggable="true" data-drag="'+spec.key+'">⠿</button>'+
    '<button data-action="move-up" data-row="'+spec.key+'" '+(index===0?"disabled":"")+' aria-label="Move '+attr(spec.title)+' up">▲</button>'+
    '<button data-action="move-down" data-row="'+spec.key+'" '+(index===shelfOrder.length-1?"disabled":"")+' aria-label="Move '+attr(spec.title)+' down">▼</button>'+
    '<button data-action="toggle-hide" data-row="'+spec.key+'">'+(currentlyHidden?"Show":"Hide")+'</button></div>';
  }
  function shelf(spec,index){
    if(!editMode && hiddenShelves.has(spec.key))return '';
    const indexes=displayBooks(spec.offset,10);
    const section='<section id="shelf-'+spec.key+'" class="shelf-block '+(hiddenShelves.has(spec.key)?"muted":"")+'">'+
      editStrip(spec,index)+'<div class="shelf-header"><h3>'+esc(spec.title)+'</h3>'+
      '<button class="shelf-seeall" data-action="see-all" data-row="'+spec.key+'">See All</button></div>'+
      '<div class="horizontal-books rail-books '+(spec.ranked?"ranked":"")+'">'+
      indexes.map(function(i,rank){return bookTile(books[i],spec.ranked?rank:i,spec.ranked,false);}).join("")+
      '</div></section>';
    return section;
  }
  function mosaics(){
    return '<section class="mosaic-stack">'+mosaicSpecs.map(function(x,i){
      return '<div class="mosaic" role="button" tabindex="0" data-action="mosaic" data-name="'+x.name+'" id="shelf-'+x.key+'">'+
        '<div class="mosaic-covers">'+displayBooks(i*3,7).map(function(index){return '<img alt="" src="'+asset(books[index])+'">';}).join("")+'</div>'+
        '<div class="mosaic-title">'+x.name+'</div></div>';
    }).join("")+'</section>';
  }
  function explore(){
    const contents=shelfOrder.map(function(key,i){const spec=shelfSpecs.find(function(s){return s.key===key;});return shelf(spec,i);}).join("");
    return '<section class="biblio-page" id="explore-content"><div class="explore-layout">'+
      sideRail("explore")+'<div class="rail-main explore-content">'+
      '<div class="explore-head"><h2>Explore</h2><button class="shelves-customize" data-action="customize">'+(editMode?"Done":"Customize shelves")+'</button></div>'+
      contents+mosaics()+'</div></div></section>';
  }
  function library(){
    return '<section class="biblio-page library-page">'+
      '<div class="explore-head"><h2>Library</h2></div>'+
      '<div class="filter-line"><input type="search" id="library-search" placeholder="Search your library" aria-label="Search your library" style="border-radius:13px;background:rgba(0,0,0,.34);border:1px solid var(--edge);color:var(--ink);height:40px;padding:0 12px;max-width:100%">'+
      '<button class="filter-pill" data-action="library-filter">All ▾</button><button class="filter-pill" data-action="library-sort">Recently added ▾</button></div>'+
      '<div class="poster-wall">'+[books[0],books[1],books[2]].map(function(b){return bookTile(b,books.indexOf(b),false,true);}).join("")+'</div></section>';
  }
  function readingDesk(){
    const home='<section class="reading-desk"><h2>Biblio</h2><span class="top-charts">Top charts</span>'+
      '<div class="desk-table"><div class="desk-pile" data-action="open-biblio" role="button" tabindex="0">'+
      '<div class="desk-slab a"></div><div class="desk-slab b"></div><div class="desk-slab c"></div>'+
      '<div class="desk-topbook"><img src="'+asset(books[0])+'" alt=""><span class="desk-number">No. 1 today</span></div></div>'+
      '<div class="desk-charts"><strong>Top 10</strong>'+
      '<p class="desktop-desk-copy">today on the Apple Books chart — fresh every day</p>'+
      '<p class="phone-desk-copy">Fresh chart picks, with the same Biblio catalogue one tap away.</p>'+
      '<div class="genre-chips">'+genres.map(function(g){return '<button class="genre-chip" data-action="genre" data-name="'+attr(g)+'">'+esc(g)+'</button>';}).join("")+'</div></div>'+
      '<div class="desk-runner" data-action="open-biblio" role="button" tabindex="0"><img src="'+asset(books[1])+'" alt=""><span class="desk-number">No. 2</span></div></div></section>';
    return home;
  }
  function applyLayout(){
    app.className="";
    if(platform==="desktop") app.classList.add("mock-desktop");
    else{app.classList.add("mock-android");if(width>=840)app.classList.add("mock-wide");else if(width>=600)app.classList.add("mock-tablet-narrow");}
    const contentWidth=width-(platform==="android"&&width>=840?92:0);const margin=contentWidth<=430?18:contentWidth<600?24:contentWidth<840?32:54;
    const heroHeight=contentWidth<600?260:contentWidth<840?300:340;app.classList.toggle("hero-narrow",contentWidth-2*margin<600);app.classList.toggle("proposal-phone-bleed",params.get("posterPhone")==="bleed");
    app.style.setProperty("--margin",margin+"px");
    app.style.setProperty("--safe-top",safeTop+"px");
    app.style.setProperty("--safe-bottom",safeBottom+"px");
    app.style.setProperty("--hero-height",heroHeight+"px");
    app.style.setProperty("--font-scale",fontScale);
    app.style.setProperty("--top-inset",platform==="desktop"||contentWidth>=840?"30px":"12px");
    app.style.setProperty("--section-space",contentWidth<600?"22px":contentWidth<840?"28px":"36px");
    app.style.setProperty("--rail-height",Math.max(160,height-(platform==="desktop"?140:208))+"px");
    app.style.setProperty("--menu-height",Math.max(130,height-(platform==="desktop"?175:205))+"px");
    const big=platform==="android"&&width>=840;
    app.style.setProperty("--shell-left",big?"92px":"0px");
    app.style.setProperty("--bottom-nav",platform==="android"&&!big?("calc(72px + "+safeBottom+"px)"):"0px");
    app.style.width=width+"px";app.style.height=height+"px";
    $("mock-frame").style.width=width+"px";$("mock-frame").style.height=height+"px";
    const stage=$("studio-stage");
    const availableWidth=capturing?width:Math.max(100,stage.clientWidth-42);
    const availableHeight=capturing?height:Math.max(100,window.innerHeight-135);
    const scale=capturing?1:Math.min(1,availableWidth/width,availableHeight/height);
    $("mock-frame").style.transform="scale("+scale+")";
    $("scale-box").style.width=Math.ceil(width*scale)+"px";
    $("scale-box").style.height=Math.ceil(height*scale)+"px";
  }
  function fitShortLandscapeMenu(){
    if(!catalogMenu && !genreMenu)return;
    const menu=app.querySelector(catalogMenu?".catalog-header .original-menu":".filter-menu");
    const trigger=app.querySelector(catalogMenu?".category-chooser":".filter-pill");
    if(!menu||!trigger)return;
    const frame=app.getBoundingClientRect(),tr=trigger.getBoundingClientRect();
    const ratio=width/Math.max(1,frame.width);
    const nav=platform==="android"&&width<840?72+safeBottom:0;
    const topEdge=safeTop+76;
    const bottomEdge=height-nav-6;
    const menuHeight=Math.min(380,menu.scrollHeight,bottomEdge-topEdge);
    const triggerBottom=(tr.bottom-frame.top)*ratio+6;const triggerTop=(tr.top-frame.top)*ratio-6;const top=Math.max(topEdge,Math.min(bottomEdge-menuHeight,triggerBottom+menuHeight<=bottomEdge?triggerBottom:triggerTop-menuHeight));
    const x=catalogMenu?(tr.right-frame.left)*ratio-menu.offsetWidth:(tr.left-frame.left)*ratio;
    const left=Math.max(8,Math.min(width-menu.offsetWidth-8,x));
    menu.style.position="fixed";
    menu.style.left=left+"px";
    menu.style.right="auto";
    menu.style.top=top+"px";
    menu.style.maxHeight=Math.max(88,bottomEdge-top)+"px";
    menu.style.zIndex="100";
    menu.style.overflowY="auto";
  }
  function render(preserveScroll){
    const focused=document.activeElement;const focusAction=focused?.dataset?.action;const focusLabel=focused?.getAttribute("aria-label");
    const y=preserveScroll?scroll.scrollTop:0;
    shell();
    if(scene==="home"){
      board.hidden=true;$("home-board").hidden=false;$("home-board").innerHTML=readingDesk();
    }else{
      board.hidden=false;$("home-board").hidden=true;
      const page=tab==="discover"?discover():tab==="explore"?explore():library();
      board.innerHTML=hero()+continueShelf()+worldTabs()+page;
    }
    applyLayout();
    scroll.scrollTop=y;
    updateDock();
    fitShortLandscapeMenu();updateIndex();if(catalogMenu||genreMenu)pickerOpenedY=scroll.scrollTop;
    let focusTarget=catalogMenu||genreMenu?app.querySelector((catalogMenu?".catalog-header .original-menu":".filter-menu")+" button.selected")||app.querySelector(".original-menu:not([hidden]) button"):null;
    if(!focusTarget&&focusAction){const action=/filter/.test(focusAction)?"toggle-filter":/category/.test(focusAction)?"toggle-category":focusAction;focusTarget=Array.from(app.querySelectorAll("[data-action]")).find(e=>e.dataset.action===action&&(!focusLabel||e.getAttribute("aria-label")===focusLabel))||app.querySelector('[data-action="'+action+'"]');}if(focusTarget)focusTarget.focus({preventScroll:true});
  }
  function updateIndex(){const rows=Array.from(app.querySelectorAll(".rail-item[data-row]"));let active=rows[0];rows.forEach(function(r){const e=$("shelf-"+r.dataset.row);if(e&&e.getBoundingClientRect().top<=scroll.getBoundingClientRect().top+120)active=r;});rows.forEach(function(r){r.classList.toggle("active",r===active);r.setAttribute("aria-current",r===active?"true":"false");});}
  function updateDock(){
    const tabs=$("sticky-tabs");if(!tabs) return;
    tabs.classList.toggle("docked",scroll.scrollTop>(app.querySelector("#featured")?.offsetHeight||300)+130);
  }
  function selectTab(next){
    tabScroll[tab]=scroll.scrollTop;tab=next;catalogMenu=false;genreMenu=false;render(false);
    if($("review-tab"))$("review-tab").value=tab;
    const b=$("sticky-tabs");if(b)scroll.scrollTop=tabScroll[next]!==undefined?tabScroll[next]:b.offsetTop-6;
    updateDock();
  }
  function showExplore(){
    if(scene==="home"){scene="world";if($("scene"))$("scene").value="world";}
    selectTab("explore");
    const el=$("explore-content");
    if(el)scroll.scrollTop=el.offsetTop-65;
  }
  function showDiscoverFromShelf(shelfKey){
    const spec=shelfSpecs.find(function(s){return s.key===shelfKey;});
    category=spec?(spec.ranked?"Popular":spec.title):"Popular";
    currentSource=sourceFor(category);
    pinReturn=true;categoryOpenFromSeeAll=true;
    selectTab("discover");
    const content=$("discover-content");if(content)scroll.scrollTop=content.offsetTop-65;
    notify('See All → Discover / '+category+'; ‹ Back returns to Explore.');
  }
  function rotateHero(dir){heroIndex=(heroIndex+dir+4)%4;render(true);}
  function updateInspector(){
    if($("viewport")){
      const current=$("viewport").value.split("x").map(Number);
      if(current[0]!==width||current[1]!==height) $("viewport").value="";
    }
    if($("view-width"))$("view-width").value=width;
    if($("view-height"))$("view-height").value=height;
    if($("scene"))$("scene").value=scene;
    if($("review-tab"))$("review-tab").value=tab;
  }
  function sizeFromInspector(){
    width=Math.max(320,Math.min(2560,Number($("view-width").value)||412));
    height=Math.max(240,Math.min(1800,Number($("view-height").value)||900));
    safeTop=Math.max(0,Number($("inset-top").value)||0);
    safeBottom=Math.max(0,Number($("inset-bottom").value)||0);
    fontScale=Number($("font-scale").value)||1;
    platform=$("viewport").selectedOptions[0]&&$("viewport").selectedOptions[0].textContent.startsWith("Desktop")?"desktop":"android";
    render(true);updateInspector();
  }
  function setupInspector(){
    if(params.has("w"))width=Math.max(320,Number(params.get("w"))||412);
    if(params.has("h"))height=Math.max(240,Number(params.get("h"))||900);
    if(params.get("mode")==="desktop")platform="desktop";
    else if(params.has("w")||params.has("h"))platform="android";
    if(params.has("top"))safeTop=Math.max(0,Number(params.get("top"))||0);
    if(params.has("bottom"))safeBottom=Math.max(0,Number(params.get("bottom"))||0);
    if(params.has("font"))fontScale=Number(params.get("font"))||1;
    if(capturing){
      document.body.classList.add("capture-mode");
      const css=document.createElement("style");
      css.textContent=".capture-mode #inspector,.capture-mode .stage-caption,.capture-mode .stage-foot{display:none!important}.capture-mode #studio,.capture-mode #studio-stage{height:100vh;width:100vw;min-height:0;margin:0;padding:0;overflow:hidden;display:block}.capture-mode #scale-box{margin:0} .capture-mode #mock-frame{box-shadow:none}";
      document.head.appendChild(css);
    }
    const vp=$("viewport");
    const value=width+"x"+height;
    if(Array.from(vp.options).some(function(o){return o.value===value;}))vp.value=value;
    updateInspector();
    vp.addEventListener("change",function(){
      const parts=vp.value.split("x");width=Number(parts[0]);height=Number(parts[1]);
      platform=vp.selectedOptions[0].textContent.startsWith("Desktop")?"desktop":"android";
      updateInspector();render(true);
    });
    ["view-width","view-height","inset-top","inset-bottom","font-scale"].forEach(function(id){
      $(id).addEventListener("change",sizeFromInspector);
    });
    $("scene").addEventListener("change",function(e){scene=e.target.value;scroll.scrollTop=0;render(false);});
    $("review-tab").addEventListener("change",function(e){scene="world";$("scene").value="world";selectTab(e.target.value);});
    $("jump-shelves").addEventListener("click",showExplore);
    $("jump-continue").addEventListener("click",function(){scene="world";$("scene").value="world";render(false);const s=$("continue-section");if(s)scroll.scrollTop=s.offsetTop-35;});
    $("jump-discover").addEventListener("click",function(){scene="world";$("scene").value="world";tab="discover";catalogMenu=true;render(false);const s=$("discover-content");if(s)scroll.scrollTop=s.offsetTop-60;});
    $("jump-index").addEventListener("click",function(){showExplore();rowIndexOpen=true;render(true);});
  }
  function handleAction(target){
    const action=target.dataset.action;
    const book=books[Number(target.dataset.book)||0];
    if(action==="tab"){selectTab(target.dataset.tab);return;}
    if(action==="slide"){heroIndex=Number(target.dataset.index);render(true);return;}
    if(action==="hero-read"||action==="hero-detail"){
      notify((action==="hero-read"?"Read":"Details")+" selected for "+books[heroIndex].title+"; native destination deliberately not recreated.");return;
    }
    if(action==="book-detail"){notify("Open the original Biblio book page for "+book.title+" (outside this study).");return;}
    if(action==="resume"){notify("Resume book "+book.title+" (reader is outside this study).");return;}
    if(action==="remove-continue"){continueEntries.splice(Number(target.dataset.index),1);render(true);return;}
    if(action==="continue-all"){notify("Continue Reading See all selected; full progress page is outside assigned scope.");return;}
    if(action==="home"){scene="home";$("scene").value="home";scroll.scrollTop=0;render(false);return;}
    if(action==="open-biblio"){scene="world";$("scene").value="world";tab="discover";render(false);return;}
    if(action==="genre"){scene="world";$("scene").value="world";selectedFilter=target.dataset.name;tab="discover";render(false);return;}
    if(action==="other-world"){notify(target.dataset.name+" navigation belongs to the native shell and is not mocked.");return;}
    if(["search","trackers","account","wallpaper","window"].indexOf(action)>=0){notify(target.getAttribute("aria-label")+" opens its existing native destination; no imitation added.");return;}
    if(action==="toggle-sidebar"){sidebarOpen=!sidebarOpen;render(true);return;}
    if(action==="toggle-index"){rowIndexOpen=!rowIndexOpen;render(true);return;}
    if(action==="source"){currentSource=target.dataset.name;category=currentSource==="Open Library"?"Most Read":"Popular";sidebarOpen=false;selectedFilter="";render(true);return;}
    if(action==="lens"){return;}
    if(action==="toggle-category"){catalogMenu=!catalogMenu;genreMenu=false;render(true);return;}
    if(action==="choose-category"){category=target.dataset.name;currentSource=sourceFor(category);catalogMenu=false;render(true);return;}
    if(action==="clear-filter"){selectedFilter="";genreMenu=false;render(true);return;}
    if(action==="toggle-filter"){genreMenu=!genreMenu;catalogMenu=false;render(true);return;}
    if(action==="choose-filter"){selectedFilter=target.dataset.name==="All"?"":target.dataset.name;genreMenu=false;render(true);return;}
    if(action==="back-explore"){pinReturn=false;selectTab("explore");return;}
    if(action==="see-all"){showDiscoverFromShelf(target.dataset.row);return;}
    if(action==="customize"){editMode=!editMode;render(true);return;}
    if(action==="move-up"||action==="move-down"){
      const index=shelfOrder.indexOf(target.dataset.row),next=index+(action==="move-up"?-1:1);
      if(index>=0&&next>=0&&next<shelfOrder.length){const a=shelfOrder.splice(index,1)[0];shelfOrder.splice(next,0,a);render(true);}
      return;
    }
    if(action==="toggle-hide"){hiddenShelves.has(target.dataset.row)?hiddenShelves.delete(target.dataset.row):hiddenShelves.add(target.dataset.row);render(true);return;}
    if(action==="jump-row"){const el=$("shelf-"+target.dataset.row);if(el)scroll.scrollTop+=el.getBoundingClientRect().top-scroll.getBoundingClientRect().top-64;updateIndex();return;}
    if(action==="mosaic"){category="Popular";selectedFilter=target.dataset.name==="Audience"?"Young Adult":target.dataset.name;pinReturn=true;selectTab("discover");return;}
    if(action==="library-sort"||action==="library-filter"){notify("Original library picker; Library is only a contextual preview for this Biblio home study.");return;}
  }
  app.addEventListener("click",function(ev){
    const t=ev.target.closest("[data-action]");
    if(!t || !app.contains(t))return;
    ev.preventDefault();ev.stopPropagation();handleAction(t);
  });
  document.addEventListener("keydown",function(ev){
    if((catalogMenu||genreMenu)&&["ArrowDown","ArrowUp","Home","End"].includes(ev.key)){const buttons=Array.from(app.querySelectorAll(".original-menu:not([hidden]) button"));let n=buttons.indexOf(document.activeElement);n=ev.key==="Home"?0:ev.key==="End"?buttons.length-1:(n+(ev.key==="ArrowUp"?-1:1)+buttons.length)%buttons.length;if(buttons[n])buttons[n].focus();ev.preventDefault();return;}
    if(ev.key==="Escape"){
      if(catalogMenu||genreMenu||sidebarOpen||rowIndexOpen){catalogMenu=false;genreMenu=false;sidebarOpen=false;rowIndexOpen=false;render(true);}
      return;
    }
    if((ev.key==="Enter"||ev.key===" ")&&ev.target.matches("[role=button][data-action]")){
      ev.preventDefault();handleAction(ev.target);
    }
  });
  app.addEventListener("pointerdown",function(ev){
    if(ev.target.closest("#featured")) app.dataset.slideX=String(ev.clientX);
  });
  app.addEventListener("pointerup",function(ev){
    if(ev.target.closest("#featured")&&app.dataset.slideX){
      const dx=ev.clientX-Number(app.dataset.slideX);
      if(Math.abs(dx)>55)rotateHero(dx<0?1:-1);
      delete app.dataset.slideX;
    }
  });
  let dragged="";
  app.addEventListener("dragstart",function(ev){
    const t=ev.target.closest("[data-drag]");if(!t)return;dragged=t.dataset.drag;
    if(ev.dataTransfer)ev.dataTransfer.setData("text/plain",dragged);
  });
  app.addEventListener("dragover",function(ev){if(dragged&&ev.target.closest(".shelf-block"))ev.preventDefault();});
  app.addEventListener("drop",function(ev){
    const row=ev.target.closest(".shelf-block");if(!row||!dragged)return;
    ev.preventDefault();
    const toKey=row.id.replace("shelf-",""),from=shelfOrder.indexOf(dragged),to=shelfOrder.indexOf(toKey);
    if(from>=0&&to>=0){shelfOrder.splice(from,1);shelfOrder.splice(to,0,dragged);render(true);}
    dragged="";
  });
  app.addEventListener("dragend",function(){dragged="";});
  scroll.addEventListener("scroll",function(){updateDock();updateIndex();if((catalogMenu||genreMenu)&&Math.abs(scroll.scrollTop-pickerOpenedY)>1){catalogMenu=false;genreMenu=false;app.querySelectorAll(".original-menu").forEach(function(m){m.hidden=true;});}},{passive:true});
  window.addEventListener("resize",applyLayout);
  setupInspector();
  render(false);
  if(params.has("scroll"))window.requestAnimationFrame(function(){scroll.scrollTop=Math.max(0,Number(params.get("scroll"))||0);updateDock();});
})();