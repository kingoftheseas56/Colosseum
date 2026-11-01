"use strict";
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const fs=require("fs"),path=require("path"),{pathToFileURL}=require("url");
const root=__dirname,capDir=path.join(root,"captures");
const chrome="C:/Program Files/Google/Chrome/Application/chrome.exe";
const specs=[
 ["extension-setup.html","extension-setup-portrait",412,900,"preset=portrait&state=open"],
 ["extension-setup.html","extension-setup-landscape",900,412,"preset=landscape&state=open"],
 ["extension-setup.html","extension-setup-desktop",1280,700,"preset=desktop&state=open"],
 ["extension-setup.html","extension-setup-small",354,792,"preset=small&state=open&scale=1.3&insetTop=24&insetBottom=24"],
 ["extension-setup.html","extension-setup-long-addon",354,792,"preset=small&addon=Comet%20%7C%20ElfHosted"],
 ["extension-setup.html","extension-setup-short-keyboard",792,354,"preset=smallLandscape&state=open&keyboard=1&ime=150"],
 ["extension-setup.html","extension-setup-short-long",792,354,"preset=smallLandscape&keyboard=1&ime=150&addon=OpenSubtitles%20v3"],
 ["extension-setup.html","extension-setup-portrait-keyboard",412,900,"preset=portrait&state=open&keyboard=1&ime=270&scale=1.3"],
 ["extension-setup.html","extension-setup-closed",412,900,"preset=portrait&state=closed"],
 ["extension-setup.html","extension-setup-tablet",800,1280,"preset=tablet&state=open"],
 ["extension-setup.html","extension-setup-tablet-landscape",1280,800,"preset=tabletLandscape&state=open"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-portrait",412,900,"preset=portrait&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-landscape",900,412,"preset=landscape&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-desktop",1280,700,"preset=desktop&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-small",354,792,"preset=small&language=es&scale=1.3&insetTop=24&insetBottom=24"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-short",792,354,"preset=smallLandscape&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-menu-open",412,900,"preset=portrait&language=es&menu=open"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-about",412,900,"preset=portrait&tab=about"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-keyboard",412,900,"preset=portrait&keyboard=1&ime=270&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-tablet",800,1280,"preset=tablet&language=es"],
 ["tankoyomi-configuration.html","tankoyomi-configuration-tablet-landscape",1280,800,"preset=tabletLandscape&language=es"]
];
(async()=>{
 if(!fs.existsSync(capDir))fs.mkdirSync(capDir,{recursive:true});
 const browser=await puppeteer.launch({executablePath:chrome,headless:true,userDataDir:path.join(capDir,".isolated-headless-profile"),args:["--no-first-run","--disable-background-networking","--disable-extensions"]});
 const receipts=[];
 try {
  async function captureOne([file,basename,width,height,params]){
   const page=await browser.newPage(),errors=[];
   page.on("pageerror",e=>errors.push(String(e)));page.on("console",m=>{if(m.type()==="error")errors.push("console:"+m.text())});
   await page.setViewport({width,height,deviceScaleFactor:1});
   const url=pathToFileURL(path.join(root,file)).href+"?capture=1&"+params;
   const response=await page.goto(url,{waitUntil:"load",timeout:15000});
   await page.evaluate(()=>document.fonts.ready);
   await new Promise(r=>setTimeout(r,250));
   const png=path.join(capDir,basename+".png");await page.screenshot({path:png,captureBeyondViewport:false});
   const match=basename.match(/^(extension-setup|tankoyomi-configuration)-(portrait|landscape|desktop)$/);
   if(match){const folder=path.join(capDir,match[1]);fs.mkdirSync(folder,{recursive:true});fs.copyFileSync(png,path.join(folder,match[2]+".png"));}
   const details=await page.evaluate(()=>{
    const frame=document.querySelector("#viewport"),r=frame.getBoundingClientRect();
    const scroll=document.querySelector("#tank-scroll");
    const imgs=Array.from(document.querySelectorAll("#viewport img")).map(im=>({path:im.getAttribute("src"),ok:im.complete&&im.naturalWidth>0}));
    const visible=Array.from(document.querySelectorAll("#viewport button,#viewport input")).filter(el=>!!el.getClientRects().length);
    const over=visible.map(el=>{const b=el.getBoundingClientRect();return {id:el.id||el.getAttribute("aria-label")||el.textContent.trim().slice(0,30),left:b.left,right:b.right,top:b.top,bottom:b.bottom}}).filter(b=>b.left<r.left-2||b.right>r.right+2);
    return {frame:{width:r.width,height:r.height},bodyScrollWidth:document.body.scrollWidth,scrollWidth:scroll?.scrollWidth,clientWidth:scroll?.clientWidth,
       interactiveCount:visible.length,horizontalOutOfFrame:over,assets:imgs,
       hasPanel:!document.querySelector("#setup-shade")?.hidden, menuOpen:document.querySelector("#default-selector")?.getAttribute("aria-expanded")};
   });
   receipts.push({file,basename,width,height,params,png,details,errors});
   console.log(JSON.stringify({basename,frame:details.frame,overflows:details.horizontalOutOfFrame.length,scrollOverflow:(details.scrollWidth||0)-(details.clientWidth||0),assetsBroken:details.assets.filter(x=>!x.ok),errors}));
   await page.close();
  }
  // Single isolated headless page at a time: concurrent captures stalled on loaded Laptop 1.
  for(const spec of specs)await captureOne(spec);
 } finally {await browser.close();}
 fs.writeFileSync(path.join(capDir,"render-audit.json"),JSON.stringify({tool:"Installed Puppeteer + isolated headless Chrome",command:"node "+path.join(root,"render-mocks.js"),executablePath:chrome,receipts},null,2));
})().catch(e=>{console.error("RENDER_FAILED",e.stack||e);process.exitCode=1});