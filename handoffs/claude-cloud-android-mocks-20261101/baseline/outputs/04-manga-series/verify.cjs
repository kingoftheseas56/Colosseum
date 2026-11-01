'use strict';
const fs=require('fs'),path=require('path'),{pathToFileURL}=require('url');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const edge='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',root=__dirname;
const result={command:'node '+__filename,errors:[],checks:[],captures:[]};
const check=(name,value,details='')=>{result.checks.push({name,pass:!!value,details});if(!value)result.errors.push(name+(details?': '+details:''));};
(async()=>{
 let browser;
 try{
 browser=await puppeteer.launch({headless:true,executablePath:edge,userDataDir:path.join(root,'captures','verification-profile'),args:['--disable-background-networking','--disable-extensions','--no-first-run']});
 const create=async(filename,w,h,query='')=>{
 const page=await browser.newPage();await page.setViewport({width:w,height:h,deviceScaleFactor:1});
 page.on('pageerror',e=>result.errors.push(filename+' JS '+e.message));
 await page.goto(pathToFileURL(path.join(root,filename)).href+'?capture=1'+query,{waitUntil:'load',timeout:20000});
 await page.waitForSelector('.hero-content');return page;};
 const box=async(page,selector)=>page.$eval(selector,el=>{const r=el.getBoundingClientRect();return{x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height};});
 let pg=await create('manga-series.html',354,792);
 check('Synopsis collapsed initially',await pg.$eval('#synopsisMore',e=>e.textContent)==='More');
 await pg.click('#synopsisMore');check('Synopsis expands',await pg.$eval('#synopsisWrap',e=>e.classList.contains('expanded')));
 await pg.screenshot({path:path.join(root,'captures','manga-series-synopsis-open-compact.png')});result.captures.push('manga-series-synopsis-open-compact.png');
 await pg.click('#synopsisMore');check('Synopsis collapses',!(await pg.$eval('#synopsisWrap',e=>e.classList.contains('expanded'))));
 await pg.click('#bookmark');check('Bookmark toggles locally',await pg.$eval('#bookmark',e=>e.classList.contains('saved')));
 await pg.click('#selectToggle');await pg.click('.volume-card[data-vol="2"]');
 check('Volume Select retains owned style and local multi-selection',await pg.$eval('#selectionTray',e=>e.classList.contains('shown'))&&await pg.$eval('#selectedCount',e=>e.textContent)==='1 selected');
 await pg.click('#selectToggle');check('Done clears selection',!(await pg.$eval('#selectionTray',e=>e.classList.contains('shown'))));
 await pg.click('.volume-card[data-vol="5"]');check('Volume focus updates action',await pg.$eval('#focusVolumeLabel',e=>e.textContent)==='Vol. 5');
 await pg.evaluate(()=>{const el=document.querySelector('#appScroll');el.scrollTop=el.scrollHeight;});
 await new Promise(r=>setTimeout(r,120));
 const volAction=await box(pg,'.volume-action'),volApp=await box(pg,'.app');
 check('Volume action bar reachable after scrolling',volAction.y>=volApp.y-1&&volAction.bottom<=volApp.bottom+1,JSON.stringify({volAction,volApp}));
 await pg.screenshot({path:path.join(root,'captures','manga-series-action-scrolled.png')});result.captures.push('manga-series-action-scrolled.png');
 await pg.close();
 pg=await create('manga-chapters.html',412,900);
 await pg.click('#languageSelector');
 check('Original language dropdown opens',await pg.$eval('#menuPopup',e=>e.classList.contains('open')));
 let dims=await pg.evaluate(()=>{const p=document.querySelector('.popup').getBoundingClientRect(),a=document.querySelector('.app').getBoundingClientRect();return{x:p.left-a.left,y:p.top-a.top,right:p.right-a.right,bottom:p.bottom-a.bottom,w:a.width,h:a.height};});
 check('Language popup clamped within frame',dims.x>=-1&&dims.y>=-1&&dims.right<=dims.w+1&&dims.bottom<=dims.h+1,JSON.stringify(dims));
 await pg.click('#popupList button[data-choice="2"]');
 check('Portuguese selection applied',await pg.$eval('#languageText',e=>e.textContent)==='Português (Brasil)');
 await pg.click('#pageSelector');
 check('Original page dropdown opens',await pg.$eval('#menuPopup',e=>e.classList.contains('open')));
 dims=await pg.evaluate(()=>{const p=document.querySelector('.popup').getBoundingClientRect(),a=document.querySelector('.app').getBoundingClientRect();return{x:p.left-a.left,y:p.top-a.top,right:p.right-a.right,bottom:p.bottom-a.bottom,w:a.width,h:a.height};});
 check('Page popup clamped within frame',dims.x>=-1&&dims.y>=-1&&dims.right<=dims.w+1&&dims.bottom<=dims.h+1,JSON.stringify(dims));
 await pg.keyboard.press('Escape');check('Escape closes page popup',!(await pg.$eval('#menuPopup',e=>e.classList.contains('open'))));
 await pg.click('#pageSelector');await pg.click('#popupList button[data-choice="12"]');
 check('Page 13 selection loads bounded 10 chapters',await pg.$eval('#chapterRange',e=>e.textContent)==='Chapters 121–130'&&(await pg.$$eval('.chapter-row',n=>n.length))===10);
 await pg.$eval('#goTo',e=>e.value='42');await pg.focus('#goTo');await pg.keyboard.press('Enter');
 check('Go to Chapter navigates to corresponding page',await pg.$eval('#chapterRange',e=>e.textContent)==='Chapters 41–50');
 await pg.evaluate(()=>{const sc=document.querySelector('#appScroll');sc.scrollTop=sc.scrollHeight;});
 await new Promise(r=>setTimeout(r,120));
 const chapter=await box(pg,'.chapter-row:last-child'),chapterApp=await box(pg,'.app');
 check('Last chapter row reachable by vertical scroll',chapter.y>=chapterApp.y-1&&chapter.bottom<=chapterApp.bottom+1,JSON.stringify({chapter,chapterApp}));
 await pg.screenshot({path:path.join(root,'captures','manga-chapters-rows-scrolled.png')});result.captures.push('manga-chapters-rows-scrolled.png');
 await pg.close();
 for(const [w,h,label,expected] of [[1280,700,'desktop','desktop'],[800,1280,'tablet portrait','tablet'],[1280,800,'tablet landscape','tablet']]){
   pg=await create('manga-series.html',w,h);
   const cls=await pg.$eval('#device',e=>e.className);
   check('Preset route '+label,cls.split(' ').includes(expected),cls);
   const noOverflow=await pg.$eval('#appScroll',e=>e.scrollWidth<=e.clientWidth+1);
   check('No horizontal overflow '+label,noOverflow);
   await pg.close();
 }
 for(const [w,h,tag] of [[900,412,'landscape'],[792,354,'short'],[354,792,'compact'],[412,900,'insetsScale'],[354,792,'compactScale']]){
   const suffix=(tag==='insetsScale'||tag==='compactScale')?'&top=24&bottom=24&scale=1.3':'';
   pg=await create('manga-chapters.html',w,h,suffix);
   const headerFit=await pg.evaluate(()=>{const a=document.querySelector('.back-action').getBoundingClientRect(),b=document.querySelector('.chrome-brand').getBoundingClientRect(),c=document.querySelector('.chrome').getBoundingClientRect();return (a.right<=b.left||a.left>=b.right||a.bottom<=b.top||a.top>=b.bottom)&&b.bottom<=c.bottom+1;});
   check('Header back and wordmark separate '+tag,headerFit);
   if(tag==='compactScale'){await pg.screenshot({path:path.join(root,'captures','manga-chapters-compact-scale.png')});result.captures.push('manga-chapters-compact-scale.png');}
   await pg.click('#languageSelector');dims=await pg.evaluate(()=>{const b=document.querySelector('.popup').getBoundingClientRect(),a=document.querySelector('.app').getBoundingClientRect();return {fits:b.left>=a.left-1&&b.top>=a.top-1&&b.right<=a.right+1&&b.bottom<=a.bottom+1};});
   check('Language popup fits '+tag,dims.fits);
   await pg.keyboard.press('Escape');
   await pg.click('#pageSelector');dims=await pg.evaluate(()=>{const b=document.querySelector('.popup').getBoundingClientRect(),a=document.querySelector('.app').getBoundingClientRect();return {fits:b.left>=a.left-1&&b.top>=a.top-1&&b.right<=a.right+1&&b.bottom<=a.bottom+1, popup:{left:b.left,top:b.top,right:b.right,bottom:b.bottom},app:{left:a.left,top:a.top,right:a.right,bottom:a.bottom},open:document.querySelector('.popup').classList.contains('open')};});
   check('Page popup fits '+tag,dims.fits&&dims.open,JSON.stringify(dims));
   if(tag==='compactScale'){await pg.screenshot({path:path.join(root,'captures','manga-chapters-compact-page-popup.png')});result.captures.push('manga-chapters-compact-page-popup.png');}
   await pg.close();
 }
 }catch(e){result.errors.push(String(e.stack||e));}finally{
 if(browser)await browser.close();
 try{fs.rmSync(path.join(root,'captures','verification-profile'),{recursive:true,force:true});}catch(e){result.errors.push('profile cleanup: '+String(e));}
 fs.writeFileSync(path.join(root,'captures','interaction-checks.json'),JSON.stringify(result,null,2));
 console.log('checks',result.checks.length,'pass',result.checks.filter(x=>x.pass).length,'fail',result.errors.length);
 for(const e of result.errors)console.error(e);
 console.log('SAVED '+path.join(root,'captures','interaction-checks.json'));
 if(result.errors.length)process.exitCode=1;
 }
})();

