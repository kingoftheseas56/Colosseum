"use strict";
// Isolated headless validation of the two STATIC local HTML studies only.
// This script never opens a user browser/tab or touches the Colosseum repository.
const path=require("path"),fs=require("fs"),{pathToFileURL}=require("url");
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const root=__dirname, captures=path.join(root,"captures");
const chrome="C:/Program Files/Google/Chrome/Application/chrome.exe";
const variants=[
  {name:"original",file:"volume-flow-original-fit.html"},
  {name:"alternative",file:"volume-flow-minimal-alternative.html"}
];
const presets=[
  {name:"portrait",width:412,height:900},
  {name:"portrait-small",width:354,height:792},
  {name:"landscape",width:900,height:412},
  {name:"landscape-small",width:792,height:354},
  {name:"desktop",width:1280,height:700},
  {name:"tablet-portrait",width:800,height:1280},
  {name:"tablet-landscape",width:1280,height:800},
  {name:"selection",preset:"portrait",width:412,height:900,state:"select"},
  {name:"expanded",preset:"portrait",width:412,height:900,state:"synopsis"},
  {name:"insets-font",preset:"portrait-small",width:354,height:792,top:24,bottom:24,font:1.3}
];
const profile=path.join(captures,"_ephemeral-headless-profile-"+process.pid);
const records=[];
async function main(){
  fs.mkdirSync(captures,{recursive:true});
  const browser=await puppeteer.launch({
    executablePath:chrome,headless:true,userDataDir:profile,
    args:["--no-first-run","--no-default-browser-check","--disable-extensions","--disable-sync","--disable-background-networking","--no-proxy-server","--no-sandbox"]
  });
  try{
    for(const variant of variants){
      fs.mkdirSync(path.join(captures,variant.name),{recursive:true});
      for(const preset of presets){
        const page=await browser.newPage();
        const pageErrors=[],networkViolations=[],brokenResources=[];
        try{
          await page.setViewport({width:preset.width,height:preset.height,deviceScaleFactor:1});
          await page.setRequestInterception(true);
          page.on("request",request=>{
            const url=request.url();
            if(url.startsWith("file:")||url.startsWith("data:")||url.startsWith("about:"))request.continue();
            else{networkViolations.push(url);request.abort();}
          });
          page.on("pageerror",error=>pageErrors.push(String(error)));
          page.on("requestfailed",req=>brokenResources.push(req.url()+" "+(req.failure()?.errorText||"")));
          const url=pathToFileURL(path.join(root,variant.file));
          url.searchParams.set("capture","1");
          url.searchParams.set("preset",preset.preset||preset.name);
          if(preset.state)url.searchParams.set("state",preset.state);
          if(preset.top)url.searchParams.set("top",String(preset.top));
          if(preset.bottom)url.searchParams.set("bottom",String(preset.bottom));
          if(preset.font)url.searchParams.set("font",String(preset.font));
          await page.goto(url.toString(),{waitUntil:"load",timeout:20000});
          await page.evaluate(async()=>{await document.fonts.ready;await Promise.all([...document.images].map(img=>img.complete?Promise.resolve():new Promise(done=>{img.addEventListener("load",done,{once:true});img.addEventListener("error",done,{once:true})})));});
          await new Promise(done=>setTimeout(done,250));
          const metrics=await page.evaluate(()=>{
            const $=s=>document.querySelector(s);
            const rect=r=>r?{left:r.left,right:r.right,top:r.top,bottom:r.bottom,width:r.width,height:r.height}:null;
            const vr=$(".flow-viewport").getBoundingClientRect(), fr=$(".volume-card.focused").getBoundingClientRect();
            const ar=$(".action-bar").getBoundingClientRect(), sr=$(".app-screen").getBoundingClientRect();
            const cover=$(".volume-card.focused .cover-frame").getBoundingClientRect();
            const scroll=$(".series-scroll");
            const imgs=[...document.querySelectorAll(".volume-card img")];
            const controls=["#backAction","#tankobanMode","#chapterMode",".language-selector","#collections","#selectToggle","#downloadAction","#readAction"];
            const sizes={};for(const s of controls){const el=$(s),r=el?.getBoundingClientRect();sizes[s]=rect(r);}
            return {
              viewport:{width:innerWidth,height:innerHeight},
              frame:rect($("#appFrame").getBoundingClientRect()),
              lane:rect($(".lane-head").getBoundingClientRect()),
              flow:rect(vr),focus:rect(fr),cover:rect(cover),
              hero:rect($(".hero").getBoundingClientRect()),actionBar:rect(ar),
              currentVolume:$("#currentVolume").textContent,
              currentAction:$("#readAction").textContent,
              coverFit:fr.top>=vr.top-1&&fr.bottom<=vr.bottom+1,
              coverVisibleInScreen:fr.bottom<=sr.bottom+1&&fr.top>=sr.top-1,
              actionVisibleInScreen:ar.bottom<=sr.bottom+1&&ar.top>=sr.top-1,
              scrollable:scroll.scrollHeight>scroll.clientHeight+1,
              maxScroll:scroll.scrollHeight-scroll.clientHeight,
              imageCount:imgs.length,loadedImages:imgs.filter(im=>im.complete&&im.naturalWidth>0).length,
              controls:sizes,geometryStatus:$("#geometryStatus").textContent
            };
          });
          const out=path.join(captures,variant.name,preset.name+".png");
          await page.screenshot({path:out,type:"png",fullPage:false});
          let galleryShot=null;
          if((preset.name==="landscape-small" && variant.name==="alternative") || preset.name==="insets-font"){
            const scrollTop=await page.evaluate(()=>{
              const el=document.querySelector(".series-scroll");
              el.scrollTop=el.scrollHeight-el.clientHeight;
              return el.scrollTop;
            });
            await new Promise(done=>setTimeout(done,140));
            galleryShot=path.join(captures,variant.name,"landscape-small-gallery.png");
            await page.screenshot({path:galleryShot,type:"png",fullPage:false});
            metrics.galleryScrollTop=scrollTop;
          }
          records.push({variant:variant.name,preset:preset.name,path:path.relative(root,out),
            dimensions:[preset.width,preset.height],metrics,pageErrors,networkViolations,brokenResources,
            galleryShot:galleryShot?path.relative(root,galleryShot):null,
            pngBytes:fs.statSync(out).size});
          process.stdout.write(variant.name+"/"+preset.name+" focusFit="+metrics.coverFit+
            " visible="+metrics.coverVisibleInScreen+" image="+metrics.loadedImages+"/"+metrics.imageCount+
            " scroll="+metrics.maxScroll+" jsErrors="+pageErrors.length+"\n");
        }finally{await page.close();}
      }
    }
    // Exercise original controls in a dedicated headless context: no backend operations.
    const test=await browser.newPage();
    try{
      await test.setViewport({width:412,height:900});
      const url=pathToFileURL(path.join(root,"volume-flow-original-fit.html"));
      url.searchParams.set("capture","1");url.searchParams.set("preset","portrait");
      await test.goto(url.toString(),{waitUntil:"load"});
      await new Promise(done=>setTimeout(done,200));
      const interactions=await test.evaluate(()=>{
        const $=s=>document.querySelector(s);
        const before=$("#currentVolume").textContent;
        const neighbour=[...document.querySelectorAll(".volume-card")].find(x=>x.getAttribute("aria-label")==="Volume 4");
        neighbour.click();
        const afterNeighbour=$("#currentVolume").textContent;
        const focus=$(".volume-card.focused");focus.click();
        const acted=$("#eventOutput").textContent.includes("readVolumeRequested");
        $("#selectToggle").click();
        $(".volume-card.focused").click();
        const selection=$("#selectedCount").textContent;
        const selectionOpen=!$("#selectionFloat").hidden;
        $("#selectToggle").click();
        const selectionClosed=$("#selectionFloat").hidden;
        const synopsisBefore=$(".hero").getBoundingClientRect().height;
        const synopsisSelector=$("#reviewState");
        synopsisSelector.value="synopsis";
        synopsisSelector.dispatchEvent(new Event("change",{bubbles:true}));
        const synopsisExpanded=$("#synopsisText").textContent.length>250;
        const synopsisAfter=$(".hero").getBoundingClientRect().height;
        synopsisSelector.value="default";
        synopsisSelector.dispatchEvent(new Event("change",{bubbles:true}));
        return {before,afterNeighbour,acted,selection,selectionOpen,selectionClosed,synopsisExpanded,
          synopsisBefore,synopsisAfter};
      });
      await test.focus("#flowViewport");
      await test.keyboard.press("ArrowRight");
      interactions.afterKey=await test.$eval("#currentVolume",el=>el.textContent);
      await test.keyboard.press("PageDown");
      interactions.afterPageDown=await test.$eval("#currentVolume",el=>el.textContent);
      records.push({interactions});
      process.stdout.write("INTERACTIONS "+JSON.stringify(interactions)+"\n");
    }finally{await test.close();}
  }finally{
    await browser.close();
    // Only our scratch browser profile is removed; all authored review evidence stays.
    fs.rmSync(profile,{recursive:true,force:true,maxRetries:4,retryDelay:100});
  }
  const target=path.join(captures,"geometry-review.json");
  fs.writeFileSync(target,JSON.stringify({task:"05-volume-flow",tool:"puppeteer headless Chrome isolated local file URL",chrome,records},null,2));
  console.log("GEOMETRY_REPORT "+target);
}
main().catch(e=>{console.error(e.stack||String(e));process.exitCode=1});
