'use strict';
/* Standalone illustrative layout harness. No app services, network calls or repository mutations. */
const ASSETS='../../../../reference/desktop/assets/';
const icon=(s)=>ASSETS+'icons/'+s+'.svg';
const art=(s)=>ASSETS+s;
const PAGES=[['search.html','Search surfaces'],['biblio-explore.html','Biblio Explore'],['genre-index.html','Genre directory'],['genre-results.html','Genre results'],['continue-see-all.html','Continue See All']];
const PRESETS={smallPortrait:[354,792],smallLandscape:[792,354],'small-portrait':[354,792],'small-landscape':[792,354],'tablet-portrait':[800,1280],'tablet-landscape':[1280,800],portrait:[412,900],landscape:[900,412],smallPortrait:[354,792],smallLandscape:[792,354],desktop:[1280,700],tabletPortrait:[800,1280],tabletLandscape:[1280,800]};
const BOOKS=[
 {title:'Dune',author:'Frank Herbert',year:'1965',cover:'wallpaper/sagas/dune.jpg',genre:'Sci-Fi & Fantasy',synopsis:'Arrakis is the only source of the spice melange. Paul Atreides faces a struggle over the planet and its future.'},
 {title:'The Lord of the Rings',author:'J. R. R. Tolkien',year:'1954',cover:'wallpaper/sagas/lord-of-the-rings.jpg',genre:'Sci-Fi & Fantasy',synopsis:'A fellowship must carry the One Ring towards Mount Doom while war spreads across Middle-earth.'},
 {title:'Harry Potter and the Philosopher’s Stone',author:'J. K. Rowling',year:'1997',cover:'wallpaper/sagas/harry-potter.jpg',genre:'Young Adult',synopsis:'Harry discovers the wizarding world and starts his first year at Hogwarts School of Witchcraft and Wizardry.'},
 {title:'The Adventures of Sherlock Holmes',author:'Arthur Conan Doyle',year:'1892',cover:'wallpaper/sagas/sherlock-holmes.jpg',genre:'Mysteries & Thrillers',synopsis:'Twelve short stories follow Sherlock Holmes and Dr. Watson through mysteries in Victorian London.'},
 {title:'A Song of Ice and Fire',author:'George R. R. Martin',year:'1996',cover:'wallpaper/sagas/ice-and-fire.jpg',genre:'Sci-Fi & Fantasy',synopsis:'Great houses compete for rule of Westeros as an ancient danger gathers in the north.'},
 {title:'Percy Jackson & the Olympians',author:'Rick Riordan',year:'2005',cover:'wallpaper/sagas/percy-jackson.jpg',genre:'Young Adult',synopsis:'Percy Jackson learns of his connection to the Greek gods and the quests that follow.'},
 {title:'Jurassic Park',author:'Michael Crichton',year:'1990',cover:'wallpaper/sagas/jurassic-park.jpg',genre:'Sci-Fi & Fantasy',synopsis:'A theme park built around cloned dinosaurs becomes a cautionary tale of technology and control.'},
 {title:'The Witcher',author:'Andrzej Sapkowski',year:'1990',cover:'wallpaper/sagas/the-witcher.jpg',genre:'Sci-Fi & Fantasy',synopsis:'Geralt of Rivia, a monster hunter, travels through a world of warring kingdoms and magic.'}
];
const MANGA=[
 {title:'One Piece — Romance Dawn',author:'Eiichiro Oda',year:'1997',cover:'universes/one-piece/east-blue/canon/romance-dawn.png',genre:'Adventure',synopsis:'Luffy begins his quest to become King of the Pirates.',meta:'Manga · Publishing · ongoing'},
 {title:'One Piece — Baratie',author:'Eiichiro Oda',year:'1998',cover:'universes/one-piece/east-blue/canon/baratie.png',genre:'Adventure',synopsis:'The Straw Hats encounter the floating restaurant Baratie.',meta:'Manga · Adventure · ongoing'},
 {title:'One Piece — Arlong Park',author:'Eiichiro Oda',year:'1999',cover:'universes/one-piece/east-blue/canon/arlong-park.png',genre:'Adventure',synopsis:'The Straw Hats confront Arlong and his crew.',meta:'Manga · Adventure · ongoing'},
 {title:'One Piece — Loguetown',author:'Eiichiro Oda',year:'1999',cover:'universes/one-piece/east-blue/canon/loguetown.png',genre:'Adventure',synopsis:'A stop in the town where the Pirate King was executed.',meta:'Manga · Adventure · ongoing'},
 {title:'One Piece — Skypiea',author:'Eiichiro Oda',year:'2002',cover:'universes/one-piece/paradise/banners/skypiea.png',genre:'Fantasy',synopsis:'An adventure on an island among the clouds.',meta:'Manga · Fantasy · ongoing'},
 {title:'One Piece — Enies Lobby',author:'Eiichiro Oda',year:'2005',cover:'universes/one-piece/paradise/banners/enies-lobby.png',genre:'Action',synopsis:'A rescue mission takes the Straw Hats to a World Government stronghold.',meta:'Manga · Action · ongoing'},
 {title:'One Piece — Wano Country',author:'Eiichiro Oda',year:'2018',cover:'universes/one-piece/new-world/banners/wano-country.png',genre:'Adventure',synopsis:'The Straw Hats arrive in the closed country of Wano.',meta:'Manga · Adventure · ongoing'},
 {title:'One Piece — Egghead',author:'Eiichiro Oda',year:'2022',cover:'universes/one-piece/final-sea/banners/egghead.png',genre:'Sci-Fi',synopsis:'The crew reaches the futuristic island of Egghead.',meta:'Manga · Sci-Fi · ongoing'}
];
const FILMS=[
 {title:'Star Wars: A New Hope',author:'1977 · Movie',cover:'universes/star-wars/jakku.jpg',year:'1977',genre:'Adventure',synopsis:'Luke Skywalker joins the Rebellion against the Galactic Empire.'},
 {title:'The Empire Strikes Back',author:'1980 · Movie',cover:'universes/star-wars/hoth.jpg',year:'1980',genre:'Adventure',synopsis:'The Rebels are pursued after the Battle of Hoth.'},
 {title:'Return of the Jedi',author:'1983 · Movie',cover:'universes/star-wars/ahch-to.jpg',year:'1983',genre:'Adventure',synopsis:'The Rebellion challenges the Empire while Luke confronts Vader.'},
 {title:'Star Wars: The Force Awakens',author:'2015 · Movie',cover:'universes/star-wars/jakku.jpg',year:'2015',genre:'Sci-Fi',synopsis:'A scavenger becomes entangled in the conflict with the First Order.'},
 {title:'Star Wars: The Last Jedi',author:'2017 · Movie',cover:'universes/star-wars/ahch-to.jpg',year:'2017',genre:'Adventure',synopsis:'Rey seeks guidance while the Resistance faces the First Order.'},
 {title:'Star Wars: Revenge of the Sith',author:'2005 · Movie',cover:'universes/star-wars/mustafar.jpg',year:'2005',genre:'Action',synopsis:'The Republic becomes an Empire as Anakin falls to the dark side.'}
];
const CONTINUE=[
 {id:'c1',kind:'video',title:'The Empire Strikes Back',sub:'42 min left',cover:'universes/star-wars/hoth.jpg',progress:.61,watched:false},
 {id:'c2',kind:'manga',title:'One Piece — Romance Dawn',sub:'Chapter 4',cover:'universes/one-piece/east-blue/canon/romance-dawn.png',progress:.31,watched:false},
 {id:'c3',kind:'book',title:'Dune',sub:'Page 183',cover:'wallpaper/sagas/dune.jpg',progress:.45,watched:false},
 {id:'c4',kind:'comic',title:'Batman: The Animated Series',sub:'Issue 2',cover:'universes/dcau/portals/gotham.jpg',progress:.78,watched:false},
 {id:'c5',kind:'video',title:'Star Wars: A New Hope',sub:'Watched',cover:'universes/star-wars/coruscant.jpg',progress:1,watched:true},
 {id:'c6',kind:'book',title:'The Lord of the Rings',sub:'Page 57',cover:'wallpaper/sagas/lord-of-the-rings.jpg',progress:.12,watched:false},
 {id:'c7',kind:'manga',title:'One Piece — Enies Lobby',sub:'Chapter 10',cover:'universes/one-piece/paradise/banners/enies-lobby.png',progress:.59,watched:false},
 {id:'c8',kind:'book',title:'The Adventures of Sherlock Holmes',sub:'Watched',cover:'wallpaper/sagas/sherlock-holmes.jpg',progress:1,watched:true}
];
const GENRES_MANGA=['Action','Adventure','Avant Garde','Award Winning','Boys Love','Comedy','Drama','Fantasy','Girls Love','Gourmet','Horror','Mystery','Romance','Sci-Fi','Slice of Life','Sports','Supernatural','Suspense'];
const THEMES=['Adult Cast','Detective','Historical','Isekai','Martial Arts','Mythology','Psychological','School','Space','Time Travel','Urban Fantasy','Workplace'];
const DEMO=['Shounen','Shoujo','Seinen','Josei','Kids'];
const GENRES_BIBLIO=['Fiction & Literature','Mysteries & Thrillers','Sci-Fi & Fantasy','Romance','Biographies & Memoirs','History','Young Adult','Comics & Graphic Novels','Humor','Travel & Adventure'];
const GENRES_FILM=['Action','Drama','Comedy','Sci-Fi','Thriller','Horror','Romance','Animation','Adventure','Crime','Mystery','Fantasy','Documentary'];
const GENRE_DESCRIPTIONS={
 'Adventure':'Whether aiming for a specific goal or struggling to survive, the hero is thrust into unfamiliar lands and continually faces unexpected dangers. The story is how they react to the journey’s trials.',
 'Sci-Fi & Fantasy':'Reality with a lever pushed. Science fiction tests what might be true if technology or time took one more step; fantasy builds worlds from myth and magic.',
 'Mysteries & Thrillers':'A puzzle and a pulse. Mysteries bait the mind with a hidden truth; thrillers grip the body with escalating danger.',
 'Fantasy':'Worlds that run on rules that are not ours. Magic, myth, and invented orders — fantasy asks what changes when the impossible becomes ordinary.',
 'Action':'Fists, blades, and the will to use them. Action lives in the clash itself — the choreography of conflict.',
 'Romance':'The pull between two people, and everything in the way. Romance follows the distance closing — or not.',
 'Drama':'The weight of being human. Drama leans into conflict that is emotional rather than physical.',
 'Sci-Fi':'What-if, made rigorous. Science fiction extrapolates from technology and its consequences.'
};
const SHELF_NAMES=['Top 10','Popular','Top Rated','New Releases','Trending','Most Read','Classics'];
const MOSAICS=['Fiction','Nonfiction','Audience'];
const PARAMS=new URLSearchParams(location.search);
const STATE=PARAMS.get('state')||document.body.dataset.state||'';
const page=document.body.dataset.page||'search';
const requestedPreset=PARAMS.get('preset')||'portrait';
const defaultSize=PRESETS[requestedPreset]||PRESETS.portrait;
const S={page:page,preset:requestedPreset,width:defaultSize[0],height:defaultSize[1],top:+PARAMS.get('top')||0,bottom:+PARAMS.get('bottom')||0,left:0,right:0,font:+PARAMS.get('font')||1,keyboard:STATE==='keyboard',
 lane:PARAMS.get('lane')||document.body.dataset.lane|| (page==='search'?'Biblio':page==='genre-index'||page==='genre-results'?'Tankoban':'Biblio'),searchState:STATE==='empty'?'empty':STATE==='browse'?'browse':'results',
 query:'the',recent:['Dune','The Lord of the Rings','Harry Potter and the Philosopher’s Stone'],browseGenre:'Adventure',genre:PARAMS.get('genre')||document.body.dataset.genre||'Adventure',sort:'readers',compact:false,expanded:{},library:{},edit:false,hidden:[],order:SHELF_NAMES.slice(),railOpen:STATE==='rail',selectedRail:0,activeTab:'Explore',pinned:'',continueSort:'recent',medium:'All',scope:PARAMS.get('scope')||'home',removed:[],showExplicit:false,showExt:false,continueMenu:null};
