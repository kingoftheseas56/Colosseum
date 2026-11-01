(function(){
"use strict";
const ASSET="../../reference/desktop/assets/";
const P=document.body.dataset.page;
const image=(name)=>ASSET+name;
const sagaArt=image("wallpaper/sagas/harry-potter.jpg"),bondArt=image("universes/marks/james-bond.png"),jumpArt=image("universes/marks/shonen-jump.png"),cold=image("wallpaper/cold-ripple.jpg");
const storyNames=["Sorcerer's Stone","Chamber of Secrets","Prisoner of Azkaban","Goblet of Fire","Order of the Phoenix","Half-Blood Prince","Deathly Hallows"];
const sagaBookTitles=["Harry Potter and the Sorcerer's Stone","Harry Potter and the Chamber of Secrets","Harry Potter and the Prisoner of Azkaban","Harry Potter and the Goblet of Fire","Harry Potter and the Order of the Phoenix","Harry Potter and the Half-Blood Prince","Harry Potter and the Deathly Hallows"];
const sagaFilmTitles=["Harry Potter and the Sorcerer's Stone","Harry Potter and the Chamber of Secrets","Harry Potter and the Prisoner of Azkaban","Harry Potter and the Goblet of Fire","Harry Potter and the Order of the Phoenix","Harry Potter and the Half-Blood Prince","Harry Potter and the Deathly Hallows: Part 1","Harry Potter and the Deathly Hallows: Part 2"];
const sagaBooks=sagaBookTitles.map((title,i)=>({key:"book"+i,title,medium:"book",author:"J. K. Rowling",art:sagaArt,story:i+1,progress:i===0?.36:0}));
const sagaYears=[2001,2002,2004,2005,2007,2009,2010,2011];
const sagaFilms=sagaFilmTitles.map((title,i)=>({key:"film"+i,title,medium:"film",year:String(sagaYears[i]),art:sagaArt,story:Math.min(7,i+1),progress:i===1?1:0}));
const sagaExtras={books:["Fantastic Beasts and Where to Find Them Newt Scamander","Quidditch Through the Ages","The Tales of Beedle the Bard"].map((title,i)=>({key:"extraBook"+i,title,medium:"book",author:"J. K. Rowling",art:sagaArt})),
films:["Fantastic Beasts and Where to Find Them","Fantastic Beasts: The Crimes of Grindelwald","Fantastic Beasts: The Secrets of Dumbledore"].map((title,i)=>({key:"extraFilm"+i,title,medium:"film",art:sagaArt})),
shows:[{key:"hbo",title:"Harry Potter",medium:"series",upcoming:true,art:sagaArt,year:"2026"}]};
const SAGA={name:"Harry Potter",wallpaper:sagaArt,facts:["7 stories","10 Books","11 Films","1 Show"],blurb:"The Wizarding World — Rowling's seven novels, the eight films, and the Fantastic Beasts era beyond.",
information:[["CREATOR","J. K. Rowling"],["BOOKS","10"],["FILMS","11 · 2001 to 2022"],["TELEVISION","1 series"],["CATALOGUE","Apple Books · Cinemeta"]]};
const bondFilms=[
["Sean Connery",[["Dr. No",1962],["From Russia with Love",1963],["Goldfinger",1964],["Thunderball",1965],["You Only Live Twice",1967],["Diamonds Are Forever",1971]]],
["George Lazenby",[["On Her Majesty's Secret Service",1969]]],
["Roger Moore",[["Live and Let Die",1973],["The Man with the Golden Gun",1974],["The Spy Who Loved Me",1977],["Moonraker",1979],["For Your Eyes Only",1981],["Octopussy",1983],["A View to a Kill",1985]]],
["Timothy Dalton",[["The Living Daylights",1987],["Licence to Kill",1989]]],
["Pierce Brosnan",[["GoldenEye",1995],["Tomorrow Never Dies",1997],["The World Is Not Enough",1999],["Die Another Day",2002]]],
["Daniel Craig",[["Casino Royale",2006],["Quantum of Solace",2008],["Skyfall",2012],["Spectre",2015],["No Time to Die",2021]]]];
const bondBooks=["Casino Royale","Live and Let Die","Moonraker","Diamonds Are Forever","From Russia with Love","Dr. No","Goldfinger","For Your Eyes Only","Thunderball","The Spy Who Loved Me","On Her Majesty's Secret Service","You Only Live Twice","The Man with the Golden Gun","Octopussy and The Living Daylights"];
const BOND={name:"James Bond",category:"UNIVERSE",wallpaper:cold,art:bondArt,
blurb:"The James Bond franchise focuses on the character of James Bond, a fictional British Secret Service agent created in 1953 by writer Ian Fleming, who featured him in twelve novels and two short-story collections. Since Fleming's death in 1964, nine other authors have written authorised Bond novels or novelisations: Kingsley Amis, Christopher Wood, John Gardner, Raymond Benson, Sebastian Faulks, Jeffery Deaver, William Boyd, Anthony Horowitz and Charlie Higson. The latest novel is On His Majesty's Secret Service by Charlie Higson, published in May 2023. Charlie Higson wrote a series on a young James Bond, and Kate Westbrook wrote three novels based on the diaries of a recurring series character, Moneypenny.",
source:"Wikipedia — James Bond",
shelves:[{title:"Films",kind:"movie",runs:bondFilms.map(([label,pairs],k)=>({label,items:pairs.map(([title,year],i)=>({key:"bondFilm"+k+"-"+i,title,medium:"film",year,art:bondArt,progress:k===0&&i===0?.45:0}))}))},
{title:"Books",kind:"book",runs:[{label:"",items:bondBooks.map((title,i)=>({key:"bondBook"+i,title,medium:"book",art:bondArt,progress:i===0?1:0}))}]}]};
const jumpNow=[["One Piece","Eiichirou Oda",1997],["Me & Roboco","Shuuhei Miyazaki",2020],["Sakamoto Days","Yuuto Suzuki",2020],["Witch Watch","Kenta Shinohara",2021],["Akane-banashi","Yuuki Suenaga & Takamasa Moue",2022],["RuriDragon","Masaoki Shindou",2022],["Nue's Exorcist","Kouta Kawae",2023],["Kagurabachi","Takeru Hokazono",2023],["Ultimate Exorcist Kiyoshi","Shouichi Usui",2024],["Ichi the Witch","Osamu Nishi & Shiro Usazaki",2024],["Shinobi Undercover","Ippon Takegushi & Santa Mitarashi",2024],["Someone Hertz","Ei Yamano",2025],["Under Doctor","Kyou Tanimoto",2026],["Class 2-B Hero Destroyerz","Hideaki Sorachi",2026],["Roku's House of Oddities","Atsushi Nakamura",2026],["Animal Signal","Robinson Haruhara & Taishi Tsutsui",2026],["Hal Formula","Kento Terasaka",2026],["Cannon Master","Reiya Machida",2026],["The Ura Files","Shun Numa & Yuushin Kuroki",2026],["Graykeith: Translator Daemonica","Natsuki Hokami",2026]];
const jumpClassics=[["Harenchi Gakuen","Gou Nagai",1968,1972,13],["Mazinger Z","Gou Nagai",1972,1973,5],["KochiKame","Osamu Akimoto",1976,2021,203],["Ring ni Kakero","Masami Kurumada",1977,1981,25],["Cobra","Buichi Terasawa",1978,1984,18],["Dr. Slump","Akira Toriyama",1980,1984,18],["Fist of the North Star","Yoshiyuki Okamura & Tetsuo Hara",1983,1988,27],["Dragon Ball","Akira Toriyama",1984,1995,42],["City Hunter","Tsukasa Houjou",1985,1991,35],["Saint Seiya","Masami Kurumada",1985,1990,28],["Yu Yu Hakusho","Yoshihiro Togashi",1990,1994,19],["Slam Dunk","Takehiko Inoue",1990,1996,31],["Rurouni Kenshin","Nobuhiro Watsuki",1994,1999,28],["Yu-Gi-Oh!","Kazuki Takahashi",1996,2004,38],["Hikaru no Go","Yumi Hotta & Takeshi Obata",1998,2003,23],["Naruto","Masashi Kishimoto",1999,2014,72],["Bleach","Tite Kubo",2001,2016,74],["Eyeshield 21","Riichirou Inagaki & Yuusuke Murata",2002,2009,37],["Death Note","Tsugumi Ooba & Takeshi Obata",2003,2006,12],["Gintama","Hideaki Sorachi",2003,2019,77]];
const toJump=(arr,prefix)=>arr.map((w,i)=>({key:prefix+i,title:w[0],by:w[1],from:w[2],to:w[3]||"",volumes:w[4]||"",medium:"manga",art:jumpArt,progress:prefix==="now"&&i===0?.18:0}));
const JUMP={name:"Weekly Shōnen Jump",category:"UNIVERSE",wallpaper:cold,art:jumpArt,blurb:"Weekly Shōnen Jump is a weekly shōnen manga anthology published in Japan by Shueisha under the Jump line of magazines. It is one of the longest-running manga magazines, with the first issue being released on July 11, 1968.",source:"Wikipedia — Weekly Shōnen Jump",
shelves:[{title:"In the magazine now",tab:"Now",pill:"running now",kind:"manga",form:"table",order:"Longest-running first",asOf:"October 2026",runs:[{label:"",items:toJump(jumpNow,"now")}]},
{title:"The classics",tab:"Classics",pill:"classics",kind:"manga",form:"table",order:"In order of first issue",runs:[{label:"",items:toJump(jumpClassics,"classic")}]}]};
const SIBLINGS_SAGA=[["Lord of the Rings","wallpaper/sagas/lord-of-the-rings.jpg"],["Dune","wallpaper/sagas/dune.jpg"],["The Witcher","wallpaper/sagas/the-witcher.jpg"],["Percy Jackson","wallpaper/sagas/percy-jackson.jpg"]];
const SIBLINGS_SHELVES=[["Marvel Cinematic Universe","universes/marks/marvel.png"],["Star Trek","universes/marks/star-trek.png"],["Studio Ghibli","universes/marks/studio-ghibli.svg"],["Avatar: The Last Airbender","universes/marks/avatar.svg"]];
const presets={portrait:[412,900,"phone"],landscape:[900,412,"phone"],narrow:[354,792,"phone"],"narrow-land":[792,354,"phone"],desktop:[1280,700,"desktop"],tablet:[800,1280,"tablet"],"tablet-land":[1280,800,"tablet"]};
const $=id=>document.getElementById(id);
const safe=s=>String(s===undefined||s===null?"":s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const state={selected:"book0",fixture:"bond",kind:"phone",progress:true,logs:[],items:new Map()};
const clickLog=s=>{state.logs.unshift(s);if($("actionLog"))$("actionLog").textContent="Mock only: "+s;};
const kindOf=item=>item.medium==="manga"?"MANGA":item.medium==="book"?"BOOK":item.medium==="series"?"SERIES":"FILM";
const routeOf=item=>item.medium==="manga"?"Tankoban":item.medium==="book"?"Biblio":"Theatre";
const actionOf=item=>{clickLog((item.progress>0?"Continue ":"Open ")+safe(item.title)+" in "+routeOf(item)+" (simulated)");};
const allSagaItems=[...sagaBooks,...sagaFilms,...sagaExtras.books,...sagaExtras.films,...sagaExtras.shows];
for(const item of allSagaItems)state.items.set(item.key,item);

function button(label,action,sub,disabled){return '<button class="primary-action" type="button" '+(disabled?'disabled':'')+' data-route="'+safe(action)+'">'+safe(label)+(sub?'<span class="sub">'+safe(sub)+'</span>':'')+'</button>';}
function heroHtml(cfg,kicker,actions){return '<div class="hero" id="hero"><p class="kicker">'+safe(kicker)+'</p><h1 class="hero-name">'+safe(cfg.name)+'</h1><div class="fact-flow">'+cfg.facts.map(s=>'<span class="fact">'+safe(s)+'</span>').join('')+'</div>'+(cfg.notice?'<p class="notice">'+safe(cfg.notice)+'</p>':'')+'<div class="hero-actions">'+actions+'</div></div><div class="switcher-gap"></div>';}
function sectionHtml(id,title,content){return '<section class="section" id="'+safe(id)+'"><h2>'+safe(title)+'</h2>'+content+'</section>';}
function tileHtml(item,opt){opt=opt||{};state.items.set(item.key,item);let d=state.progress?(item.progress||0):0;const selected=state.selected===item.key&&P==="saga",brand=opt.brand===true;
return '<button type="button" class="work-tile'+(selected?' selected':'')+'" data-select="'+safe(item.key)+'" aria-label="'+safe(item.title)+'" title="'+safe(item.title)+'" '+(item.upcoming?'aria-disabled="true"':'')+'>'+(item.art?'<img '+(brand?'class="poster-brand" ':'')+'src="'+safe(item.art)+'" alt="">':'<span class="fallback">'+safe(item.title)+'</span>')+(item.upcoming?'<span class="tag">UPCOMING</span>':'')+(d>=1?'<span class="watched">✓</span>':d>0?'<span class="progress-bar"><i style="width:'+(d*100)+'%"></i></span>':'')+'</button>';}
function continuedHtml(item){return '<div class="continue-card glass" role="button" tabindex="0" data-detail="'+safe(item.key)+'" aria-label="Open '+safe(item.title)+' details"><div class="continue-art" style="background-image:url('+safe(item.art)+')"></div><div class="continue-body"><p class="continue-kind">'+kindOf(item)+'</p><div class="continue-main"><div class="continue-title">'+safe(item.title)+'</div><div class="continue-sub">'+Math.round(item.progress*100)+'% '+(item.medium==="book"||item.medium==="manga"?"read":"watched")+'</div><div class="continue-bar"><span style="width:'+(item.progress*100)+'%"></span></div></div></div><button type="button" data-route="'+safe(item.key)+'" class="continue-resume" aria-label="Resume '+safe(item.title)+'">▶</button></div>';}
function sagaGridHtml(){
const heads=storyNames.map((t,i)=>'<div class="story-head" style="grid-column:'+(i+1+(i===6?0:0))+' / span '+(i===6?2:1)+'"><span class="story-number">'+(i+1)+'</span><span class="story-short">'+safe(t)+'</span></div>').join('');
const bookCells=sagaBooks.map((item,i)=>'<div style="grid-column:'+(i+1)+' / span '+(i===6?2:1)+';display:flex;justify-content:center">'+tileHtml(item)+'</div>').join('');
const filmCells=sagaFilms.map((item,i)=>'<div style="grid-column:'+(i+1)+'">'+tileHtml(item)+'</div>').join('');
const row=(label,cells)=>'<div class="matrix-row"><div class="matrix-row-name">'+label+'</div>'+cells+'</div>';
return '<div class="matrix-panel glass"><div class="matrix-window" id="matrixWindow" tabindex="0" aria-label="The saga: horizontal story grid"><div class="matrix-inner"><div class="story-heads">'+heads+'</div>'+row('BOOKS',bookCells)+row('FILMS',filmCells)+'</div></div><button class="grid-chev prev" data-scroll="#matrixWindow" data-dir="-1" aria-label="Show earlier stories" hidden>‹</button><button class="grid-chev next" data-scroll="#matrixWindow" data-dir="1" aria-label="Show later stories">›</button><div class="saga-detail" id="saga-detail"></div></div>';}
function branchColumn(label,items){return '<div><div class="branch-type">'+safe(label)+'</div><div class="branch-shelf">'+items.map(item=>'<div class="branch-item">'+tileHtml(item)+'<div class="branch-caption">'+safe(item.title)+'</div>'+(item.medium==='book'?'<div class="branch-by">'+safe(item.author)+'</div>':'')+'</div>').join('')+'</div></div>';}
function branchBlock(title,content){return '<div class="branch-block glass"><h3>'+safe(title)+'</h3>'+content+'</div>';}
function upcomingBlock(){let items=sagaExtras.shows;return '<div class="glass branch-block">'+branchColumn('SERIES',items)+'</div>';}
function galleryHtml(srcs){return '<div class="gallery-row">'+srcs.map((src,i)=>'<div class="gallery-item"><img src="'+safe(src)+'" alt="Local review artwork '+(i+1)+'"></div>').join('')+'</div>';}
function creatorsHtml(){return '<div class="creators"><div class="creator"><div class="glass creator-avatar">JR</div><div class="creator-name">J. K. Rowling</div><div class="creator-role">Author · 10 books</div></div></div>';}
function infoPanel(blurb,source,pairs,page){return '<div class="glass information-panel"><p class="blurb">'+safe(blurb)+'</p>'+(source?'<p class="source">'+safe(source)+'</p>':'')+'<div class="info-hair"></div><div class="info-grid '+safe(page)+'">'+pairs.map(p=>'<div><div class="info-label">'+safe(p[0])+'</div><div class="info-value">'+safe(p[1])+'</div></div>').join('')+'</div></div>';}
function moreHtml(siblings,kicker){return '<div class="more-row">'+siblings.map(s=>{let asset=image(s[1]);return '<button class="more-card" data-universe="'+safe(s[0])+'" type="button" style="background-image:url(\''+safe(asset)+'\')"><span class="text"><small>'+safe(kicker)+'</small><strong>'+safe(s[0])+'</strong></span></button>';}).join('')+'</div>';}
function renderSaga(){state.items.clear();allSagaItems.forEach(i=>state.items.set(i.key,i));
let html=heroHtml(SAGA,"SAGA",button("Read","book0")+button("Watch","film0"))+'<div class="page-content">';
if(state.progress)html+=sectionHtml("saga-continue","Continue",'<div class="continue-cards">'+continuedHtml(sagaBooks[0])+'</div>');
html+=sectionHtml("saga-main","The saga",sagaGridHtml());
html+=sectionHtml("saga-branches","Beyond the saga",'<div class="branch-grid">'+branchBlock("Hogwarts Library",branchColumn("BOOKS",sagaExtras.books))+branchBlock("Fantastic Beasts",branchColumn("FILMS",sagaExtras.films))+'</div>');
html+=sectionHtml("saga-upcoming","Upcoming",upcomingBlock());
html+='<div id="saga-about" class="about-anchor"></div>';
html+=sectionHtml("saga-gallery","Gallery",galleryHtml([sagaArt,sagaArt,sagaArt,sagaArt]));
html+=sectionHtml("saga-creators","Creators",creatorsHtml());
html+=sectionHtml("saga-information","Information",infoPanel(SAGA.blurb,"",SAGA.information,"saga"));
html+=sectionHtml("saga-more","More universes",moreHtml(SIBLINGS_SAGA,"SAGA"))+'</div>';
return {html,tabs:[["The saga","saga-main"],["Beyond the saga","saga-branches"],["Upcoming","saga-upcoming"],["About","saga-about"]]};}
function selectSaga(key){const item=state.items.get(key);if(!item)return;state.selected=key;document.querySelectorAll('.work-tile[data-select]').forEach(e=>e.classList.toggle('selected',e.dataset.select===key));
let where=item.story?"STORY "+item.story+" OF 7 · "+kindOf(item):"BEYOND THE SAGA · "+kindOf(item);
if(item.upcoming)where+=" · UPCOMING";else if(state.progress&&item.progress>=1)where+=" · "+(item.medium==="book"?"READ":"WATCHED");else if(state.progress&&item.progress>0)where+=" · "+Math.round(item.progress*100)+"% "+(item.medium==="book"?"READ":"WATCHED");
const meta=item.author||item.year||"",cta=item.medium==="book"?"Read":"Watch",hint=item.upcoming?"Details pending":"Opens in "+routeOf(item);
let detail='<div class="detail-copy"><div class="detail-type">'+safe(where)+'</div><div class="detail-title">'+safe(item.title)+'</div>'+(meta?'<p class="detail-meta">'+safe(meta)+'</p>':'')+'</div><div class="detail-cta">'+button(cta,item.key,"",item.upcoming)+'<span class="detail-hint">'+safe(hint)+'</span></div>';
if($("saga-detail"))$("saga-detail").innerHTML=detail;}

function shelfInfo(cfg){const pairs=cfg.shelves.map(s=>{let works=s.runs.flatMap(r=>r.items),years=works.map(w=>Number(w.year||w.from)||0).filter(Boolean).sort((a,b)=>a-b);return [(s.pill||s.title).toUpperCase(),works.length+(years.length>1&&years[0]!==years[years.length-1]?" · "+years[0]+" to "+years[years.length-1]:"")];});const cats=[...new Set(cfg.shelves.map(s=>s.kind==="book"?"Apple Books":s.kind==="manga"?"AniList":"Cinemeta"))];pairs.push(["CATALOGUE","Curation · "+cats.join(" · ")]);return pairs;}
function mediumHtml(s,index){const works=s.runs.flatMap(r=>r.items),labelled=s.runs.filter(r=>r.items.length>0).length>1,id="shelf-"+index;
const liveRuns=s.runs.filter(r=>r.items.length);const contentWidth=(Number($("viewportW").value)||412)-2*(state.kind==="phone"?24:64);
const rowWidth=liveRuns.reduce((n,r,i)=>n+r.items.length*164+Math.max(0,r.items.length-1)*16+(i?28:0),0);
const naturalWidth=Math.max(48+rowWidth,48+Math.max(160,s.title.length*14));const isShort=state.kind!=="phone"&&naturalWidth+24<contentWidth;
let runs=s.runs.filter(r=>r.items.length).map(r=>'<div class="run">'+(labelled?'<div class="run-label">'+safe(r.label)+'</div>':'')+'<div class="run-tiles">'+r.items.map(item=>'<div class="shelf-card">'+tileHtml(item,{brand:true})+'<div class="shelf-title">'+safe(item.title.replace(/\s+[—-]\s+Live Action$/i,""))+'</div>'+(item.year?'<div class="shelf-year">'+safe(item.year)+'</div>':'')+'</div>').join('')+'</div></div>').join('');
return '<article class="glass medium-panel '+(isShort?"short":"full")+'" style="width:'+(isShort?naturalWidth+"px":"100%")+'" id="'+id+'"><div class="medium-heading"><h2>'+safe(s.title)+'</h2><span class="medium-count">'+works.length+'</span></div><div class="shelf-window"><div class="rail" id="'+id+'-rail" tabindex="0" aria-label="'+safe(s.title)+' works">'+runs+'</div><button class="shelf-chev prev" type="button" data-scroll="#'+id+'-rail" data-dir="-1" aria-label="Earlier '+safe(s.title)+'" hidden>‹</button><button class="shelf-chev next" type="button" data-scroll="#'+id+'-rail" data-dir="1" aria-label="More '+safe(s.title)+'">›</button></div></article>';}
function magazineHtml(s,index){const rows=s.runs.flatMap(r=>r.items),id="shelf-"+index,per=Math.ceil(rows.length/2);
let contents=rows.map((w,i)=>'<button class="magazine-row" type="button" data-select="'+safe(w.key)+'" aria-label="Open '+safe(w.title)+'"><span class="magazine-index">'+(i+1)+'</span><img class="magazine-cover" src="'+safe(w.art)+'" alt=""><span class="magazine-details"><span class="magazine-name">'+safe(w.title)+'</span><span class="magazine-by">'+safe(w.by)+'</span>'+(state.progress&&w.progress>0&&w.progress<1?'<span class="continue-bar" style="display:block;max-width:220px"><span style="width:'+w.progress*100+'%"></span></span>':'')+'</span><span class="magazine-years">'+(w.to?safe(w.from+" to "+w.to):safe("Since "+w.from))+(w.volumes?'<br>'+w.volumes+' volumes':'')+'</span></button>').join('');
return '<article class="glass magazine-panel full" id="'+id+'"><div class="magazine-heading"><div class="medium-heading" style="padding:0;margin:0"><h2>'+safe(s.title)+'</h2><span class="medium-count">'+rows.length+'</span></div><div class="order">'+safe(s.order+(s.asOf?" · as of "+s.asOf:""))+'</div></div><div class="magazine-rows" style="grid-template-rows:repeat('+per+',78px)">'+contents+'</div></article>';}
function renderShelves(){
const cfg=state.fixture==="jump"?JUMP:BOND;state.items.clear();let all=cfg.shelves.flatMap(s=>s.runs.flatMap(r=>r.items));all.forEach(i=>state.items.set(i.key,i));
cfg.facts=cfg.shelves.map(s=>s.runs.reduce((n,r)=>n+r.items.length,0)+" "+(s.pill||s.title));
const hasRead=cfg.shelves.some(s=>["book","manga","comic"].includes(s.kind)),hasWatch=cfg.shelves.some(s=>["movie","series"].includes(s.kind)),read=all.find(i=>["book","manga","comic"].includes(i.medium)),watch=all.find(i=>i.medium==="film"||i.medium==="series");
let html=heroHtml(cfg,"UNIVERSE",(hasRead?button("Read",read.key):"")+(hasWatch?button("Watch",watch.key):""))+'<div class="page-content">';
if(state.progress&&all.some(i=>i.progress>0))html+=sectionHtml("shelves-continue","Continue",'<div class="continue-cards">'+continuedHtml(all.find(i=>i.progress>0))+'</div>');
html+='<div class="shelves-layout">'+cfg.shelves.map((s,i)=>s.form==="table"?magazineHtml(s,i):mediumHtml(s,i)).join('')+'</div>';
html+='<div id="shelves-about" class="about-anchor"></div>';
if(state.fixture==="bond")html+=sectionHtml("shelves-gallery","Gallery",galleryHtml([bondArt,bondArt,bondArt,bondArt]));
html+=sectionHtml("shelves-information","Information",infoPanel(cfg.blurb,cfg.source,shelfInfo(cfg),"shelves"));
html+=sectionHtml("shelves-more","More universes",moreHtml(SIBLINGS_SHELVES,"UNIVERSE"))+'</div>';
return {html,tabs:cfg.shelves.map((s,i)=>[s.tab||s.title,"shelf-"+i,s.runs.reduce((n,r)=>n+r.items.length,0)]).concat([["About","shelves-about",0]]),wallpaper:cfg.wallpaper};}
function chromeHtml(tabs){const chromeButtons=[["minimize.svg","Minimize"],["fullscreen.svg","Enter fullscreen"],["power.svg","Close Colosseum"]];
return '<div class="chrome-fade" id="chromeFade"></div><nav class="tabbar glass" id="tabbar" aria-label="Universe sections"><div class="tabs-track">'+tabs.map((t,i)=>'<button class="tab'+(i===0?' active':'')+'" type="button" data-tab="'+safe(t[1])+'">'+safe(t[0])+(t[2]?'<span class="count">'+t[2]+'</span>':'')+'</button>').join('')+'</div></nav><button class="back-action" id="back" type="button" aria-label="Back"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M14.5 5.5 L8 12 L14.5 18.5"/></svg><span>Back</span></button><div class="window-actions">'+chromeButtons.map(c=>'<button type="button" data-window="'+safe(c[1])+'" aria-label="'+safe(c[1])+'"><img src="'+safe(image("icons/"+c[0]))+'" alt=""></button>').join('')+'</div>';}
let currentTabs=[];
function render(){const app=$("app");const output=P==="saga"?renderSaga():renderShelves();currentTabs=output.tabs;app.dataset.page=P;
const bg=output.wallpaper||(P==="saga"?SAGA.wallpaper:cold);
app.innerHTML='<div class="wallpaper" style="background-image:url(\''+safe(bg)+'\')"></div><div class="page-scroll" id="pageScroll">'+output.html+'</div>'+chromeHtml(currentTabs);
if(P==="saga")selectSaga(state.selected);attachEvents();requestAnimationFrame(()=>{syncTabs();syncRailEdges();});}
function syncTabs(){const tabbar=$("tabbar"),page=$("pageScroll"),hero=$("hero"),app=$("app");if(!tabbar||!page||!hero)return;
const phone=state.kind==="phone",left=Number.parseFloat(getComputedStyle(app).getPropertyValue("--gutter"))||24,top=Number($("insetT").value)||0;
const floor=(phone?74:22)+top,dock=hero.offsetHeight-page.scrollTop<=floor;
tabbar.style.top=Math.max(floor,hero.offsetHeight-page.scrollTop)+"px";tabbar.style.left=left+(Number($("insetL").value)||0)+(dock&&!phone?118:0)+"px";
$("chromeFade").style.opacity=page.scrollTop>120?Math.min(1,(page.scrollTop-120)/240):0;
let active=0;for(let i=0;i<currentTabs.length;i++){let el=$(currentTabs[i][1]);if(el&&el.offsetTop-140<=page.scrollTop)active=i;}
document.querySelectorAll('.tab').forEach((t,i)=>{t.classList.toggle("active",i===active);t.setAttribute("aria-current",i===active?"true":"false");});}
function syncRailEdges(){document.querySelectorAll("[data-scroll]").forEach(b=>{const el=document.querySelector(b.dataset.scroll);if(!el)return;const dir=Number(b.dataset.dir);b.hidden=dir<0?el.scrollLeft<=4:el.scrollLeft+el.clientWidth>=el.scrollWidth-4;});}
function attachEvents(){const page=$("pageScroll");page.addEventListener("scroll",syncTabs,{passive:true});document.querySelectorAll(".matrix-window,.rail").forEach(el=>el.addEventListener("scroll",syncRailEdges,{passive:true}));
if(P==="saga"){document.querySelectorAll(".work-tile[data-select]").forEach(el=>{el.addEventListener("mouseenter",()=>selectSaga(el.dataset.select));el.addEventListener("focus",()=>selectSaga(el.dataset.select));});}
$("app").addEventListener("keydown",e=>{const card=e.target.closest(".continue-card[data-detail]");if(card&&(e.key==="Enter"||e.key===" ")){e.preventDefault();const item=state.items.get(card.dataset.detail);if(item)clickLog("Open "+item.title+" details (simulated)");}});
$("app").addEventListener("click",event=>{const card=event.target.closest(".continue-card[data-detail]");if(card&&!event.target.closest("button")){const item=state.items.get(card.dataset.detail);if(item)clickLog("Open "+item.title+" details (simulated)");return;}
const action=event.target.closest("button");if(!action)return;
if(action.id==="back"){clickLog("Back requested");return;}
if(action.dataset.tab){const el=$(action.dataset.tab);if(el)page.scrollTo({top:Math.max(0,el.offsetTop-(state.kind==="phone"?130:110)),behavior:"smooth"});return;}
if(action.dataset.scroll){const el=document.querySelector(action.dataset.scroll);el.scrollBy({left:Number(action.dataset.dir)*(el.classList.contains("matrix-window")?el.clientWidth*.8:720),behavior:"smooth"});return;}
if(action.dataset.window){clickLog(action.dataset.window+" requested (desktop mock only)");return;}
if(action.dataset.universe){clickLog("Open "+action.dataset.universe+" universe (simulated)");return;}
if(action.dataset.select){const item=state.items.get(action.dataset.select);if(P==="saga")selectSaga(action.dataset.select);if(item)actionOf(item);return;}
if(action.dataset.route){const item=state.items.get(action.dataset.route);if(item)actionOf(item);return;}
});}
function setPreset(name){const data=presets[name]||presets.portrait;state.kind=data[2];$("viewportW").value=data[0];$("viewportH").value=data[1];applyGeometry(true);}
function applyGeometry(rerender){const d=$("device"),app=$("app"),w=Math.max(320,Number($("viewportW").value)||412),h=Math.max(320,Number($("viewportH").value)||900);
d.style.setProperty("--view-w",w+"px");d.style.setProperty("--view-h",h+"px");app.dataset.kind=state.kind;
["T","R","B","L"].forEach(t=>app.style.setProperty("--safe-"+({T:"top",R:"right",B:"bottom",L:"left"}[t]),(Number($("inset"+t).value)||0)+"px"));
const scale=Math.max(1,Math.min(1.6,Number($("fontScale").value)||1));app.style.setProperty("--font-scale",scale);app.style.setProperty("--hero-font",Math.round((state.kind==="phone"?40:state.kind==="tablet"?64:72)*scale)+"px");
$("geometryNote").textContent=w+" × "+h+" logical px · "+state.kind+" · insets adjustable (not measured) · font "+scale.toFixed(1)+"×";
if(rerender)render();else requestAnimationFrame(()=>{syncTabs();syncRailEdges();});}
const args=new URLSearchParams(location.search);if(args.get("capture")==="1")document.body.classList.add("capture");
const requested=args.get("v");const preset=presets[requested]?requested:"portrait";$("preset").value=preset;
if(P==="shelves"&&args.get("fixture")==="jump")$("fixture").value="jump";
if(P==="shelves")state.fixture=$("fixture").value;
$("progressFixture").addEventListener("change",e=>{state.progress=e.target.checked;render();});
$("preset").addEventListener("change",e=>setPreset(e.target.value));
["viewportW","viewportH","fontScale","insetT","insetR","insetB","insetL"].forEach(id=>$(id).addEventListener("input",()=>applyGeometry(false)));
$("illustrativeInsets").addEventListener("click",()=>{$("insetT").value=24;$("insetB").value=24;applyGeometry(false);});
$("zeroInsets").addEventListener("click",()=>{["T","R","B","L"].forEach(t=>$("inset"+t).value=0);applyGeometry(false);});
if(P==="shelves")$("fixture").addEventListener("change",e=>{state.fixture=e.target.value;render();});
setPreset(preset);
window.__mockAudit=()=>{const app=$("app"),rect=app.getBoundingClientRect(),buttons=[...app.querySelectorAll("button")].filter(e=>getComputedStyle(e).display!=="none").map(e=>{const b=e.getBoundingClientRect();return {label:e.getAttribute("aria-label")||e.textContent.trim().slice(0,40),w:Math.round(b.width),h:Math.round(b.height),left:Math.round(b.left-rect.left),right:Math.round(b.right-rect.left)};});return {viewport:[rect.width,rect.height],kind:state.kind,scrollWidth:$("pageScroll").scrollWidth,clippedButtons:buttons.filter(b=>b.left< -2||b.right>rect.width+2),tinyControls:buttons.filter(b=>b.w<40||b.h<40)};};
})();
