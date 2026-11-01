import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import readline from 'node:readline';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import puppeteer from 'file:///C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer/lib/puppeteer/puppeteer.js';
const own=path.dirname(fileURLToPath(import.meta.url)), root=path.resolve('D:/Colosseum-Android-Phone-Mocks-20261101');
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


const results=[];const base='review-batches/02-detail-pages/finished-mocks/';
async function open(file,q,w,h){await action({op:'goto',path:base+file+'?'+q,w,h});}
async function snap(name,selector='#app'){await action({op:'capture',selector,name});results.push({name,url:page.url(),metrics:await action({op:'metrics',selector})});}
try{
for(const id of ['film','series'])for(const renderer of ['legacy','rebuilt'])for(const [orient,w,h] of [['portrait',354,792],['landscape',792,354]]){
 await open('17-theatre-details/'+id+'-detail.html','capture=1&scale=1.3&top=24&bottom=24&renderer='+renderer,w,h);
 await snap(id+'-'+renderer+'-'+orient);
}
for(const id of ['saga','shelves'])for(const [orient,v,w,h] of [['portrait','narrow',354,792],['landscape','narrow-land',792,354],['desktop','desktop',1280,700]]){
 await open('19-generic-universes/'+id+'-universe.html','capture=1&v='+v+'&scale=1.3&top=24&bottom=24',w,h);await snap(id+'-'+orient);
 await page.click('[data-tab]');await new Promise(r=>setTimeout(r,120));await snap(id+'-'+orient+'-docked');
 const proposed=await page.$eval('#tabbar',e=>e.getBoundingClientRect().toJSON());
 await page.select('#dockMode','original');await new Promise(r=>setTimeout(r,120));await snap(id+'-'+orient+'-original');
 const original=await page.$eval('#tabbar',e=>e.getBoundingClientRect().toJSON());results.push({test:'tab-comparison',id,orient,proposed,original});
 // Restore proposal, exercise Continue context removal: signal only, card retained.
 await page.select('#dockMode','proposed');const card=await page.$('.continue-card');if(card){await card.focus();await page.keyboard.press('Shift+F10');results.push({test:'continue-context-open',id,visible:await page.$eval('#app',e=>e.innerText)});await page.keyboard.press('Escape');}
}
await open('06-biblio-book/biblio-book.html','width=354&height=792&scale=1.3&insetTop=24&insetBottom=24',1450,1000);
await page.click('#read-button');await page.keyboard.press('End');await page.keyboard.press('Escape');results.push({test:'biblio-read-escape',focus:await page.evaluate(()=>document.activeElement.id),hidden:await page.$eval('#read-shade',e=>e.hidden)});
await page.click('#collections-button');results.push({test:'biblio-collections-open',dialogs:await page.$$eval('[role="dialog"]',es=>es.map(e=>({text:e.innerText,rect:e.getBoundingClientRect().toJSON()})))});await snap('biblio-collections','#app-screen');await page.keyboard.press('Escape');results.push({test:'biblio-collections-escape',focus:await page.evaluate(()=>document.activeElement.id)});
for(const id of ['comic-series','comic-edition','comic-archive']){
 await open('18-comic-details/'+id+'.html','capture=1&font=1.3&top=24&bottom=24'+(id==='comic-edition'?'&state=alt':''),792,354);
 results.push({test:'comic-controls',id,controls:await page.$$eval('#app button',es=>es.map(e=>({text:e.innerText,action:e.dataset.act||e.dataset.action,id:e.id})))});
 await snap(id+'-focused-state');
}
}finally{fs.writeFileSync(path.join(own,'focused-results.json'),JSON.stringify({utc:new Date().toISOString(),results,errors,blocked,resources:[...resources.values()]},null,2));await browser.close();await new Promise(r=>server.close(r));fs.writeFileSync(path.join(own,'closed-focused.json'),JSON.stringify({closed:true,pid:browser.process().pid,utc:new Date().toISOString()}));}