const capture=PARAMS.get('capture')==='1'||document.body.classList.contains('capture');
const h=(s)=>String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');
const cap=(s)=>s.charAt(0).toUpperCase()+s.slice(1);
const esc=(s)=>h(s);
const choose=(items)=>items.map((x,i)=>Object.assign({rank:i+1},x));
const kindLabel=(l)=>l==='Biblio'?'BIBLIO · BOOKS':l==='Theatre'?'THEATRE · MOVIES':'TANKOBAN · MANGA';
const arrForLane=(l)=>l==='Biblio'?BOOKS:l==='Theatre'?FILMS:MANGA;
const pageHref=(name)=>name;
const img=(src,cls,alt)=>'<img class="'+(cls||'')+'" src="'+h(art(src))+'" alt="'+h(alt||'')+'" loading="lazy" onerror="this.classList.add(\'asset-error\')">';
function iconButton(name,title,action,extra){return '<button class="icon-control '+(extra||'')+'" data-action="'+action+'" aria-label="'+h(title)+'">'+img('icons/'+name+'.svg','icon',title)+'</button>';}
function chrome(showSearch){return '<div class="fixed-chrome"><button class="back-capsule" data-action="back" aria-label="Back"><svg class="back-vector" width="20" height="20" viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 8 12 14.5 18.5" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button><div class="sys-controls">'+(showSearch?iconButton('search','Search','search-link'):'')+iconButton('minimize','Minimize','noop','desktop-only')+iconButton('fullscreen','Toggle fullscreen','noop','desktop-only')+iconButton('power','Close','noop','desktop-only')+'</div></div>';}
function stickyHeader(title){const d=new Date(),hr=d.getHours()%12||12,minute=String(d.getMinutes()).padStart(2,'0'),hh=hr+':'+minute,amp=d.getHours()<12?'AM':'PM',date=d.toLocaleDateString('en-US',{weekday:'long',month:'long',day:'numeric'});return '<div class="world-shell"><div class="top-shell"><div class="shell-left"><button data-action="back">‹ Home</button><div class="shell-clock"><div class="main-clock">'+hh+' <small>'+amp+'</small></div><small>'+date+'</small></div></div><div class="phone-clock"><span>'+hh+'</span><small>'+amp+'</small></div><div class="worlds">'+['Tankoban','Biblio','Theatre','Universes','Feria','Vinyl'].map(n=>'<button class="world '+(n==='Biblio'?'current':'')+'" data-world="'+n+'" '+(['Feria','Vinyl'].includes(n)?'disabled':'')+'>'+n+(['Feria','Vinyl'].includes(n)?' <small>SOON</small>':'')+'</button>').join('')+'</div><div class="sys-controls">'+iconButton('search','Search','search-link') +iconButton('trackers','Trackers','noop')+'<button class="account-circle" data-action="noop" aria-label="Account"></button>'+iconButton('settings','Wallpaper','noop')+'</div></div><div class="world-tabs">'+['Discover','Explore','Library'].map(t=>'<button class="'+(S.activeTab===t?'current':'')+'" data-action="tab" data-value="'+t+'">'+t+'</button>').join('')+'</div></div>';}
function flagAction(message){const el=document.getElementById('status');if(el)el.textContent=message+' · local demonstration only; no services called.';}
function aButton(action,value,label,css){return '<button class="'+(css||'pill')+'" data-action="'+h(action)+'" data-value="'+h(value||'')+'">'+h(label)+'</button>';}
function headerTitle(label,subtitle,number){return '<div class="kicker">'+h(label)+'</div><h1 class="index-title">'+h(subtitle)+'</h1><div class="countline"><b>'+h(number)+'</b> genres'+(S.lane==='Tankoban'||S.lane==='Anime'?', four ways in — by genre, theme, and who they’re for.':' — every shelf in the house.')+'</div><div style="height:20px"></div><div class="gold-rule"></div>';}
function formatLane(){return S.lane==='Anime'?'THEATRE · ANIME':S.lane==='Theatre'?'THEATRE · MOVIES':kindLabel(S.lane);}
function laneArt(){return S.lane==='Biblio'?BOOKS:S.lane==='Theatre'||S.lane==='Anime'?FILMS:MANGA;}
function directoryGroups(){
 if(S.lane==='Biblio')return [{group:'Genres',sub:'The core shelves',names:GENRES_BIBLIO}];
 if(S.lane==='Theatre')return [{group:'Genres',sub:'The core shelves',names:GENRES_FILM}];
 return [{group:'Genres',sub:'The core shelves',names:GENRES_MANGA}].concat(S.showExplicit?[{group:'Explicit Genres',sub:'',names:['Erotica','Hentai']}]:[]).concat([{group:'Themes',sub:'Stories by theme',names:THEMES},{group:'Demographics',sub:'Who they’re for',names:DEMO}]);
}
function genreImageAt(i){const l=laneArt();return l[i%l.length].cover;}
function genreTile(name,index){
 return '<button class="genre-tile" type="button" data-action="genre-pick" data-value="'+h(name)+'" aria-label="Open '+h(name)+' genre">'+img(genreImageAt(index),'','Local representative project artwork for '+name)+'<span>'+h(name)+'</span><small>'+h((index+2)*1300>=10000?Math.round((index+2)*1.3)+'k':((index+2)*1.3).toFixed(1)+'k')+'</small></button>';
}
function renderGenreIndex(){
 const groups=directoryGroups();const count=groups.reduce((s,g)=>s+g.names.length,0);
 return '<div class="wall"></div><div class="app-inner">'+chrome(true)+'<div class="scroll"><div class="scroll-body genre-index-body">'+headerTitle(formatLane(),'Explore Genres',count)+groups.map((g,i)=>'<section class="directory-section"><div class="section-heading"><h2 class="section-title" style="margin:0">'+h(g.group)+'</h2><span class="count-badge">'+g.names.length+'</span><span class="section-subtitle">'+h(g.sub)+'</span></div><div class="genre-index-grid">'+g.names.map((name,n)=>genreTile(name,n+i*3)).join('')+'</div></section>').join('')+'</div></div></div>';
}
function genreDetailCard(item,index){
 const selected=!!S.library[item.title];const tags=[S.genre,item.genre&&item.genre!==S.genre?item.genre:'',S.lane==='Biblio'?'Books':'Adventure'].filter((x,i,a)=>x&&a.indexOf(x)===i);
 return '<article class="detail-card"><span class="rank">'+('0'+(index+1)).slice(-2)+'</span>'+
 img(item.cover,'detail-art',item.title+' project artwork')+
 '<div class="detail-main"><button class="detail-name" data-action="item-open" data-value="'+h(item.title)+'" style="padding-left:0;text-align:left;color:var(--ink);border:0;background:none">'+h(item.title)+'</button>'+
 '<div class="detail-meta">'+h(item.meta||[S.lane==='Biblio'?'Book':S.lane==='Theatre'?'Movie':'Manga',item.year,S.lane==='Biblio'||S.lane==='Theatre'?'': 'Completed'].filter(Boolean).join(' · '))+'</div>'+
 '<div class="micro-tags">'+tags.map(t=>'<span class="micro-tag '+(t===S.genre?'active':'')+'">'+h(t)+'</span>').join('')+'</div>'+
 '<p class="card-synopsis">'+h(item.synopsis||'')+'</p>'+
 '<div class="detail-foot">'+(S.lane==='Biblio'?'': '<span class="rating"><em>★</em> '+(item.score||8.4).toFixed(1)+'</span>')+
 '<span>'+h(S.lane==='Biblio'?item.author:(item.readers||4)+'K '+(S.lane==='Theatre'?'watchers':'readers'))+'</span>'+
 '<button class="library-btn '+(selected?'selected':'')+'" data-action="library" data-value="'+h(item.title)+'">'+(selected?'✓ In Library':'+ Library')+'</button></div></div></article>';
}
function genreCoverCard(item,index){
 return '<button class="cover-card" data-action="item-open" data-value="'+h(item.title)+'" aria-label="Open '+h(item.title)+'">'+img(item.cover,'',item.title+' local project artwork')+(S.lane==='Biblio'?'':'<span class="score">★ '+(item.score||8.4).toFixed(1)+'</span>')+'<span class="label">'+h(item.title)+'</span></button>';
}
function renderGenreResults(){
 const items=laneArt().filter(x=>x.genre===S.genre).map((x,i)=>({...x,score:8.1+(i%4)*.2,readers:8-i}));if(S.sort==='score')items.sort((a,b)=>b.score-a.score);const sourceDescriptions=S.lane==='Biblio'?OFFICIAL_BIBLIO_DESCS:S.lane==='Theatre'?OFFICIAL_THEATRE_DESCS:OFFICIAL_MANGA_DESCS;const desc=sourceDescriptions[S.genre]||'';const sibling=S.lane==='Biblio'?GENRES_BIBLIO:S.lane==='Theatre'?GENRES_FILM:GENRES_MANGA;
 const hero='<header class="genre-hero"><div class="genre-montage">'+items.slice(0,4).map(i=>img(i.cover,'',i.title)).join('')+'</div><div class="genre-hero-text"><div class="kicker">'+formatLane()+' · <span class="accent">GENRE</span></div><h1 class="genre-name">'+h(S.genre)+'</h1><p class="genre-desc">'+h(desc)+'</p><div class="genre-count"><span class="gold-rule"></span><span><strong>'+items.length+'</strong> titles · sorted by '+(S.sort==='score'?'score':S.lane==='Theatre'?'watchers':'readers')+'</span></div></div></header>';
 const siblingPills='<div class="pill-row">'+sibling.map(t=>aButton('genre-select',t,t,'pill '+(S.genre===t?'active':''))).join('')+aButton('explore-link','','Explore','pill explore')+'</div>';
 const sort=S.lane==='Theatre'?'':
 '<button class="sort-control" data-action="sort-toggle"><span>Sorted by </span><b>'+h(S.sort==='score'?'Score':'Readers')+'</b> ⇅</button>';
 const controls='<div class="genre-toolbar"><div class="eyebrow" style="margin:0">'+(S.sort==='score'?'TOP RATED':S.lane==='Theatre'?'MOST WATCHED':'MOST READ')+'</div><div style="display:flex;gap:12px;align-items:center;flex-wrap:wrap">'+sort+'<div class="view-toggle"><button class="'+(!S.compact?'selected':'')+'" data-action="view-detail" aria-label="Detailed grid view">▤</button><button class="'+(S.compact?'selected':'')+'" data-action="view-covers" aria-label="Cover grid view">▦</button></div></div></div>';
 return '<div class="wall"></div><div class="app-inner">'+chrome(true)+'<div class="scroll"><div class="scroll-body">'+hero+'<div class="genre-body">'+siblingPills+controls+'<div class="genre-grid '+(S.compact?'covers':'')+'">'+(items.length?items.map((item,i)=>S.compact?genreCoverCard(item,i):genreDetailCard(item,i)).join(''):'<p class="no-results">Nothing in this genre yet — try another.</p>')+'</div></div></div></div></div>';
}
function searchResult(item,audio){
 return '<button class="search-result" data-action="item-open" data-value="'+h(item.title)+'" aria-label="Open '+h(item.title)+'"><span class="poster-wrap">'+img(item.cover,'poster',item.title)+(audio?img('icons/music.svg','audio-mark','Audiobook'):'')+'</span><span class="result-copy"><span class="result-text">'+h(item.title)+'</span><span class="result-sub">'+h(item.author||item.year||'')+'</span></span></button>';
}
function searchResultsSection(title,items,audio,total){
 const key=title;const cols=S.preset!=='desktop'&&S.width<840?1:6;const expanded=!!S.expanded[key];const shown=expanded?items:items.slice(0,cols);
 return '<section class="search-section"><div class="eyebrow">'+(audio?img('icons/music.svg','icon','Audiobooks'):'')+h(title.toUpperCase())+'  ·  '+(total||items.length)+(S.lane==='Biblio'?' FOUND':'')+'</div><div class="result-grid">'+shown.map(i=>searchResult(i,audio)).join('')+'</div>'+
 (items.length>cols?'<button class="more-pill" data-action="see-more" data-value="'+h(key)+'">'+(expanded?'Show less  ▴':'See more · '+(items.length-cols)+'  ▾')+'</button>':'')+'</section>';
}
function searchEmpty(){
 const isBib=S.lane==='Biblio';
 const recent='<section><div class="eyebrow">'+(isBib?'RECENT':'RECENT SEARCHES')+'</div><div class="chips-flow">'+S.recent.map((x,i)=>'<button class="recent-chip" data-action="recent" data-value="'+h(x)+'"><span>'+h(x)+'</span><span class="remove" data-action="recent-remove" data-value="'+h(x)+'">×</span></button>').join('')+'</div></section>';
 const jumps=isBib?'<section class="search-section"><div class="eyebrow">JUMP TO</div><div class="chips-flow">'+['Home','Top 10','Genres'].map((x,i)=>aButton('jump-to',x,x,'jump-button')).join('')+'</div></section>':'';
 const browse='<section class="search-section"><div class="eyebrow">'+(isBib?'BROWSE A GENRE':'TRY A GENRE')+'</div><div class="chips-flow">'+(isBib?['Fiction','Mystery & Thriller','Romance','Sci-Fi & Fantasy','Biography','History','Nonfiction','Young Adult']:(S.lane==='Theatre'?GENRES_FILM:GENRES_MANGA).slice(0,8)).map(x=>aButton(isBib?'genre-search':'browse-genre',x,x,'pill')).join('')+'</div></section>';
 const surprise=isBib?'':'<section class="search-section" style="text-align:center"><button data-action="surprise" class="subtle-action">'+img('icons/star.svg','icon','Surprise me')+' Surprise me</button></section>';
 return recent+'<div class="empty-gap"></div>'+jumps+browse+surprise;
}
function searchBrowse(){
 const genre=S.browseGenre;const items=laneArt().filter(x=>x.genre===genre);
 return '<div class="browse-inline-header"><button class="back-capsule" data-action="browse-close" aria-label="Back to search" style="width:auto;padding:0 12px;font:13px var(--ui)">‹ Search</button><div><small>BROWSING</small><h2>'+h(genre)+'</h2></div></div><div style="height:24px"></div><div class="result-grid">'+(items.length?items.map(i=>searchResult(i,false)).join(''):'<p class="no-results">No results</p>')+'</div>';
}
function renderSearch(){
 const isBib=S.lane==='Biblio';const query=S.query.toLowerCase().trim();const items=laneArt().filter(x=>[x.title,x.author,x.genre].some(t=>String(t||'').toLowerCase().includes(query)));const top=items[0]||laneArt()[0];const empty=S.searchState==='empty';const browsing=S.searchState==='browse';
 const searchHeader='<div class="search-header"><button class="back-capsule search-back" data-action="back" aria-label="Back to world"><svg class="back-vector" width="20" height="20" viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 8 12 14.5 18.5" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg><span>Back</span></button><div class="search-field">'+img('icons/search.svg','glass-icon','Search')+'<input id="searchInput" type="text" value="'+h(S.query)+'" placeholder="'+(isBib?'Search books…':S.lane==='Theatre'?'Search movies, series, anime…':'Search manga…')+'" aria-label="Search">'+(S.query.length?'<button class="clear" data-action="search-clear" aria-label="Clear search">×</button>':'')+'<button class="esc-key" data-action="back">Esc</button></div><div class="sys-controls">'+iconButton('minimize','Minimize','noop')+iconButton('fullscreen','Fullscreen','noop')+iconButton('power','Close','noop')+'</div></div>';
 const topMatch='<div class="eyebrow top-match-label">TOP MATCH</div><div class="top-match"><div class="top-match-content">'+img(top.cover,'',top.title+' local artwork')+'<div class="top-copy"><h2>'+h(top.title)+'</h2><div class="meta">'+h((isBib?[top.genre,top.author,top.year]:[S.lane==='Theatre'?'MOVIE':'MANGA',top.author||top.year,top.year]).filter(Boolean).join(' · ')).toUpperCase()+'</div>'+(isBib?'':'<p class="synopsis">'+h(top.synopsis)+'</p>')+'</div></div><button class="gold-button" data-action="item-open" data-value="'+h(top.title)+'">Open &nbsp;›</button></div>';
 let body='';
 if(empty)body=searchEmpty();
 else if(browsing)body=searchBrowse();
 else if(!items.length)body='<p class="no-results">'+(isBib?'No books found':'No results')+'</p>';
 else body=topMatch+(isBib?searchResultsSection('Books',items.slice(1),false,items.length)+(items.length>1?searchResultsSection('Audiobooks',items.slice(1,6),true):''):S.lane==='Theatre'?searchResultsSection('Movies',items.slice(1,5),false)+searchResultsSection('Series',items.slice(4),false):searchResultsSection('Manga',items.slice(1),false));
 return '<div class="wall"></div><div class="app-inner">'+searchHeader+'<div class="scroll search-scroll"><div class="scroll-body search-content">'+body+'</div></div></div>';
}
function rowGlyph(name){
 let paths='<path d="M4 6h16M4 12h16M4 18h16"/>';
 if(/top|rated|popular|best/i.test(name))paths='<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9z"/>';
 else if(/recent|new|release|continue/i.test(name))paths='<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>';
 else if(/collection|library|classic|shelf/i.test(name))paths='<path d="M4 4v16h5V4zM9 6h5v14H9zM16 4l4 15-4 1-4-15z"/>';
 else if(/explore|discover|genre/i.test(name))paths='<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5z"/>';
 return '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+paths+'</svg>';
}
function rowRail(labels,world){
 return '<aside class="row-rail '+(S.railOpen?'open':'')+'"><button class="rail-toggle" data-action="rail-toggle" title="'+(S.railOpen?'Retract sidebar':'Expand sidebar')+'" aria-label="'+(S.railOpen?'Retract sidebar':'Expand sidebar')+'">'+(S.railOpen?'‹':'›')+'</button>'+
 (S.railOpen?'<div class="rail-brand"><small>WORLD</small>'+h(world)+'</div>':'')+
 '<div class="rail-list">'+labels.map((name,index)=>'<button class="rail-entry '+(S.selectedRail===index?'current':'')+'" data-action="rail-jump" data-value="'+h(name)+'" aria-label="Go to '+h(name)+'">'+rowGlyph(name)+'<span>'+h(name)+'</span></button>').join('')+'</div></aside>';
}
function bookCard(book,idx,ranked){
 return '<button class="book-card '+(ranked?'ranked':'')+'" data-action="item-open" data-value="'+h(book.title)+'" aria-label="Open '+h(book.title)+'">'+(ranked?'<span class="big-rank">'+(idx+1)+'</span>':'')+
 img(book.cover,'book-cover',book.title+' local artwork')+'<span class="book-name">'+h(book.title)+'</span><span class="author">'+h(book.author)+'</span><span class="source">'+(book.source||'Apple Books')+'</span></button>';
}
function shelfBlock(name,idx){
 const hidden=S.hidden.includes(name);if(hidden&&!S.edit)return '';
 const books=BOOKS.slice(idx%BOOKS.length).concat(BOOKS.slice(0,idx%BOOKS.length));
 let editButtons='';
 if(S.edit)editButtons='<div class="edit-actions"><button data-action="move-up" data-value="'+h(name)+'" aria-label="Move '+h(name)+' up">▲</button><button data-action="move-down" data-value="'+h(name)+'" aria-label="Move '+h(name)+' down">▼</button><button class="hide" data-action="hide-shelf" data-value="'+h(name)+'">'+(hidden?'Show':'Hide')+'</button><button data-action="reorder-handle" data-value="'+h(name)+'" title="Drag to reorder · Ctrl+Shift+Up/Down" aria-label="Reorder '+h(name)+'">⠿</button></div>';
 return '<section class="shelf '+(hidden?'hidden-shelf':'')+'" data-shelf="'+h(name)+'" id="shelf-'+idx+'">'+editButtons+
 '<header class="shelf-header"><h2>'+h(name)+'</h2><button class="see-all" data-action="see-all" data-value="'+h(name)+'">See All</button></header>'+
 '<div class="shelf-track">'+books.map((b,i)=>bookCard({...b,source:['Most Read','Classics'].includes(name)?'Open Library':'Apple Books'},i,name==='Top 10')).join('')+'</div></section>';
}
function mosaicCard(name,idx){
 const imgs=BOOKS.slice(idx*2).concat(BOOKS).slice(0,7);
 return '<button class="mosaic-card" data-action="mosaic-pin" data-value="'+h(name)+'"><span class="mosaic-books">'+imgs.map(x=>img(x.cover,'',x.title)).join('')+'</span><h3>'+h(name)+'</h3></button>';
}
function renderPinnedDiscover(){
 const name=S.pinned||'Popular';const sources=['Apple Books','Open Library'];const items=BOOKS;const genres=GENRES_BIBLIO;
 const sourceRail='<aside class="row-rail discover-source-rail '+(S.railOpen?'open':'')+'"><button class="rail-toggle" data-action="rail-toggle" aria-label="'+(S.railOpen?'Retract sources':'Expand sources')+'">'+(S.railOpen?'‹':'›')+'</button>'+(S.railOpen?'<div class="rail-brand"><small>WORLD</small>Biblio</div>':'')+'<div class="rail-list">'+sources.map((x,i)=>'<button class="rail-entry '+(x===(S.source||'Apple Books')?'current':'')+'" data-action="source-select" data-value="'+h(x)+'">'+img(i===0?'icons/books.svg':'icons/book-library.svg','icon',x)+'<span>'+h(x)+'</span></button>').join('')+'</div></aside>';
 const picker='<div class="discover-pickers"><div class="select-wrap"><button class="sort-control" data-action="category-open">'+h(name)+' &nbsp;⌄</button>'+(S.categoryOpen?'<div class="original-popup">'+SHELF_NAMES.map(x=>'<button data-action="category-pick" data-value="'+h(x)+'">'+h(x)+'</button>').join('')+'</div>':'')+'</div><div class="select-wrap"><button class="sort-control" data-action="filter-open">Filters &nbsp;⌄</button>'+(S.filterOpen?'<div class="original-popup">'+['All',...genres].map(x=>'<button class="'+(S.filter===x?'active':'')+'" data-action="filter-pick" data-value="'+h(x)+'">'+h(x)+'</button>').join('')+'</div>':'')+'</div></div>';
 const displayed=S.filter&&S.filter!=='All'?items.filter((x)=>x.genre===S.filter):items;
 return '<div class="explore-layout discover-layout">'+sourceRail+'<div class="explore-main"><div class="discover-heading"><button data-action="explore-return" class="subtle-action">‹ Explore</button><div class="kicker">BIBLIO · BOOKS</div><h1 class="serif">'+h(name)+'</h1><p class="countline">'+displayed.length+' books</p>'+picker+'</div><div class="discover-wall">'+displayed.map((b,i)=>bookCard(b,i,false)).join('')+'</div></div></div>';
}
function batchWorldNav(){if(S.preset==='desktop')return '';const names=['Theatre','Tankoban','Biblio','Universes','Profile'],icons=['projector-theatre','comic-book','book-library','universes-atom','your-colosseum-laurel'];return '<nav class="batch-world-nav" aria-label="Worlds">'+names.map((n,i)=>'<button data-world="'+n+'" class="'+(n==='Biblio'?'current':'')+'">'+img('icons/'+icons[i]+'.svg','',n)+'<span>'+n+'</span></button>').join('')+'</nav>';}
function updateShelfIndex(){const sc=document.querySelector('.explore-scroll');if(!sc)return;const entries=[...document.querySelectorAll('.rail-entry[data-action="rail-jump"]')];let active=entries[0];for(const e of entries){const ix=S.order.indexOf(e.dataset.value),row=ix>=0?document.getElementById('shelf-'+ix):document.querySelectorAll('.mosaic-card')[MOSAICS.indexOf(e.dataset.value)];if(row&&row.getBoundingClientRect().top<=sc.getBoundingClientRect().top+60)active=e;}entries.forEach(e=>{e.classList.toggle('current',e===active);e.setAttribute('aria-current',e===active?'true':'false');});}

