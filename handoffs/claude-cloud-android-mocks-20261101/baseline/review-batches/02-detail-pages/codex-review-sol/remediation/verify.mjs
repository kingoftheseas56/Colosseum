
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import puppeteer from 'file:///C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer/lib/puppeteer/puppeteer.js';
import {startOverlay} from './overlay-server.mjs';
const own=path.dirname(fileURLToPath(import.meta.url));fs.mkdirSync(path.join(own,'evidence'),{recursive:true});const {server,origin}=await startOverlay();
const browser=await puppeteer.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,userDataDir:path.join(own,'owned-profile'),args:['--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--disable-extensions']});const page=await browser.newPage();const results=[],errors=[],blocked=[];const pid=browser.process().pid;
await page.setRequestInterception(true);page.on('request',r=>{if(r.url().startsWith(origin+'/')||r.url().startsWith('data:'))r.continue();else{blocked.push(r.url());r.abort();}});page.on('pageerror',e=>errors.push(e.message));page.on('response',r=>{if(r.status()>=400&&!r.url().endsWith('/favicon.ico'))errors.push(r.status()+' '+r.url());});
async function capture(name){const p=path.join(own,'evidence',name+'.png');await page.screenshot({path:p});const meta={url:page.url(),viewport:page.viewport(),utc:new Date().toISOString(),sha256:crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')};fs.writeFileSync(p.replace('.png','.json'),JSON.stringify(meta,null,2));}
async function click(selector){await page.click(selector);}
try{
for(const kind of ['film','series'])for(const renderer of ['legacy','rebuilt'])for(const [host,w,h] of [['phone-portrait',354,792],['phone-landscape',792,354],['tablet',1280,800],['desktop',1280,700]]){
 await page.setViewport({width:w,height:h,deviceScaleFactor:1});await page.goto(origin+'/review-batches/02-detail-pages/finished-mocks/17-theatre-details/'+kind+'-detail.html?capture=1&renderer='+renderer+'&scale=1.3&top=24&bottom=24',{waitUntil:'networkidle0'});await page.evaluate(()=>document.fonts.ready);
 const dom=await page.$eval('#app',app=>({host:app.className,windowActions:[...app.querySelectorAll('[data-action]')].filter(e=>['system-menu','minimize','fullscreen','quit','arm-quit','keep-watching'].includes(e.dataset.action)).map(e=>({action:e.dataset.action,tab:e.tabIndex,label:e.getAttribute('aria-label')||e.innerText})),back:!!app.querySelector('[data-action="back"]'),search:!!app.querySelector('[data-action="search"]')}));
 if(host!=='desktop'){
  if(dom.windowActions.length)throw Error('Android DOM has window actions '+kind+renderer+host);
  if(!dom.back|| (renderer==='rebuilt'&&!dom.search))throw Error('Missing source navigation');
  await page.focus('#chrome [data-action="back"]');const tabs=[];for(let i=0;i<4;i++){tabs.push(await page.evaluate(()=>({action:document.activeElement.dataset.action,label:document.activeElement.getAttribute('aria-label')})));await page.keyboard.press('Tab');}
  await click('#chrome [data-action="back"]');if(renderer==='rebuilt')await click('[data-action="search"]');
  results.push({kind,renderer,host,dom,tabs,pass:true});
  if((kind==='film'&&renderer==='rebuilt')||host==='tablet'&&kind==='series')await capture(kind+'-'+renderer+'-'+host);
 }else{
  if(renderer==='rebuilt'){
   await page.focus('[data-action="system-menu"]');await page.keyboard.press('Enter');
   const labels=await page.$$eval('.system-menu button',es=>es.map(e=>e.innerText));if(!labels.includes('Minimize')||!labels.includes('Exit fullscreen')||!labels.includes('Quit Colosseum…'))throw Error('Desktop menu lost actions');await capture(kind+'-rebuilt-desktop-menu');
   await click('[data-action="minimize"]');await click('[data-action="system-menu"]');await click('[data-action="fullscreen"]');await click('[data-action="system-menu"]');await click('[data-action="arm-quit"]');const quit=await page.$('[data-action="quit"]');if(!quit)throw Error('Quit confirmation lost');await click('[data-action="keep-watching"]');
   // A formerly open desktop menu must not survive conversion to Android.
   await click('[data-action="system-menu"]');await page.setViewport({width:354,height:792,deviceScaleFactor:1});await new Promise(r=>setTimeout(r,150));const remaining=await page.$$eval('#chrome [data-action]',es=>es.map(e=>e.dataset.action));if(remaining.some(a=>['system-menu','minimize','fullscreen','quit','arm-quit'].includes(a)))throw Error('Stale desktop menu on Android resize');
   results.push({kind,renderer,host,dom,labels,resizeToAndroid:remaining,pass:true});
  }else{for(const action of ['minimize','fullscreen','quit'])await click('[data-action="'+action+'"]');results.push({kind,renderer,host,dom,desktopActionsClicked:['minimize','fullscreen','quit'],pass:true});}
 }
}
}catch(e){errors.push(e.stack);}finally{await browser.close();await new Promise(r=>server.close(r));fs.writeFileSync(path.join(own,'focused-verification.json'),JSON.stringify({utc:new Date().toISOString(),pid,browserClosed:true,serverClosed:true,browserVersion:await browser.version().catch(()=>null),results,errors,blocked,passed:results.length===16&&errors.length===0&&blocked.length===0},null,2));}
