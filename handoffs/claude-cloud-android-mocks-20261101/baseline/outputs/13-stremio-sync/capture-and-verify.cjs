"use strict";
// Isolated 127.0.0.1 server + headless Chrome. No shared browser/profile, provider calls, or external network.
const fs = require("node:fs");
const path = require("node:path");
const http = require("node:http");
const puppeteer = require(require.resolve("puppeteer", {
  paths: ["C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Brotherhood-Desktop-Commander"]
}));
const root = path.resolve(__dirname, "../..");
const captures = path.join(__dirname, "captures");
const profile = path.join(captures, ".isolated-chrome-profile");
const mime = {".html":"text/html; charset=utf-8",".css":"text/css; charset=utf-8",
  ".js":"application/javascript; charset=utf-8",".svg":"image/svg+xml",
  ".ttf":"font/ttf",".otf":"font/otf",".png":"image/png"};
const cases = [
  {name:"portrait", width:412, height:900, params:"capture=portrait"},
  {name:"landscape", width:900, height:412, params:"capture=landscape"},
  {name:"desktop", width:1280, height:700, params:"capture=desktop"},
  {name:"small-portrait", width:354, height:792, params:"capture=smallPortrait"},
  {name:"small-landscape", width:792, height:354, params:"capture=smallLandscape"},
  {name:"tablet-portrait", width:800, height:1280, params:"capture=tabletPortrait"},
  {name:"tablet-landscape", width:1280, height:800, params:"capture=tabletLandscape"},
  {name:"switch-confirm-portrait", width:354, height:792,
    params:"capture=smallPortrait&state=synced&confirm=switch&long=1"},
  {name:"disconnect-confirm-landscape", width:900, height:412,
    params:"capture=landscape&state=synced&confirm=disconnect&long=1"},
  {name:"keyboard-portrait", width:412, height:900,
    params:"capture=portrait&state=reconnectRequired&long=1&keyboard=1&inset=24&font=1.3"},
  {name:"keyboard-landscape", width:792, height:354,
    params:"capture=smallLandscape&state=reconnectRequired&long=1&keyboard=1&inset=24&font=1.3&ime=140"},
  {name:"closed-portrait", width:412, height:900, params:"capture=portrait&closed=1"}
];
function assetServer() {
  return http.createServer((req,res)=>{
    try {
      const relative = decodeURIComponent(new URL(req.url,"http://127.0.0.1").pathname).replace(/^\/+/,"");
      const target = path.resolve(root,relative);
      if (!target.startsWith(root+path.sep)) { res.writeHead(403); res.end("Forbidden"); return; }
      if (!fs.statSync(target).isFile()) { res.writeHead(404); res.end("Not found"); return; }
      res.writeHead(200,{"Content-Type":mime[path.extname(target).toLowerCase()]||"application/octet-stream",
        "Cache-Control":"no-store","X-Content-Type-Options":"nosniff"});
      fs.createReadStream(target).pipe(res);
    } catch (_) { res.writeHead(404); res.end("Not found"); }
  });
}
function assertCheck(condition,message,errors) { if (!condition) errors.push(message); }
async function auditPage(page,def,errors) {
  const payload = await page.evaluate(()=>{
    const x=window.__stremioMockAudit();
    const panel=document.getElementById("panel"), frame=document.getElementById("frame");
    const pr=panel.getBoundingClientRect(),fr=frame.getBoundingClientRect();
    const overlay=document.getElementById("overlay");
    const st=getComputedStyle(frame);
    const safe={top:parseFloat(st.getPropertyValue("--inset-top"))||0,
      bottom:parseFloat(st.getPropertyValue("--inset-bottom"))||0,
      left:parseFloat(st.getPropertyValue("--inset-left"))||0,
      right:parseFloat(st.getPropertyValue("--inset-right"))||0,
      ime:parseFloat(st.getPropertyValue("--ime-height"))||0};
    let actionsAccessible=true;
    const scroll=document.getElementById("panelScroll");
    for(const id of ["primaryAction","switchAction","disconnectAction","cancelAction","headerClose"]) {
      const b=document.getElementById(id),cs=getComputedStyle(b);
      if(b.hidden||cs.display==="none")continue;
      b.scrollIntoView({block:"nearest",inline:"nearest"});
      const r=b.getBoundingClientRect(), sr=scroll.getBoundingClientRect();
      if(r.width<28||r.height<42||r.right>pr.right+1||r.left<pr.left-1)
        actionsAccessible=false;
      if(r.top<sr.top-1||r.bottom>sr.bottom+1)actionsAccessible=false;
    }
    scroll.scrollTop=0;
    return {audit:x,safe,closed:overlay.hidden,
      panelInsideSafe:pr.left>=fr.left+safe.left-1 && pr.right<=fr.right-safe.right+1
        && pr.top>=fr.top+safe.top-1 && pr.bottom<=fr.bottom-safe.bottom-safe.ime+1,
      actionsAccessible,
      fonts:{fraunces:document.fonts.check("16px FrauncesMock"),
        segoe:document.fonts.check("16px SegoeMock")},
      heading:document.getElementById("panelHeading").textContent,
      primary:document.getElementById("primaryAction").textContent};
  });
  if(!payload.closed) {
    assertCheck(payload.panelInsideSafe,"panel escapes safe/IME area",errors);
    assertCheck(payload.actionsAccessible,"action not accessible at bottom of scroll",errors);
    assertCheck(!payload.audit.horizontalOverflow,"horizontal panel scroll overflow",errors);
    for(const a of payload.audit.actions) {
      assertCheck(a.insidePanel,"button horizontally clipped: "+a.id,errors);
      assertCheck(a.h>=42,"button height too small: "+a.id,errors);
    }
  }
  assertCheck(payload.audit.frame.w===def.width && payload.audit.frame.h===def.height,
    "frame differs from configured viewport",errors);
  assertCheck(payload.fonts.fraunces && payload.fonts.segoe,"approved fonts failed to load",errors);
  return payload;
}
async function clickSequence(browser,baseUrl,errors) {
  const context=await browser.createBrowserContext();
  const page=await context.newPage();
  await page.setViewport({width:412,height:900,deviceScaleFactor:1});
  await page.goto(baseUrl+"?capture=portrait&state=synced",{waitUntil:"networkidle0"});
  await page.click("#switchAction");
  assertCheck((await page.$eval("#primaryAction",x=>x.textContent))==="Confirm switch","switch confirmation not shown",errors);
  await page.click("#cancelAction");
  assertCheck((await page.$eval("#primaryAction",x=>x.textContent))==="Sync now","cancel did not restore actions",errors);
  await page.click("#disconnectAction");
  await page.click("#primaryAction");
  assertCheck((await page.$eval("#panelHeading",x=>x.textContent))==="Connect Stremio","disconnect simulation did not return to unconnected state",errors);
  await page.click("#primaryAction");
  assertCheck((await page.$eval("#panelHeading",x=>x.textContent))==="Connecting to Stremio","connect simulation failed",errors);
  assertCheck(await page.$eval("#primaryAction",x=>x.disabled),"connecting must disable primary",errors);
  await page.click("#cancelAction");
  assertCheck(await page.$eval("#overlay",x=>x.hidden),"close did not hide original overlay",errors);
  await page.click("#openSettings");
  assertCheck(!(await page.$eval("#overlay",x=>x.hidden)),"Open settings failed to restore overlay",errors);
  assertCheck(await page.evaluate(()=>document.activeElement.tagName!=="INPUT"),"keyboard-like field auto-focused",errors);
  await context.close();
}
async function main() {
  fs.mkdirSync(captures,{recursive:true});
  const server=assetServer();
  await new Promise((resolve,reject)=>server.once("error",reject).listen(0,"127.0.0.1",resolve));
  const address=server.address();
  const base="http://127.0.0.1:"+address.port+"/outputs/13-stremio-sync/stremio-sync.html";
  let browser;
  const records=[];
  const overallErrors=[];
  try {
    browser=await puppeteer.launch({headless:true,
      executablePath:"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
      userDataDir:profile,
      args:["--no-first-run","--no-default-browser-check","--disable-extensions",
        "--disable-background-networking","--disable-sync"]});
    for(const def of cases) {
      const context=await browser.createBrowserContext();
      const page=await context.newPage();
      await page.setViewport({width:def.width,height:def.height,deviceScaleFactor:1});
      const errors=[];
      const failedRequests=[];
      page.on("pageerror",err=>errors.push("browser JS: "+String(err)));
      await page.setRequestInterception(true);
      page.on("request",request=>{
        if(!request.url().startsWith("http://127.0.0.1:"+address.port+"/")) {
          failedRequests.push("external request blocked "+request.url());
          request.abort();
        } else request.continue();
      });
      const resp=await page.goto(base+"?"+def.params,{waitUntil:"networkidle0",timeout:20000});
      assertCheck(resp && resp.status()===200,"local HTTP page not served",errors);
      await page.evaluate(()=>document.fonts.ready);
      const audit=await auditPage(page,def,errors);
      await page.screenshot({path:path.join(captures,def.name+".png"),fullPage:false});
      if (def.name.startsWith("keyboard-")) {
        await page.evaluate(()=>document.getElementById("cancelAction").scrollIntoView({block:"nearest"}));
        await page.screenshot({path:path.join(captures,def.name+"-actions.png"),fullPage:false});
      }
      const record={case:def.name,viewport:{width:def.width,height:def.height,deviceScaleFactor:1},
        query:def.params,caption:audit.heading,primary:audit.primary,
        checks:{panelInsideSafe:audit.panelInsideSafe,actionsAccessible:audit.actionsAccessible,
          horizontalOverflow:audit.audit.horizontalOverflow,loadedFonts:audit.fonts,
          scrollablePx:audit.audit.availableScroll,closed:audit.closed},
        requestsBlocked:failedRequests,errors};
      records.push(record);
      if(errors.length||failedRequests.length) overallErrors.push(def.name+": "+errors.concat(failedRequests).join("; "));
      console.log((errors.length?"FAIL ":"PASS ")+def.name+" "+def.width+"x"+def.height+
        " panel="+JSON.stringify(audit.audit.panel)+
        " scroll="+audit.audit.availableScroll+" errors="+errors.length);
      await context.close();
    }
    await clickSequence(browser,base,overallErrors);
  } finally {
    if(browser) await browser.close();
    await new Promise(resolve=>server.close(resolve));
    fs.rmSync(profile,{recursive:true,force:true});
  }
  const receipt={command:"node capture-and-verify.cjs",captureTool:"Puppeteer with installed Chrome, isolated headless browser contexts and ephemeral 127.0.0.1 HTTP server",cases:records,
    interactionCheck:"switch/cancel/disconnect/connect/close/reopen; no backend operations",
    failures:overallErrors,passed:overallErrors.length===0};
  fs.writeFileSync(path.join(captures,"audit.json"),JSON.stringify(receipt,null,2)+"\n");
  console.log("RECEIPT "+path.join(captures,"audit.json"));
  if(overallErrors.length){ console.error(overallErrors.join("\n")); process.exitCode=1; }
}
main().catch(err=>{console.error(err.stack||err);process.exitCode=1;});
