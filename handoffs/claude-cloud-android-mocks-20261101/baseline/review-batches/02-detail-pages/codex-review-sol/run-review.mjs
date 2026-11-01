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

const pages=[['biblio','06-biblio-book/biblio-book.html'],['film','17-theatre-details/film-detail.html'],['series','17-theatre-details/series-detail.html'],['comic-series','18-comic-details/comic-series.html'],['comic-edition','18-comic-details/comic-edition.html'],['comic-archive','18-comic-details/comic-archive.html'],['saga','19-generic-universes/saga-universe.html'],['shelves','19-generic-universes/shelves-universe.html']];
const results=[];
try{
for(const [id,file] of pages)for(const [orientation,w,h] of [['portrait',354,792],['landscape',792,354],['desktop',1280,700]]){
 let q='capture=1&top=24&bottom=24&scale=1.3&font=1.3&v='+ (orientation==='portrait'?'portraitSmall':orientation==='landscape'?'landscapeSmall':'desktop');
 if(id==='biblio')q='width='+w+'&height='+h+'&scale=1.3&insetTop=24&insetBottom=24';
 await action({op:'goto',path:'review-batches/02-detail-pages/finished-mocks/'+file+'?'+q,w:Math.max(w,1450),h:Math.max(h,1000)});
 if(id!=='biblio'&& !['saga','shelves'].includes(id)) {await page.setViewport({width:w,height:h,deviceScaleFactor:1});await page.reload({waitUntil:'networkidle0'});}
 await page.evaluate(()=>document.fonts.ready);const selector=id==='biblio'?'#app-screen':'#app';
 const metrics=await action({op:'metrics',selector});
 await action({op:'capture',selector,name:id+'-'+orientation});
 results.push({id,orientation,url:page.url(),metrics});
}
// Focused actual actions, with resulting state captured in evidence.
for(const renderer of ['legacy','rebuilt']){
 await action({op:'goto',path:'review-batches/02-detail-pages/finished-mocks/17-theatre-details/series-detail.html?capture=1&top=24&bottom=24&scale=1.3&renderer='+renderer,w:792,h:354});
 await page.$eval('#page-scroll',e=>e.scrollTop=e.scrollHeight);await new Promise(r=>setTimeout(r,150));
 await action({op:'capture',selector:'#app',name:'series-'+renderer+'-ledger'});
 const buttons=await page.$$eval('#app button',es=>es.map(e=>({action:e.dataset.action,text:e.textContent.trim(),id:e.id})));
 results.push({test:'theatre-ledger',renderer,buttons,metrics:await action({op:'metrics',selector:'#app'})});
 if(renderer==='legacy'){
 await page.click('#season-trigger');await page.keyboard.press('End');await page.keyboard.press('Enter');
 results.push({test:'season-end',text:await page.$eval('#season-trigger',e=>e.innerText)});
 }
}
for(const id of ['saga','shelves']) {
 const file=pages.find(p=>p[0]===id)[1];
 await action({op:'goto',path:'review-batches/02-detail-pages/finished-mocks/'+file+'?capture=1&v=portraitSmall&top=24&bottom=24&scale=1.3',w:1450,h:1000});
 await page.$eval('[data-tab]',e=>e.click());await new Promise(r=>setTimeout(r,400));
 const proposed=await page.$eval('#tabbar',e=>({rect:e.getBoundingClientRect().toJSON(),scroll:e.scrollWidth,client:e.clientWidth}));
 await page.select('#dockMode','original');const original=await page.$eval('#tabbar',e=>({rect:e.getBoundingClientRect().toJSON(),scroll:e.scrollWidth,client:e.clientWidth}));
 results.push({test:'universe-tabs',id,proposed,original});
 await action({op:'capture',selector:'#app',name:id+'-original-tabs'});
}
}finally{
 fs.writeFileSync(path.join(own,'fresh-results.json'),JSON.stringify({utc:new Date().toISOString(),results,errors,blocked,resources:[...resources.values()]},null,2));
 await browser.close(); await new Promise(r=>server.close(r));fs.writeFileSync(path.join(own,'closed.json'),JSON.stringify({closed:true,pid:browser.process().pid,utc:new Date().toISOString()}));
}