function renderExplore(){
 const labels=S.order.filter(x=>S.edit||!S.hidden.includes(x)).concat(MOSAICS);const body=S.activeTab==='Discover'&&S.pinned?renderPinnedDiscover():
 '<div class="explore-layout">'+rowRail(labels,'Biblio')+'<div class="explore-main"><div class="explore-head"><h1>Explore</h1><button class="subtle-action" data-action="edit-shelves">'+(S.edit?'Done':'Customize shelves')+'</button></div>'+
 S.order.map((name,index)=>shelfBlock(name,index)).join('')+
 '<div class="mosaic-list">'+MOSAICS.map((name,i)=>mosaicCard(name,i)).join('')+'</div></div></div>';
 return '<div class="wall"></div>'+batchWorldNav()+'<div class="app-inner">'+stickyHeader('Biblio')+'<div class="scroll explore-scroll"><div class="scroll-body">'+body+'</div></div></div>';
}
function continueItems(){
 let out=CONTINUE.filter(item=>!S.removed.includes(item.id));
 if(S.scope==='video')out=out.filter(x=>x.kind==='video');
 else if(S.scope==='book')out=out.filter(x=>x.kind==='book');
 else if(S.scope==='tankoban')out=out.filter(x=>x.kind==='manga'||x.kind==='comic'||x.kind==='tankoban');
 if(S.scope==='home'&&S.medium!=='All')out=out.filter(x=>S.medium==='Manga'?(x.kind==='manga'||x.kind==='tankoban'):S.medium==='Comics'?x.kind==='comic':S.medium==='Books'?x.kind==='book':x.kind==='video');
 if(S.continueSort==='watched')out=out.filter(x=>x.watched);
 if(S.continueSort==='unwatched')out=out.filter(x=>!x.watched);
 if(S.continueSort==='az'||S.continueSort==='za')out.sort((a,b)=>S.continueSort==='az'?a.title.localeCompare(b.title):b.title.localeCompare(a.title));
 return out;
}
function continueTile(item){
 return '<article class="continue-tile '+(item.watched?'watched':'')+'" data-action="continue-detail" data-value="'+h(item.id)+'" tabindex="0" role="button" aria-label="Open '+h(item.title)+' details">'+
 img(item.cover,'',item.title+' local project artwork')+'<div class="tile-badge">'+(item.kind==='video'?'VIDEO':item.kind==='manga'?'MANGA':item.kind==='comic'?'COMIC':'BOOK')+'</div>'+
 '<button class="resume" data-action="continue-resume" data-value="'+h(item.id)+'" aria-label="Resume '+h(item.title)+'">'+(img('icons/'+(item.kind==='video'?'play':item.kind==='book'?'books':'manga')+'.svg','','Resume'))+'</button>'+
 '<button class="forget" data-action="continue-remove" data-value="'+h(item.id)+'" aria-label="Forget '+h(item.title)+'">×</button>'+
 '<div class="tile-copy"><div class="tile-name">'+h(item.title)+'</div><div class="tile-sub">'+h(item.sub)+'</div></div>'+
 (item.watched?'<span class="watched-mark">✓</span>':'')+'<div class="progress"><span style="width:'+(Math.max(0,Math.min(1,item.progress))*100)+'%"></span></div>'+(S.continueMenu===item.id?'<div class="tile-context" role="menu" aria-label="Continue actions">'+[['detail','Open details'],['resume','Resume'],['remove','Remove']].map(([k,t])=>'<button role="menuitem" data-action="context-'+k+'" data-value="'+h(item.id)+'">'+t+'</button>').join('')+'</div>':'')+'</article>';
}
function renderContinue(){
 const items=continueItems();
 const title=S.scope==='video'?'Continue Watching':S.scope==='home'?'Continue':'Continue Reading';
 const kicker=S.scope==='video'?'THEATRE · CONTINUE':S.scope==='book'?'BIBLIO · CONTINUE':S.scope==='tankoban'?'TANKOBAN · CONTINUE':'EVERYTHING · CONTINUE';
 const sorts=[['recent','Last Watched'],['az','A–Z'],['za','Z–A'],['watched','Watched'],['unwatched','Not Watched']];
 const sortHtml='<div class="pill-row continue-sort">'+sorts.map(([key,label])=>aButton('continue-sort',key,label,'pill small '+(S.continueSort===key?'active':''))).join('')+'</div>';
 const mediumHtml=S.scope==='home'?'<div class="pill-row continue-media">'+['All','Video','Manga','Comics','Books'].map(x=>aButton('continue-medium',x,x,'pill small '+(S.medium===x?'active':''))).join('')+'</div>':'';
 const head='<div class="continue-head"><div class="continue-back-row"><button class="back-plain" data-action="back" aria-label="Back"><svg class="back-vector" width="20" height="20" viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 8 12 14.5 18.5" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button><span class="kicker">'+kicker+'</span></div><h1>'+title+'</h1><p class="countline"><b>'+items.length+'</b> '+(items.length===1?'title':'titles')+'</p><div style="height:20px"></div><div class="gold-rule"></div>'+sortHtml+mediumHtml+'</div>';
 return '<div class="wall"></div><div class="app-inner">'+chromeSystemOnly()+'<div class="scroll"><div class="scroll-body">'+head+(items.length?'<div class="continue-grid">'+items.map(continueTile).join('')+'</div>':'<p class="serif italic" style="padding:30px var(--pad);color:var(--dim)">'+(CONTINUE.filter(x=>!S.removed.includes(x.id)).filter(x=>S.scope==='video'?x.kind==='video':S.scope==='book'?x.kind==='book':S.scope==='tankoban'?['manga','comic'].includes(x.kind):true).length===0?'Nothing to continue.':'Nothing here yet.')+'</p>')+'</div></div></div>';
}
function initControls(){
 const host=document.getElementById('controls');if(!host)return;
 const selectOptions=(list,value)=>list.map(([key,label])=>'<option value="'+h(key)+'" '+(key===value?'selected':'')+'>'+h(label)+'</option>').join('');
 const presetList=Object.keys(PRESETS).map(k=>[k,k+' · '+PRESETS[k].join(' × ')]);
 const linkList=PAGES.map(([file,title])=>'<a class="'+(page===file.replace('.html','')?'active':'')+'" href="'+h(file)+'">'+h(title)+'</a>').join('');
 host.innerHTML='<div class="kicker" style="color:var(--gold)">COLOSSEUM · DESIGN REVIEW</div><h1>'+h(PAGES.find(x=>x[0]===page+'.html')?.[1]||'Browse mocks')+'</h1><p>Standalone local HTML fitting study. These controls and notes are outside the Colosseum app surface.</p><h2>Pages</h2><nav>'+linkList+'</nav>'+
 '<h2>Viewport · provisional logical px</h2><label>Preset<select data-control="preset">'+selectOptions(presetList,S.preset)+'</select></label><div class="two"><label>Width<input data-control="width" type="number" min="260" max="1920" value="'+S.width+'"></label><label>Height<input data-control="height" type="number" min="260" max="1920" value="'+S.height+'"></label></div>'+
 '<h2>Safe-area insets · px</h2><div class="two">'+['top','bottom','left','right'].map(k=>'<label>'+cap(k)+'<input data-control="'+k+'" type="number" min="0" max="120" value="'+S[k]+'"></label>').join('')+'</div>'+
 '<label>Font scale<input data-control="font" type="number" min="1" max="1.4" step="0.05" value="'+S.font+'"></label><label class="row-label"><input type="checkbox" data-control="keyboard" '+(S.keyboard?'checked':'')+'>Reserve phone keyboard space (no OS chrome)</label>'+
 (page==='search'?'<h2>Search study states</h2><label>World<select data-control="lane">'+selectOptions([['Biblio','Biblio · Books & audiobooks'],['Theatre','Theatre · Movies & series'],['Tankoban','Tankoban · Manga']],S.lane)+'</select></label><label>State<select data-control="searchState">'+selectOptions([['results','Populated results'],['empty','Recent / empty'],['browse','Inline genre browse']],S.searchState)+'</select></label>':'')+
 (page==='genre-index'||page==='genre-results'?'<h2>Genre study states</h2><label>World<select data-control="lane">'+selectOptions([['Tankoban','Tankoban · Manga'],['Theatre','Theatre · Movies'],['Biblio','Biblio · Books'],['Anime','Theatre · Anime']],S.lane)+'</select></label>'+ (page==='genre-index'?'<label class="row-label"><input type="checkbox" data-control="showExplicit" '+(S.showExplicit?'checked':'')+'> Show explicit group (approved preference)</label>':'<label>Genre<select data-control="genre">'+selectOptions((S.lane==='Biblio'?GENRES_BIBLIO:S.lane==='Theatre'?GENRES_FILM:GENRES_MANGA).map(x=>[x,x]),S.genre)+'</select></label>'):'')+
 (page==='biblio-explore'?'<h2>Explore study state</h2><label class="row-label"><input type="checkbox" data-control="railOpen" '+(S.railOpen?'checked':'')+'> Expanded original row index</label><label class="row-label"><input type="checkbox" data-control="edit" '+(S.edit?'checked':'')+'> Customize shelves</label>':'')+
 (page==='continue-see-all'?'<h2>Continue scope</h2><label>Original entry scope<select data-control="scope">'+selectOptions([['home','Everything · Continue'],['video','Theatre · Continue Watching'],['tankoban','Tankoban · Continue Reading'],['book','Biblio · Continue Reading']],S.scope)+'</select></label>':'')+
 '<h2>Fixture & review limits</h2><div class="fixture"><p style="margin-top:0">The displayed works, poster substitutions, ratings, counts, progress positions and metadata are illustrative, not claims of app data. Art comes from the local frozen project assets (saga wallpapers, universe scenes and chapter banners).</p><p class="muted">The OnePlus is disconnected. These are provisional logical viewport sizes, not measured device dimensions. No app data or provider is called.</p></div><div class="status" id="status">Interactions are local HTML demonstrations only.</div>';
}
function applyViewport(){
 const frame=document.getElementById('appFrame'),holder=document.getElementById('holder'),stage=document.querySelector('.preview-stage');
 const kind=S.page==='search'?(S.preset!=='desktop'&&S.width<840?'phone':'desktop'):(S.preset.startsWith('tablet')?'tablet':Math.min(S.width,S.height)<600?'phone':'desktop');
 frame.className='app-frame '+S.page+' '+(S.preset!=='desktop'?(S.width>=840?'shell-wide':'shell-bottom'):'desktop-host')+' '+kind+' '+(S.width>S.height?'landscape':'portrait')+' '+(S.page==='search'?'search search-layout'+(S.lane==='Biblio'?' biblio-search':''):'')+(S.page==='genre-results'&&S.genre.length>=16&&S.width<S.height?' long-genre':'')+(S.font>=1.2?' enlarged-font':'')+(S.keyboard?' ime-active':'');
 frame.style.width=S.width+'px';frame.style.height=S.height+'px';if(S.page==='biblio-explore'){const w=S.width-(S.preset!=='desktop'&&S.width>=840?92:0);frame.style.setProperty('--pad',(w<=430?18:w<600?24:w<840?32:54)+'px');}
 frame.style.setProperty('--rail-available',Math.max(140,S.height-S.top-S.bottom-116-(S.preset!=='desktop'&&S.width<840?72:0))+'px');
 frame.style.setProperty('--safe-top',S.top+'px');frame.style.setProperty('--safe-bottom',S.bottom+'px');frame.style.setProperty('--safe-left',S.left+'px');frame.style.setProperty('--safe-right',S.right+'px');frame.style.setProperty('--font-scale',S.font);frame.style.setProperty('--ime',S.keyboard?Math.min(260,Math.max(100,Math.min(S.height*.42,S.height-S.top-S.bottom-(S.width>S.height?76:148)-110)))+'px':'0px');
 if(capture){document.body.classList.add('capture');holder.style.width=S.width+'px';holder.style.height=S.height+'px';frame.style.transform='none';return;}
 const availWidth=Math.max(260,stage.clientWidth-24);const scale=Math.min(1,availWidth/S.width);
 holder.style.width=(S.width*scale)+'px';holder.style.height=(S.height*scale)+'px';
 frame.style.transform='scale('+scale+')';frame.style.transformOrigin='top left';
}
function render(resetScroll=false){
 const frame=document.getElementById('appFrame');const old=frame.querySelector('.scroll');const pos=resetScroll?0:(old?.scrollTop||0);
 let html='';
 if(S.page==='search')html=renderSearch();
 else if(S.page==='biblio-explore')html=renderExplore();
 else if(S.page==='genre-index')html=renderGenreIndex();
 else if(S.page==='genre-results')html=renderGenreResults();
 else if(S.page==='continue-see-all')html=renderContinue();
 frame.innerHTML=html;
 applyViewport();
 const next=frame.querySelector('.scroll');if(next){next.scrollTop=pos;if(S.page==='biblio-explore'){next.addEventListener('scroll',updateShelfIndex,{passive:true});updateShelfIndex();}}
}
function navigate(file,extras){
 if(window.batchNavigate){window.batchNavigate("20-search-browse/"+file,extras);return;}
 const query=new URLSearchParams(extras||{});query.set('preset',S.preset);
 if(capture)query.set('capture','1');
 location.href=file+'?'+query.toString();
}
function renderAndControls(reset){render(reset);if(!capture)initControls();}
function scrollToRailTarget(name){
 const labels=S.order.concat(MOSAICS),idx=labels.indexOf(name);if(idx<0)return;
 const target=idx<S.order.length?document.getElementById('shelf-'+idx):document.querySelectorAll('.mosaic-card')[idx-S.order.length];
 if(target){const sc=document.querySelector('#appFrame .scroll');if(sc){sc.scrollTo({top:Math.max(0,target.getBoundingClientRect().top-sc.getBoundingClientRect().top+sc.scrollTop-14),behavior:'instant'});}}
}
function onAppClick(event){
 const el=event.target.closest('[data-action]');if(!el)return;
 const type=el.dataset.action, value=el.dataset.value||'';
 const up=(key,dir)=>{const i=S.order.indexOf(key);if(i<0)return;const j=Math.max(0,Math.min(S.order.length-1,i+dir));if(j===i)return;S.order.splice(i,1);S.order.splice(j,0,key);};
 if(type==='back'){if(S.page==='genre-results')navigate('genre-index.html',{lane:S.lane});else if(S.page==='genre-index')navigate('biblio-explore.html');else if(S.page==='search')navigate('biblio-explore.html');else navigate('biblio-explore.html');return;}
 if(type==='search-link'){navigate('search.html',{lane:S.lane});return;}
 if(type==='explore-link'){navigate('genre-index.html',{lane:S.lane});return;}
 if(type==='genre-pick'){navigate('genre-results.html',{lane:S.lane,genre:value});return;}
 if(type==='jump-to'){if(value==='Home'){flagAction('Home destination is outside this batch');return;}const back=document.querySelector('[data-action=back]');if(PARAMS.has('from')&&back)back.click();else if(window.batchNavigate)window.batchNavigate('07-biblio-home/biblio-home.html');return;}
 if(type==='item-open'){flagAction('Selected '+value+'; actual detail routing is outside this mock');return;}
 if(type==='noop'){flagAction('Shell action remains in the application; no host operation performed');return;}
 if(type==='search-clear'){window.clearTimeout(window.mockSearchTimer);S.query='';S.searchState='empty';renderSearchUpdate(true,true);return;}
 if(type==='recent'){S.query=value;S.searchState='results';S.expanded={};render(true);return;}
 if(type==='recent-remove'){event.preventDefault();event.stopPropagation();S.recent=S.recent.filter(x=>x!==value);render();return;}
 if(type==='browse-genre'){S.browseGenre=value;S.searchState='browse';render(true);return;}
 if(type==='genre-search'){const queries={'Fiction':'fiction','Mystery & Thriller':'thriller','Romance':'romance','Sci-Fi & Fantasy':'science fiction','Biography':'biography','History':'history','Nonfiction':'nonfiction','Young Adult':'young adult'};S.query=queries[value]||value.toLowerCase();S.searchState='results';render(true);return;}
 if(type==='browse-close'){S.searchState='empty';render(true);return;}
 if(type==='surprise'){flagAction('Surprise me would request a randomly chosen title; remote request disabled in mock');return;}
 if(type==='see-more'){S.expanded[value]=!S.expanded[value];render();return;}
 if(type==='genre-select'){S.genre=value;render(true);return;}
 if(type==='sort-toggle'){S.sort=S.lane==='Biblio'?'readers':S.sort==='readers'?'score':'readers';render();return;}
 if(type==='view-detail'){S.compact=false;render();return;}
 if(type==='view-covers'){S.compact=true;render();return;}
 if(type==='library'){S.library[value]=!S.library[value];render();return;}
 if(type==='tab'){if(value==='Explore'){S.activeTab='Explore';S.pinned='';render(true);}else if(value==='Discover'){S.activeTab='Discover';S.pinned=S.pinned||'Popular';render(true);}else flagAction('Library is a separate retained world tab, outside this fitting mock');return;}
 if(type==='rail-toggle'){S.railOpen=!S.railOpen;render();return;}
 if(type==='rail-jump'){S.selectedRail=S.order.concat(MOSAICS).indexOf(value);render();scrollToRailTarget(value);return;}
 if(type==='edit-shelves'){S.edit=!S.edit;render();return;}
 if(type==='move-up'||type==='move-down'){up(value,type==='move-up'?-1:1);render();return;}
 if(type==='hide-shelf'){S.hidden=S.hidden.includes(value)?S.hidden.filter(x=>x!==value):S.hidden.concat(value);render();return;}
 if(type==='see-all'||type==='mosaic-pin'){if(window.batchNavigate){window.batchNavigate('07-biblio-home/biblio-home.html',{tab:'discover',category:value==='Top 10'?'Popular':SHELF_NAMES.includes(value)?value:'Popular',scroll:'650',return:'explore',filter:type==='mosaic-pin'?(value==='Audience'?'Young Adult':value):''});return;}S.pinned=value;S.activeTab='Discover';S.railOpen=false;S.categoryOpen=false;S.filterOpen=false;S.filter='All';S.source='Apple Books';render(true);return;}
 if(type==='explore-return'){S.activeTab='Explore';S.pinned='';S.railOpen=false;render(true);return;}
 if(type==='source-select'){S.source=value;render();flagAction('Selected '+value+' source preview; no provider request performed');return;}
 if(type==='category-open'){S.categoryOpen=!S.categoryOpen;S.filterOpen=false;render();return;}
 if(type==='category-pick'){S.pinned=value;S.categoryOpen=false;render();return;}
 if(type==='filter-open'){S.filterOpen=!S.filterOpen;S.categoryOpen=false;render();return;}
 if(type==='filter-pick'){S.filter=value;S.filterOpen=false;render();return;}
 if(type==='continue-sort'){S.continueSort=value;render();return;}
 if(type==='continue-medium'){S.medium=value;render();return;}
 if(type==='continue-detail'||type==='continue-resume'||type==='context-detail'||type==='context-resume'){S.continueMenu=null;render();flagAction((type.includes('resume')?'Resume':'Details')+' for progress item '+value+' (demo only)');return;}
 if(type==='continue-remove'||type==='context-remove'){event.stopPropagation();S.continueMenu=null;S.removed.push(value);render();return;}
}
// Search input text is independent of the suggestions/results presentation.
// Rebuild the fixture without taking focus from an intentionally selected control.
function renderSearchUpdate(resetScroll=false,focusInput=false){
 const frame=document.getElementById('appFrame'),input=document.getElementById('searchInput'),active=document.activeElement;
 const keepInput=focusInput||active===input;
 const selection=input?[input.selectionStart,input.selectionEnd,input.selectionDirection]:[0,0,'none'];
 const action=!keepInput&&frame.contains(active)&&active.dataset.action?{type:active.dataset.action,value:active.dataset.value||'',label:active.getAttribute('aria-label')}:null;
 render(resetScroll);
 if(keepInput){
  const replacement=document.getElementById('searchInput');
  replacement.focus({preventScroll:true});
  replacement.setSelectionRange(selection[0],selection[1],selection[2]);
 }else if(action){
  const replacement=[...frame.querySelectorAll('[data-action]')].find(e=>e.dataset.action===action.type&&(e.dataset.value||'')===action.value&&e.getAttribute('aria-label')===action.label);
  if(replacement)replacement.focus({preventScroll:true});
 }
}
function onAppInput(event){
 if(event.target.id!=='searchInput')return;
 S.query=event.target.value;window.clearTimeout(window.mockSearchTimer);
 window.mockSearchTimer=window.setTimeout(()=>{S.searchState=S.query.trim().length>1?'results':'empty';S.expanded={};renderSearchUpdate();},210);
}
function onControlChange(event){
 const el=event.target,key=el.dataset.control;if(!key)return;
 const newVal=el.type==='checkbox'?el.checked:el.value;
 if(key==='preset'){S.preset=newVal;[S.width,S.height]=PRESETS[newVal];}
 else if(['width','height','top','bottom','left','right'].includes(key)){S[key]=Math.max(0,Number(newVal)||0);}
 else if(key==='font')S.font=Math.max(1,Math.min(1.4,Number(newVal)||1));
 else if(key==='lane'){S.lane=newVal;S.genre=S.lane==='Biblio'?'Sci-Fi & Fantasy':'Adventure';S.query=S.lane==='Biblio'?'Dune':S.lane==='Theatre'?'Star Wars':'One Piece';S.searchState='results';}
 else if(key==='genre')S.genre=newVal;
 else if(key==='searchState'){S.searchState=newVal;if(newVal==='empty')S.query='';else if(!S.query)S.query='Dune';}
 else S[key]=newVal;
 renderAndControls(true);
}
function init(){
 initBatchKeyboardAndDrag();
 if(S.page==='biblio-explore'){try{const saved=JSON.parse(PARAMS.get('order')||'null');if(saved&&saved.length===7&&saved.every(x=>SHELF_NAMES.includes(x)))S.order=saved;S.hidden=JSON.parse(PARAMS.get('hidden')||'[]').filter(x=>SHELF_NAMES.includes(x));S.railOpen=PARAMS.get('rail')==='1'||S.railOpen;}catch(e){}window.batchReturnUrl=()=>{const u=new URL(location.href);u.searchParams.set('order',JSON.stringify(S.order));u.searchParams.set('hidden',JSON.stringify(S.hidden));u.searchParams.set('rail',S.railOpen?'1':'0');u.searchParams.set('returnY',document.querySelector('.explore-scroll')?.scrollTop||0);return u.href;};}
 S.page=page;
 if(capture&&document.body.dataset.fontScale)S.font=Math.min(1.4,Math.max(1,Number(document.body.dataset.fontScale)||1));
 if(S.page==='search')S.query=S.lane==='Biblio'?'Dune':S.lane==='Theatre'?'Star Wars':'One Piece';
 if(STATE==='empty'){S.searchState='empty';S.query='';}
 if(STATE==='browse'){S.searchState='browse';S.query='';}
 if(STATE==='keyboard'){S.searchState='results';S.keyboard=true;}
 if(STATE==='covers')S.compact=true;
 if(STATE==='context')S.continueMenu='c1';
 if(STATE==='seeall'){S.activeTab='Discover';S.pinned='Popular';}
 if(STATE==='filter-open'){S.activeTab='Discover';S.pinned='Popular';S.filterOpen=true;}
 if(S.lane==='Biblio'&&page==='genre-results'&&!PARAMS.has('genre')&&!document.body.dataset.genre)S.genre='Sci-Fi & Fantasy';
 if(capture){document.body.classList.add('capture');S.preset=document.body.dataset.viewport||S.preset;const dims=PRESETS[S.preset]||PRESETS.portrait;S.width=dims[0];S.height=dims[1];document.body.style.setProperty('--capture-width',S.width+'px');document.body.style.setProperty('--capture-height',S.height+'px');}
 document.getElementById('appFrame').addEventListener('click',onAppClick);
 document.getElementById('appFrame').addEventListener('input',onAppInput);
 document.getElementById('appFrame').addEventListener('contextmenu',e=>{const tile=e.target.closest('.continue-tile');if(tile){e.preventDefault();S.continueMenu=tile.dataset.value;render();}});
 document.getElementById('appFrame').addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.id==='searchInput'){e.preventDefault();window.clearTimeout(window.mockSearchTimer);S.searchState=S.query.trim().length>1?'results':'empty';renderSearchUpdate();document.querySelector('.top-match [data-action="item-open"]')?.click();}if(e.key==='Escape'&&S.continueMenu){S.continueMenu=null;render();}if((e.key==='ContextMenu'||(e.key==='F10'&&e.shiftKey))&&e.target.closest('.continue-tile')){e.preventDefault();S.continueMenu=e.target.closest('.continue-tile').dataset.value;render();}});
 const controls=document.getElementById('controls');if(controls)controls.addEventListener('change',onControlChange);
 window.addEventListener('resize',()=>{if(!capture)applyViewport();});
 renderAndControls(true);
 if(S.page==='biblio-explore'&&PARAMS.has('returnY'))requestAnimationFrame(()=>{document.querySelector('.explore-scroll').scrollTop=Number(PARAMS.get('returnY'))||0;});
}
// Boot is deferred until the approved genre standfirst constants below have initialized.
function chromeSystemOnly(){
 return '<div class="fixed-chrome"><span></span><div class="sys-controls">'+iconButton('minimize','Minimize','noop','desktop-only')+iconButton('fullscreen','Toggle fullscreen','noop','desktop-only')+iconButton('power','Close','noop','desktop-only')+'</div></div>';
}

