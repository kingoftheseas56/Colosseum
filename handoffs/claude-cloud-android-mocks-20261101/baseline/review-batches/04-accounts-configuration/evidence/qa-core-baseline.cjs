"use strict";
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{pathToFileURL}=require('url');
const tasks={'profile':'10-profile','account-welcome':'11-account-entry','account-flyout':'11-account-entry','account-centre':'11-account-entry','account-profile':'12-account-forms','account-security':'12-account-forms','account-recovery':'12-account-forms','account-privacy':'12-account-forms','account-devices':'12-account-forms','account-your-colosseum':'12-account-forms','stremio-sync':'13-stremio-sync','extension-setup':'14-extension-setup','tankoyomi-configuration':'14-extension-setup'};
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const dimensions={portrait:[412,900],landscape:[900,412],small:[354,792],short:[792,354],desktop:[1280,700],tablet:[800,1280],tabletLandscape:[1280,800]};
const presets={
 '10-profile':{},
 '11-account-entry':{portrait:'portrait',landscape:'landscape',small:'narrow',short:'narrow-landscape',desktop:'desktop',tablet:'tablet',tabletLandscape:'tablet-landscape'},
 '12-account-forms':{portrait:'phone412',landscape:'landscape900',small:'phone354',short:'landscape792',desktop:'desktop',tablet:'tablet800',tabletLandscape:'tablet1280'},
 '13-stremio-sync':{portrait:'portrait',landscape:'landscape',small:'smallPortrait',short:'smallLandscape',desktop:'desktop',tablet:'tabletPortrait',tabletLandscape:'tabletLandscape'},
 '14-extension-setup':{portrait:'portrait',landscape:'landscape',small:'small',short:'smallLandscape',desktop:'desktop',tablet:'tablet',tabletLandscape:'tabletLandscape'}
};
function queryFor(task,kind,extra={}){
 const [w,h]=dimensions[kind],q=new URLSearchParams({capture:'1'});
 if(task==='10-profile'){q.set('w',w);q.set('h',h);}
 if(task==='11-account-entry')q.set('v',presets[task][kind]);
 if(task==='12-account-forms'||task==='14-extension-setup')q.set('preset',presets[task][kind]);
 if(task==='13-stremio-sync')q.set('capture',presets[task][kind]);
 for(const [key,value] of Object.entries(extra))q.set(key,value);
 return q.toString();
}
function stressFor(task){
 if(task==='10-profile'||task==='11-account-entry')return {top:24,bottom:24,scale:1.3,...(task==='11-account-entry'?{stress:1}:{})};
 if(task==='12-account-forms')return {top:24,bottom:24,font:1.3,state:'long'};
 if(task==='13-stremio-sync')return {inset:24,font:1.3,long:1};
 return {insetTop:24,insetBottom:24,scale:1.3};
}
const roots={'10-profile':'#device','11-account-entry':'#mock','12-account-forms':'#app-frame','13-stremio-sync':'#frame','14-extension-setup':'#viewport'};
async function measure(page,rootSelector){
 return page.evaluate(sel=>{
  const root=document.querySelector(sel),r=root.getBoundingClientRect();
  const box=e=>{const b=e.getBoundingClientRect();return {x:b.x-r.x,y:b.y-r.y,width:b.width,height:b.height,right:b.right-r.x,bottom:b.bottom-r.y};};
  const visible=e=>{const s=getComputedStyle(e),b=e.getBoundingClientRect();return b.width>0&&b.height>0&&s.visibility!=='hidden'&&s.display!=='none'&&!e.closest('[hidden]');};
  const label=e=>e.getAttribute('aria-label')||e.id||e.innerText?.trim().slice(0,75)||e.tagName;
  const controls=[...root.querySelectorAll('button,input,select,a,[tabindex]')].filter(visible).map(e=>({tag:e.tagName,id:e.id,label:label(e),disabled:!!e.disabled,type:e.getAttribute('type'),rect:box(e),font:getComputedStyle(e).fontSize,tabIndex:e.tabIndex}));
  const scrollers=[root,...root.querySelectorAll('*')].filter(e=>{const s=getComputedStyle(e);return visible(e)&&/(auto|scroll)/.test(s.overflowY+s.overflowX);}).map(e=>({tag:e.tagName,id:e.id,cls:e.className,rect:box(e),scrollWidth:e.scrollWidth,clientWidth:e.clientWidth,scrollHeight:e.scrollHeight,clientHeight:e.clientHeight,scrollTop:e.scrollTop,overflowX:getComputedStyle(e).overflowX,overflowY:getComputedStyle(e).overflowY}));
  const textLeaves=[...root.querySelectorAll('*')].filter(e=>visible(e)&&e.children.length===0&&e.textContent.trim()).map(e=>({tag:e.tagName,cls:e.className,text:e.textContent.trim().slice(0,160),rect:box(e),font:getComputedStyle(e).font,fontFamily:getComputedStyle(e).fontFamily}));
  return {viewport:{width:innerWidth,height:innerHeight},root:box(root),active:{tag:document.activeElement.tagName,id:document.activeElement.id,label:label(document.activeElement)},text:root.innerText,controls,scrollers,textLeaves,brokenImages:[...document.images].filter(e=>!e.complete||!e.naturalWidth).map(e=>e.getAttribute('src')),fonts:[...document.fonts].map(f=>({family:f.family,status:f.status,weight:f.weight})),horizontalControlOverflow:controls.filter(c=>c.rect.x<-.5||c.rect.right>r.width+.5).map(c=>({label:c.label,rect:c.rect}))};
 },rootSelector);
}
async function prepare(browser,url,width,height){
 const page=await browser.newPage(),errors=[],blocked=[];
 page.setDefaultTimeout(6000);page.setDefaultNavigationTimeout(12000);
 await page.setViewport({width,height,deviceScaleFactor:1});
 await page.setRequestInterception(true);
 page.on('request',r=>{if(/^https?:|^wss?:/i.test(r.url())){blocked.push(r.url());r.abort();}else r.continue();});
 page.on('pageerror',e=>errors.push(String(e)));
 await page.goto(url,{waitUntil:'load'});
 await page.evaluate(()=>Promise.race([document.fonts.ready,new Promise(r=>setTimeout(r,4000))]));
 await page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 return {page,errors,blocked};
}
exports.tasks=tasks;exports.dimensions=dimensions;exports.queryFor=queryFor;exports.roots=roots;exports.prepare=prepare;exports.measure=measure;exports.stressFor=stressFor;
exports.run=async(env,command)=>{
 const {browser,base,batch,out}=env,variant=command.variant||'before';
 const pages=command.pages||Object.keys(tasks);
 const summary=[];
 for(const name of pages){
 const task=tasks[name],dir=variant==='before'?path.join(base,'outputs',task):path.join(batch,'finished-mocks',task),html=path.join(dir,name+'.html');
 const dest=path.join(out,variant,name);fs.mkdirSync(dest,{recursive:true});
 const sourceFiles=[name+'.html','app.js','style.css'].map(n=>({path:path.join(dir,n),sha256:hash(path.join(dir,n))}));
 const result={page:name,task,variant,generatedUtc:new Date().toISOString(),sourceFiles,cases:[],errors:[]};
 const cases=command.cases||[{name:'portrait',kind:'portrait'},{name:'landscape',kind:'landscape'},{name:'desktop',kind:'desktop'},{name:'small-stress',kind:'small',stress:true},{name:'short-stress',kind:'short',stress:true}];
 for(const c of cases){
 let page;
 try{
 const [width,height]=dimensions[c.kind],extra={...(c.stress?stressFor(task):{}),...c.query};
 const url=pathToFileURL(html).href+'?'+queryFor(task,c.kind,extra);
 const p=await prepare(browser,url,width,height);page=p.page;
 const metrics=await measure(page,roots[task]);
 const png=path.join(dest,c.name+'.png');await page.screenshot({path:png,captureBeyondViewport:false});
 const entry={name:c.name,kind:c.kind,url,width,height,metrics,errors:p.errors,blockedRequests:p.blocked,screenshot:path.relative(batch,png).replace(/\\/g,'/')};
 if(c.bottom){
  await page.evaluate(sel=>{for(const e of [document.querySelector(sel),...document.querySelector(sel).querySelectorAll('*')])if(/auto|scroll/.test(getComputedStyle(e).overflowY))e.scrollTop=e.scrollHeight;},roots[task]);
  entry.bottomMetrics=await measure(page,roots[task]);const bottom=path.join(dest,c.name+'-bottom.png');await page.screenshot({path:bottom,captureBeyondViewport:false});entry.bottomScreenshot=path.relative(batch,bottom).replace(/\\/g,'/');
 }
 result.cases.push(entry);
 console.log(variant+' '+name+' '+c.name+' '+width+'x'+height+' errors='+p.errors.length+' assets='+metrics.brokenImages.length+' horizontalCandidates='+metrics.horizontalControlOverflow.length);
 }catch(e){result.errors.push({case:c.name,error:e.stack});console.log('CASE_ERROR '+name+' '+c.name+' '+e.message);}
 finally{if(page)await page.close();}
 fs.writeFileSync(path.join(dest,'audit.json'),JSON.stringify(result,null,2)+'\n');
 }
 result.unchangedDuringCapture=sourceFiles.every(s=>hash(s.path)===s.sha256);
 fs.writeFileSync(path.join(dest,'audit.json'),JSON.stringify(result,null,2)+'\n');
 summary.push({page:name,cases:result.cases.length,errors:result.errors.length,inputStable:result.unchangedDuringCapture});
 fs.appendFileSync(path.join(batch,'CHECKPOINT.md'),'\n- '+new Date().toISOString()+': '+variant+' capture checkpoint '+name+': '+result.cases.length+' cases; '+result.errors.length+' capture errors. Visual/source/interaction verdict pending.\n');
 }
 return {variant,pages:summary};
};