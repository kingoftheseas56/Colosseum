"use strict";
const path=require("node:path"),fs=require("node:fs"),{pathToFileURL}=require("node:url");
const puppeteer=require("C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Brotherhood-Desktop-Commander\\node_modules\\puppeteer");
const root=__dirname,profile=path.join(root,".smoke-profile"),results=[];
const check=(label,condition,extra={})=>{results.push({label,pass:!!condition,...extra});console.log((condition?"PASS ":"FAIL ")+label);};
(async()=>{
 const browser=await puppeteer.launch({executablePath:"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",headless:true,userDataDir:profile,
 args:["--no-first-run","--disable-background-networking","--disable-sync","--no-sandbox","--allow-file-access-from-files"]});
 try{
  const page=await browser.newPage();await page.setViewport({width:412,height:900,deviceScaleFactor:1});
  await page.setRequestInterception(true);page.on("request",r=>r.url().startsWith("file:")||r.url().startsWith("data:")?r.continue():r.abort());
  const base=n=>pathToFileURL(path.join(root,n+"-catalogue.html")).href;
  await page.goto(base("theatre")+"?capture=1&vp=412x900",{waitUntil:"load"});
  await page.evaluate(()=>document.fonts.ready);
  const evalCheck=async fn=>page.evaluate(fn);
  check("Theatre starts collapsed",await evalCheck(()=>document.querySelector("#railToggle").getAttribute("aria-expanded")==="false"));
  await page.click("#railToggle");
  check("Rail expands in-place with no overlay",await evalCheck(()=>{const d=document.querySelector("#catalogue");return d.classList.contains("open")&&d.querySelector(".index-rail").getBoundingClientRect().right<d.querySelector(".shelf-stack").getBoundingClientRect().left;}));
  check("Expanded rail retains wrapped long row label",await evalCheck(()=>[...document.querySelectorAll(".rail-label")].some(x=>x.textContent==="International Cinema")));
  await page.click('[data-action="customize"]');
  check("Customize mode contains original controls",await evalCheck(()=>document.querySelectorAll(".edit-controls").length>5&&!!document.querySelector('[data-edit="rename"]')));
  await page.click('[data-edit="rename"][data-key="top-10"]');
  await page.type('.rename-input','Top Ten Example');
  await page.keyboard.press("Enter");
  check("Rename commits inline",await evalCheck(()=>[...document.querySelectorAll(".widget-title")].some(x=>x.textContent.endsWith("Top Ten Example"))));
  await page.click('[data-edit="reset"][data-key="top-10"]');
  check("Reset restores source title",await evalCheck(()=>[...document.querySelectorAll(".shelf-stack .widget-title")].some(x=>x.textContent==="Top 10")));
  await page.click('[data-edit="visibility"][data-key="hidden-gems"]');
  check("Hide dim in edit, retained rail navigation",await evalCheck(()=>document.querySelector("#shelf-hidden-gems").classList.contains("is-hidden")));
  await page.click('[data-action="customize"]');
  check("Hide removes row on Done",await evalCheck(()=>!document.querySelector("#shelf-hidden-gems")));
  await page.click('[data-tab="shows"]');
  check("Shows selected with original shelf titles",await evalCheck(()=>!!document.querySelector("#shelf-currently-airing")&&!!document.querySelector('[data-tab="shows"].active')));
  await page.click('[data-tab="anime"]');
  check("Anime selected with original shelf titles",await evalCheck(()=>!!document.querySelector("#shelf-airing-now")&&!!document.querySelector("#shelf-top-anime-movies")));
  await page.goto(base("manga")+"?capture=1&vp=354x792&rail=open&font=1.3&topInset=24&bottomInset=24",{waitUntil:"load"});
  await page.evaluate(()=>document.fonts.ready);
  const m=await page.evaluate(()=>{
    const cat=document.querySelector("#catalogue"),rail=cat.querySelector(".index-rail"),stack=cat.querySelector(".shelf-stack");
    const footer=document.querySelector(".bottom-tabs");
    return {width:stack.getBoundingClientRect().width,gap:stack.getBoundingClientRect().left-rail.getBoundingClientRect().right,
      bottom:footer.getBoundingClientRect().top,scrollerBottom:document.querySelector("#worldScroll").getBoundingClientRect().bottom,
      topbarTop:document.querySelector(".topbar").getBoundingClientRect().top,
      genre:!!document.querySelector("#shelf-genres")};
  });
  check("354 portrait font 1.3 + 24px insets no rail/shelf overlap",m.gap>=8&&m.width>100&&m.bottom>=m.scrollerBottom-1&&m.topbarTop>=24,m);
  const scBefore=await page.$eval("#worldScroll",e=>e.scrollTop);
  await page.click('[data-go-row="completed-in-five-volumes-or-fewer"]');
  await new Promise(r=>setTimeout(r,550));
  const scAfter=await page.$eval("#worldScroll",e=>e.scrollTop);
  check("Long Manga row index scrolls target",scAfter>scBefore+100,{before:scBefore,after:scAfter});
  await page.goto(base("comics")+"?capture=1&vp=900x412&rail=open",{waitUntil:"load"});
  const c=await page.evaluate(()=>({wide:getComputedStyle(document.querySelector(".world-rail")).display!=="none",
    bottom:getComputedStyle(document.querySelector(".bottom-tabs")).display==="none",
    explore:!!document.querySelector("#shelf-explore"),rowCount:document.querySelectorAll(".shelf-stack .shelf").length,
    gap:document.querySelector(".shelf-stack").getBoundingClientRect().left-document.querySelector(".index-rail").getBoundingClientRect().right}));
  check("900 landscape uses wide world rail and preserved Explore Comics",c.wide&&c.bottom&&c.explore&&c.rowCount>=15&&c.gap>=8,c);
  await page.close();
 }finally{await browser.close();fs.rmSync(profile,{recursive:true,force:true});}
 const out=path.join(root,"interaction-audit.json");fs.writeFileSync(out,JSON.stringify({command:"node interaction-smoke.js",results},null,2)+"\n");
 console.log(JSON.stringify({checks:results.length,failures:results.filter(r=>!r.pass),audit:out}));
 if(results.some(r=>!r.pass))process.exitCode=2;
})().catch(e=>{console.error("SMOKE_FAILED "+e.stack);process.exitCode=1;});
