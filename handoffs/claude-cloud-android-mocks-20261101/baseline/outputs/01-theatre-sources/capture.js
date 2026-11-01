"use strict";
// Reproducible isolated headless captures; never touches a shared browser profile.
const fs=require("fs"),path=require("path"),http=require("http");
const puppeteer=require("C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer");
const root=path.resolve(__dirname,"../.."),out=__dirname,captures=path.join(out,"captures");
const profile=path.join(captures,".temporary-profile");
const chrome="C:/Program Files/Google/Chrome/Application/chrome.exe";
const mime={".html":"text/html;charset=utf-8",".css":"text/css;charset=utf-8",".js":"application/javascript;charset=utf-8",".png":"image/png",".jpg":"image/jpeg",".svg":"image/svg+xml",".ttf":"font/ttf",".otf":"font/otf",".json":"application/json"};
const server=http.createServer((req,res)=>{
  let name;
  try {name=path.resolve(root,"."+decodeURIComponent(new URL(req.url,"http://localhost").pathname))}
  catch {res.writeHead(400);res.end();return}
  if(!name.startsWith(root+path.sep)&&name!==root){res.writeHead(403);res.end();return}
  fs.stat(name,(err,stats)=>{
    if(err||!stats.isFile()){res.writeHead(404);res.end();return}
    res.writeHead(200,{"Content-Type":mime[path.extname(name).toLowerCase()]||"application/octet-stream","Cache-Control":"no-store"});
    fs.createReadStream(name).pipe(res);
  });
});
const tests=[
  ["film","portrait",412,900,""],
  ["film","landscape",900,412,""],
  ["film","desktop",1280,700,""],
  ["film","small-portrait",354,792,""],
  ["film","small-landscape",792,354,""],
  ["film","tablet-portrait",800,1280,""],
  ["film","provider-open",412,900,"&menu=1"],
  ["film","large-font",354,792,"&scale=1.3"],
  ["episode","portrait",412,900,""],
  ["episode","landscape",900,412,""],
  ["episode","desktop",1280,700,""],
  ["episode","small-portrait",354,792,""],
  ["episode","small-landscape",792,354,""],
  ["episode","tablet-landscape",1280,800,""],
  ["episode","download",412,900,"&mode=download"],
  ["episode","season",900,412,"&mode=season"],
  ["episode","insets",412,900,"&insetTop=24"],
  ["film","closed",412,900,"&closed=1"],
  ["episode","context-open",412,900,"&context=1"],
  ["episode","quality-filter",354,792,"&quality=1080p"],
  ["film","provider-filter",412,900,"&provider=NoTorrent"],
  ["episode","large-font-landscape",792,354,"&scale=1.3"]
];
const diagnostics=[],interactions={};
async function main(){
  fs.mkdirSync(captures,{recursive:true});
  await new Promise((resolve,reject)=>server.listen(0,"127.0.0.1",resolve).once("error",reject));
  const origin="http://127.0.0.1:"+server.address().port;
  const browser=await puppeteer.launch({executablePath:chrome,headless:true,userDataDir:profile,
    args:["--no-first-run","--no-default-browser-check","--disable-extensions","--disable-background-networking","--disable-sync","--disable-popup-blocking"]});
  try{
    for(const [kind,label,w,h,extra] of tests){
      const page=await browser.newPage();
      await page.setViewport({width:w,height:h,deviceScaleFactor:1});
      const errors=[],missing=[];
      page.on("pageerror",e=>errors.push(String(e)));
      page.on("response",r=>{if(r.status()>=400)missing.push(r.status()+" "+r.url())});
      await page.goto(origin+"/outputs/01-theatre-sources/"+kind+"-sources.html?render=1&w="+w+"&h="+h+extra,{waitUntil:"load",timeout:60000});
      await page.evaluate(()=>document.fonts.ready);
      const actual=await page.evaluate(async()=>{
        const $=id=>document.getElementById(id),rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,right:r.right,bottom:r.bottom}};
        const frame=$("viewport"),table=document.querySelector(".source-table"),scroller=$("rows");
        const before={frame:rect(frame),table:rect(table),list:rect(scroller),rows:scroller.querySelectorAll(".source-row").length,
          phone:frame.classList.contains("is-phone"),landscape:frame.classList.contains("is-landscape"),
          overflowX:frame.scrollWidth-frame.clientWidth,tableOverflowX:table.scrollWidth-table.clientWidth,
          menuOpen:!$("providerMenu").hidden,fonts:document.fonts.check('14px HouseSegoe')&&document.fonts.check('16px Fraunces'),
          negativeSeederText:scroller.textContent.includes("-1 seeders")};
        let unreachable=0;
        for(const btn of [...scroller.querySelectorAll(".circle")]){
          btn.scrollIntoView({block:"nearest",inline:"nearest"});
          const r=btn.getBoundingClientRect(),parent=scroller.getBoundingClientRect();
          if(r.bottom<=parent.top+1 || r.top>=parent.bottom-1 || r.right>parent.right+2 || r.left<parent.left-2) unreachable++;
        }
        before.unreachableButtons=unreachable;scroller.scrollTop=0;
        return before;
      });
      const name=kind+"-"+label+".png";
      await page.screenshot({path:path.join(captures,name),fullPage:false,animations:"disabled"});
      const result={file:name,viewport:[w,h],mode:extra||"default",...actual,errors,missing};
      diagnostics.push(result);
      console.log(name+" "+JSON.stringify({rows:actual.rows,tableH:Math.round(actual.table.h),
        listH:Math.round(actual.list.h),unreachable:actual.unreachableButtons,errors,missing}));
      await page.close();
    }
    const page=await browser.newPage();
    await page.setViewport({width:412,height:900,deviceScaleFactor:1});
    await page.goto(origin+"/outputs/01-theatre-sources/film-sources.html?render=1&w=412&h=900",{waitUntil:"load",timeout:60000});
    await page.click('[data-quality="1080p"]');
    interactions.qualityFiltered=await page.$eval("#tableCount",e=>e.textContent);
    await page.click("#providerTrigger");
    interactions.dropdownOpen=await page.$eval("#providerMenu",e=>!e.hidden);
    await page.click('[data-provider="NoTorrent"]');
    interactions.providerFiltered=await page.$eval("#tableCount",e=>e.textContent);
    await page.click('[data-act="copy"]');
    interactions.copyTick=await page.$eval('[data-act="copy"]',e=>e.textContent.trim());
    await page.click("#backAction");
    interactions.backClosed=await page.$eval("#sourceSheet",e=>e.hidden);
    await page.click("#reopen");
    interactions.reopenWorked=await page.$eval("#sourceSheet",e=>!e.hidden);
    await page.goto(origin+"/outputs/01-theatre-sources/episode-sources.html?render=1&w=412&h=900&mode=season",{waitUntil:"load",timeout:60000});
    interactions.seasonCount=await page.$eval("#tableCount",e=>e.textContent);
    interactions.seasonOnlyFullPacks=await page.$$eval(".source-row",a=>a.every(e=>e.textContent.includes("FULL SEASON")));
    await page.goto(origin+"/outputs/01-theatre-sources/episode-sources.html?render=1&w=412&h=900&mode=download",{waitUntil:"load",timeout:60000});
    await page.click('[data-act="activate"]');
    interactions.downloadPickCloses=await page.$eval("#sourceSheet",e=>e.hidden);
    await page.close();
    for(const d of diagnostics){
      if(d.errors.length||d.missing.length||d.frame.w!==d.viewport[0]||d.frame.h!==d.viewport[1]||(!d.mode.includes("closed=1")&&(d.unreachableButtons||d.negativeSeederText||d.table.h<80)))
        throw new Error("Layout/capture check failed: "+JSON.stringify(d));
    }
    if(interactions.qualityFiltered!=="3 sources"||!interactions.dropdownOpen||interactions.providerFiltered!=="2 sources"||
       interactions.copyTick!=="✓"||!interactions.backClosed||!interactions.reopenWorked||
       interactions.seasonCount!=="4 sources"||!interactions.seasonOnlyFullPacks||!interactions.downloadPickCloses)
      throw new Error("Interaction check failed: "+JSON.stringify(interactions));
    console.log("INTERACTIONS_OK "+JSON.stringify(interactions));
  } finally {await browser.close()}
}
main().then(()=>{
  fs.writeFileSync(path.join(captures,"verification.json"),JSON.stringify({command:"node capture.js",browser:"installed Chrome via Puppeteer",root,interactions,diagnostics},null,2)+"\n");
  console.log("CAPTURE_OK "+diagnostics.length);
}).catch(e=>{console.error("CAPTURE_FAILED",e.stack||e);process.exitCode=1})
.finally(()=>{server.close();try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:4,retryDelay:250})}catch(e){console.error("PROFILE_CLEANUP_ERROR",e.message)}});

