(function moduleBody(){
"use strict";
const fs=require("fs"),path=require("path"),crypto=require("crypto"),{pathToFileURL}=require("url"),qa=require("./qa-core.cjs");
const hash=f=>crypto.createHash("sha256").update(fs.readFileSync(f)).digest("hex");
exports.run=async(env,cmd)=>{
 const dest=path.join(env.out,"proposals","account-keyboard");fs.mkdirSync(dest,{recursive:true});
 const pages={"account-profile":{state:"long",field:"#profile-name"},"account-security":{state:"password",field:"#security-current"},"account-recovery":{state:"replace",field:"#recovery-current"},"account-privacy":{state:"delete",field:"#delete-current"}},results=[];
 for(const [name,info]of Object.entries(pages)){
 for(const variant of ["original","bounded","scroll-proposal"]){
 const dir=variant==="original"?path.join(env.base,"outputs","12-account-forms"):path.join(env.batch,"finished-mocks","12-account-forms");
 const url=pathToFileURL(path.join(dir,name+".html")).href+"?"+qa.queryFor("12-account-forms","short",{state:info.state,keyboard:1,ime:152,imeLayout:variant==="scroll-proposal"?"scroll":"fixed",top:24,bottom:24,font:1.3});
 let p;const row={page:name,variant,url,utc:new Date().toISOString(),inputs:[name+".html","app.js","style.css",...(variant==="original"?[]:["../mock-support.js"])].map(f=>({path:path.resolve(dir,f),sha256:hash(path.resolve(dir,f))}))};
 try{
 const opened=await qa.prepare(env.browser,url,792,354);p=opened.page;
 row.errors=opened.errors;row.blockedRequests=opened.blocked;
 await p.$eval(info.field,e=>{e.focus();e.scrollIntoView({block:"center",inline:"nearest"});});
 await p.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 row.geometry=await p.evaluate((field,variant)=>{
 const root=document.querySelector("#app-frame"),el=document.querySelector(field),area=document.querySelector(variant==="scroll-proposal"?".main":".page-scroll");
 const r=el.getBoundingClientRect(),a=area.getBoundingClientRect(),b=root.getBoundingClientRect(),ime=root.querySelector(".keyboard-sim");
 const rect=x=>({x:x.x,y:x.y,width:x.width,height:x.height,bottom:x.bottom,right:x.right});
 return {requestedIme:152,appliedIme:Number(root.dataset.imeApplied||getComputedStyle(root).getPropertyValue("--keyboard").replace("px","")),input:rect(r),scrollViewport:rect(a),frame:rect(b),inputFullyVisible:r.top>=a.top-.5&&r.bottom<=a.bottom+.5&&r.left>=a.left-.5&&r.right<=a.right+.5,scrollHeight:area.scrollHeight,scrollTop:area.scrollTop};
 },info.field,variant);
 row.metrics=await qa.measure(p,"#app-frame");row.screenshot=path.relative(env.batch,path.join(dest,name+"-"+variant+".png")).replace(/\\/g,"/");await p.screenshot({path:path.join(env.batch,row.screenshot),captureBeyondViewport:false});
 row.pass=row.errors.length===0&&row.metrics.brokenImages.length===0&&row.blockedRequests.length===0&&(variant==="original"?!row.geometry.inputFullyVisible:row.geometry.inputFullyVisible);
 }catch(e){row.failure=e.stack;row.pass=false;}finally{if(p)await p.close();}
 results.push(row);fs.writeFileSync(path.join(dest,"audit.json"),JSON.stringify({generatedUtc:new Date().toISOString(),decision:"Hemanth: whether original account navigation may join vertical scrolling while the native IME is visible; opt-in proposal only.",cases:results},null,2)+"\n");console.log("KEYBOARD "+name+" "+variant+" "+row.pass+" "+JSON.stringify(row.geometry));
 }
 }
 return {cases:results.length,passed:results.filter(x=>x.pass).length,failures:results.filter(x=>!x.pass).map(x=>({page:x.page,variant:x.variant,error:x.failure,geometry:x.geometry}))};
};
})();
