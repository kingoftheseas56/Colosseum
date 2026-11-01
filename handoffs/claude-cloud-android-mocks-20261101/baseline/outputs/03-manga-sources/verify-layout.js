"use strict";
// Headless Chrome DevTools Protocol only: isolated process/profile, no shared desktop input.
const fs=require("node:fs"),path=require("node:path"),http=require("node:http"),{spawn,execFileSync}=require("node:child_process");
const root=__dirname,chromeExe="C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
const profile=path.join(root,"captures",".chrome-inspection-profile");
const scenarios=[
 {label:"portrait",width:412,height:900,scene:"sources",save:true},
 {label:"landscape",width:900,height:412,scene:"sources",save:true},
 {label:"desktop",width:1280,height:700,scene:"sources",save:true},
 {label:"small-portrait",width:354,height:792,scene:"sources",save:true},
 {label:"small-landscape",width:792,height:354,scene:"sources",save:true},
 {label:"disabled",width:412,height:900,scene:"disabled",save:true},
 {label:"batch",width:412,height:900,scene:"sources",context:"batch",save:true},
 {label:"series",width:412,height:900,scene:"sources",context:"series",save:true},
 {label:"live",width:412,height:900,scene:"downloading",save:true},
 {label:"long-130",width:354,height:792,scene:"long",scale:1.3,top:24,bottom:24,save:true},
 {label:"landscape-long-130",width:792,height:354,scene:"long",scale:1.3,top:24,bottom:24,save:true},
 {label:"tablet",width:800,height:1280,scene:"sources",save:true},
 {label:"tablet-landscape",width:1280,height:800,scene:"sources",save:true}
];
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const fetchJson=(port,url)=>new Promise((resolve,reject)=>{
 http.get({hostname:"127.0.0.1",port,path:url,timeout:2000},res=>{
  let s="";res.on("data",chunk=>s+=chunk);res.on("end",()=>{try{resolve(JSON.parse(s));}catch(e){reject(e);}});
 }).on("error",reject);
});
async function findPort(){
 for(let n=0;n<80;n++){
  try{
   const file=path.join(profile,"DevToolsActivePort");
   if(fs.existsSync(file))return Number(fs.readFileSync(file,"utf8").split(/\r?\n/)[0]);
  }catch(_){}
  await sleep(150);
 }
 throw Error("Chrome DevToolsActivePort not found");
}
function connect(url){
 return new Promise((resolve,reject)=>{
  const ws=new WebSocket(url);const pending=new Map();let seq=0;
  ws.addEventListener("error",reject,{once:true});
  ws.addEventListener("open",()=>resolve({
   send(method,params={}){
    const id=++seq;
    return new Promise((yes,no)=>{pending.set(id,{yes,no});ws.send(JSON.stringify({id,method,params}));});
   },
   close(){ws.close();}
  }),{once:true});
  ws.addEventListener("message",ev=>{
   let msg;try{msg=JSON.parse(String(ev.data));}catch(e){return;}
   if(!msg.id)return;
   const p=pending.get(msg.id);if(!p)return;pending.delete(msg.id);
   if(msg.error)p.no(Error(msg.error.message));else p.yes(msg.result);
  });
 });
}
const auditExpression=`(async()=>{
 await document.fonts.ready;
 const box=el=>{if(!el)return null;const r=el.getBoundingClientRect();return {x:+r.x.toFixed(1),y:+r.y.toFixed(1),w:+r.width.toFixed(1),h:+r.height.toFixed(1),right:+r.right.toFixed(1),bottom:+r.bottom.toFixed(1)}};
 const rows=[...document.querySelectorAll(".source-row")];
 return {
  ready:document.documentElement.dataset.mockReady||"absent",
  screen:[window.innerWidth,window.innerHeight],
  fonts:document.fonts.status,
  appClass:document.getElementById("app").className,
  hero:box(document.querySelector(".hero")),back:box(document.querySelector(".back")),
  heroCopy:box(document.querySelector(".hero-copy")),
  glass:box(document.querySelector(".glass")),header:box(document.querySelector(".table-head")),
  scrollArea:box(document.querySelector(".result-viewport")),
  viewportScrollHeight:document.querySelector(".result-viewport").scrollHeight,
  viewportClientHeight:document.querySelector(".result-viewport").clientHeight,
  empty:document.getElementById("emptyText").textContent,
  rows:rows.map((r,i)=>({i,row:box(r),tile:box(r.querySelector(".source-tile")),body:box(r.querySelector(".source-body")),action:box(r.querySelector(".source-action")),scrollWidth:r.scrollWidth,clientWidth:r.clientWidth})),
  scrollWidth:document.documentElement.scrollWidth,appScrollWidth:document.getElementById("app").scrollWidth
 };
})()`;
let child,cdp,report=[];
(async()=>{
 try{
  fs.mkdirSync(path.join(root,"captures"),{recursive:true});
  fs.rmSync(profile,{recursive:true,force:true});fs.mkdirSync(profile,{recursive:true});
  child=spawn(chromeExe,["--headless=new","--no-first-run","--no-default-browser-check","--disable-extensions","--disable-background-networking","--remote-debugging-port=0", "--user-data-dir="+profile,"about:blank"],{stdio:"ignore",windowsHide:true});
  const port=await findPort();
  const pages=await fetchJson(port,"/json/list");
  const target=pages.find(p=>p.type==="page");
  if(!target)throw Error("No Chrome page target");
  cdp=await connect(target.webSocketDebuggerUrl);
  await cdp.send("Page.enable");await cdp.send("Runtime.enable");
  for(const scenario of scenarios){
   await cdp.send("Emulation.setDeviceMetricsOverride",{width:scenario.width,height:scenario.height,deviceScaleFactor:1,mobile:false});
   const qs=new URLSearchParams({capture:"1",w:String(scenario.width),h:String(scenario.height),scene:scenario.scene});
   if(scenario.context)qs.set("context",String(scenario.context));
   if(scenario.scale)qs.set("scale",String(scenario.scale));
   if(scenario.top)qs.set("top",String(scenario.top));
   if(scenario.bottom)qs.set("bottom",String(scenario.bottom));
   const url="file:///"+path.join(root,"manga-volume-sources.html").replace(/\\/g,"/")+"?"+qs;
   await cdp.send("Page.navigate",{url});
   await sleep(550);
   const result=await cdp.send("Runtime.evaluate",{expression:auditExpression,returnByValue:true,awaitPromise:true});
   if(result.exceptionDetails)throw Error("Browser evaluate exception for "+scenario.label+": "+JSON.stringify(result.exceptionDetails));
   const data=result.result.value;
   const checks={
     sourceRows:data.rows.length,
     hasExpectedRows:["disabled","empty","loading"].includes(scenario.scene)?data.rows.length===0:data.rows.length>=2,
     horizontallyWithinPanel:data.rows.every(r=>r.action.x>=data.glass.x-1&&r.action.right<=data.glass.right+1),
     noRowPartsOverlap:data.rows.every(r=>r.tile.right<=r.body.x&&r.body.right<=r.action.x+1),
     noHorizontalRowOverflow:data.rows.every(r=>r.scrollWidth<=r.clientWidth+1),
     backHitHeight:data.back.h>=44,
     heroTextWithinHero:data.heroCopy.y>=data.hero.y&&data.heroCopy.bottom<=data.hero.bottom+2,
     headerWithinGlass:data.rows.length===0||(data.header.y>=data.glass.y&&data.header.bottom<=data.glass.bottom),
     emptyStateNoScrollbar:data.rows.length>0||data.viewportScrollHeight<=data.viewportClientHeight+1
   };
   if(scenario.save){
    const shot=await cdp.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:false,fromSurface:true});
    fs.writeFileSync(path.join(root,"captures",scenario.label+".png"),Buffer.from(shot.data,"base64"));
   }
   const interactions={};
   if(scenario.label==="portrait"){
    const pick=await cdp.send("Runtime.evaluate",{expression:"(()=>{document.querySelector('.source-row').click(); return {disc:document.querySelector('.source-action').classList.contains('disc'),status:document.querySelector('.source-meta.live')?.textContent,dim:document.querySelectorAll('.source-row.dimmed').length,toast:!document.getElementById('toast').hidden};})()",returnByValue:true});
    interactions.rowPick=pick.result.value;
    interactions.rowPickPass=pick.result.value.disc&&pick.result.value.status==="Resolving torrent…"&&pick.result.value.dim===3&&pick.result.value.toast;
    const back=await cdp.send("Runtime.evaluate",{expression:"(()=>{document.getElementById('back').click();return document.getElementById('app').classList.contains('picker-closed')})()",returnByValue:true});
    interactions.backCloses=back.result.value===true;
   }
   if(scenario.label==="series"){
    const key=await cdp.send("Runtime.evaluate",{expression:"(()=>{const row=document.querySelector('.source-row');row.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));return document.querySelector('.source-action').classList.contains('disc')})()",returnByValue:true});
    interactions.keyboardRowActivation=key.result.value===true;
   }
   if(scenario.label==="disabled"){
    const open=await cdp.send("Runtime.evaluate",{expression:"(()=>{document.getElementById('extensionsRoute').click();return document.getElementById('reviewMessage').textContent})()",returnByValue:true});
    interactions.extensionsRouteOnlySignals=String(open.result.value).includes("openExtensionsRequested()");
   }
   if(scenario.label==="landscape-long-130"){
    const scroll=await cdp.send("Runtime.evaluate",{expression:"(()=>{const v=document.querySelector('.result-viewport');v.scrollTop=55;const a=document.querySelector('.source-action').getBoundingClientRect(),r=v.getBoundingClientRect();return {scrollTop:v.scrollTop,visible:a.top>=r.top&&a.bottom<=r.bottom}})()",returnByValue:true});
    interactions.pickedActionReachableByScroll=scroll.result.value.visible;
    const shot=await cdp.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:false,fromSurface:true});
    fs.writeFileSync(path.join(root,"captures","landscape-long-130-scrolled.png"),Buffer.from(shot.data,"base64"));
   }
   report.push({scenario,checks,interactions,bounds:data});
   if(Object.entries(checks).some(([k,v])=>k!=="sourceRows"&&v!==true)||Object.entries(interactions).some(([k,v])=>k.endsWith("Pass")||k==="backCloses"||k==="extensionsRouteOnlySignals"||k==="pickedActionReachableByScroll"||k==="keyboardRowActivation"?v!==true:false)){process.exitCode=2;}

   process.stdout.write(JSON.stringify({scenario:scenario.label,checks,first:data.rows[0],heroCopy:data.heroCopy,back:data.back,scroll:data.scrollArea})+"\n");
  }
  await cdp.send("Emulation.setDeviceMetricsOverride",{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await cdp.send("Page.navigate",{url:"file:///"+path.join(root,"manga-volume-sources.html").replace(/\\/g,"/")});
  await sleep(400);
  const edit=await cdp.send("Runtime.evaluate",{expression:"(()=>{const change=(id,value)=>{const x=document.getElementById(id);x.value=value;x.dispatchEvent(new Event('change',{bubbles:true}));};change('preset','900x412');change('fontScale','1.3');change('topInset','24');change('scene','disabled');const a=document.getElementById('app');return {w:a.style.width,h:a.style.height,top:a.style.getPropertyValue('--top-inset'),scale:a.style.getPropertyValue('--font-scale'),rows:document.querySelectorAll('.source-row').length,review:!document.body.classList.contains('capture')}})()",returnByValue:true});
  const ui=edit.result.value;
  const controlsPass=ui.w==="900px"&&ui.h==="412px"&&ui.top==="24px"&&ui.scale==="1.3"&&ui.rows===0&&ui.review===true;
  report.push({scenario:{label:"external-review-controls",width:1280,height:900,scene:"reviewer"},checks:{editableViewportInsetsFontScaleState:controlsPass},interactions:ui,bounds:null});
  process.stdout.write(JSON.stringify({scenario:"external-review-controls",checks:{editableViewportInsetsFontScaleState:controlsPass},values:ui})+"\\n");
  if(!controlsPass)process.exitCode=2;
  fs.writeFileSync(path.join(root,"captures","geometry-audit.json"),JSON.stringify(report,null,2));
 }catch(e){console.error("VERIFY_FAIL",e.stack||String(e));process.exitCode=1;}
 finally{
  try{cdp?.close();}catch(_){}
  try{if(child?.pid)execFileSync("taskkill.exe",["/PID",String(child.pid),"/T","/F"],{windowsHide:true,stdio:"ignore"});}catch(_){try{child?.kill();}catch(__){}}
  try{execFileSync("powershell.exe",["-NoProfile","-NonInteractive","-Command",'Get-CimInstance Win32_Process -Filter "Name = \'chrome.exe\'" | Where-Object { $_.CommandLine -like "*chrome-inspection-profile*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }'],{windowsHide:true,stdio:"ignore"});}catch(_){}
  await sleep(900);
  try{fs.rmSync(profile,{recursive:true,force:true,maxRetries:4,retryDelay:150});}catch(e){console.error("Profile cleanup deferred:",e.message);}
 }
})();