// Exact editorial standfirst from approved GenreApi.js (read-only)
const OFFICIAL_MANGA_DESCS = {
    "Action": "Fists, blades, and the will to use them. Action lives in the clash itself — the choreography of conflict, where every fight comes down to who wants it more.",
    "Adventure": "Whether aiming for a specific goal or just struggling to survive, the hero is thrust into unfamiliar lands and continuously faces unexpected dangers. The story is always how they react to the journey's trials — growth or setback by the choices they make. Simply seeing foreign worlds is not adventure; the change is.",
    "Comedy": "Built to make you laugh — through timing, absurdity, or the slow burn of a running gag. The plot is a stage; the joke is the point.",
    "Drama": "The weight of being human. Drama leans into conflict that's emotional rather than physical — relationships, loss, ambition, and the long cost of a single choice.",
    "Fantasy": "Worlds that run on rules that aren't ours. Magic, myth, and invented orders — fantasy asks what changes when the impossible becomes ordinary.",
    "Horror": "Made to unsettle. Horror works the dread before the reveal — the wrongness at the edge of the page, and the things that don't stay hidden.",
    "Mystery": "A question the reader is invited to solve. Mystery withholds, plants, and pays off — the pleasure is piecing it together a step behind the detective.",
    "Romance": "The pull between two people, and everything in the way. Romance follows the distance closing — or not — and the feeling that carries the whole story.",
    "Sci-Fi": "What-if, made rigorous. Science fiction extrapolates from technology and its consequences — the future as a lens on the present.",
    "Slice of Life": "The ordinary, paid attention to. Slice of life finds its drama in small days — routines, friendships, and the quiet texture of being somewhere real.",
    "Sports": "The discipline of getting better. Sports manga is the grind toward a goal — training, rivalry, and the team that forms around the chase.",
    "Supernatural": "The everyday world with something extra in it. Spirits, powers, and the unexplained — the uncanny walking among the ordinary.",
    "Shounen": "Aimed at a young male readership — and grown far past it. Shounen prizes growth, friendship, and the next impossible goal, told at a propulsive pace.",
    "Seinen": "Written for adult readers. Seinen takes its time and its subjects seriously — darker, knottier, and more willing to sit with consequence.",
    "Shoujo": "Aimed at a young female readership. Shoujo centers emotion and relationship, with an eye for interior life and the look of feeling on the page.",
    "Josei": "Written for adult women. Josei trades fantasy for the textured real — work, love, and the complicated adult life shoujo grows into.",
    "Isekai": "Carried into another world. The hero arrives from our reality into a second one — and the story is what they make of a clean slate with old memories.",
    "School": "Set where so much of life is decided — the classroom, the club, the years between childhood and after. Ordinary stakes at human scale.",
    "Magic": "Power with rules and a cost. Magic turns on systems of the impossible — what it can do, what it demands, and who is willing to pay."
};

