"use strict";
(function () {
  const $ = (id) => document.getElementById(id);
  const ASSET = "../../reference/desktop/assets/";
  const genres = ["Action","Adventure","Animation","Biography","Comedy","Crime","Documentary","Drama","Family","Fantasy","History","Horror","Mystery","Romance","Sci-Fi","Sport","Thriller","War","Western"];
  const years = ["2026","2025","2024","2023","2022","2021","2020","2019","2018","2015","2010","2000","1990","1980"];
  const languages = ["English","Spanish","Portuguese","French","German","Italian","Japanese","Korean","Hindi"];
  const filters = [
    {group:"Genre",options:genres},
    {group:"Year",options:years},
    {group:"Language",options:languages}
  ];
  const providerSpecs = [
    ["The Movie Database","tmdb.png",["Trending","Popular","Top Rated","Now Playing","Upcoming","Documentaries","Critically Acclaimed","Award Winners","International Cinema"],"many"],
    ["Cinemeta",null,["Popular"],"genre"],
    ["Streaming Catalogs","streaming-catalogs.png",["Popular Movies","Trending Movies","Netflix","Prime Video","Apple TV+","Disney+","HBO","Hulu","Paramount+"],"many"],
    ["MDBList","mdblist.png",["Most Watched","Highly Rated","Film Collections","New and Noteworthy","Coming Soon","Hidden Gems","Top 250"],"many"],
    ["Letterboxd","letterboxd.png",["Popular Films","Most Popular This Week","Top 250 Narrative Features","Films with Fans"],"many"],
    ["FlixPatrol","flixpatrol.png",["Streaming Top 10","Movies","Television","Worldwide"],"genre"],
    ["Netflix Catalog","netflix-catalog.png",["Popular","Trending","Originals","New Releases"],"genre"],
    ["AIOLISTS","aiolists.png",["Discover","Trending","Best of the Year","Critics' Choice"],"many"],
    ["Anime Kitsu","anime-kitsu.png",["Trending Anime","Popular Anime","Highest Rated","Upcoming"],"genre"],
    ["Jikan","jikan.png",["Top Anime","Seasonal","Currently Airing","Most Popular"],"genre"],
    ["More Like This","morelikethis.png",["Discover","Similar Titles","Recommendations"],"genre"],
    ["Colosseum Grand Database","colosseum-grand-database.png",["Popular","New Arrivals","Highlights","Editor's Picks"],"many"],
    ["MediaFusion","mediafusion.png",["Movies","Series","Latest"],"genre"],
    ["TheTVDB","tvdb.svg",["Popular Series","Top Rated","Continuing Series"],"genre"]
  ];
  const providers = providerSpecs.map((spec, i) => ({
    id:"source-"+i,
    name:spec[0],
    logo:spec[1],
    catalogs:spec[2],
    groups:spec[3] === "many" ? filters : [{group:"Genre",options:genres}]
  }));
  const posters = [
    {title:"Dune",image:"wallpaper/sagas/dune.jpg",rating:"8.0",genre:"Sci-Fi",year:"2021"},
    {title:"Harry Potter",image:"wallpaper/sagas/harry-potter.jpg",rating:"7.6",genre:"Fantasy",year:"2001"},
    {title:"The Lord of the Rings",image:"wallpaper/sagas/lord-of-the-rings.jpg",rating:"8.8",genre:"Adventure",year:"2001"},
    {title:"Jurassic Park",image:"wallpaper/sagas/jurassic-park.jpg",rating:"8.2",genre:"Sci-Fi",year:"1993"},
    {title:"The Witcher",image:"wallpaper/sagas/the-witcher.jpg",rating:"8.0",genre:"Fantasy",year:"2019"},
    {title:"Sherlock Holmes",image:"wallpaper/sagas/sherlock-holmes.jpg",rating:"7.6",genre:"Mystery",year:"2009"},
    {title:"Percy Jackson",image:"wallpaper/sagas/percy-jackson.jpg",rating:"7.3",genre:"Adventure",year:"2010"},
    {title:"A Song of Ice and Fire",image:"wallpaper/sagas/ice-and-fire.jpg",rating:"8.7",genre:"Fantasy",year:"2011"},
    {title:"Star Wars: Revenge of the Sith",image:"universes/star-wars/coruscant.jpg",rating:"7.6",genre:"Sci-Fi",year:"2005"},
    {title:"Star Wars: The Empire Strikes Back",image:"universes/star-wars/hoth.jpg",rating:"8.7",genre:"Sci-Fi",year:"1980"},
    {title:"Star Wars: The Force Awakens",image:"universes/star-wars/jakku.jpg",rating:"7.8",genre:"Sci-Fi",year:"2015"},
    {title:"Star Wars: The Last Jedi",image:"universes/star-wars/ahch-to.jpg",rating:"6.9",genre:"Sci-Fi",year:"2017"},
    {title:"Star Wars: A New Hope",image:"universes/star-wars/valo.jpg",rating:"8.6",genre:"Adventure",year:"1977"},
    {title:"Batman: The Animated Series",image:"universes/dcau/portals/gotham.jpg",rating:"9.0",genre:"Animation",year:"1992"},
    {title:"Superman: The Animated Series",image:"universes/dcau/portals/metropolis.jpg",rating:"8.1",genre:"Animation",year:"1996"},
    {title:"Batman Beyond",image:"universes/dcau/portals/future-gotham.jpg",rating:"8.1",genre:"Animation",year:"1999"},
    {title:"Justice League",image:"universes/dcau/portals/watchtower.jpg",rating:"8.5",genre:"Animation",year:"2001"}
  ];
  const worlds = [
    ["Theatre","projector-theatre.svg"],
    ["Tankoban","comic-book.svg"],
    ["Biblio","book-library.svg"],
    ["Universes","universes-atom.svg"],
    ["Profile","your-colosseum-laurel.svg"]
  ];
  const ui = {type:"movie",source:0,category:0,chosen:null,rail:false,popup:null,view:"412x900",width:412,height:900,top:0,bottom:0,font:1};
  const params = new URLSearchParams(location.search);
  const isCapture = params.get("capture") === "1";
  const presets = ["412x900","354x792","900x412","792x354","1280x700","800x1280","1280x800"];
  const seedView = params.get("view");
  if (seedView && /^\d{3,4}x\d{3,4}$/.test(seedView)) ui.view = seedView;
  const initialState = params.get("state") || "closed";
  ui.popupPlacement=params.get("placement")==="original"?"original":"fit";
  ui.top=Math.min(80,Math.max(0,Number(params.get("top"))||0));
  ui.bottom=Math.min(80,Math.max(0,Number(params.get("bottom"))||0));
  ui.font=params.get("font")==="1.3"?1.3:1;
  const note = (text) => { $("interaction-note").textContent = text; };
  const source = () => providers[ui.source];
  const sourceName = () => source().name;
  const asset = (relative) => ASSET+relative;
  const elt = (tag,className,text) => {
    const node = document.createElement(tag);
    if (className) node.className=className;
    if (text!==undefined && text!==null) node.textContent=String(text);
    return node;
  };
  function makeWorldNav(target) {
    worlds.forEach((item) => {
      const button = elt("button","world-tab"+(item[0]==="Theatre"?" active":""),null);
      button.type="button";
      button.setAttribute("aria-label",item[0]);
      button.setAttribute("aria-current",item[0]==="Theatre"?"page":"false");
      const glyph = elt("img","world-icon");
      glyph.src=asset("icons/"+item[1]);
      glyph.alt="";
      const text = elt("span","",item[0]);
      button.append(glyph,text);
      button.addEventListener("click",() => {
        note(item[0] + " world selected; real-world navigation is outside this local Discover study.");
      });
      target.appendChild(button);
    });
  }
  function mediaTabs() {
    const tabs = [["movie","Movie"],["series","Series"],["anime","Anime"]];
    $("type-tabs").replaceChildren();
    tabs.forEach(([key,label])=>{
      const b=elt("button","type-tab"+(ui.type===key?" active":""),label);
      b.type="button"; b.setAttribute("aria-pressed",String(ui.type===key));
      b.addEventListener("click",()=>{
        if(ui.type===key)return;
        ui.type=key;ui.source=0;ui.category=0;ui.chosen=null;
        closePopup();draw();
        note("The " + label + " lens is active; source and category selections reset for this illustrative type.");
      });
      $("type-tabs").appendChild(b);
    });
  }
  function drawSources() {
    const priorScroll=$("source-list").scrollTop;
    $("source-list").replaceChildren();
    providers.forEach((item,i)=>{
      const b=elt("button","source-entry"+(i===ui.source?" selected":""),null);
      b.type="button";b.title=item.name;b.setAttribute("aria-label","Source "+item.name+(i===ui.source?", selected":""));
      b.setAttribute("aria-pressed",String(i===ui.source));
      if(item.logo) {
        const img=elt("img","addon-icon");img.src=asset("addon-logos/"+item.logo);img.alt="";b.appendChild(img);
      }else{
        const initial=elt("span","addon-icon source-initial",item.name.slice(0,1));b.appendChild(initial);
      }
      const label=elt("span","source-name",item.name.replace(/\s+add-?on$/i,""));
      b.appendChild(label);
      b.addEventListener("click",()=>{
        if (ui.source===i) return;
        ui.source=i;ui.category=0;ui.chosen=null;closePopup();draw();
        note("Switched to " + item.name + ". The first source category is now selected, as in the approved Discover sidebar.");
      });
      $("source-list").appendChild(b);
    });
    $("source-list").scrollTop=priorScroll;
    const rail=$("source-rail");
    rail.classList.toggle("expanded",ui.rail);
    rail.classList.toggle("collapsed",!ui.rail);
    $("discover-layout").classList.toggle("rail-open",ui.rail);
    $("source-toggle").setAttribute("aria-expanded",String(ui.rail));
    $("source-toggle").setAttribute("aria-label",ui.rail?"Retract sidebar":"Expand sidebar");
    $("source-list").setAttribute("aria-label",ui.rail?"Sources (expanded)":"Sources (collapsed)");
  }
  function drawCategory() {
    $("category-title").textContent=source().catalogs[ui.category] || source().catalogs[0];
    $("source-byline").textContent=sourceName();
    const pickable=source().catalogs.length>1;
    $("category-trigger").disabled=!pickable;
    $("category-chevron").hidden=!pickable;
    $("category-trigger").setAttribute("aria-expanded",String(ui.popup==="category"));
  }
  function drawFilter() {
    const grps=source().groups;
    const label=ui.chosen?ui.chosen.key:(grps.length===1?grps[0].group:"Filter");
    $("filter-value").textContent=label;
    $("filter-trigger").classList.toggle("has-selection",!!ui.chosen);
    $("filter-clear").hidden=!ui.chosen;
    $("filter-chevron").hidden=!!ui.chosen;
    $("filter-trigger").setAttribute("aria-expanded",String(ui.popup==="filter"));
    $("filter-trigger").setAttribute("aria-label",ui.chosen?("Filter: "+label):("Choose "+(grps.length===1?grps[0].group:"filter")));
    $("filter-clear").style.left=($("filter-trigger").offsetLeft+$("filter-trigger").offsetWidth-36)+"px";
  }
  function selectFilter(group,key) {
    ui.chosen=group?{group,key}:null;
    closePopup();drawFilter();drawPosters();
    note(group ? group+": "+key+" selected through the original picker (illustrative local result set)." : "Filter cleared through the original All option.");
  }
  function drawPosters() {
    const wall=$("poster-wall");wall.replaceChildren();
    let selection=posters;
    if(ui.chosen && ui.chosen.group==="Genre") {
      const hits=posters.filter(item=>item.genre===ui.chosen.key);
      if(hits.length)selection=hits;
    }
    if(ui.chosen && ui.chosen.group==="Year") {
      const hits=posters.filter(item=>item.year===ui.chosen.key);
      if(hits.length)selection=hits;
    }
    selection.forEach((item)=>{
      const card=elt("article","poster-card");card.setAttribute("role","button");card.tabIndex=0;card.setAttribute("aria-label",item.title);
      const frame=elt("div","poster-frame");
      const cover=elt("img","cover");cover.src=asset(item.image);cover.alt="";cover.loading="eager";
      const reveal=elt("div","poster-reveal");
      const star=elt("img");star.src=asset("icons/rating-star.svg");star.alt="";
      reveal.append(star,document.createTextNode(item.rating),elt("span","poster-source",sourceName()));
      frame.append(cover,reveal);card.append(frame,elt("div","poster-caption",item.title));
      const activate=()=>note("Selected "+item.title+". The production app opens the title details; this local study makes no provider call.");
      card.addEventListener("click",activate);
      card.addEventListener("keydown",(event)=>{if(event.key==="Enter"||event.key===" "){event.preventDefault();activate();}});
      wall.appendChild(card);
    });
    adjustColumns();
  }
  function adjustColumns() {
    const wall=$("poster-wall");
    const width=wall.clientWidth;
    const cols=(ui.mode==="phone"&&ui.width<ui.height)
      ? Math.max(1,Math.floor((width+20)/126))
      : Math.max(1,Math.floor((width+20)/152));
    wall.style.setProperty("--cols",String(cols));
    drawFilter();
  }
  function draw() {mediaTabs();drawSources();drawCategory();drawFilter();drawPosters();}
  function rowsMenu(target,rows,activeKey,choose) {
    target.replaceChildren();
    let firstActive=null;
    rows.forEach((item)=>{
      if(item.header) {
        target.appendChild(elt("div","menu-header",item.header));return;
      }
      const b=elt("button","menu-option"+(item.key===activeKey?" active":""),null);
      b.type="button";b.setAttribute("role","option");
      b.setAttribute("aria-selected",String(item.key===activeKey));b.dataset.key=item.key;
      b.appendChild(elt("span","",item.label));
      b.addEventListener("click",()=>choose(item));
      target.appendChild(b);
      if(item.key===activeKey)firstActive=b;
    });
    target.scrollTop=0;
    if(firstActive)target.scrollTop=Math.max(0,firstActive.offsetTop-target.offsetTop-60);
  }
  function placePopup(kind) {
    const node=$(kind+"-popup");
    const trigger=$(kind+"-trigger");
    const vp=$("viewport").getBoundingClientRect();
    const tr=trigger.getBoundingClientRect();
    const desiredWidth=kind==="category"?260:Math.max(280,tr.width);
    const width=Math.max(160,Math.min(desiredWidth,ui.width-16));
    const children=node.querySelectorAll(".menu-option,.menu-header");
    let desiredHeight=12;
    children.forEach((c)=>{desiredHeight+=c.classList.contains("menu-header")?27:38;});
    desiredHeight=Math.min(kind==="category"?392:380,desiredHeight);
    const topLimit=Math.max(8,ui.top+8);
    const bottomLimit=ui.height-(ui.mode==="phone"?72+ui.bottom:ui.bottom)-8;
    const originalCategory=kind==="category"&&ui.popupPlacement==="original";
    const below=originalCategory?$("discover-masthead").getBoundingClientRect().bottom-vp.top+6:tr.bottom-vp.top+6;
    const above=tr.top-vp.top-6;
    const roomBelow=Math.max(0,bottomLimit-below),roomAbove=Math.max(0,above-topLimit);
    const putBelow=originalCategory||desiredHeight<=roomBelow||(desiredHeight>roomAbove&&roomBelow>=roomAbove);
    const height=Math.max(38,Math.min(desiredHeight,putBelow?roomBelow:roomAbove));
    const y=putBelow?below:Math.max(topLimit,above-height);
    const anchorX=kind==="filter"?tr.left-vp.left:tr.right-vp.left-width;
    const x=Math.max(8,Math.min(anchorX,ui.width-width-8));
    node.style.left=Math.round(x)+"px";node.style.top=Math.round(y)+"px";
    node.style.width=Math.round(width)+"px";node.style.height=Math.round(height)+"px";
    node.dataset.flip=putBelow?"below":"above";
  }
  function openPopup(kind) {
    if(kind==="category"&&source().catalogs.length<2)return;
    if(ui.popup===kind){closePopup();return;}
    closePopup();
    ui.popup=kind;
    const dest=$(kind+"-popup");
    let rows,activeKey,choose;
    if(kind==="category") {
      rows=source().catalogs.map((title,i)=>({key:String(i),label:title}));
      activeKey=String(ui.category);
      choose=(item)=>{
        ui.category=Number(item.key);ui.chosen=null;closePopup();draw();
        note("Category "+source().catalogs[ui.category]+" chosen from "+sourceName()+"; filters reset.");
      };
    }else{
      const groups=source().groups;
      rows=[{key:"",label:"All"}];
      groups.forEach((group)=>{
        if(groups.length>1)rows.push({header:group.group});
        group.options.forEach((key)=>rows.push({key:group.group+"|"+key,label:key,group:group.group,value:key}));
      });
      activeKey=ui.chosen?(ui.chosen.group+"|"+ui.chosen.key):"";
      choose=(item)=>item.key===""?selectFilter(null,null):selectFilter(item.group,item.value);
    }
    rowsMenu($(kind+"-options"),rows,activeKey,choose);
    dest.hidden=false;
    $("popup-catcher").hidden=false;
    drawCategory();drawFilter();
    placePopup(kind);
    const active=dest.querySelector(".menu-option.active")||dest.querySelector(".menu-option");
    if(active)active.focus({preventScroll:true});
    note((kind==="category"?"Category dropdown":"Filter popup")+" opened as the original anchored floating menu. Long lists scroll inside it.");
  }
  function closePopup() {
    $("category-popup").hidden=true;$("filter-popup").hidden=true;
    $("popup-catcher").hidden=true;
    ui.popup=null;
    $("category-trigger").setAttribute("aria-expanded","false");
    $("filter-trigger").setAttribute("aria-expanded","false");
  }
  function setRail(open) {
    ui.rail=open;
    drawSources();
    if(ui.mode==="phone"&&open) {
      const rail=$("source-rail");
      rail.scrollIntoView({block:"nearest",behavior:"instant"});
    }
    adjustColumns();
    note(open?"Original 240px source rail expanded; on phones it overlays rather than compressing the poster wall.":"Source rail retracted to its original 52px icon strip.");
  }
  function applyViewport(newWidth,newHeight,which) {
    const width=Math.min(1920,Math.max(320,Number(newWidth)||412));
    const height=Math.min(1600,Math.max(320,Number(newHeight)||900));
    ui.width=width;ui.height=height;ui.view=width+"x"+height;
    ui.mode=(which==="desktop"||ui.view==="1280x700")?"desktop":
      (which==="tablet"||Math.min(width,height)>=600)?"tablet":"phone";
    const vp=$("viewport");
    vp.className=ui.mode+" "+(width>=height?"landscape":"portrait")+(isCapture?" capture-mode":"");
    vp.style.setProperty("--vp-w",width+"px");vp.style.setProperty("--vp-h",height+"px");
    $("stage").style.setProperty("--vp-w",width+"px");$("stage").style.setProperty("--vp-h",height+"px");
    vp.style.setProperty("--top-inset",ui.top+"px");vp.style.setProperty("--bottom-inset",ui.bottom+"px");
    vp.style.setProperty("--fs",String(ui.font));
    $("vp-width").value=width;$("vp-height").value=height;
    $("preset").value=presets.includes(ui.view)?ui.view:"";
    $("phone-world-tabs").hidden=ui.mode!=="phone";
    $("tablet-world-rail").hidden=ui.mode!=="tablet";
    $("viewport-note").textContent=ui.view+" logical pixels · "+ui.mode+" · top "+ui.top+" / bottom "+ui.bottom+" px · font "+ui.font+"× · device geometry provisional";
    if(ui.mode!=="phone") {
      $("source-rail").classList.remove("expanded");
      $("source-rail").classList.add("collapsed");
      $("source-rail").classList.toggle("expanded",ui.rail);
      $("source-rail").classList.toggle("collapsed",!ui.rail);
    }
    requestAnimationFrame(()=>{
      adjustColumns();
      if(ui.popup)placePopup(ui.popup);
    });
  }
  function scrollToDiscover() {
    const scroller=$("world-scroll"),tab=$("theatre-tabs");
    scroller.scrollTop=tab.offsetTop-5;
  }
  function showStudyState(mode) {
    closePopup();
    if(mode==="selected") {
      ui.chosen={group:"Genre",key:"Action"};ui.rail=false;draw();
      note("Selected-filter state: Genre · Action, original clear × available.");
    }else{
      if(mode!=="selected")ui.chosen=null;
      ui.rail=mode==="sources";
      draw();
      if(mode==="category"||mode==="filter")openPopup(mode);
      else if(mode==="sources")note("Expanded original source rail with long, locally scrollable provider list.");
      else note("Closed default state. The category name, FILTER pill and round rail chevron are live.");
    }
  }
  function setupStudio() {
    $("preset").addEventListener("change",(e)=>{
      const value=e.target.value;
      if(!value)return;
      const pair=value.split("x").map(Number);
      applyViewport(pair[0],pair[1]);
      requestAnimationFrame(scrollToDiscover);
    });
    for(const id of ["vp-width","vp-height"])$(id).addEventListener("change",()=>{
      applyViewport($("vp-width").value,$("vp-height").value);
      requestAnimationFrame(scrollToDiscover);
    });
    for(const [id,key] of [["inset-top","top"],["inset-bottom","bottom"]]){
      $(id).addEventListener("change",()=>{ui[key]=Math.max(0,Number($(id).value)||0);applyViewport(ui.width,ui.height);});
    }
    $("font-scale").addEventListener("change",(e)=>{
      ui.font=Number(e.target.value)||1;applyViewport(ui.width,ui.height);
      requestAnimationFrame(()=>{drawPosters();if(ui.popup)placePopup(ui.popup);});
    });
    $("state").addEventListener("change",(e)=>showStudyState(e.target.value));
    $("popup-placement").addEventListener("change",(e)=>{ui.popupPlacement=e.target.value;if(ui.popup)placePopup(ui.popup);note(ui.popupPlacement==="original"?"Category menu remains below its original masthead anchor (scroll inside).":"Category menu may flip upward in a short viewport; geometrical alternative for review.");});
    $("reset").addEventListener("click",()=>{
      ui.top=0;ui.bottom=0;ui.font=1;
      $("inset-top").value=0;$("inset-bottom").value=0;$("font-scale").value="1";
      ui.source=0;ui.category=0;ui.type="movie";
      ui.popupPlacement="fit";$("popup-placement").value="fit";
      applyViewport(412,900);$("state").value="closed";showStudyState("closed");
      requestAnimationFrame(scrollToDiscover);
    });
    $("category-trigger").addEventListener("click",()=>openPopup("category"));
    $("filter-trigger").addEventListener("click",()=>openPopup("filter"));
    $("filter-clear").addEventListener("click",(e)=>{e.stopPropagation();selectFilter(null,null);});
    $("source-toggle").addEventListener("click",()=>setRail(!ui.rail));
    $("popup-catcher").addEventListener("click",()=>{
      const previous=ui.popup;closePopup();drawCategory();drawFilter();
      note("Closed "+previous+" menu by tapping outside.");
    });
    document.addEventListener("keydown",(e)=>{
      if(e.key==="Escape"&&ui.popup){e.preventDefault();closePopup();drawCategory();drawFilter();return;}
      if(!ui.popup)return;
      const list=$(ui.popup+"-options");
      const options=Array.from(list.querySelectorAll(".menu-option"));
      let idx=options.indexOf(document.activeElement),next=idx;
      if(e.key==="ArrowDown")next=Math.min(options.length-1,idx+1);
      else if(e.key==="ArrowUp")next=Math.max(0,idx-1);
      else if(e.key==="Home")next=0;
      else if(e.key==="End")next=options.length-1;
      else if((e.key==="Enter"||e.key===" ")&&idx>=0){e.preventDefault();options[idx].click();return;}
      else return;
      e.preventDefault();
      if(next>=0&&options[next])options[next].focus({preventScroll:false});
    });
    $("world-scroll").addEventListener("scroll",()=>{if(ui.popup)closePopup();},{passive:true});
    window.addEventListener("resize",()=>{adjustColumns();if(ui.popup)placePopup(ui.popup);});
    document.querySelectorAll("[data-external]").forEach((node)=>{
      node.addEventListener("click",()=>note(node.dataset.external+" remains an original Colosseum control. Its destination is outside this isolated Discover mock."));
    });
    document.querySelectorAll("[data-theatre-tab]").forEach((node)=>{
      node.addEventListener("click",()=>{
        if(node.dataset.theatreTab==="Discover")return;
        note(node.dataset.theatreTab+" belongs to the Theatre world. This assigned mock illustrates Discover; no new destination UI has been invented.");
      });
    });
  }
  function init() {
    makeWorldNav($("phone-world-tabs"));makeWorldNav($("tablet-world-rail"));
    setupStudio();
    if(isCapture)document.body.classList.add("capture-mode");
    $("popup-placement").value=ui.popupPlacement;
    $("inset-top").value=ui.top;$("inset-bottom").value=ui.bottom;
    $("font-scale").value=String(ui.font);
    const pair=ui.view.split("x").map(Number);
    applyViewport(pair[0],pair[1]);
    draw();
    requestAnimationFrame(()=>{
      scrollToDiscover();
      requestAnimationFrame(()=>{
        $("state").value=["category","filter","sources","selected"].includes(initialState)?initialState:"closed";
        showStudyState($("state").value);
        if(ui.popup)placePopup(ui.popup);
        document.documentElement.dataset.mockReady="true";
      });
    });
  }
  init();
})();