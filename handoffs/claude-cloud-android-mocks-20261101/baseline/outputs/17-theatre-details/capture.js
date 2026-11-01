"use strict";
// Local-only Chrome headless reviewer. Does not touch shared tabs, network, repositories or devices.
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");
const puppeteer = require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const root = __dirname;
const captures = path.join(root, "captures");
const profile = fs.mkdtempSync(path.join(root, ".chrome-review-"));
fs.mkdirSync(captures,{recursive:true});
const tests = [
  {file:"capture-film.html",name:"film-portrait.png",w:412,h:900},
  {file:"capture-film.html",name:"film-portrait-small.png",w:354,h:792},
  {file:"capture-film.html",name:"film-landscape.png",w:900,h:412},
  {file:"capture-film.html",name:"film-landscape-small.png",w:792,h:354},
  {file:"capture-film.html",name:"film-desktop.png",w:1280,h:700},
  {file:"capture-film.html",name:"film-tablet.png",w:800,h:1280},
  {file:"capture-film-rebuilt-synopsis.html",name:"film-synopsis-open.png",w:412,h:900},
  {file:"capture-series.html",name:"series-portrait.png",w:412,h:900},
  {file:"capture-series.html",name:"series-portrait-small.png",w:354,h:792},
  {file:"capture-series.html",name:"series-landscape.png",w:900,h:412},
  {file:"capture-series.html",name:"series-landscape-small.png",w:792,h:354},
  {file:"capture-series.html",name:"series-desktop.png",w:1280,h:700},
  {file:"capture-series.html",name:"series-tablet.png",w:800,h:1280},
  {file:"capture-series-episodes.html",name:"series-episodes-portrait.png",w:412,h:900},
  {file:"capture-series-episodes.html",name:"series-episodes-landscape.png",w:900,h:412},
  {file:"capture-series-season-open.html",name:"series-season-open-portrait.png",w:354,h:792},
  {file:"capture-series-season-open.html",name:"series-season-open-landscape.png",w:792,h:354},
  {file:"capture-series-jump-open.html",name:"series-jump-open-portrait.png",w:412,h:900},
  {file:"capture-series-rebuilt.html",name:"series-rebuilt-episodes.png",w:412,h:900},
  {file:"capture-series.html",name:"series-insets-scale.png",w:412,h:900,top:24,bottom:24,scale:1.3}
];
const chosenTests = process.argv.includes("quick") ? tests.filter(t =>
  ["film-portrait.png","film-desktop.png","film-tablet.png","series-episodes-portrait.png",
   "series-season-open-landscape.png","series-jump-open-portrait.png","series-rebuilt-episodes.png"].includes(t.name)) : tests;
let browser;
const receipt={tool:"node capture.js via Brotherhood Desktop Commander start_process; Puppeteer + installed Google Chrome in isolated headless profile",
              browser:"C:/Program Files/Google/Chrome/Application/chrome.exe",date:"2026-11-01",
              results:[],errors:[]};