// Exact editorial standfirst from approved BiblioGenreApi.js (read-only)
const OFFICIAL_BIBLIO_DESCS = {
    "Fiction & Literature": "Stories where character runs deeper than plot. Fiction and literature trade in the interior weight of ordinary lives - the slow press of memory, the friction between what is said and what is meant. These books earn their pace by refusing to look away from what other genres skim past.",
    "Mysteries & Thrillers": "A puzzle and a pulse. Mysteries bait the mind with a hidden truth; thrillers grip the body with escalating danger. Both begin with something broken - a crime, a lie, a disappearance - and make the reader a second investigator, sifting evidence alongside the detective until the room shrinks to one answer.",
    "Sci-Fi & Fantasy": "Reality with a lever pushed. Science fiction tests what might be true if technology or time took one more step; fantasy builds worlds from myth and magic that never were but feel whole. Both genres measure humanity against unfamiliar orders - the alien, the ancient, the yet-to-be.",
    "Romance": "The architecture of falling. Romance novels build toward a promise kept - an emotionally just finish - but earn it through friction: the wrong person in the right light, a wall two people chip at from opposite sides. At its centre is the radical act of trusting another person with the self you barely trust alone.",
    "Biographies & Memoirs": "A single life held up to the light. Biographies build from the outside in - documents, witnesses, the grain of an era - while memoirs speak from inside the skin, narrowing focus to a season or a wound. Together they ask the same unanswerable question: how does one life add up to something that means?",
    "History": "What happened and why it still moves. History reconstructs the dead from what they left behind - letters, ledgers, ruins, silence - then argues with itself about cause and meaning. Every generation questions the archive anew and gets different answers, because the present is always the hidden co-author of the past.",
    "Young Adult": "The loud years. Young adult fiction puts a teenage mind at the centre of the frame and refuses to patronize it - first love, first danger, first betrayal arrive at full voltage because they are happening for the first time. The door between childhood and the rest of the world swings one way only, and these books live in its hinge.",
    "Comics & Graphic Novels": "Stories told in panels - a medium where time lives in the space between frames. A graphic novel is a self-contained work of sequential art: image and text share every decision about pacing, silence, and revelation. What a page shows in one glance might take a prose novel three chapters to arrive at, and what it hides between panels belongs to the reader alone.",
    "Humor": "The release valve and the scalpel. Comic writing turns awkwardness, pain, and the unspoken into shared recognition - a sudden exhale that means you are not alone in what you noticed. The best humor does not dodge gravity; it slips under its guard faster than sincerity can, and lands the truth while you are still laughing.",
    "Travel & Adventure": "A departure that changes the departee. Travel and adventure writing charts the space between leaving and arriving - the foreign street, the wrong turn, the stranger whose kindness rearranges your sense of scale. The outer journey is never the whole story; every mile outward is also a mile inward, measured in the self that comes back different."
};

