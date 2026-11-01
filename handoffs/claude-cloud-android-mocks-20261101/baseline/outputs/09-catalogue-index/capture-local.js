"use strict";
// Isolated, read-only browser capture. All generated files stay under this task output.
const path=require("node:path"),fs=require("node:fs"),{pathToFileURL}=require("node:url");
const puppeteer=require("C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Brotherhood-Desktop-Commander\\node_modules\\puppeteer");
const DIR=__dirname,OUT=path.join(DIR,"captures"),PROFILE=path.join(DIR,".edge-final-profile");
const PAGES=process.argv.slice(2).length?process.argv.slice(2):["theatre","manga","comics"];
const VIEWS=[
 {tag:"portrait",v:"412x900",w:412,h:900,rail:false},
 {tag:"landscape",v:"900x412",w:900,h:412,rail:false},
 {tag:"desktop",v:"1280x700",w:1280,h:700,rail:false},
 {tag:"portrait-expanded",v:"412x900",w:412,h:900,rail:true},
 {tag:"narrow-expanded",v:"354x792",w:354,h:792,rail:true},
 {tag:"landscape-expanded",v:"900x412",w:900,h:412,rail:true},
 {tag:"short-landscape",v:"792x354",w:792,h:354,rail:true},
 {tag:"tablet-portrait",v:"800x1280",w:800,h:1280,rail:true},
 {tag:"tablet-landscape",v:"1280x800",w:1280,h:800,rail:true}
];
(async()=>{
 fs.mkdirSync(OUT,{recursive:true});
 const browser=await puppeteer.launch({
  executablePath:"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  headless:true,userDataDir:PROFILE,
  args:["--no-first-run","--no-default-browser-check","--disable-background-networking","--disable-sync","--disable-extensions","--disable-features=MediaRouter","--allow-file-access-from-files","--disable-gpu","--no-sandbox"]
 });
 const audit=[];
 try{
  for(const which of PAGES)for(const item of VIEWS){
   const pg=await browser.newPage(),errs=[];
   try{
    await pg.setViewport({width:item.w,height:item.h,deviceScaleFactor:1});
    await pg.setRequestInterception(true);
    pg.on("request",r=>{const url=r.url();if(url.startsWith("file:")||url.startsWith("data:"))r.continue();else {errs.push("external request blocked: "+url);r.abort();}});
    pg.on("pageerror",e=>errs.push("pageerror: "+e.message));
    const uri=pathToFileURL(path.join(DIR,which+"-catalogue.html")).href+
     "?capture=1&vp="+item.v+(item.rail?"&rail=open":"");
    await pg.goto(uri,{waitUntil:"domcontentloaded",timeout:30000});
    await pg.evaluate(()=>document.fonts.ready);
    await new Promise(resolve=>setTimeout(resolve,900));
    const result=await pg.evaluate(()=>{
      const by=s=>document.querySelector(s);
      const rect=el=>el?(()=>{const r=el.getBoundingClientRect();return {x:Math.round(r.x),y:Math.round(r.y),left:Math.round(r.left),right:Math.round(r.right),top:Math.round(r.top),bottom:Math.round(r.bottom),width:Math.round(r.width),height:Math.round(r.height)}})():null;
      const device=by("#device"),cat=by("#catalogue"),rail=by(".index-rail"),stack=by(".shelf-stack"),scroll=by("#worldScroll"),tabs=by("#tabZone");
      const rr=rect(rail),ss=rect(stack),dd=rect(device),tr=rect(tabs),s0=rect(by(".shelf"));
      const imgs=[...document.images],broken=imgs.filter(x=>x.complete&&!x.naturalWidth).map(x=>x.src).slice(0,9);
      const togg=by("#railToggle"),toggRect=rect(togg),hit=document.elementFromPoint(toggRect.x+14,toggRect.y+14);
      return {rendered:!!cat,deviceClass:device.className,rows:document.querySelectorAll(".rail-row").length,
        shelfCount:document.querySelectorAll(".shelf-stack .shelf").length,images:imgs.length,loaded:imgs.filter(x=>x.naturalWidth).length,
        broken,rail:rr,content:ss,device:dd,tabs:tr,firstShelf:s0,
        horizontalOverlap:rr&&ss?Math.max(0,rr.right-ss.left):null,
        contentOutside:!!(ss&&dd&&ss.right>dd.right+1),
        contentWidth:ss?.width,
        hasBottomNav:getComputedStyle(by(".bottom-tabs")).display!=="none",
        hasWideNav:getComputedStyle(by(".world-rail")).display!=="none",
        toggleHit:!!(hit&&(hit===togg||togg.contains(hit))),
        toggleLabel:togg.getAttribute("aria-label"),
        availableScroll:scroll.scrollHeight-scroll.clientHeight,
        pageHorizontalOverflow:document.documentElement.scrollWidth-window.innerWidth};
    });
    const shot=path.join(OUT,which+"-"+item.tag+".png");
    await pg.screenshot({path:shot,fullPage:false,captureBeyondViewport:false});
    const record={page:which,preset:item.tag,size:item.v,open:item.rail,file:shot,...result,errors:errs};
    audit.push(record);
    console.log(which+"/"+item.tag+": "+JSON.stringify({rows:record.rows,shelves:record.shelfCount,loaded:record.loaded,overlap:record.horizontalOverlap,tooWide:record.contentOutside,width:record.contentWidth,broken:record.broken.length,errors:record.errors.length}));
   }catch(e){audit.push({page:which,preset:item.tag,error:String(e),errors:errs});console.error(which+"/"+item.tag+" ERROR "+e.message);}
   finally{await pg.close();}
  }
 }finally{await browser.close();}
 const target=path.join(DIR,"capture-audit.json");
 const previous=fs.existsSync(target)?JSON.parse(fs.readFileSync(target,"utf8")).results:[];
 const complete=previous.filter(r=>!PAGES.includes(r.page)).concat(audit);
 fs.writeFileSync(target,JSON.stringify({command:"node capture-local.js "+PAGES.join(" "),browser:"Chrome initial, Edge refreshed",config:"Puppeteer isolated headless; file://; no network; viewport logical px, device scale 1",results:complete},null,2)+"\n");
 fs.rmSync(PROFILE,{recursive:true,force:true});
 const problems=complete.filter(r=>r.error||r.errors?.length||r.horizontalOverlap>1||r.contentOutside||r.broken?.length);
 console.log("AUDIT "+JSON.stringify({screenshots:complete.filter(r=>r.file).length,problems:problems.map(r=>({page:r.page,preset:r.preset,error:r.error,broken:r.broken?.length,overlap:r.horizontalOverlap,outside:r.contentOutside,errors:r.errors})),saved:target}));
 if(problems.length)process.exitCode=2;
})().catch(e=>{console.error("CAPTURE_FAILED "+e.stack);process.exitCode=1;});