async function geometry(page) {
  return await page.evaluate(() => {
    const app=document.querySelector("#app"), sc=document.querySelector("#page-scroll");
    const view=app.getBoundingClientRect(), popup=document.querySelector("#overlay .popup");
    const imgs=[...document.querySelectorAll("#app img")];
    const badImgs=imgs.filter(x=>!x.complete || x.naturalWidth<1).map(x=>x.getAttribute("src"));
    const title=document.querySelector(".hero-title")?.getBoundingClientRect();
    const targetControls=[...document.querySelectorAll(".circle-action")].slice(0,4).map(x=>{
      const r=x.getBoundingClientRect();return {w:Math.round(r.width),h:Math.round(r.height)};
    });
    let pop=null;
    if(popup){const r=popup.getBoundingClientRect();pop={
      left:Math.round(r.left-view.left),right:Math.round(r.right-view.left),
      top:Math.round(r.top-view.top),bottom:Math.round(r.bottom-view.top),
      inside:r.left>=view.left-1 && r.right<=view.right+1 && r.top>=view.top-1 && r.bottom<=view.bottom+1
    };}
    return {
      captureClass:document.body.classList.contains("capture"),
      reviewToolsDisplay:getComputedStyle(document.querySelector("#review-tools")).display,
      appWidth:app.clientWidth,appHeight:app.clientHeight,
      contentScrollHeight:sc.scrollHeight,contentScrollWidth:sc.scrollWidth,
      horizontalOverflow:sc.scrollWidth>sc.clientWidth+2,
      titleRight:Math.round((title?.right||0)-view.left),
      titleWithinWidth:!title || title.right<=view.right+1,
      buttonTargets:targetControls, popup:pop,
      missingImgSrc:badImgs,
      topVisibleText:document.querySelector(".hero-title")?.textContent||"",
      heroArt:getComputedStyle(document.querySelector(".hero-art")).backgroundImage
    };
  });
}
(async()=>{
  try{
    browser=await puppeteer.launch({executablePath:receipt.browser,headless:true,userDataDir:profile,
      args:["--disable-background-networking","--no-first-run","--no-default-browser-check","--disable-sync","--disable-extensions","--allow-file-access-from-files"]});
    for(const test of chosenTests){
      let page;
      try{
        page=await browser.newPage();
        await page.setViewport({width:test.w,height:test.h,deviceScaleFactor:1});
        const pageErrors=[];
        page.on("pageerror",e=>pageErrors.push(String(e)));
        page.on("request",request=>{if(/^https?:/i.test(request.url())) pageErrors.push("Unexpected external URL: "+request.url());});
        const uri=pathToFileURL(path.join(root,test.file));
        const q=[];
        if(test.top)q.push("top="+test.top);
        if(test.bottom)q.push("bottom="+test.bottom);
        if(test.scale)q.push("scale="+test.scale);
        await page.goto(uri.href+(q.length?"?"+q.join("&"):""),{waitUntil:"domcontentloaded",timeout:11000});
        await page.evaluate(()=>document.fonts.ready);
        await new Promise(ok=>setTimeout(ok,220));
        const info=await geometry(page);
        const dest=path.join(captures,test.name);
        await page.screenshot({path:dest,fullPage:false,animations:"disabled"});
        const result={file:test.file,capture:test.name,viewport:[test.w,test.h],insets:[test.top||0,test.bottom||0],fontScale:test.scale||1,
          bytes:fs.statSync(dest).size,...info,pageErrors};
        receipt.results.push(result);
        console.log("RENDER",test.name,JSON.stringify({size:result.bytes,overflow:info.horizontalOverflow,popup:info.popup,errors:pageErrors,missingImg:info.missingImgSrc.length,captureClass:info.captureClass}));
      }catch(e){
        receipt.errors.push({capture:test.name,error:String(e)});
        console.log("FAILED",test.name,String(e));
      }finally{if(page)await page.close();}
    }
    // Interaction-only acceptance on fresh page; no backend calls or player.
    const p=await browser.newPage();
    await p.setViewport({width:354,height:792});
    await p.goto(pathToFileURL(path.join(root,"capture-series.html")).href,{waitUntil:"domcontentloaded",timeout:11000});
    await p.click("#season-trigger");
    const seasonOpen=await p.evaluate(()=>!!document.querySelector("#overlay .popup-season"));
    await p.click('#overlay [data-season="17"]');
    const selectedSeason=await p.$eval("#season-trigger",x=>x.textContent.trim());
    await p.click("#jump-trigger");
    const jumpOpen=await p.evaluate(()=>!!document.querySelector("#overlay .popup-jump"));
    await p.$eval("#jump-input",el=>el.value="5");
    await p.click('#overlay [data-action="jump-submit"]');
    const jumpClosed=await p.evaluate(()=>!document.querySelector("#overlay .popup-jump"));
    receipt.interactions={seasonOpen,selectedSeason,jumpOpen,jumpClosed};
    console.log("INTERACTIONS",JSON.stringify(receipt.interactions));
    await p.close();
  }catch(e){receipt.errors.push({phase:"launch-or-fatal",error:String(e)});console.log("FATAL",String(e));}
  finally{
    if(browser)try{await browser.close();}catch(e){}
    try{fs.rmSync(profile,{recursive:true,force:true});}catch(e){receipt.errors.push({phase:"profile-cleanup",error:String(e)});}
    fs.writeFileSync(path.join(captures,process.argv.includes("quick") ? "validation-quick.json" : "validation.json"),JSON.stringify(receipt,null,2),"utf8");
    console.log("VALIDATION",path.join(captures,process.argv.includes("quick") ? "validation-quick.json" : "validation.json"));
    if(receipt.errors.length)process.exitCode=1;
  }
})();
