(function smokeModule(){
"use strict";
const fs=require("fs"),path=require("path"),{pathToFileURL}=require("url"),qa=require("./qa-core.cjs");
exports.run=async(env,cmd)=>{
const out=path.join(env.out,"pack-smoke");fs.mkdirSync(out,{recursive:true});const rows=[];
async function test(name,relative,fn){let p;const r={name,path:relative,utc:new Date().toISOString()};
try{const x=await qa.prepare(env.browser,pathToFileURL(path.join(env.batch,"finished-mocks",relative)).href,1440,1100);p=x.page;r.errors=x.errors;r.blocked=x.blocked;r.detail=await fn(p);
r.brokenImages=await p.evaluate(()=>[...document.images].filter(e=>!e.complete||!e.naturalWidth).map(e=>e.src));r.fontErrors=await p.evaluate(()=>[...document.fonts].filter(f=>f.status==="error").map(f=>f.family));
r.pass=r.errors.length===0&&r.blocked.length===0&&r.brokenImages.length===0&&r.fontErrors.length===0&&r.detail.pass;
if(name==="catalogue"||name==="keyboard-decision"){r.screenshot=path.relative(env.batch,path.join(out,name+".png")).replace(/\\/g,"/");await p.screenshot({path:path.join(env.batch,r.screenshot),captureBeyondViewport:false});}
}catch(e){r.failure=e.stack;r.pass=false;}finally{if(p)await p.close();}rows.push(r);console.log("PACK "+name+" "+r.pass);}
await test("catalogue","index.html",async p=>{const count=await p.$$eval("tbody tr",es=>es.length);return {pass:count===13,finishedPageRows:count};});
await test("keyboard-decision","keyboard-decision.html",async p=>({pass:await p.$$("img").then(x=>x.length)===12}));
for(const [name,task]of Object.entries(qa.tasks)){
await test("workbench-"+name,task+"/"+name+".html",async p=>{
const root=await p.$(qa.roots[task]);return {pass:!!root,title:await p.title(),frame:root?await root.evaluate(e=>({w:e.offsetWidth,h:e.offsetHeight})):null};});
await test("compare-"+name,"comparisons/"+name+".html",async p=>{
await p.select("#viewport","landscape");
await p.waitForFunction(()=>{const fs=[...document.querySelectorAll("iframe")];return fs.length===2&&fs.every(f=>f.style.width==="900px"&&f.style.height==="412px");});
const frames=p.frames().filter(f=>f.parentFrame());
for(const f of frames)await f.waitForFunction(()=>{const e=document.querySelector("#device,#mock,#app-frame,#frame,#viewport");return e&&e.offsetWidth===900&&e.offsetHeight===412;});
const sizes=[];for(const f of frames)sizes.push(await f.evaluate(()=>{const e=document.querySelector("#device,#mock,#app-frame,#frame,#viewport");return{w:e.offsetWidth,h:e.offsetHeight,url:location.href};}));
return {pass:frames.length===2&&sizes.every(x=>x.w===900&&x.h===412),sizes};});
}
for(const task of [...new Set(Object.values(qa.tasks))])await test("subindex-"+task,task+"/index.html",async p=>({pass:await p.$$("a").then(x=>x.length)>0}));
const summary={utc:new Date().toISOString(),cases:rows.length,passed:rows.filter(x=>x.pass).length,failures:rows.filter(x=>!x.pass),results:rows};fs.writeFileSync(path.join(out,"audit.json"),JSON.stringify(summary,null,2)+"\n");return {cases:summary.cases,passed:summary.passed,failures:summary.failures};
};
})();