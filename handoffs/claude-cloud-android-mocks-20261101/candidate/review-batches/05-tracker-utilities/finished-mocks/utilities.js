'use strict';
// Standalone visual study, not QML, not Colosseum state, and never a provider/network client.
(function(){
const A='../../../../baseline/reference/desktop/assets/';
const PAGE=document.body.dataset.page;
const DEVICE=document.getElementById('device');
const MOCK=document.getElementById('mock');
const TOOLS=document.getElementById('review-tools');
const Q=new URLSearchParams(location.search);
const CAPTURE=Q.get('capture')==='1';
const PRESETS={'portrait':[412,900],'landscape':[900,412],'compact portrait':[354,792],'compact landscape':[792,354],'desktop':[1280,700],'tablet portrait':[800,1280],'tablet landscape':[1280,800]};
const FACES={back:'<path d="m15 5.5-7 6.5 7 6.5"/>',plus:'<path d="M12 5v14M5 12h14"/>',close:'<path d="m6 6 12 12M6 18 18 6"/>',search:'<circle cx="10.5" cy="10.5" r="6.5"/><path d="m16 16 4.5 4.5"/>',collections:'<rect x="6" y="7" width="15" height="14" rx="2"/><path d="M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5"/>',edit:'<path d="m15 5 4 4M4 20l4-1L20 7a2.1 2.1 0 0 0-3-3L5 16Z"/>',trash:'<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M9 10v7m6-7v7"/>',check:'<path d="m5 12 4 4L19 6"/>',lock:'<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V6a4 4 0 0 1 8 0v4m-4 5v2"/>',up:'<path d="m6 15 6-6 6 6"/>',down:'<path d="m6 9 6 6 6-6"/>',arrow:'<path d="M5 12h14m-5-5 5 5-5 5"/>',reset:'<path d="M3 10a9 9 0 1 1 1 8M3 4v6h6"/>'};
if(PAGE==='collections')Object.assign(FACES,{"collections":"<rect x=\"6\" y=\"7\" width=\"15\" height=\"14\" rx=\"2\"/><path d=\"M3 17V5a2 2 0 0 1 2-2h12M10 11h7m-7 5h5\"/>","folder":"<path d=\"M3 7a2 2 0 0 1 2-2h5l2 2h7a2 2 0 0 1 2 2v10H3Z\"/>","colosseum":"<path d=\"M3 20h18M4 20V8l8-4 8 4v12M4 10h16M7 8V6m5 4V4m5 4V6M7 20v-5a2 2 0 0 1 4 0v5m2 0v-5a2 2 0 0 1 4 0v5\"/>","plus":"<path d=\"M12 5v14M5 12h14\"/>","close":"<path d=\"m6 6 12 12M6 18 18 6\"/>","check":"<path d=\"m5 12 4 4L19 6\"/>","search":"<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\"/><path d=\"m16 16 4.5 4.5\"/>","back":"<path d=\"M19 12H5m5-5-5 5 5 5\"/>","arrow":"<path d=\"M5 12h14m-5-5 5 5-5 5\"/>","film":"<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M7 3v18m10-18v18M3 8h4m-4 8h4M17 8h4m-4 8h4\"/>","series":"<rect x=\"3\" y=\"5\" width=\"18\" height=\"14\" rx=\"2\"/><path d=\"M8 22h8M9 2l3 3 3-3\"/>","album":"<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><circle cx=\"12\" cy=\"12\" r=\"5\"/><circle cx=\"12\" cy=\"12\" r=\"1\"/>","song":"<path d=\"M9 18V5l11-2v13M9 9l11-2\"/><ellipse cx=\"6\" cy=\"18\" rx=\"3\" ry=\"2\"/><ellipse cx=\"17\" cy=\"16\" rx=\"3\" ry=\"2\"/>","artist":"<circle cx=\"12\" cy=\"7\" r=\"4\"/><path d=\"M4 21v-3a8 8 0 0 1 16 0v3\"/>","manga":"<path d=\"M3 5c3-1 6-1 9 1 3-2 6-2 9-1v15c-3-1-6-1-9 1-3-2-6-2-9-1Z\"/><path d=\"M12 6v15M6 9h3m-3 4h3m6-4h3m-3 4h3\"/>","book":"<path d=\"M6 4h14v17H6a3 3 0 0 1-3-3V7a3 3 0 0 1 3-3Z\"/><path d=\"M6 4v12h14M6 18h13\"/>","comic":"<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M3 10h18M11 10v11M3 15h8M16 3v7\"/>","grid":"<rect x=\"3\" y=\"3\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"14\" y=\"3\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"3\" y=\"14\" width=\"7\" height=\"7\" rx=\"1\"/><rect x=\"14\" y=\"14\" width=\"7\" height=\"7\" rx=\"1\"/>","list":"<path d=\"M8 5h13M8 12h13M8 19h13M3 5h.01M3 12h.01M3 19h.01\"/>","edit":"<path d=\"m15 5 4 4M4 20l4-1L20 7a2.1 2.1 0 0 0-3-3L5 16Z\"/>","lock":"<rect x=\"5\" y=\"10\" width=\"14\" height=\"11\" rx=\"2\"/><path d=\"M8 10V6a4 4 0 0 1 8 0v4m-4 5v2\"/>","up":"<path d=\"m6 15 6-6 6 6\"/>","down":"<path d=\"m6 9 6 6 6-6\"/>","more":"<circle cx=\"5\" cy=\"12\" r=\"1\"/><circle cx=\"12\" cy=\"12\" r=\"1\"/><circle cx=\"19\" cy=\"12\" r=\"1\"/>","trash":"<path d=\"M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M9 10v7m6-7v7\"/>","note":"<path d=\"M5 3h14v18H5ZM8 7h8m-8 4h8m-8 4h5\"/>","reset":"<path d=\"M3 10a9 9 0 1 1 1 8M3 4v6h6\"/>"});
function ico(name,size){return '<svg aria-hidden="true" width="'+(size||20)+'" height="'+(size||20)+'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">'+(FACES[name]||FACES.collections)+'</svg>';}
function esc(x){return String(x===undefined||x===null?'':x).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function path(p){return p.startsWith("batch:")?p.slice(6):A+p;}
function chrome(){return '<div class="window-controls"><button aria-label="Minimize" title="Minimize"><img src="'+path('icons/minimize.svg')+'" alt=""></button><button aria-label="Fullscreen" title="Fullscreen"><img src="'+path('icons/fullscreen.svg')+'" alt=""></button><button aria-label="Power" title="Close"><img src="'+path('icons/power.svg')+'" alt=""></button></div>';}
function back(label,act){return '<button class="back-plain" data-act="'+act+'" aria-label="'+esc(label||'Back')+'">'+ico('back',20)+(label?'<span>'+esc(label)+'</span>':'')+'</button>';}
function cb(text,cls,act,icon){return '<button class="collection-action '+(cls||'')+'" data-act="'+act+'" aria-label="'+esc(text|| (act==='collectionDelete'?'Delete collection':act==='dismissCollectionMessage'?'Dismiss message':act))+'">'+(icon?ico(icon,18):'')+esc(text)+'</button>';}
function modalClose(){return '<button class="icon-control" data-act="closeModal" aria-label="Close">'+ico('close',20)+'</button>';}
const S={
 w:412,h:900,top:0,bottom:0,scale:1,keyboard:false,mode:'',sort:'relevance',query:'landscape',
 pick:'',extra:0,querySubmitted:'landscape', houseOff:{},addonOn:{},addonRemoved:{}, collectionQuery:'',collectionId:'',collectionFilter:'all',collectionEdits:{},collectionNewName:'',detailName:'',detailDesc:'',chapter:0,releaseState:'Available',extSearch:'',url:'',urlPreview:false
};
const WALL_ANIM=[
 {id:'noir',title:'Noir Flow',spec:'Slow silver flow - Colosseum shader (QML)',scene:'noir',source:'Colosseum Animated'},
 {id:'aurora',title:'Aurora Flow',spec:'Living gradient - ported from KDE Plasma (QML)',scene:'aurora',source:'Colosseum Animated'},
 {id:'poly',title:'Low Poly',spec:'Slow-morphing silver low-poly - Colosseum shader (QML)',scene:'poly',source:'Colosseum Animated'}
];
const WALL_NATIVE=[
 {id:'twilight',title:'Twilight',spec:'Still mesh gradient - Colosseum QML',scene:'twilight',source:'Colosseum Native'},
 {id:'ember',title:'Ember',spec:'Still mesh gradient - Colosseum QML',scene:'ember',source:'Colosseum Native'},
 {id:'mint',title:'Mint',spec:'Still mesh gradient - Colosseum QML',scene:'mint',source:'Colosseum Native'}
];
const WALL_KDE=[{"id":"kde-cascade","title":"Cascade","artist":"Ken Vermette","license":"LGPLv3","spec":"Cascade - Ken Vermette - LGPLv3","art":"batch:assets/kde/cascade.png","source":"KDE Plasma"},{"id":"kde-flow","title":"Flow","artist":"Sandra Smukaste","license":"CC-BY-SA-4.0","spec":"Flow - Sandra Smukaste - CC-BY-SA-4.0","art":"batch:assets/kde/flow.jpg","source":"KDE Plasma"},{"id":"kde-kay","title":"Kay","artist":"ruvkr","license":"CC-BY-SA-4.0","spec":"Kay - ruvkr - CC-BY-SA-4.0","art":"batch:assets/kde/kay.png","source":"KDE Plasma"},{"id":"kde-shell","title":"Shell","artist":"Lucas Andrade","license":"CC-BY-SA-4.0","spec":"Shell - Lucas Andrade - CC-BY-SA-4.0","art":"batch:assets/kde/shell.jpg","source":"KDE Plasma"},{"id":"kde-volna","title":"Volna","artist":"Nikita Babin","license":"CC-BY-SA-4.0","spec":"Volna - Nikita Babin - CC-BY-SA-4.0","art":"batch:assets/kde/volna.jpg","source":"KDE Plasma"},{"id":"kde-nexus","title":"Nexus","artist":"Krystian Zajdel","license":"CC-BY-SA-4.0","spec":"Nexus - Krystian Zajdel - CC-BY-SA-4.0","art":"batch:assets/kde/nexus.png","source":"KDE Plasma"},{"id":"kde-opal","title":"Opal","artist":"Ken Vermette","license":"LGPLv3","spec":"Opal - Ken Vermette - LGPLv3","art":"batch:assets/kde/opal.png","source":"KDE Plasma"},{"id":"cold","title":"Cold Ripple","artist":"Risto Saukonpää","license":"LGPLv3","spec":"Cold Ripple - Risto Saukonpää - LGPLv3","art":"wallpaper/cold-ripple.jpg","source":"KDE Plasma"},{"id":"kde-elarun","title":"Elarun","artist":"Nuno Pinheiro","license":"LGPLv3","spec":"Elarun - Nuno Pinheiro - LGPLv3","art":"batch:assets/kde/elarun.png","source":"KDE Plasma"},{"id":"kde-icecold","title":"Ice Cold","artist":"Santiago Cézar","license":"CC-BY-SA-4.0","spec":"Ice Cold - Santiago Cézar - CC-BY-SA-4.0","art":"batch:assets/kde/icecold.png","source":"KDE Plasma"},{"id":"kde-pastelhills","title":"Pastel Hills","artist":"Lionel","license":"LGPLv3","spec":"Pastel Hills - Lionel - LGPLv3","art":"batch:assets/kde/pastelhills.jpg","source":"KDE Plasma"},{"id":"kde-honeywave","title":"Honeywave","artist":"Ken Vermette","license":"CC-BY-SA-4.0","spec":"Honeywave - Ken Vermette - CC-BY-SA-4.0","art":"batch:assets/kde/honeywave.jpg","source":"KDE Plasma"},{"id":"kde-kokkini","title":"Kokkini","artist":"Ken Vermette","license":"LGPLv3","spec":"Kokkini - Ken Vermette - LGPLv3","art":"batch:assets/kde/kokkini.png","source":"KDE Plasma"}];
const WALL_RESULTS=[
 {id:'w1',title:'Dune',art:'wallpaper/sagas/dune.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w2',title:'Lord of the Rings',art:'wallpaper/sagas/lord-of-the-rings.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w3',title:'The Witcher',art:'wallpaper/sagas/the-witcher.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w4',title:'Percy Jackson',art:'wallpaper/sagas/percy-jackson.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w5',title:'Sherlock Holmes',art:'wallpaper/sagas/sherlock-holmes.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w6',title:'Harry Potter',art:'wallpaper/sagas/harry-potter.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w7',title:'Jurassic Park',art:'wallpaper/sagas/jurassic-park.jpg',source:'Colosseum',spec:'Landscape'},
 {id:'w8',title:'A Song of Ice and Fire',art:'wallpaper/sagas/ice-and-fire.jpg',source:'Colosseum',spec:'Landscape'}
];
const ALL_WALL=WALL_ANIM.concat(WALL_NATIVE,WALL_KDE,WALL_RESULTS);
const HOUSE=[
 {name:'Tributes',logo:'icons/universes-atom.svg',id:'tributes',kind:'universe'},
 {name:'Grand Database',logo:'addon-logos/colosseum-grand-database.png',id:'database',kind:'builtin'},
 {name:'Nyaa',logo:'addon-logos/nyaa.png',id:'nyaa',kind:'source'},
 {name:'Tankoyomi',logo:'addon-logos/tankoyomi.png',id:'tankoyomi',kind:'source'},
 {name:'GetComics',logo:'addon-logos/getcomics.png',id:'getcomics',kind:'source'},
 {name:'Tankorent',logo:'addon-logos/tankorent.png',id:'tankorent',kind:'source'},
 {name:'LibGen',logo:'addon-logos/libgen.ico',id:'libgen',kind:'source'},
 {name:'AudioBookBay',logo:'addon-logos/audiobookbay.png',id:'audiobookbay',kind:'source'}
];
const ADDONS=[
 {name:'OpenSubtitles v3',logo:'addon-logos/opensubtitles.png',desc:'Subtitles in every language, matched to the exact episode playing.',kind:'Subtitles',enabled:true,core:true},
 {name:'Streaming Catalogs',logo:'addon-logos/streaming-catalogs.png',desc:'New streaming catalogues and recommendations for Theatre.',kind:'Catalogs',enabled:true,core:true},
 {name:'Comet',logo:'addon-logos/comet.png',desc:'Fast, filterable torrent sources with clean quality labels.',kind:'Streams',configurable:true},
 {name:'Anime Kitsu',logo:'addon-logos/anime-kitsu.png',desc:'The anime shelf’s brain — proper seasons, splits and episode orders.',kind:'Catalogs · Details',enabled:true,core:true},
 {name:'Torrentio',logo:'addon-logos/torrentio.png',desc:'Play sources from twelve indexers, sorted by quality and health.',kind:'Streams',configurable:true,enabled:true},
 {name:'MediaFusion',logo:'addon-logos/mediafusion.png',desc:'A wide net — torrents, usenet and live feeds under one roof.',kind:'Streams',configurable:true},
 {name:'AIOStreams',logo:'addon-logos/aiostreams.png',desc:'Many source addons wrapped into one, with your own ranking rules.',kind:'Streams',configurable:true},
 {name:'Netflix Catalog',logo:'addon-logos/netflix-catalog.png',desc:'What’s new and leaving on Netflix, as browsable rows.',kind:'Catalogs'},
 {name:'SubDL',logo:'addon-logos/subdl.jpg',desc:'Community subtitles with strong regional coverage.',kind:'Subtitles'},
 {name:'OpenSubtitles',logo:'addon-logos/opensubtitles.png',desc:'Subtitles in multiple languages.',kind:'Subtitles'}
];
const INSTALLED=[{name:'Anime Kitsu',logo:'addon-logos/anime-kitsu.png',core:true,kind:'Catalogs · Details'}, {name:'Torrentio',logo:'addon-logos/torrentio.png',kind:'Streams',configurable:true}, {name:'OpenSubtitles v3',logo:'addon-logos/opensubtitles.png',core:true,kind:'Subtitles'}, {name:'Tankoyomi',logo:'addon-logos/tankoyomi.png',core:true,kind:'Catalogs · Details'}, {name:'Nyaa',logo:'addon-logos/nyaa.png',kind:'Streams'}, {name:'MediaFusion',logo:'addon-logos/mediafusion.png',kind:'Streams',configurable:true}];
const RAILS=[
 {title:'Essentials',sub:'The five a fresh Colosseum needs.',items:[0,1,2,3,4],noAll:true},
 {title:'Most popular',sub:'The most starred add-ons on stremio-addons.net.',items:[4,2,5,6,7]},
 {title:'Play sources',sub:'Where your movies and shows actually stream from.',items:[4,2,5,6]},
 {title:'Something to watch',sub:'New rows and catalogues for Theatre.',items:[1,7]},
 {title:'Anime',sub:'Seasons, catalogues and sources for anime.',items:[3,4]},
 {title:'Subtitles',sub:'Every language, matched to what is playing.',items:[0,8,9]}
];
const MEDIA=[
 {title:'Dune',cover:'wallpaper/sagas/dune.jpg',kind:'book',kindLabel:'Book',creator:'Frank Herbert'},
 {title:'Harry Potter',cover:'wallpaper/sagas/harry-potter.jpg',kind:'book',kindLabel:'Book',creator:'J. K. Rowling'},
 {title:'The Lord of the Rings',cover:'wallpaper/sagas/lord-of-the-rings.jpg',kind:'book',kindLabel:'Book',creator:'J. R. R. Tolkien'},
 {title:'A Song of Ice and Fire',cover:'wallpaper/sagas/ice-and-fire.jpg',kind:'book',kindLabel:'Book',creator:'George R. R. Martin'},
 {title:'The Witcher',cover:'wallpaper/sagas/the-witcher.jpg',kind:'series',kindLabel:'Series',creator:''},
 {title:'Sherlock Holmes',cover:'wallpaper/sagas/sherlock-holmes.jpg',kind:'book',kindLabel:'Book',creator:'Arthur Conan Doyle'},
 {title:'Jurassic Park',cover:'wallpaper/sagas/jurassic-park.jpg',kind:'film',kindLabel:'Movie',creator:''},
 {title:'Percy Jackson',cover:'wallpaper/sagas/percy-jackson.jpg',kind:'book',kindLabel:'Book',creator:'Rick Riordan'}
];
const COLLECTIONS=[
 {id:'worlds',name:'Worlds worth returning to',description:'The things you want to keep together.',items:[0,1,2,3,4,7],tint:'#536078'},
 {id:'stories',name:'Stories worth revisiting',description:'The things you want to keep together.',items:[5,6,1,4],tint:'#725b4b'},
 {id:'mystery',name:'Mysteries and detectives',description:'The things you want to keep together.',items:[5,0,6],tint:'#596661'},
 {id:'long',name:'The worlds I keep coming back to, even after the last chapter',description:'The things you want to keep together.',items:[2,3,7],tint:'#554d72'}
];
const CHAPTERS=[
 {section:'RELEASE',title:'The latest chapter',body:'The latest Colosseum chronicle lives here.'},
 {section:'COLLECTIONS',title:'Your Collections',body:'The things you want to keep together.'},
 {section:'EXTENSIONS',title:'Add-ons and sources for every world.',body:'The five a fresh Colosseum needs.'}
];

function note(message){window.__mockNotice=message;let el=document.getElementById('localStatus');if(!el){el=document.createElement('p');el.id='localStatus';el.className='review-warning';el.setAttribute('role','status');TOOLS.append(el);}el.textContent=message;}
function wallVisual(x){return x.scene?'<canvas class="wall-scene" data-scene="'+x.scene+'" aria-label="'+esc(x.title)+'"></canvas>':'<img src="'+path(x.art)+'" alt="">';}
function wallTile(x){let sel=S.pick===x.id;return '<button class="wall-tile'+(sel?' active':'')+'" data-act="wallPick" data-id="'+x.id+'" aria-pressed="'+sel+'" aria-label="Select '+esc(x.title)+'" title="'+esc(x.spec)+'">'+wallVisual(x)+'<span class="wall-tile-caption">'+esc(x.title)+(x.artist?'<small>'+esc(x.artist)+'</small>':'')+'</span></button>';}
function wallShelf(title,items){return '<section class="wall-section"><h2>'+title+'</h2><div class="wall-shelf scroll-x" tabindex="0" aria-label="'+title+' wallpapers">'+items.map(wallTile).join('')+'</div></section>';}
function renderWallpaper(){let sel=ALL_WALL.find(x=>x.id===S.pick),applied=ALL_WALL.find(x=>x.id===S.applied);let all=WALL_RESULTS.slice();const term=S.querySubmitted.trim().toLowerCase();if(term&&!['landscape','cinema'].includes(term))all=all.filter(x=>x.title.toLowerCase().includes(term));if(S.sort==='top')all.sort((a,b)=>a.title.localeCompare(b.title));if(S.sort==='random')all.sort((a,b)=>((Number(a.id.slice(1))*7+S.shuffle)%11)-((Number(b.id.slice(1))*7+S.shuffle)%11));let rows=all.slice(0,S.extra?8:6);
const detach=S.w>S.h&&(S.h-S.top-S.bottom-(S.keyboard?Math.min(185,Math.round(S.h*.4)):0)<330);const selected=sel?'<section class="wall-selected"><h2>'+esc(sel.title)+'</h2><p>'+esc(sel.spec)+'</p><div class="wall-scope"><button class="gold-pill" data-act="wallApply" data-scope="all">For All Worlds</button><button class="pill-ghost" data-act="wallApply" data-scope="world">For Home</button></div></section>':'';
const bg=sel||applied||ALL_WALL.find(x=>x.id==="cold");
return '<main class="app-surface wall-page"><div class="wall-wallpaper">'+wallVisual(bg)+'</div><div class="wall-scrim"></div>'+chrome()+(S.wallClosed?'':'<section class="wall-panel'+(sel?' has-pick':'')+'"><header class="wall-head"><h1>Wallpapers</h1><button class="wall-close" data-act="wallClose" aria-label="Close wallpaper picker">x</button></header><form class="wall-search" id="wallSearchForm"><input type="search" aria-label="Search wallpapers" id="wallSearchInput" value="'+esc(S.query)+'" spellcheck="false"><button class="gold-pill" type="submit">Search</button></form><div class="wall-sorts">'+['relevance','top','random'].map(x=>'<button class="wall-sort'+(S.sort===x?' active':'')+'" data-act="wallSort" data-mode="'+x+'" aria-pressed="'+(S.sort===x)+'">'+(x==='relevance'?'Relevance':x==='top'?'Top':'Random')+'</button>').join('')+'</div><div class="wall-status" role="status">'+rows.length+' wallpapers'+(all.length>rows.length?' · more available':'')+'</div><div class="wall-body scroll-y">'+wallShelf('Colosseum Animated',WALL_ANIM)+wallShelf('Colosseum Native',WALL_NATIVE)+wallShelf('KDE Plasma',WALL_KDE)+'<div class="wall-results">'+rows.map(wallTile).join('')+'</div><div class="wall-more"><button class="gold-pill" data-act="wallMore" '+(rows.length>=all.length?'disabled':'')+'>Load more</button></div></div>'+(detach?'':selected)+'</section>'+(detach?selected:''))+'</main>';}

function isInstalled(x){return INSTALLED.some(y=>y.name===x.name)&&!S.addonRemoved[x.name];}
function installLocal(x,replacing=false){if(!INSTALLED.some(y=>y.name===x.name))INSTALLED.push({...x});S.addonRemoved[x.name]=false;if(!replacing)S.addonOn[x.name]=true;note('Installed-state fixture updated locally for '+x.name+'. No service, application or device operation.');}
function openSetup(x,replacing=false){S.setupAddon=x;S.setupReturn=S.mode==='manage'?'manage':'store';S.setupReplacing=replacing;S.setupLink='';S.setupStatus='';S.mode='setup';}
function normalizedLink(value){try{let u=new URL(value.trim().replace(/^stremio:\/\//i,'https://'));return u.protocol==='https:'&&/\/manifest\.json$/i.test(u.pathname)?u.href:'';}catch(e){return '';}}
function fixtureForUrl(value){let u=normalizedLink(value);if(!u)return null;let host=new URL(u).hostname,pathname=new URL(u).pathname;if(host==='torrentio.strem.fun')return ADDONS[4];if(host==='fixture.colosseum.invalid'){if(pathname==='/comet/manifest.json')return ADDONS[2];if(pathname==='/subdl/manifest.json')return ADDONS[8];}return null;}
function setupModal(){const x=S.setupAddon||ADDONS[2],skip=!isInstalled(x)&&!['Comet','MediaFusion','AIOStreams'].includes(x.name);const field='<input class="modal-input" id="setupLink" aria-label="Add-on link from setup" placeholder="Paste the add-on link the page gave you" value="'+esc(S.setupLink||'')+'">';
return '<div class="modal-shade setup-shade"><section class="modal-panel setup-panel '+(S.desktop?'setup-desktop':'setup-android')+'"><div class="modal-heading"><h2>Set up '+esc(x.name)+'</h2>'+modalClose()+'</div>'+(S.desktop?'<div class="setup-web-fallback"><h3>This setup page won\'t open inside Colosseum.</h3><p>Try again, or set it up in your web browser, copy the add-on link it gives you, and paste it below.</p><button class="setup-open" data-act="setupOpen">Try again</button></div><div class="setup-foot"><span>Link only?</span>'+field+'<button class="setup-open" data-act="setupInstall" '+(!S.setupLink?'disabled':'')+'>Install</button><button class="setup-outline" data-act="setupPaste">Paste link</button>'+(skip?'<button class="setup-outline" data-act="setupSkip">Install without setup</button>':'')+'</div>':'<p>Setup opens in your browser. Finish it there, copy the add-on link it gives you, then come back and paste it.</p><button class="setup-open" data-act="setupOpen">Open setup page</button>'+field+'<button class="setup-outline" data-act="setupInstall">'+(S.setupLink?'Install':'Paste link')+'</button>'+(skip?'<button class="setup-outline" data-act="setupSkip">Install without setup</button>':''))+'<p class="setup-status" role="status">'+esc(S.setupStatus||'')+'</p></section></div>';}
function textMenuMarkup(){const m=S.textMenu;if(!m)return '';return '<div class="text-menu" role="menu" aria-label="Text editing" style="left:'+m.x+'px;top:'+m.y+'px">'+[['Cut','Ctrl+X',m.end>m.start],['Copy','Ctrl+C',m.end>m.start],['Paste','Ctrl+V',!!S.localClipboard],['Select all','Ctrl+A',!!S.url]].map(x=>'<button role="menuitem" data-act="textMenuAction" data-name="'+x[0]+'" '+(x[2]?'':'disabled')+'><span>'+x[0]+'</span><small>'+x[1]+'</small></button>').join('')+'</div>';}

function installedEnabled(x){return S.addonOn[x.name]===undefined?true:!!S.addonOn[x.name];}
function installedCard(x,manage){let enabled=installedEnabled(x);let status=x.core?'Core · Always on':(enabled?'Enabled':'Disabled');return '<div class="installed-card"><div class="installed-head"><img class="installed-logo" src="'+path(x.logo)+'" alt=""><div><div class="installed-name">'+esc(x.name)+'</div><div class="installed-status" style="'+(enabled?'':'color:#9a99a5')+'">'+status+'</div></div></div><div class="installed-capabilities">'+esc(x.kind)+'</div>'+
(manage?'<div class="installed-actions">'+
'<button class="extension-action" '+(x.core?'disabled':'')+' data-act="installedToggle" data-name="'+esc(x.name)+'">'+(enabled?'Disable':'Enable')+'</button>'+
(x.configurable?'<button class="extension-action" data-act="extConfigure" data-name="'+esc(x.name)+'">Configure</button>':'')+
'<button class="extension-action destructive" '+(x.core?'disabled':'')+' data-act="extRemove" data-name="'+esc(x.name)+'">Remove</button></div>':'')+
(!manage?'<button style="position:absolute;opacity:0;pointer-events:none" tabindex="-1">Manage</button>':'')+'</div>';}
function installedSection(){let list=INSTALLED.filter(x=>!S.addonRemoved[x.name]);return '<section class="ext-section"><div class="ext-section-head"><div><h2>Your Add-ons</h2><p>'+list.length+' installed · Manage what powers your worlds.</p></div><button class="extension-action" data-act="extManage">See all ›</button></div><div class="ext-installed-list scroll-x">'+list.map(x=>'<button class="installed-card" data-act="extManage" style="cursor:pointer"><span class="installed-head"><img class="installed-logo" src="'+path(x.logo)+'" alt=""><span><span class="installed-name" style="display:block">'+esc(x.name)+'</span><span class="installed-status" style="display:block">'+(x.core?'Core · Always on':installedEnabled(x)?'Enabled':'Disabled')+'</span></span></span><span class="installed-capabilities">'+esc(x.kind)+'</span></button>').join('')+'</div></section>';}
function huePair(i){let h=0;for(const c of ADDONS[i].name)h=(h*31+c.charCodeAt(0))%360;return ['hsl('+h+' 34% 16%)','hsl('+((h+30)%360)+' 32% 10%)'];}
function storeCard(index){let x=ADDONS[index],pair=huePair(index),enabled=isInstalled(x);
return '<div class="store-card"><button data-act="storeAction" data-idx="'+index+'" aria-label="'+esc(x.name)+'" style="border:0;padding:0;background:none;color:inherit;text-align:left;width:100%">'+
'<div class="store-art" style="--a:'+pair[0]+';--b:'+pair[1]+'"><img class="store-wash" src="'+path(x.logo)+'" alt=""><div class="store-logo-stage"><img src="'+path(x.logo)+'" alt=""></div>'+(enabled?'<span class="store-badge">✓ Installed</span>':'')+'</div>'+
'<h3>'+esc(x.name)+'</h3><p>'+esc(x.desc)+'</p></button><button class="store-mobile-action'+(enabled?' installed':'')+'" data-act="storeAction" data-idx="'+index+'" '+(enabled&&!x.configurable?'disabled':'')+'>'+ (enabled?(x.configurable?'Set up':'Installed'):x.configurable?'Set up':'Install')+'</button></div>';}
function heroTile(x){let off=S.houseOff[x.id]===true;return '<div class="house-tile"><button class="house-illustration" '+(x.id==='tributes'||x.id==='tankoyomi'?'data-act="heroOpen" data-id="'+x.id+'"':'disabled')+' title="'+esc(x.name)+'"><img src="'+path(x.logo)+'" alt="'+esc(x.name)+'"></button><div class="house-caption"><span class="house-name">'+esc(x.name)+'</span>'+ (x.kind==='source'?'<button class="house-toggle'+(off?'':' on')+'" data-act="heroToggle" data-id="'+x.id+'" aria-pressed="'+(!off)+'" aria-label="'+(off?'Add ':'Turn off ')+esc(x.name)+'">'+(off?'+':'✓')+'</button>':'')+'</div></div>';}
function extensionsHero(){return '<section class="ext-hero"><img class="ext-hero-art" src="'+path('extensions/house/starwars-banner.jpg')+'" alt=""><div class="hero-actions"><button data-act="extUrl"><span class="plus">+</span><span>Install from URL</span></button><button data-act="extSearch">'+ '<img src="'+path('icons/search.svg')+'" alt="">Search</button></div><div class="house-tray"><div class="house-row scroll-x">'+HOUSE.map(heroTile).join('')+'</div></div></section>';}
function extensionsRail(r,index){return '<section class="ext-section"><div class="ext-section-head"><div><h2>'+esc(r.title)+'</h2><p>'+esc(r.sub)+'</p></div>'+(r.noAll?'':'<button class="ext-see-all" data-act="extSeeAll" data-idx="'+index+'">See all ›</button>')+'</div><div class="ext-cards scroll-x">'+r.items.map(storeCard).join('')+'</div></section>';}
function extensionsSubpage(){let mode=S.mode;let list=INSTALLED.filter(x=>!S.addonRemoved[x.name]);let content='';
if(mode==='manage'){content='<div class="sub-head"><h1 class="sub-title">Your Add-ons</h1><p class="sub-description">'+list.length+' installed · Includes disabled add-ons. Core add-ons stay on.</p></div><div class="sub-body scroll-y"><div class="ext-manage-list">'+list.map(x=>installedCard(x,true)).join('')+'</div></div>';}
if(mode==='search'){let matches=S.extSearch.trim()?ADDONS.map((x,i)=>({x,i})).filter(x=>x.x.name.toLowerCase().includes(S.extSearch.toLowerCase())||x.x.desc.toLowerCase().includes(S.extSearch.toLowerCase())):[];content='<div class="sub-head"><input class="ext-search-field" id="extSearchField" aria-label="Search add-ons" placeholder="Search '+ADDONS.length+' add-ons" value="'+esc(S.extSearch)+'"><p class="sub-description">'+(S.extSearch.trim()?matches.length+' add-ons match.':'Names, what they do, or a category: anime, subtitles, debrid, Hindi…')+'</p></div><div class="sub-body scroll-y"><div class="ext-subgrid">'+matches.map(x=>storeCard(x.i)).join('')+'</div>'+(S.extSearch.trim()&&!matches.length?'<p class="ext-empty">No add-on matches that. Try a site name, a language or a genre.</p>':'')+'</div>';}
if(mode==='seeall'){let row=RAILS[S.seeallIndex]||RAILS[1];content='<div class="sub-head"><h1 class="sub-title">'+esc(row.title)+'</h1><p class="sub-description">'+esc(row.sub)+' '+row.items.length+' add-ons.</p></div><div class="sub-body scroll-y"><div class="ext-subgrid">'+row.items.map(storeCard).join('')+'</div></div>';}
return '<div class="ext-subpage">'+back('Store','extBack')+content+'</div>';}
function extensionsModal(){if(S.mode==='setup')return setupModal();if(S.mode==='url'||S.mode==='urlpreview'){let x=fixtureForUrl(S.url);let prev=S.urlPreview&&x;
return '<div class="modal-shade" data-shade="1"><div class="modal-panel ext-url-modal"><div class="modal-heading"><h2>Install from URL</h2>'+modalClose()+'</div><p class="modal-text">Add a Stremio-compatible extension, even if it isn’t in the Store.</p><p class="field-label">ADD-ON ADDRESS</p><input class="modal-input" id="extUrlField" aria-label="Add-on address" placeholder="https://example.com/manifest.json" value="'+esc(S.url)+'"><p class="helper">HTTPS and stremio:// links are supported.</p>'+
(prev?'<div class="url-preview"><img src="'+path(x.logo)+'" alt=""><div><h3>'+esc(x.name)+'</h3><p>'+esc(x.desc)+'</p><p>Provides: '+esc(x.kind)+'</p></div></div>':'')+
(S.urlError?'<p class="error" role="alert">'+esc(S.urlError)+'</p>':'')+(S.urlComplete?'<p class="setup-status">Added to your extensions.</p>':'')+'<div class="modal-buttons"><button class="extension-action" data-act="closeModal">Cancel</button><button class="gold-pill" data-act="urlPreview" '+(!S.url.trim()||(prev&&isInstalled(x)&&!S.urlComplete)?'disabled':'')+'>'+(S.urlComplete?'Done':prev?(isInstalled(x)?'Already installed':x.configurable?'Set up add-on':'Install add-on'):'Preview add-on')+'</button></div></div></div>';}
if(S.mode==='remove'){let name=S.removeTarget||'';return '<div class="modal-shade"><div class="modal-panel" style="max-width:440px;background:#141820"><div class="modal-heading"><h2 style="font-size:24px">Remove '+esc(name)+'?</h2></div><p class="modal-text" style="color:var(--dim);font-size:14px">This removes only this installation and its saved configuration. You can add it again from the Store.</p><div class="modal-buttons"><button class="extension-action" data-act="closeModal">Cancel</button><button class="extension-action destructive" data-act="extConfirmRemove">Remove</button></div></div></div>';}
return '';}
function renderExtensions(){return '<main class="app-surface ext-page"><div class="ext-background" style="background-image:url('+path('extensions/store-jolly-roger.jpg')+')"></div>'+
'<header class="ext-shell-head"'+(['manage','search','seeall','remove'].includes(S.mode)?' style="display:none"':'')+'>'+back('','extExit')+'<div><h1>Extensions</h1><p>Add-ons and sources for every world.</p></div></header>'+chrome()+
'<div class="ext-scroll scroll-y"'+(['manage','search','seeall','remove'].includes(S.mode)?' style="display:none"':'')+'>'+extensionsHero()+installedSection()+RAILS.map(extensionsRail).join('')+'<p class="ext-attribution">Community add-ons and stars from stremio-addons.net. Their authors run them, not Colosseum.</p></div>'+
(['manage','search','seeall','remove'].includes(S.mode)?extensionsSubpageIfNeeded():'')+extensionsModal()+'</main>';}
function extensionsSubpageIfNeeded(){return S.mode==='remove'?extensionsSubpageForRemove():extensionsSubpage();}
function extensionsSubpageForRemove(){return '<div class="ext-subpage">'+back('Store','extBack')+'<div class="sub-head"><h1 class="sub-title">Your Add-ons</h1><p class="sub-description">'+INSTALLED.filter(x=>!S.addonRemoved[x.name]).length+' installed · Includes disabled add-ons. Core add-ons stay on.</p></div><div class="sub-body scroll-y"><div class="ext-manage-list">'+INSTALLED.filter(x=>!S.addonRemoved[x.name]).map(x=>installedCard(x,true)).join('')+'</div></div></div>';}

const UPDATE_STATES={
 Checking:{status:'Checking for updates',meta:'Installed 1.1.10',button:''},
 Installing:{status:'Colosseum is updating',meta:'Target 1.2.0',button:''},
 Idle:{status:'No update check yet',meta:'Installed 1.1.10',button:'Check again'},
 UpToDate:{status:'Everything is up to date',meta:'Installed 1.1.10 · Latest 1.1.10',button:'Check again'},
 Available:{status:'Colosseum 1.2.0 is ready',meta:'Target 1.2.0',button:'Download update'},
 Downloading:{status:'Updating to 1.2.0',meta:'',button:'Pause download',progress:true},
 Paused:{status:'Update paused',meta:'Target 1.2.0',button:'Resume download',progress:true},
 Verifying:{status:'Verifying the update',meta:'Target 1.2.0',button:''},
 Ready:{status:'Ready to enter 1.2.0',meta:'Target 1.2.0',button:'Restart and update'},
 RecoverableError:{status:'The update could not finish',meta:'Target 1.2.0',button:'Retry download'},
 VerificationFailure:{status:'This update could not be verified',meta:'Target 1.2.0',button:'Check again'},
 ManualUpdateRequired:{status:'Manual update required',meta:'Target 1.2.0',button:'Check again'}
};
function renderUpdate(){let u=UPDATE_STATES[S.releaseState]||UPDATE_STATES.Available,ch=CHAPTERS[S.chapter]||CHAPTERS[0];return '<main class="app-surface update-page">'+
'<img class="update-art" src="'+path('wallpaper/captured-motion.jpg')+'" alt=""><div class="update-scrim"></div>'+chrome()+
'<button class="back-capsule update-back" data-act="updateBack" title="Back" aria-label="Back">'+ico('back',18)+'</button>'+
'<div class="update-headline"><div class="update-eyebrow">COLOSSEUM UPDATE</div><div class="update-version">'+(S.releaseState==='UpToDate'||S.releaseState==='Idle'?'1.1.10':'1.2.0')+'</div></div>'+
'<div class="update-copy"><div class="update-section">'+esc(ch.section)+'</div><h1>'+esc(ch.title)+'</h1><p>'+esc(ch.body)+'</p></div>'+
'<nav class="update-nav" aria-label="Update chapters"><span class="update-count">'+String(S.chapter+1).padStart(2,'0')+' / '+String(CHAPTERS.length).padStart(2,'0')+'</span><span class="update-nav-label">'+esc(ch.section)+'</span><span class="update-separator"></span>'+
CHAPTERS.map((x,i)=>'<button class="chapter-button'+(S.chapter===i?' active':'')+'" data-act="chapter" data-idx="'+i+'" aria-label="Chapter '+(i+1)+': '+esc(x.title)+'">'+String(i+1).padStart(2,'0')+'</button>').join('')+
'<button class="chapter-next" data-act="chapterNext" aria-label="Next chapter">Next</button></nav>'+
'<div class="update-status"><div class="update-status-text"><b>'+esc(u.status)+'</b>'+(u.meta?'<span>'+esc(u.meta)+'</span>':'')+'</div>'+
(u.progress?'<div class="update-progress"><div>'+ (Q.get('unknown')==='1'?'62 MB downloaded · size unknown':'62 MB of 100 MB · 62%')+'</div><div class="update-progress-track"><div class="update-progress-bar" style="width:'+(Q.get('unknown')==='1'?0:62)+'%"></div></div></div>':'')+
(u.button?'<button class="update-primary" data-act="updatePrimary">'+esc(u.button)+'</button>':'')+'</div></main>';}
function collectionOriginal(){return COLLECTIONS.find(x=>x.id===S.collectionId)||COLLECTIONS[0]||{id:'',name:'',description:'',items:[],notes:{}};}
function collectionById(){return ['editing','discard'].includes(S.mode)&&S.draft?S.draft:collectionOriginal();}
function beginCollectionEdit(){let x=collectionOriginal();S.draft={...x,items:x.items.slice(),notes:{...(x.notes||{})}};S.detailName=x.name;S.detailDesc=x.description;S.collectionEdits={...(x.notes||{})};S.collectionError='';S.removedItem=null;S.collectionFilter='all';}
function collectionNotice(message){S.collectionMessage=message;clearTimeout(S.messageTimer);S.messageTimer=setTimeout(()=>{S.collectionMessage='';render();},4200);}
function applyMemberships(ids){for(const c of COLLECTIONS){if(ids.includes(c.id)&&!c.items.includes(0))c.items.push(0);if(!ids.includes(c.id))c.items=c.items.filter(i=>i!==0);}}

function counts(x){return x.items.length===0?'Ready for your first title':x.items.length+' titles  ·  '+x.items.map(i=>MEDIA[i].kindLabel).filter((v,i,a)=>a.indexOf(v)===i).join(' · ');}
function shelfCard(x){let covers=x.items.slice(0,3).map(i=>MEDIA[i].cover);return '<button class="shelf-card" data-act="openCollection" data-id="'+esc(x.id)+'" aria-label="Open '+esc(x.name)+', '+x.items.length+' titles">'+
'<div class="shelf-art" style="--shelf-tint:'+esc(x.tint)+'aa">'+
(covers[1]?'<img class="shelf-cover left" src="'+path(covers[1])+'" alt="">':'')+
(covers[2]?'<img class="shelf-cover right" src="'+path(covers[2])+'" alt="">':'')+
(covers[0]?'<img class="shelf-cover center" src="'+path(covers[0])+'" alt="">':ico('collections',44))+
'</div><div class="shelf-title">'+esc(x.name)+'</div><div class="shelf-summary">'+esc(counts(x))+'</div></button>';}
function collectionSearch(){return '<div class="collection-search"><span class="collection-search-icon">'+ico('search',17)+'</span><input id="collectionSearchInput" type="search" aria-label="Find a collection" placeholder="Find a collection" value="'+esc(S.collectionQuery)+'">'+(S.collectionQuery?'<button class="collection-search-clear" aria-label="Clear search" data-act="clearCollectionSearch">'+ico('close',17)+'</button>':'')+'</div>';}
function collectionsOverview(){let show=COLLECTIONS.filter(x=>x.name.toLowerCase().includes(S.collectionQuery.toLowerCase()));return '<div class="collections-top">'+back('Back','collectionExit')+
'<div class="collections-header"><div class="collections-heading"><h1>Your Collections</h1><p>The things you want to keep together.</p></div><div class="collections-controls">'+collectionSearch()+cb('New collection','primary','collectionNew','plus')+'</div></div>'+
'<div class="collections-divider"></div>'+(show.length?'<div class="collections-grid">'+show.slice(0,S.collectionLimit||36).map(shelfCard).join('')+'</div>'+(show.length>(S.collectionLimit||36)?cb('Show more collections','','moreCollections'):''):'<div class="collections-empty">'+ico('search',44)+'<h2>'+(COLLECTIONS.length?'No collections by that name':'Make a little room for what you love')+'</h2><p>'+(COLLECTIONS.length?'Try another name, or clear your search.':'Start with a name. Add titles whenever you find them.')+'</p>'+(COLLECTIONS.length?cb('Clear search','','clearCollectionSearch','close'):cb('New collection','primary','collectionNew','plus'))+'</div>')+'</div>';}
function collectionMediaCard(midx,position,editing){let m=MEDIA[midx];let note=(editing?S.collectionEdits:collectionOriginal().notes||{})[midx]||'';
return '<article class="media-card"><button data-act="mediaOpen" data-idx="'+midx+'" '+(editing?'disabled':'')+' style="background:none;border:0;padding:0;text-align:left;color:inherit;width:100%"><img class="media-cover" src="'+path(m.cover)+'" alt=""><div class="media-title">'+esc(m.title)+'</div><div class="media-facts">'+esc(m.kindLabel+(m.creator?' · '+m.creator:''))+'</div></button>'+
(editing?'<textarea data-note="'+midx+'" aria-label="Your note for '+esc(m.title)+'" placeholder="Add a personal note" maxlength="500">'+esc(note)+'</textarea><div class="media-edit-controls"><span>'+(position+1)+' of '+collectionById().items.length+'</span><button aria-label="Move '+esc(m.title)+' earlier" data-act="mediaEarlier" '+(position===0?'disabled':'')+' data-idx="'+midx+'">'+ico('up',18)+'</button><button aria-label="Move '+esc(m.title)+' later" data-act="mediaLater" '+(position===collectionById().items.length-1?'disabled':'')+' data-idx="'+midx+'">'+ico('down',18)+'</button><button aria-label="Remove '+esc(m.title)+' from this collection" data-act="mediaRemove" data-idx="'+midx+'">'+ico('close',18)+'</button></div>':
(note?'<div class="media-note">'+esc(note)+'</div>':''))+'</article>';}
function collectionDetail(){let x=collectionById();let edit=S.mode==='editing'||S.mode==='discard';let items=x.items.filter(i=>edit||S.collectionFilter==='all'||MEDIA[i].kind===S.collectionFilter);
let options=['all'].concat(x.items.map(i=>MEDIA[i].kind).filter((v,i,a)=>a.indexOf(v)===i));
return '<div class="collection-detail">'+cb('Your Collections','ghost','collectionOverview','back')+
'<div class="collection-detail-heading"><div class="detail-copy">'+(edit?'<input id="collectionNameEdit" maxlength="100" aria-label="Collection name" class="detail-name-input" value="'+esc(S.detailName)+'"><textarea id="collectionDescriptionEdit" maxlength="300" aria-label="Collection description" class="detail-description-input" placeholder="Add a description">'+esc(S.detailDesc)+'</textarea>':
'<div class="collection-private">'+ico('lock',13)+'Private collection</div><h1>'+esc(x.name)+'</h1><p>'+esc(x.description)+'</p>')+'</div>'+
'<div class="detail-actions">'+(edit?cb('Cancel','ghost','collectionCancel')+cb('Save changes','primary','collectionSave'):cb('Edit collection','','collectionEdit','edit')+cb('','ghost compact','collectionDelete','trash'))+'</div></div>'+
(S.collectionError?'<p class="error" role="alert">'+esc(S.collectionError)+'</p>':'')+(S.mode==='discard'?'<div class="detail-leaving"><p>Discard your unsaved edits?</p><div>'+cb('Keep editing','','keepEditing')+cb('Discard edits','ghost','discardEditing')+'</div></div>':'')+
(edit?'<div style="font-size:12px;color:var(--muted)">Your order · Notes are optional'+(S.removedItem?cb('Undo remove','ghost','undoRemove'):'')+'</div>':'<div class="collection-filters">'+options.map(k=>'<button class="collection-filter'+(S.collectionFilter===k?' selected':'')+'" data-act="collectionFilter" data-kind="'+k+'">'+(k==='all'?'All':k==='book'?'Books':k==='series'?'Series':'Movies')+'</button>').join('')+'</div>')+
'<div class="collections-divider"></div>'+
(items.length?'<div class="collection-media-grid">'+items.map((i,j)=>collectionMediaCard(i,j,edit)).join('')+'</div>':'<div class="collections-empty">'+ico('collections',36)+'<h2>'+(S.collectionFilter==='all'?'Room for your next favourite':'No titles of this kind yet')+'</h2><p>Open a title and choose Add to Collections.</p>'+(S.collectionFilter!=='all'?cb('Show all titles','','collectionShowAll','collections'):'')+'</div>')+'</div>';}
function collectionReference(){const x=MEDIA[0];return '<div class="collection-reference"><img src="'+path(x.cover)+'" alt=""><div><b>'+esc(x.title)+'</b><p>'+esc(x.kindLabel+(x.year||x.creator?' · '+(x.year||x.creator):''))+'</p></div>'+ico(x.kind,20)+'</div>';}
function collectionsPopup(){const cls='modal-panel collection-popup'+(S.w<512?' small':'');if(S.mode==='new'){return '<div class="modal-shade collection-popup-shade"><div class="'+cls+'"><div class="modal-heading"><h2>'+(S.newFromMembership?'Add to Collections':'New collection')+'</h2>'+modalClose()+'</div><p class="modal-text">Give it a name. Make it yours over time.</p>'+(S.newFromMembership?collectionReference():'')+'<div class="eyebrow">Collection name</div><input class="modal-input" id="collectionNewInput" aria-label="Collection name" maxlength="100" placeholder="Something worth keeping" value="'+esc(S.collectionNewName)+'"><p class="error" id="collectionNewError" role="alert"></p><div class="modal-buttons">'+cb('Cancel','ghost',S.newFromMembership?'cancelCreate':'closeModal')+cb(S.newFromMembership?'Create & add':'Create collection','primary','collectionCreate')+'</div></div></div>';}
if(S.mode==='delete'){let x=collectionById();return '<div class="modal-shade collection-popup-shade"><div class="'+cls+'" style="max-width:480px"><div class="modal-heading"><h2>Delete this collection?</h2></div><p style="font-size:16px;font-weight:600;margin-top:16px">'+esc(x.name)+'</p><p style="font-size:14px;line-height:1.5;color:var(--dim);margin-top:16px">Only this collection will be removed. Your Libraries, downloads, and progress stay as they are.</p><div class="modal-buttons">'+cb('Cancel','ghost','closeModal')+cb('Delete collection','destructive','collectionConfirmDelete','trash')+'</div></div></div>';}
if(S.mode==='membership'){let chosen=S.memberSelected||[];let matches=COLLECTIONS.filter(c=>c.name.toLowerCase().includes((S.memberSearch||'').toLowerCase()));return '<div class="modal-shade collection-popup-shade"><div class="'+cls+'"><div class="modal-heading"><h2>Add to Collections</h2>'+modalClose()+'</div><p class="modal-text">Keep this title in one collection, or a few.</p>'+collectionReference()+'<div class="collection-search member-search"><span class="collection-search-icon">'+ico('search',17)+'</span><input id="collectionMemberSearch" type="search" aria-label="Find a collection" placeholder="Find a collection" value="'+esc(S.memberSearch||'')+'">'+(S.memberSearch?'<button class="collection-search-clear" data-act="clearMemberSearch" aria-label="Clear search">'+ico('close',17)+'</button>':'')+'</div><div class="scroll-y membership-list">'+matches.map(c=>'<button class="collection-membership-row" data-act="toggleMembership" role="checkbox" aria-checked="'+chosen.includes(c.id)+'" data-id="'+c.id+'"><span class="membership-cover">'+(c.items.length?'<img src="'+path(MEDIA[c.items[0]].cover)+'" alt="">':ico('collections',20))+'</span><span class="membership-copy"><b>'+esc(c.name)+'</b><small>'+c.items.length+' titles</small></span><span class="membership-check">'+(chosen.includes(c.id)?ico('check',16):'')+'</span></button>').join('')+(!matches.length?'<p class="membership-empty">'+(COLLECTIONS.length?'No collections match that name.<br>Create one below, or clear your search.':'Your first collection starts here.<br>Give this story a place to stay.')+'</p>':'')+'</div>'+cb('New collection','ghost member-new','collectionNew','plus')+'<div class="modal-buttons"><span class="membership-private">Private, just for you</span>'+cb('Cancel','ghost','closeModal')+cb('Save','primary','saveMembership')+'</div></div></div>';}return '';}
function renderCollections(){let overview=['','overview','new','membership'].includes(S.mode);let detail=['detail','editing','delete','discard'].includes(S.mode);return '<main class="app-surface collections-page"><div class="collections-wallpaper" style="background-image:url('+path('wallpaper/cold-ripple.jpg')+')"></div><div class="collections-tint"></div>'+
chrome()+'<div class="collections-scroll scroll-y">'+(overview?collectionsOverview():collectionDetail())+'</div>'+
'<div class="collections-dock"><button aria-label="Open taskbar"><img src="'+path('icons/colosseum.svg')+'" alt=""></button><button aria-label="Vault"><img src="'+path('icons/vault-folder.svg')+'" alt=""></button><button aria-label="Your Collections"><span style="color:var(--gold)">'+ico('collections',23)+'</span></button></div>'+
collectionsPopup()+(S.collectionMessage?'<div class="collection-message" role="status"><span>'+esc(S.collectionMessage)+'</span>'+cb('', 'ghost compact','dismissCollectionMessage','close')+'</div>':'')+'</main>';}

const REVIEW_STATES={
 wallpaper:['Default','Selected','Search','Selected + keyboard'],
 extensions:['Store','Search','Your Add-ons','URL dialog','URL preview','URL ready','URL complete','Setup','Remove confirmation','See all'],
 update:['Available','Idle','Checking','UpToDate','Downloading','Paused','Verifying','Ready','Installing','RecoverableError','VerificationFailure','ManualUpdateRequired'],
 collections:['Overview','Detail','Editing','New collection','Add to Collections','Delete collection','Discard edits','Empty collection','Empty overview']
};
function resetState(label){S.mode='';S.keyboard=false;
 if(PAGE==='wallpaper'){S.wallClosed=false;S.extra=0;S.shuffle=0;S.pick=label==='Selected'||label==='Selected + keyboard'?'noir':'';S.query=label==='Search'?'dune':'landscape';S.querySubmitted=S.query;S.keyboard=label==='Selected + keyboard';S.sort='relevance';}
 if(PAGE==='extensions'){let m={'Store':'store','Search':'search','Your Add-ons':'manage','URL dialog':'url','URL preview':'urlpreview','URL ready':'urlpreview','URL complete':'urlpreview','Setup':'setup','Remove confirmation':'remove','See all':'seeall'};S.mode=m[label]||'store';S.seeallIndex=1;S.removeTarget='Torrentio';S.extSearch=label==='Search'?'anime':'';S.url=(label==='URL preview'?'https://torrentio.strem.fun/manifest.json':'');S.urlPreview=['URL preview','URL ready','URL complete'].includes(label);S.urlComplete=label==='URL complete';S.urlError='';S.textMenu=null;if(['URL ready','URL complete'].includes(label))S.url='https://fixture.colosseum.invalid/subdl/manifest.json';if(label==='URL complete')installLocal(ADDONS[8]);if(label==='Setup'){S.setupAddon=ADDONS[2];S.setupReturn='store';S.setupLink='';S.setupStatus='';}}
 if(PAGE==='update'){S.releaseState=label||'Available';S.chapter=0;}
 if(PAGE==='collections'){if(label==='Empty overview')COLLECTIONS.length=0;if(label==='Empty collection'){if(!COLLECTIONS.some(c=>c.id==='empty'))COLLECTIONS.push({id:'empty',name:'Something worth keeping',description:'',items:[],notes:{},tint:'#465265'});}let m={'Overview':'overview','Detail':'detail','Editing':'editing','New collection':'new','Add to Collections':'membership','Delete collection':'delete','Discard edits':'discard','Empty collection':'detail','Empty overview':'overview'};S.mode=m[label]||'overview';S.collectionId=label==='Empty collection'?'empty':'worlds';S.collectionFilter='all';S.detailName='';S.detailDesc='';S.collectionEdits={};S.memberSelected=COLLECTIONS.filter(c=>c.items.includes(0)).map(c=>c.id);S.memberSearch='';S.collectionLimit=36;S.collectionMessage='';S.draft=null;S.collectionError='';S.newFromMembership=false;if(['editing','discard'].includes(S.mode))beginCollectionEdit();}
 render();
}
function render(){const active=document.activeElement,wasModal=!!active?.closest?.(".modal-panel"),focus=active?.id,da=active?.dataset||{},selStart=active?.selectionStart;let saved={};MOCK.querySelectorAll('.scroll-y,.scroll-x').forEach((x,i)=>saved[x.className+'-'+i]=[x.scrollTop,x.scrollLeft]);
MOCK.innerHTML=PAGE==='wallpaper'?renderWallpaper():PAGE==='extensions'?renderExtensions():PAGE==='update'?renderUpdate():renderCollections();
MOCK.querySelectorAll('.scroll-y,.scroll-x').forEach((x,i)=>{const old=saved[x.className+'-'+i];if(old){x.scrollTop=old[0];x.scrollLeft=old[1];}});
const modal=MOCK.querySelector('.modal-panel');if(modal){modal.setAttribute('role','dialog');modal.setAttribute('aria-modal','true');modal.setAttribute('aria-label',modal.querySelector('h2')?.textContent||'Dialog');for(const e of MOCK.querySelector('.app-surface').children)if(!e.classList.contains('modal-shade'))e.inert=true;}
let next=focus?document.getElementById(focus):[...MOCK.querySelectorAll('[data-act]')].find(e=>e.dataset.act===da.act&&e.dataset.name===da.name&&e.dataset.idx===da.idx&&e.dataset.id===da.id&&!e.closest('[inert]'));if(!modal&&wasModal&&!next){const o=S.opener||{};next=[...MOCK.querySelectorAll('[data-act]')].find(e=>e.dataset.act===o.act&&e.dataset.name===o.name&&e.dataset.idx===o.idx&&e.dataset.id===o.id)||MOCK.querySelector('.back-plain,.collection-action');}if(next?.disabled)next=MOCK.querySelector('[data-act="mediaRemove"][data-idx="'+(da.idx||'')+'"]')||null;if(modal&&(!next||!modal.contains(next)))next=modal.querySelector('input')||modal.querySelector('button:not(:disabled)');if(next){next.focus({preventScroll:true});if((S.keyboard&&next.matches('input,textarea'))||['mediaEarlier','mediaLater'].includes(da.act))next.scrollIntoView({block:'nearest',inline:'nearest'});if(selStart!=null&&next.setSelectionRange&&['text','search','url','textarea'].includes(next.type))try{next.setSelectionRange(selStart,selStart);}catch(e){}}
if(S.textMenu){MOCK.querySelector('.app-surface').insertAdjacentHTML('beforeend',textMenuMarkup());if(modal)modal.inert=true;MOCK.querySelector('.text-menu button:not(:disabled)')?.focus({preventScroll:true});}
if(S.focusNext){const f=(modal||MOCK).querySelector(S.focusNext);S.focusNext=null;f?.focus();}
window.renderNativeWallpapers?.(MOCK);
}

function readToolbar(){S.w=Math.max(300,Math.min(2000,Number(document.getElementById('viewportWidth').value)||412));
 S.h=Math.max(300,Math.min(2000,Number(document.getElementById('viewportHeight').value)||900));
 S.top=Math.max(0,Math.min(80,Number(document.getElementById('insetTop').value)||0));
 S.bottom=Math.max(0,Math.min(80,Number(document.getElementById('insetBottom').value)||0));
 S.scale=Math.max(.85,Math.min(1.4,Number(document.getElementById('fontScale').value)||1));
 S.keyboard=document.getElementById('showKeyboard').checked;S.desktop=document.getElementById('viewportPreset').value==='desktop';
 applySize();}
function applySize(){
 DEVICE.style.width=S.w+'px';DEVICE.style.height=S.h+'px';
 DEVICE.className='device '+(S.desktop?'desktop':Math.min(S.w,S.h)<600?'phone':'tablet')+' '+(S.desktop?'desktop-platform':'android')+(S.w>S.h?' landscape':' portrait')+(S.keyboard?' keyboard':'')+(S.scale>1?' text-enlarged':'')+((S.h-S.top-S.bottom-(S.keyboard?(S.h<S.w?Math.min(185,Math.round(S.h*.4)):Math.min(300,Math.round(S.h*.39))):0))<330?' compact-available':'');
 MOCK.style.setProperty('--safe-top',S.top+'px');MOCK.style.setProperty('--safe-bottom',S.bottom+'px');MOCK.style.setProperty('--font-scale',String(S.scale));MOCK.style.setProperty('--device-width',S.w+'px');MOCK.style.setProperty('--device-height',S.h+'px');
 MOCK.style.setProperty('--ime',(S.keyboard?(S.h<S.w?Math.min(185,Math.round(S.h*.4)):Math.min(300,Math.round(S.h*.39))):0)+'px');
 const stage=document.querySelector('.review-stage');
 if(CAPTURE){DEVICE.style.transform='none';stage.style.height=S.h+'px';stage.style.width=S.w+'px';}
 else{let factor=Math.max(.2,Math.min(1,(innerWidth-40)/S.w,(innerHeight-260)/S.h));DEVICE.style.transform='scale('+factor+')';stage.style.height=Math.ceil(S.h*factor+30)+'px';}
 render();
}
function initToolbar(){
 if(CAPTURE){document.body.classList.add('capture');let key=Q.get('preset')||'portrait',dim=PRESETS[key]||PRESETS.portrait;S.w=Number(Q.get('w'))||dim[0];S.h=Number(Q.get('h'))||dim[1];S.desktop=Q.get('platform')==='desktop'||key==='desktop'||(S.w===1280&&S.h===700&&Q.get('platform')!=='android');S.top=Number(Q.get('top'))||0;S.bottom=Number(Q.get('bottom'))||0;S.scale=Number(Q.get('fontScale'))||1;resetState(Q.get('state')||REVIEW_STATES[PAGE][0]);if(Q.get('keyboard')==='1')S.keyboard=true;applySize();return;}
 let options=Object.keys(PRESETS).map(name=>'<option value="'+name+'">'+name+' · '+PRESETS[name].join('×')+'</option>').join('')+'<option value="custom">Custom</option>';
 TOOLS.innerHTML='<label>Page <span style="font-weight:600;color:#f7f7f5;margin-top:5px">'+({'wallpaper':'Wallpaper search','extensions':'Extensions Store','update':'Update','collections':'Your Collections'}[PAGE])+'</span></label>'+
 '<label>Preset<select id="viewportPreset">'+options+'</select></label>'+
 '<label>Width<input id="viewportWidth" type="number" min="300" max="2000" value="412"></label><label>Height<input id="viewportHeight" type="number" min="300" max="2000" value="900"></label>'+
 '<label>Top inset<input id="insetTop" type="number" min="0" max="80" value="0"></label><label>Bottom inset<input id="insetBottom" type="number" min="0" max="80" value="0"></label>'+
 '<label>Font scale<input id="fontScale" type="number" min=".85" max="1.4" step=".05" value="1"></label><label>Page state<select id="pageState">'+REVIEW_STATES[PAGE].map(x=>'<option>'+esc(x)+'</option>').join('')+'</select></label>'+
 '<label class="toggle"><input id="showKeyboard" type="checkbox">Keyboard raised</label>'+
 '<label class="toggle"><input id="longFixtures" type="checkbox" '+(Q.get('long')==='1'?'checked':'')+'>Long fixtures</label>'+ (PAGE==='update'?'<label class="toggle"><input id="unknownSize" type="checkbox" '+(Q.get('unknown')==='1'?'checked':'')+'>Unknown size</label>':'')+'<a href="index.html" style="padding:6px 10px;color:#d9c47f;font-size:12px">All studies</a>'+
 '<p class="review-warning">Provisional logical pixels · Inset and keyboard are simulated outside the product UI · Representative collection and provider fixtures · Local only, no network or install/update/apply operations</p>';
 document.getElementById('viewportPreset').addEventListener('change',e=>{let dim=PRESETS[e.target.value];if(dim){document.getElementById('viewportWidth').value=dim[0];document.getElementById('viewportHeight').value=dim[1];}readToolbar();});
 ['viewportWidth','viewportHeight','insetTop','insetBottom','fontScale','showKeyboard'].forEach(id=>document.getElementById(id).addEventListener('change',()=>{if(id==='viewportWidth'||id==='viewportHeight')document.getElementById('viewportPreset').value='custom';readToolbar();}));
 document.getElementById('pageState').addEventListener('change',e=>{resetState(e.target.value);document.getElementById('showKeyboard').checked=S.keyboard;readToolbar();});
 for(const [id,key] of [['viewportWidth','w'],['viewportHeight','h'],['insetTop','top'],['insetBottom','bottom'],['fontScale','fontScale']])if(Q.has(key))document.getElementById(id).value=Q.get(key);const qp=Q.get('preset');if(qp&&PRESETS[qp]){document.getElementById('viewportPreset').value=qp;if(!Q.has('w'))document.getElementById('viewportWidth').value=PRESETS[qp][0];if(!Q.has('h'))document.getElementById('viewportHeight').value=PRESETS[qp][1];}let state=Q.get('state')||REVIEW_STATES[PAGE][0];document.getElementById('pageState').value=state;resetState(state);if(Q.get('keyboard')==='1')S.keyboard=true;document.getElementById('showKeyboard').checked=S.keyboard;for(const id of ['longFixtures','unknownSize'])document.getElementById(id)?.addEventListener('change',()=>{const q=new URLSearchParams({w:S.w,h:S.h,top:S.top,bottom:S.bottom,fontScale:S.scale,preset:document.getElementById('viewportPreset').value,state:document.getElementById('pageState').value,keyboard:S.keyboard?'1':'0',long:document.getElementById('longFixtures').checked?'1':'0',unknown:document.getElementById('unknownSize')?.checked?'1':'0'});location.search=q.toString();});readToolbar();
}
function chooseAction(node){let act=node.dataset.act;if(['extUrl','extRemove','collectionNew','collectionDelete','storeAction','extConfigure'].includes(act))S.opener={...node.dataset};
 switch(act){
 case 'wallPick':S.pick=node.dataset.id;break;
 case 'wallSort':S.sort=node.dataset.mode;S.shuffle=(S.shuffle||0)+1;note('Local fixture order changed; no provider ranking or search request.');break;
 case 'wallMore':S.extra=8;break;
 case 'wallClose':S.pick='';S.wallClosed=true;note('Picker closed locally. Reopen with the review state selector.');break;
 case 'wallApply':S.applied=S.pick;S.appliedScope=node.dataset.scope;S.wallClosed=true;S.pick='';note('Wallpaper applied to the local HTML preview only ('+S.appliedScope+'). No app setting changed.');break;
 case 'setupOpen':note(S.desktop?'Original embedded setup failure/reload shown locally. No remote page loaded.':'Original Android browser handoff requested locally. No external browser or service was opened.');break;
 case 'setupPaste':S.setupLink=S.localClipboard||'';if(!S.setupLink){S.setupStatus='The clipboard is empty.';break;}
 case 'setupInstall':{const link=S.setupLink||S.localClipboard||'';if(!link){S.setupStatus='The clipboard is empty.';break;}if(!normalizedLink(link)){S.setupStatus="That isn't an add-on link.";break;}installLocal(S.setupAddon,S.setupReplacing);S.mode=S.setupReturn||'store';break;}
 case 'setupSkip':installLocal(S.setupAddon);S.mode=S.setupReturn||'store';break;
 case 'textMenuAction':{let m=S.textMenu,name=node.dataset.name;let start=m.start,end=m.end;if(name==='Copy'||name==='Cut')S.localClipboard=S.url.slice(start,end);if(name==='Cut'){S.url=S.url.slice(0,start)+S.url.slice(end);end=start;}if(name==='Paste'){const v=S.localClipboard||'';S.url=S.url.slice(0,start)+v+S.url.slice(end);start=end=start+v.length;}if(name==='Select all'){start=0;end=S.url.length;}S.textMenu=null;if(['Cut','Paste'].includes(name)){S.urlPreview=false;S.urlComplete=false;S.urlError='';}render();let field=document.getElementById('extUrlField');field?.focus();field?.setSelectionRange(start,end);note('Text menu uses an isolated mock clipboard; the computer clipboard was not read.');return;}
 case 'extSearch':S.mode='search';S.extSearch='';break;
 case 'extUrl':S.mode='url';S.url='';S.urlPreview=false;S.urlComplete=false;S.urlError='';break;
 case 'extManage':S.mode='manage';break;
 case 'extBack':S.mode='store';break;
 case 'extExit':note('Original Back route requested. Its shell destination belongs outside Batch 5.');return;
 case 'extSeeAll':S.mode='seeall';S.seeallIndex=Number(node.dataset.idx)||1;break;
 case 'heroToggle':S.houseOff[node.dataset.id]=!S.houseOff[node.dataset.id];break;
 case 'heroOpen':note((node.dataset.id==='tributes'?'Universe Hall':'Tankoyomi configuration')+' route requested. The destination belongs outside Batch 5.');return;
 case 'storeAction':{let x=ADDONS[Number(node.dataset.idx)];if(x){if(x.configurable)openSetup(x);else if(!isInstalled(x))installLocal(x);}break;}
 case 'installedToggle':S.addonOn[node.dataset.name]=!installedEnabled({name:node.dataset.name});break;
 case 'extConfigure':{let x=ADDONS.find(x=>x.name===node.dataset.name)||INSTALLED.find(x=>x.name===node.dataset.name);if(x)openSetup(x,true);break;}
 case 'extRemove':S.removeTarget=node.dataset.name;S.mode='remove';break;
 case 'extConfirmRemove':S.addonRemoved[S.removeTarget]=true;S.mode='manage';break;
 case 'urlPreview':{S.url=document.getElementById('extUrlField')?.value||S.url;S.urlError='';if(S.urlComplete){S.mode='store';break;}let x=fixtureForUrl(S.url);if(!normalizedLink(S.url)){S.urlError="That isn't a valid add-on address.";break;}if(!x){S.urlError='The add-on could not be loaded.';note('No offline manifest fixture is provided for that address. Use the addresses listed in the batch index; no network request occurred.');break;}if(S.urlPreview){if(isInstalled(x)){S.urlError='This add-on address is already installed.';break;}if(x.configurable)openSetup(x);else{installLocal(x);S.urlComplete=true;}break;}S.urlPreview=true;S.mode='urlpreview';break;}
 case 'closeModal':S.textMenu=null;S.mode=PAGE==='extensions'?(S.mode==='remove'?'manage':S.mode==='setup'?S.setupReturn||'store':'store'):S.mode==='delete'?'detail':'overview';S.urlPreview=false;break;
 case 'updateBack':note('Original Back route requested. The shell destination belongs outside Batch 5.');return;
 case 'chapter':S.chapter=Number(node.dataset.idx)||0;break;
 case 'chapterNext':S.chapter=(S.chapter+1)%CHAPTERS.length;break;
 case 'updatePrimary':{let next={Available:'Downloading',Downloading:'Paused',Paused:'Downloading',UpToDate:'Checking',Ready:'Installing',Idle:'Checking',RecoverableError:'Downloading',VerificationFailure:'Checking',ManualUpdateRequired:'Checking'};S.releaseState=next[S.releaseState]||S.releaseState;note('Local update-state preview only: '+S.releaseState+'. No checking, downloading or installation.');break;}
 case 'collectionExit':note('Original Back route requested. The shell destination belongs outside Batch 5.');return;
 case 'clearCollectionSearch':S.collectionQuery='';S.collectionLimit=36;break;
 case 'moreCollections':S.collectionLimit=(S.collectionLimit||36)+36;break;
 case 'dismissCollectionMessage':S.collectionMessage='';break;
 case 'openCollection':S.collectionId=node.dataset.id;S.mode='detail';S.collectionFilter='all';break;
 case 'collectionNew':S.newFromMembership=S.mode==='membership';S.mode='new';S.collectionNewName=S.newFromMembership?(S.memberSearch||''):'';break;
 case 'cancelCreate':S.mode='membership';S.newFromMembership=false;S.focusNext='[data-act="collectionNew"]';break;
 case 'clearMemberSearch':S.memberSearch='';S.focusNext='#collectionMemberSearch';break;
 case 'collectionCreate':{let name=document.getElementById('collectionNewInput')?.value.trim()||'';if(!name){let err=document.getElementById('collectionNewError');if(err)err.textContent='Give your collection a name.';return;}let id='local'+Date.now();COLLECTIONS.push({id,name:name,description:'',items:[],notes:{},tint:'#465265'});if(S.newFromMembership)applyMemberships((S.memberSelected||[]).concat(id));collectionNotice(S.newFromMembership?'Collection created and title added':'Collection created');S.newFromMembership=false;S.mode='overview';S.collectionQuery='';note('Collection created in this HTML fixture.');break;}
 case 'collectionOverview':if(S.mode==='editing'||S.mode==='discard')S.mode='discard';else S.mode='overview';break;
 case 'collectionEdit':beginCollectionEdit();S.mode='editing';break;
 case 'collectionCancel':S.draft=null;S.collectionError='';S.removedItem=null;S.mode='detail';break;
 case 'collectionSave':{const name=S.detailName.trim();if(!name){S.collectionError='Give your collection a name.';render();document.getElementById('collectionNameEdit')?.focus();return;}let x=collectionOriginal();x.name=name;x.description=S.detailDesc;x.items=S.draft.items.slice();x.notes={...S.collectionEdits};S.draft=null;S.removedItem=null;S.collectionError='';S.mode='detail';collectionNotice('Collection updated');note('Collection changes saved to the local HTML fixture.');break;}
 case 'collectionDelete':S.mode='delete';break;
 case 'collectionConfirmDelete':{let i=COLLECTIONS.findIndex(x=>x.id===S.collectionId);if(i>=0)COLLECTIONS.splice(i,1);S.mode='overview';break;}
 case 'collectionFilter':S.collectionFilter=node.dataset.kind;break;
 case 'collectionShowAll':S.collectionFilter='all';break;
 case 'keepEditing':S.mode='editing';break;
 case 'discardEditing':S.draft=null;S.removedItem=null;S.collectionError='';S.mode='overview';break;
 case 'mediaEarlier':case 'mediaLater':{let x=collectionById(),i=x.items.indexOf(Number(node.dataset.idx));let j=i+(act==='mediaEarlier'?-1:1);if(i>=0&&j>=0&&j<x.items.length){[x.items[i],x.items[j]]=[x.items[j],x.items[i]];}break;}
 case 'mediaRemove':{let x=collectionById(),i=x.items.indexOf(Number(node.dataset.idx));if(i>=0){S.removedItem={id:x.items[i],index:i};x.items.splice(i,1);}break;}
 case 'undoRemove':if(S.removedItem){collectionById().items.splice(S.removedItem.index,0,S.removedItem.id);S.removedItem=null;}break;
 case 'mediaOpen':note('Original title route requested: '+MEDIA[Number(node.dataset.idx)].title+'. The title page belongs outside Batch 5.');return;
 case 'toggleMembership':{let id=node.dataset.id;if(!S.memberSelected)S.memberSelected=[];S.memberSelected=S.memberSelected.includes(id)?S.memberSelected.filter(x=>x!==id):S.memberSelected.concat([id]);break;}
 case 'saveMembership':applyMemberships(S.memberSelected||[]);S.mode='overview';collectionNotice('Collections updated');note('Memberships saved to the local HTML fixture.');break;
 default:return;
 }
 render();
}
MOCK.addEventListener('click',e=>{let node=e.target.closest('[data-act]');if(node){e.preventDefault();chooseAction(node);}});
MOCK.addEventListener('submit',e=>{if(e.target.id==='wallSearchForm'){e.preventDefault();S.query=document.getElementById('wallSearchInput').value;S.querySubmitted=S.query;render();}});
MOCK.addEventListener('input',e=>{
 if(e.target.id==='wallSearchInput'){S.query=e.target.value;return;}
 if(e.target.id==='extUrlField'){S.url=e.target.value;S.urlPreview=false;S.urlComplete=false;S.urlError='';render();return;}
 if(e.target.id==='setupLink'){S.setupLink=e.target.value;S.setupStatus='';render();return;}
 if(e.target.id==='collectionNewInput'){S.collectionNewName=e.target.value;document.getElementById('collectionNewError').textContent='';return;}
 if(e.target.id==='collectionNameEdit'){S.detailName=e.target.value;return;}
 if(e.target.id==='collectionDescriptionEdit'){S.detailDesc=e.target.value;return;}
 if(e.target.matches('[data-note]')){S.collectionEdits[e.target.dataset.note]=e.target.value;return;}
 if(e.target.id==='extSearchField'||e.target.id==='collectionSearchInput'||e.target.id==='collectionMemberSearch'){
  let start=e.target.selectionStart||0;let id=e.target.id;
  if(id==='extSearchField')S.extSearch=e.target.value;
  if(id==='collectionSearchInput'){S.collectionQuery=e.target.value;S.collectionLimit=36;}
  if(id==='collectionMemberSearch')S.memberSearch=e.target.value;
  render();let el=document.getElementById(id);if(el){el.focus();el.setSelectionRange(start,start);}return;
 }
});
MOCK.addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.id==='collectionNewInput'&&!e.isComposing){e.preventDefault();chooseAction(MOCK.querySelector('[data-act="collectionCreate"]'));return;}if(e.key==='Escape'){if(PAGE==='extensions'&&S.mode!=='store'){S.mode=(S.mode==='remove'?'manage':S.mode==='setup'?S.setupReturn||'store':'store');render();}else if(PAGE==='collections'){if(S.mode==='discard')S.mode='editing';else if(S.mode==='editing')S.mode='discard';else if(S.mode==='delete')S.mode='detail';else if(S.mode==='new'&&S.newFromMembership){S.mode='overview';S.newFromMembership=false;}else S.mode='overview';render();}else if(PAGE==='wallpaper'){S.pick='';render();}else if(PAGE==='update'){return;}e.preventDefault();}else if(PAGE==='update'&&(e.key==='ArrowRight'||e.key==='ArrowLeft')){S.chapter=Math.max(0,Math.min(CHAPTERS.length-1,S.chapter+(e.key==='ArrowRight'?1:-1)));render();}});
MOCK.addEventListener('contextmenu',e=>{if(e.target.id!=='extUrlField')return;e.preventDefault();let r=MOCK.getBoundingClientRect(),k=r.width/S.w;S.textMenu={x:Math.max(8,Math.min(S.w-252,(e.clientX-r.left)/k)),y:Math.max(S.top+8,Math.min(S.h-S.bottom-(S.keyboard?Math.min(300,S.h*.39):0)-182,(e.clientY-r.top)/k)),start:e.target.selectionStart||0,end:e.target.selectionEnd||0};render();});
MOCK.addEventListener('keydown',e=>{if(S.textMenu&&e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();S.textMenu=null;render();document.getElementById('extUrlField')?.focus();return;}let modal=MOCK.querySelector('.text-menu')||MOCK.querySelector('.modal-panel');if(modal&&e.key==='Tab'){let f=[...modal.querySelectorAll('button:not(:disabled),input:not(:disabled),textarea:not(:disabled),[tabindex="0"]')].filter(n=>n.getClientRects().length);let i=f.indexOf(document.activeElement);if(e.shiftKey&&i<=0){e.preventDefault();f.at(-1)?.focus();}else if(!e.shiftKey&&(i===f.length-1||i===-1)){e.preventDefault();f[0]?.focus();}}if(e.key==='Enter'&&e.target.id==='extUrlField'){e.preventDefault();S.urlPreview=false;S.urlComplete=false;chooseAction(MOCK.querySelector('[data-act="urlPreview"]'));}if(e.key==='Enter'&&e.target.id==='setupLink'){e.preventDefault();chooseAction(MOCK.querySelector('[data-act="setupInstall"]'));}},true);
window.addEventListener('resize',()=>{if(!CAPTURE)applySize();});
if(Q.get('long')==='1'){if(PAGE==='collections'){const first=MEDIA.slice();for(let i=0;i<40;i++){MEDIA.push({...first[i%first.length],title:first[i%first.length].title+' — long collection fixture '+(i+1)});COLLECTIONS[0].items.push(MEDIA.length-1);}for(let i=0;i<40;i++)COLLECTIONS.push({id:'long-overview-'+i,name:'Worlds worth returning to — collection '+(i+1),description:'The things you want to keep together.',items:[i%8,(i+1)%8],notes:{},tint:'#536078'});COLLECTIONS[0].name='The worlds I keep coming back to, even after the last chapter';COLLECTIONS[0].description='The things you want to keep together. '.repeat(7);}if(PAGE==='update'){CHAPTERS[0].title='The latest chapter — The worlds you keep coming back to';CHAPTERS[0].body=CHAPTERS[0].body+' '+CHAPTERS[1].body+' '+CHAPTERS[2].body+' '+CHAPTERS[0].body;}if(PAGE==='extensions')ADDONS.forEach(x=>x.desc=x.desc+' '+x.desc);}
initToolbar();
note('Local fixtures only. Native wallpaper renderings use frozen QML/shaders; All 13 source-named KDE picks are available locally with recorded provenance. Search uses titled project art. Service requests, installs and app changes are not performed.');
})();