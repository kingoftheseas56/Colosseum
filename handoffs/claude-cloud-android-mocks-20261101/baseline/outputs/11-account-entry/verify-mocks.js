'use strict';
const fs=require('fs'),path=require('path'),{pathToFileURL}=require('url');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const root=__dirname, cap=path.join(root,'captures');
const cases=[
 ['account-welcome','portrait',412,900,'mode=local'],
 ['account-welcome','landscape',900,412,'mode=local'],
 ['account-welcome','desktop',1280,700,'mode=local'],
 ['account-welcome','narrow',354,792,'mode=local&scale=1.3&top=24&bottom=24'],
 ['account-welcome','tablet',800,1280,'mode=local'],
 ['account-flyout','portrait',412,900,'mode=signed'],
 ['account-flyout','landscape',900,412,'mode=signed'],
 ['account-flyout','desktop',1280,700,'mode=signed'],
 ['account-flyout','local',412,900,'mode=local&v=portrait'],
 ['account-flyout','narrow-landscape',792,354,'mode=signed&stress=1&scale=1.3&top=24&bottom=24'],
 ['account-flyout','tablet',800,1280,'mode=signed'],
 ['account-centre','portrait',412,900,'mode=signed'],
 ['account-centre','landscape',900,412,'mode=signed'],
 ['account-centre','desktop',1280,700,'mode=signed'],
 ['account-centre','narrow',354,792,'mode=signed&stress=1&scale=1.3&top=24&bottom=24'],
 ['account-centre','tablet',800,1280,'mode=signed&section=privacy'],
 ['account-centre','tablet-landscape',1280,800,'mode=signed&section=devices'],
 ['account-centre','local',412,900,'mode=local&v=portrait'],
 ['account-flyout','portrait',412,900,'mode=signed&open=0','closed'],
 ['account-centre','portrait',412,900,'mode=signed&section=security&scenario=approval','security-approval'],
 ['account-centre','portrait',412,900,'mode=signed&section=security&expanded=password','security-password'],
 ['account-centre','portrait',412,900,'mode=signed&section=recovery&expanded=recovery','recovery-expanded'],
 ['account-centre','portrait',412,900,'mode=signed&section=privacy&expanded=search','privacy-clear']
];
const results=[],errors=[];
function assert(x,msg){if(!x)throw Error(msg);}
function within(r,w,h){return r.left>=-.5&&r.right<=w+.5&&r.top>=-.5&&r.bottom<=h+.5;}
(async()=>{
 const browser=await puppeteer.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true,args:['--disable-gpu','--no-first-run','--no-default-browser-check','--disable-extensions','--disable-background-networking'],userDataDir:path.join(cap,'puppeteer-process-'+process.pid)});
 try{
  for(const [name,v,w,h,query,suffix] of cases){
   const page=await browser.newPage();
   try{
    await page.setViewport({width:w,height:h,deviceScaleFactor:1});
    const u=pathToFileURL(path.join(root,name+'.html')).href+'?capture=1&v='+(v==='local'?'portrait':v)+'&'+query;
    await page.goto(u,{waitUntil:'load',timeout:12000});
    await page.evaluate(()=>document.fonts.ready);
    await page.screenshot({path:path.join(cap,name+'-'+(suffix||v)+'.png'),captureBeyondViewport:false});
    const stats=await page.evaluate(()=>{
      const box=e=>{const b=e.getBoundingClientRect();return {left:b.left,right:b.right,top:b.top,bottom:b.bottom,width:b.width,height:b.height};};
      const m=document.querySelector('.mock'),card=document.querySelector('.flyout-card'),flow=document.querySelector('.welcome-flow'),main=document.querySelector('.centre-content');
      return {pageSize:[innerWidth,innerHeight],mock:box(m),mockScroll:[m.scrollWidth,m.scrollHeight],
      card:card?box(card):null,cardScroll:card?[card.clientHeight,card.scrollHeight]:null,
      mainScroll:main?[main.clientHeight,main.scrollHeight]:null,
      flowScroll:flow?[flow.clientHeight,flow.scrollHeight]:null,
      imageErrors:[...document.images].filter(x=>!x.complete||x.naturalWidth===0).map(x=>x.src),
      fonts:{Fraunces:document.fonts.check('15px Fraunces'),Inter:document.fonts.check('13px Inter'),Segoe:document.fonts.check('13px SegoeLocal')},
      visibleSections:[...document.querySelectorAll('.rail-tab')].map(x=>({name:x.textContent.trim(),rect:box(x)})),
      clickableCount:[...document.querySelectorAll('#mock button:not(:disabled)')].length};
    });
    assert(stats.pageSize[0]===w&&stats.pageSize[1]===h,name+'/'+v+' viewport mismatch');
    assert(stats.mock.width===w&&stats.mock.height===h,name+'/'+v+' frame mismatch');
    assert(stats.imageErrors.length===0,name+'/'+v+' broken images: '+stats.imageErrors.join(','));
    if(stats.card){
      assert(stats.card.left>=-.5&&stats.card.right<=w+.5,name+'/'+v+' flyout horizontal clipping');
      assert(stats.card.top>=-.5&&stats.card.bottom<=h+.5,name+'/'+v+' flyout vertical clipping');
      assert(stats.cardScroll[0]>0,name+'/'+v+' flyout height unusable');
    }
    if(stats.visibleSections.length){
      assert(stats.visibleSections.length===5,name+'/'+v+' expected five sections');
      assert(stats.visibleSections.every(s=>within(s.rect,w,h)),name+'/'+v+' section clipping');
    }
    results.push({name,v,w,h,...stats});
   }catch(e){errors.push(name+'/'+v+': '+e.message);}
   finally{await page.close();}
  }
  const probe=async(name,query,w,h,fn)=>{
    const page=await browser.newPage();
    try{
      await page.setViewport({width:w,height:h});
      await page.goto(pathToFileURL(path.join(root,name+'.html')).href+'?capture=1&'+query,{waitUntil:'load'});
      await page.evaluate(()=>document.fonts.ready);
      await fn(page);
      results.push({interactionProbe:name,query,pass:true});
    }catch(e){errors.push('INTERACTION '+name+': '+e.message);}
    finally{await page.close();}
  };
  await probe('account-flyout','v=portrait&mode=signed',412,900,async(page)=>{
    assert(await page.$('.flyout-card'),'flyout initially open');
    await page.click('[data-action="close-flyout"]');
    assert((await page.$('.flyout-card'))===null,'flyout closed on backdrop');
    await page.click('[data-action="toggle-flyout"]');
    assert(await page.$('.flyout-card'),'profile door reopens flyout');
    await Promise.all([page.waitForNavigation(),page.click('[data-action="open-section"][data-section="devices"]')]);
    assert((await page.$eval('.centre-content h2',e=>e.textContent)).includes('Devices'),'flyout opens Devices');
  });
  await probe('account-flyout','v=portrait&mode=local',412,900,async(page)=>{
    assert(await page.$eval('.nav-wrap',e=>e.textContent.includes('Data & privacy')),'local privacy entry');
    assert(await page.$eval('.session-btn',e=>e.textContent.trim()==='Sign in'),'local sign in');
  });
  await probe('account-centre','v=portrait&mode=signed',412,900,async(page)=>{
    await page.click('[data-action="avatar"][data-id="laurel"]');
    assert(await page.$('.avatar-choice.selected[data-id="laurel"]'),'avatar preview');
    await page.click('[data-action="tab"][data-section="security"]');
    assert(await page.$eval('.centre-content h2',e=>e.textContent.trim()==='Security'),'security tab');
    await page.click('[data-action="password-toggle"]');
    assert(await page.$('.inline-panel .fields input[type="password"]'),'inline password controls');
    await page.click('[data-action="tab"][data-section="privacy"]');
    await page.click('[data-action="search-confirm"]');
    assert(await page.$eval('.inline-panel',e=>e.textContent.includes('Clear remembered searches')),'inline privacy confirmation');
  });
  await probe('account-welcome','v=portrait&mode=local',412,900,async(page)=>{
    assert((await page.$$('[data-action="continue-local"],[data-action="sign-in"],[data-action="create-account"]')).length===3,'three choices');
    await Promise.all([page.waitForNavigation(),page.click('[data-action="continue-local"]')]);
    assert(await page.$('.flyout-card'),'continue local route mock');
  });
 }finally{await browser.close();}
 const report={engine:'Puppeteer with installed Edge in isolated headless context',cases:results,failures:errors};
 fs.writeFileSync(path.join(cap,'BROWSER-VERIFICATION.json'),JSON.stringify(report,null,2),'utf8');
 console.log('CAPTURE_CASES',cases.length,'INTERACTION_PROBES',4,'PASS',errors.length===0,'FAILURES',errors.length);
 errors.forEach(e=>console.log('ERROR',e));
 process.exitCode=errors.length?1:0;
})().catch(e=>{console.error(e.stack);process.exitCode=1;});

