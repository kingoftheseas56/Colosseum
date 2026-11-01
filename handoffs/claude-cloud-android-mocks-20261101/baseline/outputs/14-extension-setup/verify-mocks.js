"use strict";
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const fs=require("fs"),path=require("path"),{pathToFileURL}=require("url");
const root=__dirname,cap=path.join(root,"captures");
const checks=[],errors=[];
function check(condition,name,detail){checks.push({name,pass:!!condition,detail});if(!condition)console.error("FAILED",name,detail);}
(async()=>{
 const browser=await puppeteer.launch({executablePath:"C:/Program Files/Google/Chrome/Application/chrome.exe",headless:true,userDataDir:path.join(cap,".isolated-interactions-profile"),args:["--no-first-run","--disable-background-networking","--disable-extensions"]});
 async function open(file,width,height,args){
  const p=await browser.newPage();p.on("pageerror",e=>errors.push(String(e)));p.on("request",request=>{if(/^https?:/i.test(request.url()))errors.push("Unexpected network request "+request.url());});
  await p.setViewport({width,height,deviceScaleFactor:1});
  await p.goto(pathToFileURL(path.join(root,file)).href+"?capture=1&"+args,{waitUntil:"load"});
  await p.evaluate(()=>document.fonts.ready);
  return p;
 }
 try{
  const tank=await open("tankoyomi-configuration.html",412,900,"preset=portrait&language=es");
  check((await tank.$$("[data-provider]")).length===4,"Populated Spanish provider ladder","4 illustrative sources");
  await tank.click("#default-selector");
  check((await tank.$eval("#default-options",el=>el.hidden))===false,"Original inline language menu opens");
  await tank.click('[data-default="en"]');
  check(await tank.$eval("#default-name",el=>el.textContent)==="English","Default language can change");
  check((await tank.$$("[data-provider]")).length===1,"Language-specific providers refresh");
  await tank.click('[data-language="es"]');
  check((await tank.$$("[data-provider]")).length===4,"Language list changes selected route");
  await tank.click('[data-move="niadd"][data-dir="-1"]');
  check(await tank.$eval(".provider-item",el=>el.dataset.provider)==="niadd","Move up changes ladder");
  await tank.click("#reset-order");
  check(await tank.$eval(".provider-item",el=>el.dataset.provider)==="zonatmo","Reset restores source order");
  await tank.click('[data-toggle="zonatmo"]');
  check(await tank.$eval('[data-toggle="zonatmo"]',el=>el.getAttribute("aria-checked"))==="false","Provider switch toggles");
  await tank.click("#master-switch");
  check(await tank.$eval("#master-description",el=>el.textContent)==="Tankoyomi is off","Master switch toggles");
  await tank.click('[data-tab="about"]');
  check(!(await tank.$eval("#about-panel",el=>el.hidden)),"About tab shows preserved copy");
  await tank.click('[data-tab="configuration"]');
  await tank.evaluate(()=>document.querySelector('[data-provider="leercapitulo"]').scrollIntoView({block:"center",behavior:"instant"}));
  await tank.screenshot({path:path.join(cap,"tankoyomi-configuration-provider-portrait.png")});
  const rect=await tank.evaluate(()=>{const e=document.querySelector('[data-provider="leercapitulo"]'),s=document.querySelector("#tank-scroll"),b=e.getBoundingClientRect(),r=s.getBoundingClientRect();return {top:b.top,bottom:b.bottom,clipTop:r.top,clipBottom:r.bottom,controls:[...e.querySelectorAll("button")].map(x=>({w:x.getBoundingClientRect().width,h:x.getBoundingClientRect().height}))};});
  check(rect.bottom>rect.clipTop&&rect.top<rect.clipBottom,"Provider visible after scroll",rect);
  check(rect.controls.every(x=>x.w>=44&&x.h>=44),"Phone provider controls have 44px touch hit boxes",rect.controls);
  await tank.close();
  for(const [name,w,h,query] of [["landscape",900,412,"preset=landscape"],["short",792,354,"preset=smallLandscape"],["small",354,792,"preset=small&scale=1.3&insetTop=24&insetBottom=24"]]){
   const p=await open("tankoyomi-configuration.html",w,h,query);
   await p.evaluate(()=>document.querySelector('[data-provider="mangasins"]').scrollIntoView({block:"center",behavior:"instant"}));
   await p.screenshot({path:path.join(cap,"tankoyomi-configuration-provider-"+name+".png")});
   const r=await p.evaluate(()=>{const x=document.querySelector('[data-provider="mangasins"]'),s=document.querySelector("#tank-scroll");return {screen:s.getBoundingClientRect().toJSON(),provider:x.getBoundingClientRect().toJSON(),widths:[...x.querySelectorAll("button")].map(b=>b.getBoundingClientRect().width),overflow:s.scrollWidth-s.clientWidth};});
   check(r.provider.bottom>r.screen.top&&r.provider.top<r.screen.bottom,"Scrolled provider reachable "+name,r);
   check(r.overflow<=2,"No horizontal scroll "+name,r.overflow);
   check(r.widths.every(x=>x>=44),"Action hit widths "+name,r.widths);
   await p.close();
  }
  const setup=await open("extension-setup.html",412,900,"preset=portrait");
  await setup.type("#paste-input","stremio://sample/manifest.json");
  check(await setup.$eval("#paste-button",el=>el.textContent)==="Install","Paste changes original button to Install");
  await setup.click("#paste-button");
  check(await setup.$eval("#setup-status",el=>el.textContent)==="Adding…","Local-only installation intent status");
  await setup.click("[data-close-setup]");
  check(await setup.$eval("#setup-shade",el=>el.hidden),"Close returns to Store context");
  await setup.select("#setup-state","open");
  check(!(await setup.$eval("#setup-shade",el=>el.hidden)),"Closed panel reopens from inspector");
  await setup.close();
  const shortSetup=await open("extension-setup.html",792,354,"preset=smallLandscape&keyboard=1&ime=150");
  await shortSetup.evaluate(()=>document.querySelector("#paste-input").scrollIntoView({block:"center",behavior:"instant"}));
  await shortSetup.screenshot({path:path.join(cap,"extension-setup-short-keyboard-focused.png")});
  const fit=await shortSetup.evaluate(()=>{const input=document.querySelector("#paste-input").getBoundingClientRect(),panel=document.querySelector(".phone-setup").getBoundingClientRect();return {input:input.toJSON(),panel:panel.toJSON(),viewportHeight:document.querySelector("#viewport").clientHeight};});
  check(fit.input.top>=fit.panel.top-2&&fit.input.bottom<=fit.panel.bottom+2,"Focused link field can be brought into short keyboard viewport",fit);
  await shortSetup.close();
 } finally {await browser.close();}
 fs.writeFileSync(path.join(cap,"interaction-audit.json"),JSON.stringify({tool:"Installed Puppeteer, isolated headless Chrome",command:'node '+path.join(root,"verify-mocks.js"),checks,errors},null,2));
 console.log(JSON.stringify({passed:checks.filter(x=>x.pass).length,total:checks.length,errors,failed:checks.filter(x=>!x.pass)}));
 if(checks.some(x=>!x.pass)||errors.length)process.exitCode=1;
})().catch(e=>{console.error("INTERACTION_FAILED",e.stack||e);process.exitCode=1});