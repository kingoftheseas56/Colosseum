"use strict";
const http=require("http"),fs=require("fs"),path=require("path");
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const batchRoot=path.resolve(__dirname,"../..");
const captureRoot=path.join(__dirname,"captures");
const profile=path.join(captureRoot,".headless-profile");
const chrome="C:/Program Files/Google/Chrome/Application/chrome.exe";
const types={".html":"text/html; charset=utf-8",".css":"text/css; charset=utf-8",".js":"application/javascript; charset=utf-8",".ttf":"font/ttf",".otf":"font/otf",".jpg":"image/jpeg",".png":"image/png",".svg":"image/svg+xml",".webp":"image/webp",".json":"application/json"};
const captures=[
  {name:"portrait.png",preset:"portrait",w:412,h:900},
  {name:"portrait-small.png",preset:"portraitSmall",w:354,h:792},
  {name:"landscape.png",preset:"landscape",w:900,h:412},
  {name:"landscape-small.png",preset:"landscapeSmall",w:792,h:354},
  {name:"desktop.png",preset:"desktop",w:1280,h:700},
  {name:"tablet-portrait.png",preset:"tabletPortrait",w:800,h:1280},
  {name:"tablet-landscape.png",preset:"tabletLandscape",w:1280,h:800},
  {name:"portrait-read-open.png",preset:"portrait",w:412,h:900,scenario:"read"},
  {name:"landscape-read-open.png",preset:"landscape",w:900,h:412,scenario:"read"},
  {name:"portrait-collections-open.png",preset:"portrait",w:412,h:900,scenario:"collections"},
  {name:"portrait-expanded-torrents.png",preset:"portrait",w:412,h:900,scenario:"expanded",scrollTo:"#torrent-heading"},
  {name:"portrait-synopsis.png",preset:"portrait",w:412,h:900,scrollTo:"#synopsis"},
  {name:"landscape-synopsis.png",preset:"landscape",w:900,h:412,scrollTo:"#synopsis"},
  {name:"portrait-font130-insets.png",preset:"portraitSmall",w:354,h:792,scale:1.3,insetTop:24,insetBottom:24},
  {name:"landscape-font130-insets.png",preset:"landscapeSmall",w:792,h:354,scale:1.3,insetTop:24,insetBottom:24},
  {name:"portrait-small-read-open-insets.png",preset:"portraitSmall",w:354,h:792,scenario:"read",scale:1.3,insetTop:24,insetBottom:24},
  {name:"landscape-small-read-open-insets.png",preset:"landscapeSmall",w:792,h:354,scenario:"read",scale:1.3,insetTop:24,insetBottom:24},
  {name:"landscape-collections-open.png",preset:"landscape",w:900,h:412,scenario:"collections"},
  {name:"landscape-sources.png",preset:"landscape",w:900,h:412,scrollTo:"#torrent-heading"},
  {name:"portrait-library-saved.png",preset:"portrait",w:412,h:900,scenario:"saved"}
];
function serve(req,res){
  try{
    const u=new URL(req.url,"http://localhost");
    const pathname=decodeURIComponent(u.pathname);
    if(pathname==="/favicon.ico"){res.writeHead(204);res.end();return;}
    const file=path.resolve(batchRoot,"."+pathname);
    if(file!==batchRoot&&!file.startsWith(batchRoot+path.sep)){res.writeHead(403);res.end("Denied");return;}
    fs.stat(file,(err,stat)=>{
      if(err||!stat.isFile()){res.writeHead(404);res.end("Not found");return;}
      res.writeHead(200,{"Content-Type":types[path.extname(file).toLowerCase()]||"application/octet-stream","Cache-Control":"no-store"});
      fs.createReadStream(file).pipe(res);
    });
  }catch(e){res.writeHead(400);res.end("Invalid request");}
}
async function inspect(page,entry){
  return page.evaluate(()=>{
    const r=s=>{const e=document.querySelector(s);if(!e)return null;const q=e.getBoundingClientRect();return{x:q.x,y:q.y,w:q.width,h:q.height,right:q.right,bottom:q.bottom};};
    const app=r("#app-screen"),sc=r("#book-scroll"),jacket=r(".book-jacket"),heading=r(".book-heading");
    const mode=document.querySelector("#app-screen").dataset.mode;
    const sourceRows=[...document.querySelectorAll(".source-row")];
    const targets=[".book-jacket",".book-heading",".action-column",".book-information","#read-button","#library-button","#collections-button",".source-list"];
    const overflowing=targets.map(s=>({selector:s,rect:r(s)})).filter(x=>x.rect&&(x.rect.x<app.x-1||x.rect.right>app.right+1));
    const horizontalOverflow=document.querySelector("#book-scroll").scrollWidth-document.querySelector("#book-scroll").clientWidth;
    const rowTooSmall=sourceRows.filter(e=>e.getBoundingClientRect().height<44).length;
    const unsafeButtons=["#book-back","#read-button","#library-button","#collections-button"].map(s=>({s,rect:r(s)})).filter(x=>mode.startsWith("phone")&&x.rect&&x.rect.h<44);
    const overlap=mode==="phone-portrait"?(heading.y<jacket.bottom-2):(heading.x<jacket.right+8);
    const image=document.querySelector(".book-jacket img");
    const modal=document.querySelector(".modal-shade:not([hidden])");
    const dialog=modal?.querySelector("[role=dialog]");
    const popup=dialog?(()=>{const z=dialog.getBoundingClientRect();return {visible:z.width>0&&z.height>0,withinWidth:z.left>=app.x-1&&z.right<=app.right+1,withinHeight:z.top>=app.y-1&&z.bottom<=app.bottom+1}})():null;
    return {mode,viewport:{w:app.w,h:app.h},imageLoaded:image.complete&&image.naturalWidth>0,horizontalOverflow,overflowing,rowTooSmall,unsafeButtons,overlap,popup,scrollHeight:document.querySelector("#book-scroll").scrollHeight,scrollClientHeight:document.querySelector("#book-scroll").clientHeight,appSceneWidth:document.querySelector("#safe-zone").clientWidth};
  });
}
async function main(){
  fs.mkdirSync(captureRoot,{recursive:true});
  const server=http.createServer(serve);
  await new Promise(ok=>server.listen(0,"127.0.0.1",ok));
  const port=server.address().port;
  const results=[];
  const browser=await puppeteer.launch({executablePath:chrome,headless:true,userDataDir:profile,args:["--no-first-run","--no-default-browser-check","--disable-background-networking","--disable-sync","--disable-extensions"]});
  try{
    for(const entry of captures){
      const page=await browser.newPage();
      const errors=[];
      page.on("pageerror",e=>errors.push("pageerror:"+e.message));
      page.on("response",r=>{if(r.status()>=400)errors.push("HTTP "+r.status()+" "+r.url());});
      await page.setViewport({width:Math.max(entry.w+140,1450),height:Math.max(entry.h+400,1500),deviceScaleFactor:1});
      const qs=new URLSearchParams({preset:entry.preset});
      if(entry.scenario)qs.set("scenario",entry.scenario);
      if(entry.scale)qs.set("scale",String(entry.scale));
      if(entry.insetTop!==undefined)qs.set("insetTop",String(entry.insetTop));
      if(entry.insetBottom!==undefined)qs.set("insetBottom",String(entry.insetBottom));
      await page.goto("http://127.0.0.1:"+port+"/outputs/06-biblio-book/biblio-book.html?"+qs,{waitUntil:"networkidle0",timeout:20000});
      await page.evaluate(()=>document.fonts.ready);
      if(entry.scrollTo){
        await page.evaluate(selector=>document.querySelector(selector).scrollIntoView({block:"start"}),entry.scrollTo);
        await new Promise(resolve=>setTimeout(resolve,140));
      }
      const checks=await inspect(page,entry);
      if(checks.viewport.w!==entry.w||checks.viewport.h!==entry.h)errors.push("wrong viewport");
      if(!checks.imageLoaded)errors.push("missing cover asset");
      if(checks.horizontalOverflow>1)errors.push("horizontal app-scroll overflow="+checks.horizontalOverflow);
      if(checks.overflowing.length)errors.push("x-overflow="+JSON.stringify(checks.overflowing));
      if(checks.rowTooSmall)errors.push("inaccessible source row target count="+checks.rowTooSmall);
      if(checks.unsafeButtons.length)errors.push("small core buttons="+JSON.stringify(checks.unsafeButtons));
      if(checks.overlap)errors.push("cover/title overlap");
      if(checks.popup&&(!checks.popup.withinWidth||!checks.popup.withinHeight))errors.push("modal exceeds app boundaries");
      const app=await page.$("#app-screen");
      await app.screenshot({path:path.join(captureRoot,entry.name)});
      results.push({capture:entry.name,preset:entry.preset,scenario:entry.scenario||"full",checks,errors});
      process.stdout.write(entry.name+" "+(errors.length?"FAIL "+errors.join("; "):"OK")+"\n");
      await page.close();
    }
    // Additional direct interaction/keyboard checks independent of screenshots.
    const page=await browser.newPage();
    await page.setViewport({width:1450,height:1500,deviceScaleFactor:1});
    await page.goto("http://127.0.0.1:"+port+"/outputs/06-biblio-book/biblio-book.html?preset=portrait",{waitUntil:"networkidle0"});
    await page.click("#library-button");
    const saved=await page.$eval("#library-label",e=>e.textContent.trim());
    await page.click("#read-button");
    const readVisible=await page.$eval("#read-shade",e=>!e.hidden);
    await page.keyboard.press("Escape");
    const readClosed=await page.$eval("#read-shade",e=>e.hidden);
    await page.evaluate(()=>document.querySelector("#torrent-more").click());
    const expanded=await page.$eval("#torrent-list",e=>e.querySelectorAll(".torrent-row").length);
    await page.click("#collections-button");
    const collectionsVisible=await page.$eval("#collections-shade",e=>!e.hidden);
    const interactions={saved,readVisible,readClosed,expanded,collectionsVisible,passed:saved==="In Library"&&readVisible&&readClosed&&expanded===7&&collectionsVisible};
    results.push({interactions,errors:interactions.passed?[]:["Interaction smoke failure"]});
    await page.close();
    const verification={date:"2026-11-01",tool:"Node.js + locally installed Puppeteer + isolated headless Google Chrome",command:'node "D:\\Colosseum-Android-Phone-Mocks-20261101\\outputs\\06-biblio-book\\capture.cjs"',server:"temporary 127.0.0.1 port, batch root, closed after run",screenshots:results};
    fs.writeFileSync(path.join(captureRoot,"validation.json"),JSON.stringify(verification,null,2));
    console.log("VALIDATION "+results.filter(x=>x.errors.length).length+" failing cases / "+results.length+" cases");
    if(results.some(x=>x.errors.length))process.exitCode=1;
  }finally{
    await browser.close();
    await new Promise(ok=>server.close(ok));
    try{fs.rmSync(profile,{recursive:true,force:true});}catch(e){console.error("Temporary browser profile cleanup failed:",e.message);}
  }
}
main().catch(e=>{console.error(e);process.exitCode=2;});
