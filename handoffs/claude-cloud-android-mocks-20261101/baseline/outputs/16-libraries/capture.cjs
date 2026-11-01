/* Local-only reproducer for isolated Chrome screenshots. Requires existing Chrome + Node 24. */
const fs=require("node:fs");
const path=require("node:path");
const {spawn}=require("node:child_process");
const {pathToFileURL}=require("node:url");
const {setTimeout:delay}=require("node:timers/promises");
const BASE=__dirname,OUT=path.join(BASE,"captures");
const chrome="C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
const profile=path.join(OUT,"_isolated_capture_profile_"+process.pid);
const portFile=path.join(profile,"DevToolsActivePort");
fs.mkdirSync(OUT,{recursive:true});
const proc=spawn(chrome,["--headless=new","--disable-gpu","--disable-background-networking","--disable-extensions","--no-first-run","--no-default-browser-check","--allow-file-access-from-files","--remote-debugging-port=0","--user-data-dir="+profile,"about:blank"],{stdio:"ignore",windowsHide:true});
let ws=null;let rpcId=0;const pending=new Map();
async function connect(){
for(let i=0;i<150&&!fs.existsSync(portFile);i++){if(proc.exitCode!==null)throw Error("Chrome exited "+proc.exitCode);await delay(100)}
if(!fs.existsSync(portFile))throw Error("Chrome DevToolsActivePort unavailable");
const parts=fs.readFileSync(portFile,"utf8").trim().split(/\r?\n/);
ws=new WebSocket("ws://127.0.0.1:"+parts[0]+parts[1]);
ws.addEventListener("message",e=>{let d;try{d=JSON.parse(e.data)}catch{return}if(!d.id)return;const rec=pending.get(d.id);if(!rec)return;pending.delete(d.id);clearTimeout(rec.timer);d.error?rec.reject(Error(d.error.message)):rec.resolve(d.result||{})});
await new Promise((resolve,reject)=>{ws.addEventListener("open",resolve,{once:true});ws.addEventListener("error",reject,{once:true})});
}
function rpc(method,params={},sessionId){return new Promise((resolve,reject)=>{const id=++rpcId;const timer=setTimeout(()=>{pending.delete(id);reject(Error("CDP timeout "+method))},16000);pending.set(id,{resolve,reject,timer});const msg={id,method,params};if(sessionId)msg.sessionId=sessionId;ws.send(JSON.stringify(msg))})}
async function evaluate(sessionId,expression){const r=await rpc("Runtime.evaluate",{expression,returnByValue:true,awaitPromise:true},sessionId);if(r.exceptionDetails)throw Error("Page evaluation failed: "+expression.slice(0,90));return r.result?r.result.value:null}
const cases=[
{name:"portrait",w:412,h:900,platform:"android"},
{name:"landscape",w:900,h:412,platform:"android"},
{name:"desktop",w:1280,h:700,platform:"desktop"},
{name:"menu",w:412,h:900,platform:"android",state:"menu"},
{name:"selection",w:412,h:900,platform:"android",state:"selection"},
{name:"confirm",w:412,h:900,platform:"android",state:"confirm"},
{name:"small",w:354,h:792,platform:"android"},
{name:"small-landscape",w:792,h:354,platform:"android"},
{name:"tablet",w:800,h:1280,platform:"android"},
{name:"tablet-landscape",w:1280,h:800,platform:"android"},
{name:"scaled",w:354,h:792,platform:"android",scale:1.3,top:24,bottom:24}
];
const diagnostics=[];
function inspect(){
const screen=document.querySelector(".screen"),imgs=[...document.querySelectorAll(".poster-image")],tabs=[...document.querySelectorAll(".tab-pill")],cards=[...document.querySelectorAll(".gallery-item")],menu=document.querySelector(".card-menu"),selection=document.querySelector(".selection-bar"),search=document.querySelector(".search-box"),viewport=document.getElementById("viewport");
const box=el=>{if(!el)return null;const b=el.getBoundingClientRect();return {x:Math.round(b.x),y:Math.round(b.y),r:Math.round(b.right),b:Math.round(b.bottom),w:Math.round(b.width),h:Math.round(b.height)}};
return {innerWidth,innerHeight,screen:box(screen),cards:cards.length,imagesLoaded:imgs.filter(i=>i.complete&&i.naturalWidth>0).length,imagesFailed:imgs.filter(i=>i.complete&&i.naturalWidth===0).map(i=>i.src),tabs:tabs.map(box),firstCard:box(cards[0]),search:box(search),menu:box(menu),selection:box(selection),viewport:box(viewport),selected:selection&&document.querySelector(".selection-count")?.textContent,fonts:document.fonts.status,bodyOverflow:document.documentElement.scrollWidth>innerWidth,headerOverflow:tabs.some(t=>t.getBoundingClientRect().right>innerWidth+.5),menuOverflow:!!menu&&(menu.getBoundingClientRect().right>innerWidth+.5||menu.getBoundingClientRect().bottom>innerHeight+.5),tabHitsTooNarrow:tabs.filter(t=>t.getBoundingClientRect().width<42).length};
}
async function capture(sessionId,world,mode){
await rpc("Emulation.setDeviceMetricsOverride",{width:mode.w,height:mode.h,deviceScaleFactor:1,mobile:false},sessionId);
const u=pathToFileURL(path.join(BASE,world+"-library.html"));
u.searchParams.set("capture","1");u.searchParams.set("platform",mode.platform);
if(mode.state)u.searchParams.set("state",mode.state);
if(mode.scale)u.searchParams.set("scale",mode.scale);
if(mode.top)u.searchParams.set("top",mode.top);
if(mode.bottom)u.searchParams.set("bottom",mode.bottom);
await rpc("Page.navigate",{url:u.href},sessionId);
let ready=false;
for(let i=0;i<100;i++){try{if(await evaluate(sessionId,'document.readyState==="complete" && !!document.querySelector(".gallery-item")')){ready=true;break}}catch{}await delay(100)}
if(!ready)throw Error("DOM never ready: "+world+"/"+mode.name);
await evaluate(sessionId,"document.fonts.ready.then(()=>true)");
await evaluate(sessionId,"Promise.all([...document.querySelectorAll('.poster-image')].map(i=>i.decode().then(()=>1).catch(()=>0))).then(()=>true)");
await delay(150);
const stats=await evaluate(sessionId,"("+inspect.toString()+")()");
const png=await rpc("Page.captureScreenshot",{format:"png",captureBeyondViewport:false,fromSurface:true},sessionId);
const file=path.join(OUT,world+"-"+mode.name+".png");
fs.writeFileSync(file,Buffer.from(png.data,"base64"));
diagnostics.push({world,mode:mode.name,viewport:[mode.w,mode.h],platform:mode.platform,file:path.relative(BASE,file),bytes:fs.statSync(file).size,...stats});
console.log(world+"/"+mode.name+": "+mode.w+"x"+mode.h+" cards="+stats.cards+" art="+stats.imagesLoaded+"/"+stats.cards+" overflow="+stats.bodyOverflow+" menuOverflow="+stats.menuOverflow+" tabUnder42="+stats.tabHitsTooNarrow);
if(stats.imagesFailed.length)console.warn("FAILED IMAGES: "+stats.imagesFailed.join(", "));
}
async function main(){
if(!fs.existsSync(chrome))throw Error("Chrome missing");
await connect();
const target=await rpc("Target.createTarget",{url:"about:blank"});
const attach=await rpc("Target.attachToTarget",{targetId:target.targetId,flatten:true});
const sessionId=attach.sessionId;
await rpc("Page.enable",{},sessionId);
await rpc("Runtime.enable",{},sessionId);
for(const world of ["theatre","tankoban","biblio"])for(const mode of cases)await capture(sessionId,world,mode);
fs.writeFileSync(path.join(OUT,"verification.json"),JSON.stringify({tool:"Chrome headless CDP, isolated profile",cases:diagnostics},null,2),"utf8");
console.log("VERIFICATION_WRITTEN "+diagnostics.length);
}
main().then(async()=>{try{ws.send(JSON.stringify({id:++rpcId,method:"Browser.close"}))}catch{}await delay(350);try{ws.close()}catch{};try{fs.rmSync(profile,{recursive:true,force:true})}catch{};process.exitCode=0;},async err=>{console.error(err.stack||String(err));try{ws&&ws.close()}catch{};try{proc.kill()}catch{};process.exitCode=1;});
