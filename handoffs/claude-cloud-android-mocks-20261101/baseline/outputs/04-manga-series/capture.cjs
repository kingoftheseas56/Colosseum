'use strict';
const fs=require('fs'),path=require('path'),{pathToFileURL}=require('url');
const puppeteer=require('C:/Users/Suprabha/Desktop/Brotherhood/Brotherhood-Desktop-Commander/node_modules/puppeteer');
const root=__dirname, capturePath=path.join(root,'captures'),profile=path.join(capturePath,'isolated-edge-profile');
const edge='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const sizes=[['portrait',412,900],['landscape',900,412],['desktop',1280,700],['compact',354,792],['short',792,354],['tablet',800,1280],['tabletLandscape',1280,800]];
const modes=['manga-series','manga-chapters'];
const result={command:'node '+__filename,browser:'Edge headless through Puppeteer (isolated profile)',screenshots:[],checks:[],errors:[]};
(async()=>{
 let browser;
 try {
   browser=await puppeteer.launch({headless:true,executablePath:edge,userDataDir:profile,args:['--disable-background-networking','--disable-extensions','--no-first-run','--no-default-browser-check']});
   for(const mode of modes) {
     const states=mode==='manga-series'?[['selected',412,900],['expanded',354,792],['insetsScale',412,900]]:[['language-open',412,900],['page-open',354,792],['insetsScale',412,900],['page-open-landscape',900,412]];
     const specs=sizes.map(s=>[s[0],s[1],s[2], '']).concat(states.map(s=>[s[0],s[1],s[2],s[0]==='insetsScale'?'top=24&bottom=24&scale=1.3':'state='+s[0].replace('-landscape','')]));
     for(const [label,w,h,state] of specs){
       const page=await browser.newPage(),errors=[];
       page.on('pageerror',e=>errors.push('JS: '+String(e)));
       page.on('requestfailed',r=>errors.push('request: '+r.url()+' '+(r.failure()&&r.failure().errorText)));
       await page.setViewport({width:w,height:h,deviceScaleFactor:1});
       const src=pathToFileURL(path.join(root,mode+'.html')).href+'?capture=1'+(state?'&'+state:'');
       await page.goto(src,{waitUntil:'load',timeout:20000});
       await page.waitForFunction(()=>!!document.querySelector('.hero-content')&&(!!document.querySelector('.volume-card')||!!document.querySelector('.chapter-row')),{timeout:10000});
       await new Promise(resolve=>setTimeout(resolve,450));
       const screenshot=mode+'-'+label+'.png';
       await page.screenshot({path:path.join(capturePath,screenshot),fullPage:false});
       const dom=await page.evaluate(()=>{
         const r=e=>e?{x:e.getBoundingClientRect().x,y:e.getBoundingClientRect().y,right:e.getBoundingClientRect().right,bottom:e.getBoundingClientRect().bottom,width:e.getBoundingClientRect().width,height:e.getBoundingClientRect().height}:null;
         const inside=(a,b)=>!a||!b||(a.x>=b.x-2&&a.right<=b.right+2);
         const img=Array.from(document.querySelectorAll('.app img')).filter(e=>!e.complete||!e.naturalWidth).map(e=>e.getAttribute('src'));
         const app=document.querySelector('#app'),scroll=document.querySelector('#appScroll'),chrome=document.querySelector('.chrome-brand'),back=document.querySelector('.back-action');
         const popup=document.querySelector('#menuPopup'),selector=document.querySelector('#languageSelector'),pager=document.querySelector('#pageSelector'),download=document.querySelector('.chapter-download');
         const abr=r(app),b=r(back),c=r(chrome),pop=r(popup);
         return {size:{w:app.clientWidth,h:app.clientHeight},scroll:{width:scroll.clientWidth,scrollWidth:scroll.scrollWidth,height:scroll.clientHeight,scrollHeight:scroll.scrollHeight,top:scroll.scrollTop},missingImages:img,brandBackOverlap:!!b&&!!c&&b.right>c.x&&b.x<c.right,
           popOpen:popup.classList.contains('open'),popupRect:popup.classList.contains('open')?pop:null,popupInside:popup.classList.contains('open')?inside(pop,abr):null,
           selectedVolume:(document.querySelector('.volume-card.active')||{}).dataset?.vol||null,chapters:document.querySelectorAll('.chapter-row').length,buttons:document.querySelectorAll('.app button').length,languageRect:r(selector),pageRect:r(pager),chapterDownloadRect:r(download),
           modeClass:document.querySelector('#device').className};
       });
       result.screenshots.push({file:screenshot,width:w,height:h,state:state||'closed'});
       result.checks.push({file:screenshot,dom,errors});
       console.log(screenshot+' | images-missing '+dom.missingImages.length+' | horizontal '+(dom.scroll.scrollWidth-dom.scroll.width)+' | popup '+dom.popOpen+' | overlap '+dom.brandBackOverlap);
       await page.close();
     }
   }
 }catch(e){result.errors.push(String(e.stack||e));console.error(e);}
 finally{
   if(browser)await browser.close();
   fs.writeFileSync(path.join(capturePath,'checks.json'),JSON.stringify(result,null,2));
   try{fs.rmSync(profile,{recursive:true,force:true});}catch(e){result.errors.push('profile cleanup: '+String(e));}
   console.log('SAVED '+path.join(capturePath,'checks.json')+'; captures '+result.screenshots.length+'; errors '+result.errors.length);
   if(result.errors.length)process.exitCode=1;
 }
})();