// Exact editorial standfirst from approved TheatreGenreApi.js (read-only)
const OFFICIAL_THEATRE_DESCS = {
    "Action": "Momentum as storytelling. Action lives in the set piece — the chase, the fight, the escape — and in who someone becomes when everything is moving.",
    "Adventure": "The road out of the known world. Adventure is scale and discovery — strange places, high stakes, and the person the journey makes of you.",
    "Animation": "Not a genre so much as total freedom. Animation draws worlds live action can't afford and feelings a camera can't frame.",
    "Comedy": "Built to make you laugh — timing, absurdity, and the long setup paying off. The plot is a stage; the joke is the point.",
    "Crime": "The rules broken, and what breaking them costs. Crime follows heists, syndicates, and detectives down streets where every choice leaves a mark.",
    "Documentary": "The real thing, framed. Documentary finds its drama in what actually happened — and in who gets to tell it.",
    "Drama": "The weight of being human. Drama leans into conflict that's emotional rather than physical — relationships, loss, ambition, and the cost of a single choice.",
    "Fantasy": "Worlds that run on rules that aren't ours. Magic, myth, and invented orders — fantasy asks what changes when the impossible becomes ordinary.",
    "Horror": "Made to unsettle. Horror works the dread before the reveal — the wrongness at the edge of the frame, and the things that don't stay hidden.",
    "Mystery": "A question the viewer is invited to solve. Mystery withholds, plants, and pays off — the pleasure is piecing it together a step behind the detective.",
    "Romance": "The pull between two people, and everything in the way. Romance follows the distance closing — or not — and the feeling that carries the whole story.",
    "Sci-Fi": "What-if, made rigorous. Science fiction extrapolates from technology and its consequences — the future as a lens on the present.",
    "Slice of Life": "The ordinary, paid attention to. Small days, routines, friendships — the quiet texture of being somewhere real.",
    "Sports": "The discipline of getting better. Training, rivalry, and the team that forms around the chase.",
    "Supernatural": "The everyday world with something extra in it. Spirits, powers, and the unexplained walking among the ordinary.",
    "Thriller": "Tension as the engine. Thrillers tighten — a secret, a clock, a pursuer — until sitting still stops being an option."
};

