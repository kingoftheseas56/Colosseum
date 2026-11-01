import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const puppeteer = require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const root = 'D:/Colosseum-Android-Phone-Mocks-20261101';
const batch = root + '/review-batches/01-sources-manga';
const own = batch + '/codex-review';
fs.mkdirSync(own + '/captures', {recursive:true});
const read = f => JSON.parse(fs.readFileSync(f, 'utf8').replace(/^\uFEFF/, ''));
const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const result = {started:new Date().toISOString(),scope:'Independent HTML handoff checks and route comparison; no app/device operation',hashChecks:[],pageChecks:[],interactions:[],limitations:['Rebuilt HTML is a component preview and omits the native TitlePageBar.','Known original flow constraints are not accepted as a usable fit.','No native runtime or physical device qualification.']};
for (const file of read(batch + '/evidence/artifact-sha256.json').files) {
  const p = path.resolve(batch,file.path);
  result.hashChecks.push({path:file.path,matches:fs.existsSync(p)&&sha(p)===file.sha256});
}
const sheets = read(batch + '/evidence/contact-sheets-manifest.json');
for (const sheet of sheets.sheets) {
  result.hashChecks.push({path:sheet.file,matches:sha(path.resolve(batch,sheet.file))===sheet.sha256});
  for (const source of sheet.sources) {
    if (source.image_sha256) result.hashChecks.push({path:source.image,matches:sha(path.resolve(root,source.image))===source.image_sha256});
    if (source.receipt&&source.receipt_sha256) result.hashChecks.push({path:source.receipt,matches:sha(path.resolve(batch,source.receipt))===source.receipt_sha256});
  }
}
const types={'.html':'text/html;charset=utf-8','.css':'text/css','.js':'text/javascript','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.ttf':'font/ttf','.otf':'font/otf','.woff2':'font/woff2'};
const server=http.createServer((req,res)=>{
  if(req.url?.startsWith('/favicon.ico')){res.writeHead(204);res.end();return;}
  try{
    const p=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname));
    if(!p.startsWith(path.resolve(root)+path.sep)||req.method!=='GET')throw Error('outside root');
    const b=fs.readFileSync(p);res.writeHead(200,{'Content-Type':types[path.extname(p)]||'application/octet-stream','Cache-Control':'no-store'});res.end(b);
  }catch{res.writeHead(404);res.end();}
});
await new Promise(ok=>server.listen(0,'127.0.0.1',ok));
const origin='http://127.0.0.1:'+server.address().port;
let browser;
try{
  browser=await puppeteer.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,userDataDir:own+'/.isolated-profile',args:['--no-first-run','--no-default-browser-check','--disable-extensions','--disable-background-networking','--disable-sync']});
  const p=await browser.newPage();
  await p.setRequestInterception(true);
  let errors=[];
  p.on('request',r=>{if(r.url().startsWith(origin+'/')||r.url().startsWith('data:')||r.url()==='about:blank')r.continue();else{errors.push('blocked '+r.url());r.abort();}});
  p.on('pageerror',e=>errors.push(String(e)));
  p.on('response',r=>{if(r.status()>=400)errors.push(r.status()+' '+r.url());});
  async function navigate(job,file,w,h,params){
    errors=[];await p.setViewport({width:w,height:h,deviceScaleFactor:1});
    const url=new URL(origin+'/review-batches/01-sources-manga/finished-mocks/'+job+'/'+file);
    for(const [k,v]of Object.entries(params))url.searchParams.set(k,String(v));
    await p.goto(url.href,{waitUntil:'load',timeout:20000});
    await p.evaluate(()=>document.fonts.ready);await new Promise(r=>setTimeout(r,150));
  }
  const prefixes=['film','episode','comic','comic-confirm','manga-sources','series','chapters','flow-original','flow-alternative'];
  for(const prefix of prefixes){
    for(const preset of ['portrait','landscape','stress-landscape']){
      const fixture=read(batch+'/evidence/finished-'+prefix+'-'+preset+'.json');
      const job=fixture.job.split('/').at(-1);
      await navigate(job,fixture.file,fixture.viewport.width,fixture.viewport.height,fixture.params);
      const metrics=await p.evaluate(()=>({
        horizontalOverflow:document.documentElement.scrollWidth-innerWidth,
        failedImages:[...document.images].filter(i=>!i.complete||!i.naturalWidth).map(i=>i.getAttribute('src')),
        unloadedFonts:[...document.fonts].filter(f=>f.status!=='loaded').map(f=>f.family),
        sourceTiles:document.querySelectorAll('.provider-tile,.source-tile').length,
        roundControls:document.querySelectorAll('.circle,.round-pick,.source-action').length,
        route:document.querySelector('#app')?.dataset.route||null
      }));
      result.pageChecks.push({page:fixture.file,preset,viewport:fixture.viewport,errors:[...errors],...metrics});
      console.log(JSON.stringify({page:fixture.file,preset,errors:errors.length,overflow:metrics.horizontalOverflow,failedImages:metrics.failedImages.length}));
    }
  }
  await navigate('01-theatre-sources','film-sources.html',412,900,{render:1,w:412,h:900});
  await p.click('#providerTrigger');await p.keyboard.press('Escape');
  result.interactions.push({name:'Provider Escape retains picker and returns focus',pass:await p.evaluate(()=>document.querySelector('#providerMenu').hidden&&!document.querySelector('#sourceSheet').hidden&&document.activeElement.id==='providerTrigger')});
  await p.click('#providerTrigger');await p.mouse.click(120,575);
  result.interactions.push({name:'Provider outside click does not activate source',pass:await p.evaluate(()=>document.querySelector('#providerMenu').hidden&&!document.querySelector('#sourceSheet').hidden&&!document.querySelector('.chosen'))});
  await navigate('02-comic-sources','comic-sources.html',412,900,{capture:1,vw:412,vh:900});
  await p.type('#queryInput','Gotham');await p.click('.search-button');await p.click('[data-row="0"]');await p.keyboard.press('Escape');
  result.interactions.push({name:'Weak-match cancel preserves filtered result and query',pass:await p.evaluate(()=>document.querySelector('#countText').textContent==='1 result'&&document.querySelector('#queryInput').value==='Gotham'&&document.querySelector('#weakLayer').classList.contains('hidden')&&document.activeElement.dataset.row==='0')});
  const pairs=[];
  for(const [page,label]of [['manga-series.html','series'],['manga-chapters.html','chapters']]){
    for(const [preset,w,h]of [['portrait',412,900],['landscape',900,412]]){
      for(const route of ['reading-room','rebuilt']){
        await navigate('04-manga-series',page,w,h,{capture:1,route});
        const name=label+'-'+route+'-'+preset+'.png';
        await p.screenshot({path:own+'/captures/'+name});
      }
      const portrait=preset==='portrait';
      const oldImage='captures/'+label+'-reading-room-'+preset+'.png',newImage='captures/'+label+'-rebuilt-'+preset+'.png';
      const html='<!doctype html><meta charset="utf-8"><title>Manga layout comparison</title><style>*{box-sizing:border-box}body{margin:0;padding:22px;background:#10131a;color:#eee;font:16px/1.45 Segoe UI,sans-serif}h1{font-size:24px;margin:0 0 8px}p{margin:8px 0 16px}.note{color:#f1cf70}.pair{display:grid;grid-template-columns:'+(portrait?'1fr 1fr':'1fr')+';gap:20px}h2{font-size:18px;margin:4px 0 8px}img{display:block;width:100%;border:1px solid #626773}.small{color:#bbb;font-size:13px}</style><h1>Manga '+label+' · '+preset+'</h1><p>Same fixture and dimensions. Local HTML previews; no app changes.</p><p class="note">Right/bottom: rebuilt CONTENT preview only. Its real TitlePageBar is missing; it still uses the older chrome. This is not a completed rebuilt-page mock.</p><div class="pair"><section><h2>Older composition · Pro finished target</h2><img src="'+oldImage+'"></section><section><h2>Rebuilt content · incomplete preview</h2><img src="'+newImage+'"></section></div><p class="small">Frozen Main.qml default: older. Desktop checkout views.ini: rebuiltTitlePages=true. Android checkout: false. Exact active runtime and approved route remain to be confirmed.</p>';
      const name='compare-'+label+'-'+preset;
      fs.writeFileSync(own+'/'+name+'.html',html);
      await p.setViewport({width:portrait?900:960,height:portrait?1200:1200,deviceScaleFactor:1});
      await p.goto(origin+'/review-batches/01-sources-manga/codex-review/'+name+'.html',{waitUntil:'load'});
      await p.screenshot({path:own+'/'+name+'.png',fullPage:true});pairs.push(name);
    }
  }
  result.comparisons=pairs;
}catch(e){result.fatal=String(e.stack||e);console.error(result.fatal);process.exitCode=1;}
finally{
  if(browser)await browser.close();
  await new Promise(ok=>server.close(ok));
  result.closed=new Date().toISOString();result.browserClosed=true;result.serverClosed=true;
  result.hashMismatches=result.hashChecks.filter(x=>!x.matches);
  result.fontFaceNote='Unused declared font variants may remain unloaded. This is informational, not a missing-resource failure. The affected CSS declares unused Segoe bold and Fraunces italic variants.';
  result.resourceFailures=result.pageChecks.filter(x=>x.errors.length||x.horizontalOverflow>1||x.failedImages.length);
  fs.writeFileSync(own+'/receiving-checks.json',JSON.stringify(result,null,2));
  console.log(JSON.stringify({hashChecks:result.hashChecks.length,hashMismatches:result.hashMismatches.length,pageCases:result.pageChecks.length,resourceFailures:result.resourceFailures.length,interactions:result.interactions,comparisons:result.comparisons,fatal:result.fatal||null}));
}
