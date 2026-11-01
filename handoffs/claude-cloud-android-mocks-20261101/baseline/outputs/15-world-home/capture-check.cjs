"use strict";
// Isolated headless verification only. Uses an existing local Puppeteer installation;
// does not drive the shared browser or change any application files.
const fs=require("fs");
const path=require("path");
const {pathToFileURL}=require("url");
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const root=__dirname, captures=path.join(root,"captures");
const profile=path.join(captures,".headless-profile-"+process.pid);
const views=[
 {key:"portrait",w:412,h:900},
 {key:"landscape",w:900,h:412},
 {key:"desktop",w:1280,h:700},
 {key:"compact-portrait",w:354,h:792},
 {key:"compact-landscape",w:792,h:354},
 {key:"tablet-portrait",w:800,h:1280},
 {key:"tablet-landscape",w:1280,h:800}
];
const files=["theatre-home.html","tankoban-home.html","universes-home.html"];
const report={command:"node capture-check.cjs",browser:"Google Chrome (isolated Puppeteer headless)",views:views,results:[],interactionChecks:[],error:[]};
async function main(){
 const browser=await puppeteer.launch({
  executablePath:"C:/Program Files/Google/Chrome/Application/chrome.exe",
  headless:true,
  timeout:55000,
  userDataDir:profile,
  args:["--no-first-run","--no-default-browser-check","--disable-extensions","--disable-background-networking","--no-proxy-server"]
 });
 try{
  for(const file of files){
   const folder=path.join(captures,path.basename(file,".html"));
   fs.mkdirSync(folder,{recursive:true});
   for(const v of views){
    const page=await browser.newPage();
    const errors=[];
    page.on("pageerror",e=>errors.push(String(e)));
    await page.setViewport({width:v.w,height:v.h,deviceScaleFactor:1,isMobile:false});
    await page.goto(pathToFileURL(path.join(root,file)).href+"?capture=1",{waitUntil:"load",timeout:30000});
    await page.evaluate(async()=>{
     await document.fonts.ready;
     await Promise.race([Promise.all(Array.from(document.images).map(img=>img.complete
       ? Promise.resolve()
       : new Promise(resolve=>{img.addEventListener("load",resolve,{once:true});img.addEventListener("error",resolve,{once:true});}))),new Promise(resolve=>setTimeout(resolve,1900))]);
    });
    const geometry=await page.evaluate(()=>{
     const app=document.querySelector(".app"),bar=document.querySelector(".topbar"),left=document.querySelector(".left-chrome"),right=document.querySelector(".action-row"),hero=document.querySelector(".featured"),scroll=document.querySelector(".world-scroll");
     const box=x=>x?x.getBoundingClientRect():null;const a=box(left),b=box(right),pills=box(document.querySelector(".world-pills"));
     const broken=Array.from(document.querySelectorAll(".app img")).filter(im=>!im.complete||im.naturalWidth===0).map(im=>im.getAttribute("src"));
     const end=document.querySelector(".bottom-world-nav");
     const far=Array.from(document.querySelectorAll(".board *")).filter(e=>{const r=e.getBoundingClientRect();return !e.closest(".poster-rail,.rank-strip,.picker-menu")&&r.width>0&&r.right>innerWidth+3&&r.left>0;}).slice(0,8).map(e=>e.tagName.toLowerCase()+"."+(e.className||""));
     return {mode:app?.className,bodyWidth:document.documentElement.scrollWidth,appWidth:app?.clientWidth,appScrollWidth:app?.scrollWidth,scrollWidth:scroll?.scrollWidth,scrollClientWidth:scroll?.clientWidth,
      topbarOverlap:!!(a&&b&&a.right>b.left-2),pillsOverlap:!!(pills&&((a&&a.right>pills.left-3)||(b&&b.left<pills.right+3))),
      hero:hero?{left:Math.round(hero.getBoundingClientRect().left),right:Math.round(hero.getBoundingClientRect().right),height:Math.round(hero.getBoundingClientRect().height)}:null,
      bottomNav:!!end,brokenImages:broken,escaped:far,sourceWidth:document.querySelector(".source-rail")?.getBoundingClientRect().width||0};
    });
    const out=path.join(folder,v.key+".png");
    await page.screenshot({path:out,type:"png",captureBeyondViewport:false});
    report.results.push({file:file,viewport:v.key,width:v.w,height:v.h,path:path.relative(root,out),...geometry,pageErrors:errors});
    if(v.key==="portrait"){
     await page.click('[data-action="slide"][data-index="1"]');
     const changed=await page.$eval(".hero-title",e=>e.textContent);
     const original=path.basename(file,".html");
     report.interactionChecks.push({file:file,test:"carousel dot selects second slide",observed:changed});
     if(file!=="universes-home.html"){
      await page.evaluate(()=>document.querySelector(".world-scroll").scrollTop=900);
      await page.click('[data-action="source-toggle"]');
      const open=await page.$eval(".source-rail",e=>e.classList.contains("open"));
      report.interactionChecks.push({file:file,test:"existing source rail expansion",passed:open});
     }else{
      await page.evaluate(()=>document.querySelector(".world-scroll").scrollTop=650);
      await page.click('[data-action="facet"][data-value="books"]');
      const count=await page.$$eval(".universe-banner",all=>all.length);
      report.interactionChecks.push({file:file,test:"Books facet filters list",visibleBanners:count});
     }
    }
    await page.close();
    process.stdout.write(file+" "+v.key+" "+v.w+"x"+v.h+" broken="+geometry.brokenImages.length+" overlap="+geometry.topbarOverlap+" pills="+geometry.pillsOverlap+"\n");
   }
  }
 }finally{
  fs.writeFileSync(path.join(captures,"verification.json"),JSON.stringify(report,null,2),"utf8");
  const launched=browser.process();
  browser.disconnect();
  if(launched&&!launched.killed)launched.kill();
 }
}
main().catch(e=>{report.error.push(String(e&&e.stack||e));process.stderr.write(String(e&&e.stack||e)+"\n");process.exitCode=1}).finally(()=>{
 fs.writeFileSync(path.join(captures,"verification.json"),JSON.stringify(report,null,2),"utf8");
 try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:5,retryDelay:100})}catch(e){process.stderr.write("profile cleanup: "+String(e)+"\n");}
});

