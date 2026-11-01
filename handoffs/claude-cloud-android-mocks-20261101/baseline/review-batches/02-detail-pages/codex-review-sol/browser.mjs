import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import readline from 'node:readline';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import puppeteer from 'file:///C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer/lib/puppeteer/puppeteer.js';
const own=path.dirname(fileURLToPath(import.meta.url)), root='D:/Colosseum-Android-Phone-Mocks-20261101';
const captureDir=path.join(own,'captures');fs.mkdirSync(captureDir,{recursive:true});
const profile=path.join(own,'owned-profile');
const resources=new Map(), errors=[], blocked=[];let currentInput='',closing=false;
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const types={'.html':'text/html; charset=utf-8','.css':'text/css','.js':'text/javascript','.ttf':'font/ttf','.otf':'font/otf','.woff2':'font/woff2','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg','.webp':'image/webp'};
const server=http.createServer((req,res)=>{
  try {
    const rel=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
    const file=path.resolve(root,'.'+rel);
    if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}
    if(!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404);res.end();return;}
    const bytes=fs.readFileSync(file);
    resources.set(file,{path:file,sha256:digest(bytes),bytes:bytes.length});
    res.writeHead(200,{'Content-Type':types[path.extname(file).toLowerCase()]||'application/octet-stream','Cache-Control':'no-store'});res.end(bytes);
  }catch(e){res.writeHead(400);res.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const origin='http://127.0.0.1:'+server.address().port;
const browser=await puppeteer.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,userDataDir:profile,args:['--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--disable-extensions']});
const page=await browser.newPage();await page.setRequestInterception(true);
page.on('request',r=>{if(r.url().startsWith(origin+'/')||r.url().startsWith('data:')||r.url().startsWith('blob:'))r.continue();else{blocked.push(r.url());r.abort();}});
page.on('pageerror',e=>errors.push({input:currentInput,error:e.message}));
page.on('response',r=>{if(r.status()>=400&&!r.url().endsWith('favicon.ico'))errors.push({input:currentInput,url:r.url(),status:r.status()});});
page.setDefaultTimeout(8000);
const browserVersion=await browser.version();
function log(value){fs.appendFileSync(path.join(own,'browser-events.jsonl'),JSON.stringify({utc:new Date().toISOString(),...value})+'\n');}
async function action(c){
  if(c.op==='goto'){
    currentInput=c.path;await page.setViewport({width:c.w||1450,height:c.h||1500,deviceScaleFactor:1});
    await page.goto(origin+'/'+c.path,{waitUntil:'networkidle0',timeout:20000});await page.evaluate(()=>document.fonts.ready);
    return {url:page.url(),title:await page.title()};
  }
  if(c.op==='eval')return await page.evaluate(c.code);
  if(c.op==='click'){await page.click(c.selector,c.options||{});return {clicked:c.selector};}
  if(c.op==='key'){await page.keyboard.press(c.key);return {key:c.key};}
  if(c.op==='type'){await page.type(c.selector,c.text);return {typed:c.selector};}
  if(c.op==='select'){return await page.select(c.selector,...c.values);}
  if(c.op==='capture'){
    await new Promise(r=>setTimeout(r,150));const target=path.join(captureDir,c.name+'.png');
    const e=c.selector?await page.$(c.selector):null;if(c.selector&&!e)throw Error('Missing screenshot target '+c.selector);
    if(e)await e.screenshot({path:target});else await page.screenshot({path:target,fullPage:!!c.fullPage});
    const meta={name:c.name,file:target,input:currentInput,url:page.url(),browserVersion,viewport:page.viewport(),utc:new Date().toISOString(),sha256:digest(fs.readFileSync(target)),errors:[...errors],blocked:[...blocked]};
    fs.writeFileSync(path.join(captureDir,c.name+'.json'),JSON.stringify(meta,null,2));log({capture:meta});return meta;
  }
  if(c.op==='metrics')return await page.evaluate((selector)=>{
    const app=document.querySelector(selector),a=app.getBoundingClientRect(),box=e=>{const r=e.getBoundingClientRect();return{x:r.x-a.x,y:r.y-a.y,w:r.width,h:r.height,right:r.right-a.x,bottom:r.bottom-a.y};};
    const controls=[...app.querySelectorAll('button,input,select,[role=button]')].filter(e=>e.getBoundingClientRect().width&&e.getBoundingClientRect().height).map(e=>({tag:e.tagName,id:e.id,cls:e.className,text:(e.innerText||e.getAttribute('aria-label')||e.placeholder||'').trim().slice(0,100),rect:box(e),tab:e.tabIndex}));
    const images=[...app.querySelectorAll('img')].map(e=>({src:e.getAttribute('src'),complete:e.complete,natural:[e.naturalWidth,e.naturalHeight],rect:box(e)}));
    const scrollers=[...app.querySelectorAll('*')].filter(e=>/(auto|scroll)/.test(getComputedStyle(e).overflowY+getComputedStyle(e).overflowX)).map(e=>({id:e.id,cls:e.className,scrollWidth:e.scrollWidth,clientWidth:e.clientWidth,scrollHeight:e.scrollHeight,clientHeight:e.clientHeight,scrollTop:e.scrollTop,rect:box(e)}));
    return {app:{w:a.width,h:a.height},controls,images,scrollers,focus:document.activeElement?.id,text:app.innerText};
  },c.selector||'#app');
  if(c.op==='close'){closing=true;await browser.close();await new Promise(r=>server.close(r));return{closed:true,browserVersion,profile};}
  throw Error('Unknown operation '+c.op);
}
console.log(JSON.stringify({ready:true,browserVersion,pid:browser.process().pid,origin,profile}));
log({ready:true,browserVersion,pid:browser.process().pid,origin,profile});
const rl=readline.createInterface({input:process.stdin,crlfDelay:Infinity});
for await(const line of rl){
  if(!line.trim())continue;let c,result;
  try{c=JSON.parse(line);if(c.op==='batch'){result=[];for(const step of c.steps){try{result.push({op:step.op,name:step.name,result:await action(step)});}catch(e){result.push({op:step.op,error:e.message});if(!c.continueOnError)break;}}}else result=await action(c);
    if(c.save)fs.writeFileSync(path.join(own,c.save),JSON.stringify({utc:new Date().toISOString(),result},null,2));
    fs.writeFileSync(path.join(own,'loaded-resources.json'),JSON.stringify({browserVersion,resources:[...resources.values()],errors,blocked},null,2));
    console.log(JSON.stringify({id:c.id||null,result}));log({command:c,result});
  }catch(e){console.log(JSON.stringify({id:c?.id||null,error:e.stack}));log({error:e.stack});}
  if(closing)break;
}
if(!closing){await browser.close();await new Promise(r=>server.close(r));}


