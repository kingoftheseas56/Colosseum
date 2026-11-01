'use strict';
/* Isolated, read-only browser testing of standalone local mock HTML only. No app/network operations. */
const fs=require('fs');
const path=require('path');
const puppeteer=require('C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Brotherhood-Desktop-Commander\\node_modules\\puppeteer');
const ROOT=__dirname;
const CHROME='C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const captures=path.join(ROOT,'captures');
const specs=[
 ['search-portrait','capture-search.html',412,900],['search-landscape','capture-search-landscape.html',900,412],['search-desktop','capture-search-desktop.html',1280,700],
 ['biblio-explore-portrait','capture-biblio-explore.html',412,900],['biblio-explore-landscape','capture-biblio-explore-landscape.html',900,412],['biblio-explore-desktop','capture-biblio-explore-desktop.html',1280,700],
 ['genre-index-portrait','capture-genre-index.html',412,900],['genre-index-landscape','capture-genre-index-landscape.html',900,412],['genre-index-desktop','capture-genre-index-desktop.html',1280,700],
 ['genre-results-portrait','capture-genre-results.html',412,900],['genre-results-landscape','capture-genre-results-landscape.html',900,412],['genre-results-desktop','capture-genre-results-desktop.html',1280,700],
 ['continue-see-all-portrait','capture-continue-see-all.html',412,900],['continue-see-all-landscape','capture-continue-see-all-landscape.html',900,412],['continue-see-all-desktop','capture-continue-see-all-desktop.html',1280,700],
 ['continue-see-all-context','capture-continue-see-all-context.html',412,900],
 ['search-keyboard','capture-search-keyboard.html',412,900],['search-empty','capture-search-empty.html',412,900],['search-browse','capture-search-browse.html',412,900],
 ['biblio-explore-rail','capture-biblio-explore-rail.html',412,900],['biblio-explore-filter-open','capture-biblio-explore-filter-open.html',412,900],['biblio-explore-tablet-portrait','capture-biblio-explore-tablet-portrait.html',800,1280],
 ['genre-results-covers','capture-genre-results-covers.html',412,900],['genre-results-biblio-long','capture-genre-results-biblio-long.html',354,792],
 ['genre-index-small-portrait','capture-genre-index-small-portrait.html',354,792],['genre-index-small-landscape','capture-genre-index-small-landscape.html',792,354],
 ['search-small-landscape-keyboard','capture-search-small-landscape-keyboard.html',792,354],['continue-see-all-small-landscape','capture-continue-see-all-small-landscape.html',792,354]
];
const toFileUrl=(name)=>'file:///'+path.join(ROOT,name).replace(/\\/g,'/');
async function makePage(browser,html,w,h) {
 const pg=await browser.newPage();await pg.setViewport({width:w,height:h,deviceScaleFactor:1});
 const errors=[],failures=[];
 pg.on('pageerror',e=>errors.push(String(e&&e.message||e)));
 pg.on('requestfailed',r=>failures.push(r.url()+' :: '+String(r.failure()?.errorText||'')));
 await pg.setRequestInterception(true);
 pg.on('request',r=>{if(/^https?:/i.test(r.url()))r.abort('blockedbyclient');else r.continue();});
 await pg.goto(toFileUrl(html),{waitUntil:'load',timeout:20000});
 await pg.waitForFunction(()=>!!document.querySelector('#appFrame .app-inner'),{timeout:10000});
 await pg.evaluate(async()=>{
   const images=Array.from(document.querySelectorAll('#appFrame img'));
   images.forEach(im=>im.loading='eager');
   await Promise.all(images.map(im=>im.decode?im.decode().catch(()=>{}):Promise.resolve()));
 });
 return {pg,errors,failures};
}
function rr(r){return {x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height),right:Math.round(r.right),bottom:Math.round(r.bottom)};}
async function audit(pg,w,h){
 return pg.evaluate((w,h)=>{
  const f=document.querySelector('#appFrame'),F=f.getBoundingClientRect();
  const keySelectors=['.genre-index-body','.genre-body','.genre-hero-text','.search-content','.top-match','.search-result','.detail-card','.genre-tile','.continue-head','.continue-tile','.explore-main','.world-tabs'];
  const over=[];
  for(const selector of keySelectors){
   for(const el of document.querySelectorAll('#appFrame '+selector)){
    const R=el.getBoundingClientRect();
    if(R.right>F.right+2||R.left<F.left-2)over.push({selector,right:Math.round(R.right),left:Math.round(R.left),frameRight:Math.round(F.right)});
   }
  }
  // Adversarial check: the fixed back control must not obscure the editorial genre kicker.
  const back=f.querySelector('.fixed-chrome .back-capsule'),kicker=f.querySelector('.genre-hero-text .kicker');
  if(back&&kicker){const A=back.getBoundingClientRect(),B=kicker.getBoundingClientRect();const dx=Math.min(A.right,B.right)-Math.max(A.left,B.left),dy=Math.min(A.bottom,B.bottom)-Math.max(A.top,B.top);if(dx>1&&dy>1)over.push({selector:'back-action vs genre-kicker',type:'overlap',intersection:[Math.round(dx),Math.round(dy)]});}
  const missing=Array.from(document.querySelectorAll('#appFrame img')).filter(im=>im.complete&&im.naturalWidth===0).map(im=>im.getAttribute('src'));
  const buttons=Array.from(document.querySelectorAll('#appFrame button')).map(el=>({name:el.getAttribute('aria-label')||el.textContent.trim().slice(0,30),h:Math.round(el.getBoundingClientRect().height),w:Math.round(el.getBoundingClientRect().width)}));
  return {frame:Math.round(F.width)+'x'+Math.round(F.height),frameRight:Math.round(F.right),viewport:w+'x'+h,
    images:document.querySelectorAll('#appFrame img').length,missingImages:[...new Set(missing)],overflow:over.slice(0,12),
    buttonCount:buttons.length,smallTouchTargets:buttons.filter(b=>b.h>0&&b.h<40&&/^(See All|[+✓] Library|Sort|Last Watched|A–Z|Z–A|Watched|Not Watched)$/.test(b.name)).slice(0,12),
    hasRoot:!!document.querySelector('#appFrame .app-inner'),
    scrollTop:Math.round(document.querySelector('#appFrame .scroll')?.getBoundingClientRect().top||0),
    visibleCardCount:document.querySelectorAll('#appFrame .search-result,#appFrame .detail-card,#appFrame .genre-tile,#appFrame .continue-tile,#appFrame .book-card').length};
 },w,h);
}
async function run(){
 fs.mkdirSync(captures,{recursive:true});
 const browser=await puppeteer.launch({headless:true,executablePath:CHROME,args:['--no-sandbox','--disable-gpu','--disable-background-networking','--disable-extensions']});
 const result={timestamp:new Date().toISOString(),browser:'installed Chrome via isolated Puppeteer headless session',command:'node verify-captures.cjs',cases:[],interactions:[],unresolved:[]};
 try {
 for(const [name,html,w,h] of specs){
  let page;
  try{
   const out=await makePage(browser,html,w,h);page=out.pg;
   const stats=await audit(page,w,h);
   const filename=path.join(captures,name+'.png');
   await page.screenshot({path:filename,fullPage:false});
   const caseResult={name,source:html,screenshot:path.relative(ROOT,filename),viewport:[w,h],...stats,pageErrors:out.errors,resourceErrors:out.failures,
    fileBytes:fs.statSync(filename).size,pass:stats.frame===w+'x'+h&&stats.missingImages.length===0&&stats.overflow.length===0&&out.errors.length===0};
   result.cases.push(caseResult);
   process.stdout.write((caseResult.pass?'PASS ':'FLAG ')+name+' '+caseResult.frame+' images='+stats.images+' missing='+stats.missingImages.length+' overflow='+stats.overflow.length+' errors='+out.errors.length+'\n');
  }catch(e){result.unresolved.push({name,error:String(e)});process.stdout.write('ERROR '+name+' '+String(e)+'\n');}
  finally{if(page)await page.close();}
 }
 async function scenario(name,html,step){
  let pg;
  try{const x=await makePage(browser,html,412,900);pg=x.pg;const checks=await step(pg);result.interactions.push({name,checks,errors:x.errors});process.stdout.write('INTERACTION '+name+' '+JSON.stringify(checks)+'\n');}
  catch(e){result.unresolved.push({interaction:name,error:String(e)});process.stdout.write('INTERACTION ERROR '+name+' '+String(e)+'\n');}
  finally{if(pg)await pg.close();}
 }
 await scenario('search-empty-to-query','capture-search-empty.html',async pg=>{
  const before=await pg.$eval('#searchInput',i=>i.value);
  await pg.click('[data-action="genre-search"]');
  const after=await pg.$eval('#searchInput',i=>i.value);
  const top=await pg.$eval('.top-match-label',i=>i.textContent.trim());
  return {before,after,top};
 });
 await scenario('biblio-explore-row-index','capture-biblio-explore.html',async pg=>{
  const before=await pg.$eval('.row-rail',e=>Math.round(e.getBoundingClientRect().width));
  await pg.click('[data-action="rail-toggle"]');
  const after=await pg.$eval('.row-rail',e=>Math.round(e.getBoundingClientRect().width));
  await pg.click('[data-action="edit-shelves"]');
  const editActions=await pg.$$eval('.edit-actions',els=>els.length);
  return {before,after,editActions};
 });
 await scenario('biblio-see-all-and-popup','capture-biblio-explore.html',async pg=>{
  await pg.click('[data-action="see-all"]');
  const pinned=await pg.$eval('.discover-heading h1',e=>e.textContent);
  await pg.click('[data-action="filter-open"]');
  const options=await pg.$$eval('.original-popup button',els=>els.length);
  await pg.click('[data-action="filter-pick"][data-value="Sci-Fi & Fantasy"]');
  const selected=await pg.$eval('.discover-heading .countline',e=>e.textContent);
  await pg.click('[data-action="explore-return"]');
  const back=await pg.$eval('.explore-head h1',e=>e.textContent);
  return {pinned,options,selected,back};
 });
 await scenario('genre-details-view-and-library','capture-genre-results.html',async pg=>{
  await pg.click('[data-action="view-covers"]');
  const compact=await pg.$eval('.genre-grid',e=>e.classList.contains('covers'));
  await pg.click('[data-action="view-detail"]');
  await pg.click('[data-action="sort-toggle"]');
  const sorted=await pg.$eval('.sort-control',e=>e.textContent.trim());
  await pg.click('[data-action="library"]');
  const selected=await pg.$eval('.library-btn',e=>e.classList.contains('selected'));
  return {compact,sorted,selected};
 });
 await scenario('continue-context-menu','capture-continue-see-all.html',async pg=>{const tile=await pg.$('.continue-tile');await tile.click({button:'right'});const menuCount=await pg.$$eval('.tile-context [role="menuitem"]',els=>els.length);await pg.click('[data-action="context-remove"]');const removedCount=await pg.$$eval('.continue-tile',els=>els.length);return {menuCount,removedCount};});
 await scenario('continue-medium-and-status','capture-continue-see-all.html',async pg=>{
  await pg.click('[data-action="continue-sort"][data-value="watched"]');
  const watched=await pg.$$eval('.continue-tile',els=>els.length);
  await pg.click('[data-action="continue-sort"][data-value="recent"]');
  await pg.click('[data-action="continue-medium"][data-value="Books"]');
  const books=await pg.$$eval('.continue-tile',els=>els.length);
  return {watched,books};
 });
 result.summary={captureCount:result.cases.length,passCount:result.cases.filter(x=>x.pass).length,flagged:result.cases.filter(x=>!x.pass).map(x=>x.name),interactiveScenarios:result.interactions.length,unresolved:result.unresolved.length};
 }finally{await browser.close();}
 fs.writeFileSync(path.join(ROOT,'VERIFICATION.json'),JSON.stringify(result,null,2),'utf8');
 process.stdout.write('SUMMARY '+JSON.stringify(result.summary)+'\n');
}
run().catch(e=>{console.error('FATAL',e);process.exitCode=1;});