if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();

// Preserve the approved keyboard and pointer interactions in the local fixture harness.
function initBatchKeyboardAndDrag(){
 const host=document.getElementById('appFrame');let dragging=null,menuScroll=0;
 const focusHandle=key=>host.querySelector('[data-action="reorder-handle"][data-value="'+CSS.escape(key)+'"]')?.focus({preventScroll:true});
 const closeContinue=()=>{const id=S.continueMenu;S.continueMenu=null;render();host.querySelector('.continue-tile[data-value="'+id+'"]')?.focus({preventScroll:true});};
 const openContinue=tile=>{S.continueMenu=tile.dataset.value;render();host.querySelector('.tile-context button')?.focus({preventScroll:true});menuScroll=host.querySelector('.scroll')?.scrollTop||0;};
 host.addEventListener('keydown',e=>{
  const handle=e.target.closest('[data-action="reorder-handle"]'),rail=e.target.closest('.rail-entry'),tile=e.target.closest('.continue-tile'),menu=e.target.closest('.tile-context');
  if(handle&&e.ctrlKey&&e.shiftKey&&['ArrowUp','ArrowDown'].includes(e.key)){e.preventDefault();e.stopImmediatePropagation();const key=handle.dataset.value,i=S.order.indexOf(key),j=Math.max(0,Math.min(S.order.length-1,i+(e.key==='ArrowUp'?-1:1)));if(i!==j){S.order.splice(i,1);S.order.splice(j,0,key);render();focusHandle(key);}return;}
  if(rail&&['ArrowUp','ArrowDown','Home','End'].includes(e.key)){e.preventDefault();const list=[...rail.parentElement.querySelectorAll('.rail-entry')],i=list.indexOf(rail);list[e.key==='Home'?0:e.key==='End'?list.length-1:(i+(e.key==='ArrowUp'?-1:1)+list.length)%list.length]?.focus();return;}
  if(tile&&(e.key==='ContextMenu'||e.key==='F10'&&e.shiftKey)){e.preventDefault();e.stopImmediatePropagation();openContinue(tile);return;}
  if(S.continueMenu&&e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();closeContinue();return;}
  if(menu&&['ArrowUp','ArrowDown','Home','End'].includes(e.key)){e.preventDefault();e.stopImmediatePropagation();const list=[...menu.querySelectorAll('button')],i=list.indexOf(document.activeElement);list[e.key==='Home'?0:e.key==='End'?list.length-1:(i+(e.key==='ArrowUp'?-1:1)+list.length)%list.length]?.focus();return;}
  if(e.key==='Enter'&&e.target.id==='searchInput'&&S.query.trim()){S.recent=[S.query.trim(),...S.recent.filter(x=>x.toLowerCase()!==S.query.trim().toLowerCase())].slice(0,12);}
  if(dragging&&e.key==='Escape'){S.order=dragging.original;const key=dragging.key;dragging=null;render();focusHandle(key);}
 },true);
 host.addEventListener('contextmenu',e=>{const tile=e.target.closest('.continue-tile');if(tile){e.preventDefault();e.stopImmediatePropagation();openContinue(tile);}},true);
 host.addEventListener('pointerdown',e=>{if(S.continueMenu&&!e.target.closest('.tile-context')){S.continueMenu=null;host.querySelector('.tile-context')?.remove();}const handle=e.target.closest('[data-action="reorder-handle"]');if(!handle||e.button!==0)return;e.preventDefault();dragging={key:handle.dataset.value,original:S.order.slice(),startY:e.clientY,moved:false,pointer:e.pointerId};host.setPointerCapture(e.pointerId);},true);
 host.addEventListener('pointermove',e=>{if(!dragging)return;if(!dragging.moved&&Math.abs(e.clientY-dragging.startY)<8)return;dragging.moved=true;const sc=host.querySelector('.explore-scroll'),r=sc.getBoundingClientRect();if(e.clientY<r.top+48)sc.scrollTop-=24;else if(e.clientY>r.bottom-48)sc.scrollTop+=24;const rows=[...host.querySelectorAll('.shelf[data-shelf]')];let target=rows.reduce((best,row)=>{const b=row.getBoundingClientRect();return Math.abs(e.clientY-(b.top+b.height/2))<best.d?{row,d:Math.abs(e.clientY-(b.top+b.height/2))}:best;},{row:null,d:Infinity}).row;const from=S.order.indexOf(dragging.key),to=S.order.indexOf(target?.dataset.shelf);if(to>=0&&from!==to){S.order.splice(from,1);S.order.splice(to,0,dragging.key);render();}});
 const end=e=>{if(!dragging)return;const d=dragging;dragging=null;if(e.type==='pointercancel')S.order=d.original;if(host.hasPointerCapture(d.pointer))host.releasePointerCapture(d.pointer);render();focusHandle(d.key);};host.addEventListener('pointerup',end);host.addEventListener('pointercancel',end);
 host.addEventListener('scroll',e=>{if(S.continueMenu&&e.target.classList?.contains('scroll')&&Math.abs(e.target.scrollTop-menuScroll)>1){S.continueMenu=null;host.querySelector('.tile-context')?.remove();}},true);
}
