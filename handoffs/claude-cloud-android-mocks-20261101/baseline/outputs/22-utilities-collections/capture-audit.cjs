
'use strict';
// Isolated, no-network local HTTP review using the BDC host's existing Puppeteer installation.
// The HTML pages do not import this tool or need its dependencies to run.
const fs=require('node:fs');
const http=require('node:http');
const path=require('node:path');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const ROOT='D:\\Colosseum-Android-Phone-Mocks-20261101';
const OUT=path.join(ROOT,'outputs','22-utilities-collections');
const EDGE='C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const MIME={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8','.ttf':'font/ttf','.otf':'font/otf','.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.ico':'image/x-icon','.svg':'image/svg+xml','.webp':'image/webp'};
const CASES=[
 ['wallpaper-portrait','wallpaper-search.html','portrait','Selected',412,900,1,false],
 ['wallpaper-landscape','wallpaper-search.html','landscape','Selected',900,412,1,false],
 ['wallpaper-desktop','wallpaper-search.html','desktop','Selected',1280,700,1,false],
 ['extensions-portrait','extensions-store.html','portrait','Store',412,900,1,false],
 ['extensions-landscape','extensions-store.html','landscape','Store',900,412,1,false],
 ['extensions-desktop','extensions-store.html','desktop','Store',1280,700,1,false],
 ['update-portrait','update.html','portrait','Available',412,900,1,false],
 ['update-landscape','update.html','landscape','Available',900,412,1,false],
 ['update-desktop','update.html','desktop','Available',1280,700,1,false],
 ['collections-portrait','collections.html','portrait','Overview',412,900,1,false],
 ['collections-landscape','collections.html','landscape','Detail',900,412,1,false],
 ['collections-desktop','collections.html','desktop','Overview',1280,700,1,false],
 ['wallpaper-compact-keyboard','wallpaper-search.html','compact portrait','Selected + keyboard',354,792,1.3,true],
 ['extensions-url-preview','extensions-store.html','compact portrait','URL preview',354,792,1.3,false],
 ['extensions-manage','extensions-store.html','portrait','Your Add-ons',412,900,1.0,false],
 ['collections-new','collections.html','compact portrait','New collection',354,792,1.3,true],
 ['collections-membership','collections.html','portrait','Add to Collections',412,900,1.0,false],
 ['collections-edit','collections.html','compact landscape','Editing',792,354,1.0,false],
 ['update-downloading','update.html','compact landscape','Downloading',792,354,1.3,false]
];
const server=http.createServer((req,res)=>{
 try{
  let u=new URL(req.url,'http://127.0.0.1');
  let target=path.resolve(ROOT,'.'+decodeURIComponent(u.pathname).split('/').join(path.sep));
  if(!target.startsWith(ROOT+path.sep)){res.writeHead(403);res.end('Forbidden');return;}
  fs.stat(target,(err,stat)=>{
   if(err||!stat.isFile()){res.writeHead(404);res.end('Not found');return;}
   res.writeHead(200,{'Content-Type':MIME[path.extname(target).toLowerCase()]||'application/octet-stream','Cache-Control':'no-store'});
   fs.createReadStream(target).pipe(res);
  });
 }catch(err){res.writeHead(400);res.end('Bad request');}
});
const audit=[];
async function main(){
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const port=server.address().port;
 const base='http://127.0.0.1:'+port;
 const browser=await puppeteer.launch({executablePath:EDGE,headless:true,userDataDir:path.join(OUT,'.audit-profile'),args:['--no-first-run','--disable-extensions','--disable-background-networking','--disable-features=EdgeFirstRunExperience','--no-default-browser-check']});
 try{
  for(const [slug,file,preset,state,w,h,fontScale,keyboard] of CASES){
   const page=await browser.newPage();
   try{
    const errors=[],blocked=[];
    await page.setViewport({width:w,height:h,deviceScaleFactor:1});
    await page.setRequestInterception(true);
    page.on('request',request=>{
     if(request.url().startsWith(base+'/'))request.continue();
     else{blocked.push(request.url());request.abort();}
    });
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    const url=base+'/outputs/22-utilities-collections/'+file+'?capture=1&preset='+encodeURIComponent(preset)+'&state='+encodeURIComponent(state)+'&fontScale='+fontScale+(keyboard?'&top=8&bottom=18&keyboard=1':'');
    await page.goto(url,{waitUntil:'load',timeout:25000});
    await page.evaluate(async()=>{await document.fonts.ready;});
    await page.screenshot({path:path.join(OUT,'captures',slug+'.png'),captureBeyondViewport:false});
    const result=await page.evaluate(()=>{
      const vp={width:window.innerWidth,height:window.innerHeight};
      const box=x=>{if(!x)return null;let r=x.getBoundingClientRect();return {x:Math.round(r.x),y:Math.round(r.y),width:Math.round(r.width),height:Math.round(r.height),right:Math.round(r.right),bottom:Math.round(r.bottom)};};
      const root=document.querySelector('.app-surface');
      const imgs=[...document.images].filter(img=>{let cs=getComputedStyle(img);return cs.display!=='none'&&cs.visibility!=='hidden'&&img.offsetParent!==null&&!img.naturalWidth;}).map(x=>x.currentSrc||x.src);
      const pageKey=document.body.dataset.page;
      const selectors={wallpaper:['.wall-panel','.wall-head','.wall-search','.wall-body','.wall-selected'],extensions:['.ext-shell-head','.ext-hero','.house-tray','.ext-section-head'],update:['.update-back','.update-headline','.update-copy','.update-nav','.update-status'],collections:['.collections-scroll','.collections-top','.collections-header','.collections-grid']};
      const keyboxes={};for(let selector of selectors[pageKey]||[]){let el=document.querySelector(selector);if(el)keyboxes[selector]=box(el);}
      const fullyVisible=el=>{let r=el.getBoundingClientRect();let s=getComputedStyle(el);return s.display!=='none'&&s.visibility!=='hidden'&&r.width>0&&r.height>0&&r.left>=-1&&r.top>=-1&&r.right<=vp.width+1&&r.bottom<=vp.height+1;};
      const small=(document.querySelector('.device').classList.contains('phone')?[...document.querySelectorAll('#mock button[data-act]')].filter(fullyVisible).filter(el=>{let r=el.getBoundingClientRect();return r.width<42||r.height<42;}).map(el=>({act:el.dataset.act,aria:el.getAttribute('aria-label')||el.textContent.trim().slice(0,45),box:box(el)})):[]);
      const unnamed=[...document.querySelectorAll('#mock button')].filter(fullyVisible).filter(el=>!(el.getAttribute('aria-label')||el.textContent.trim()||el.getAttribute('title')||el.querySelector('img[alt]:not([alt=""])'))).slice(0,12).map(el=>({className:el.className,box:box(el)}));
      const outerOverflow={x:document.documentElement.scrollWidth-vp.width,y:document.documentElement.scrollHeight-vp.height};
      let overlap=null;if(pageKey==='update'){let a=keyboxes['.update-nav'],b=keyboxes['.update-status'];if(a&&b)overlap=!(a.bottom<=b.y||b.bottom<=a.y||a.right<=b.x||b.right<=a.x);}
      return {viewport:vp,root:box(root),keyboxes,loadedImages:[...document.images].filter(x=>!!x.naturalWidth).length,missingImages:imgs,smallVisibleTargets:small,unnamedVisibleButtons:unnamed,outerOverflow,updateNavStatusOverlap:overlap};
    });
    audit.push({slug,file,preset,state,fontScale,keyboard,errors,blockedRequests:blocked,results:result});
    console.log(slug+' '+result.viewport.width+'x'+result.viewport.height+' images='+result.loadedImages+' missing='+result.missingImages.length+' smallTargets='+result.smallVisibleTargets.length+' unnamed='+result.unnamedVisibleButtons.length+' jsErrors='+errors.length);
   }catch(err){audit.push({slug,error:String(err)});console.log('FAILED '+slug+' '+err.message);}
   finally{await page.close();}
  }
  const testPage=await browser.newPage();
  await testPage.setViewport({width:412,height:900,deviceScaleFactor:1});
  const steps=[];
  async function go(file){await testPage.goto(base+'/outputs/22-utilities-collections/'+file+'?capture=1&preset=portrait',{waitUntil:'load'});}
  try{await go('wallpaper-search.html');await testPage.click('[data-act=wallPick][data-id=noir]');steps.push({test:'wallpaper-selection',pass:await testPage.$eval('.wall-selected h2',el=>el.textContent)==='Noir Flow'});await testPage.click('[data-act=wallSort][data-mode=top]');steps.push({test:'wallpaper-sort',pass:await testPage.$eval('[data-act=wallSort][data-mode=top]',el=>el.classList.contains('active'))});
  await go('extensions-store.html');await testPage.click('[data-act=extUrl]');steps.push({test:'extensions-url-open',pass:await testPage.$eval('.ext-url-modal h2',el=>el.textContent)==='Install from URL'});await testPage.click('[data-act=closeModal]');await testPage.click('[data-act=extManage]');steps.push({test:'extensions-manage-open',pass:await testPage.$eval('.ext-subpage .sub-title',el=>el.textContent)==='Your Add-ons'});
  await go('update.html');await testPage.click('[data-act=chapterNext]');steps.push({test:'update-next-chapter',pass:await testPage.$eval('.update-count',el=>el.textContent.trim())==='02 / 03'});
  await go('collections.html');await testPage.click('[data-act=openCollection]');steps.push({test:'collection-open',pass:!!(await testPage.$('.collection-detail'))});await testPage.click('[data-act=collectionEdit]');steps.push({test:'collection-edit',pass:!!(await testPage.$('#collectionNameEdit'))});await testPage.click('[data-act=collectionCancel]');await testPage.click('[data-act=collectionDelete]');steps.push({test:'collection-delete-confirmation',pass:await testPage.$eval('.collection-popup h2',el=>el.textContent)==='Delete this collection?'});
  }catch(err){steps.push({test:'interaction-run',pass:false,error:String(err)});}
  audit.push({interactions:steps});
  console.log('interaction '+JSON.stringify(steps));
  await testPage.close();
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
 fs.writeFileSync(path.join(OUT,'captures','audit.json'),JSON.stringify({engine:'Puppeteer + installed Edge, isolated headless; no external requests permitted',testedAt:new Date().toISOString(),cases:audit},null,2));
}
main().catch(err=>{console.error(err);server.close();process.exitCode=1;});
