(function statesModule(){
"use strict";
const fs=require("fs"),path=require("path"),crypto=require("crypto"),{pathToFileURL}=require("url"),qa=require("./qa-core.cjs");
const hash=f=>crypto.createHash("sha256").update(fs.readFileSync(f)).digest("hex");
exports.run=async(env,cmd)=>{
const matrix={
"account-profile":[["edited",{state:"edit"},"#profile-save"],["long",{state:"long"},".hero-name"],["saving",{state:"edit",response:"pending"},"#profile-save","Saving…"]],
"account-security":[["password",{state:"password"},"[data-act=security-password-save]"],["approval",{state:"approval"},"[data-act=approval-approve]"],["signout",{state:"logout"},"[data-act=security-logout-confirm]"],["saving",{state:"password",response:"pending"},"[data-act=security-password-save]","Saving…"],["approving",{state:"approval",response:"pending"},"[data-act=approval-approve]","Working…"],["signing-out",{state:"logout",response:"pending"},"[data-act=security-logout-confirm]","Signing out…"]],
"account-recovery":[["replace",{state:"replace"},"[data-act=recovery-generate]"],["generating",{state:"replace",response:"pending"},"[data-act=recovery-generate]","Generating…"]],
"account-privacy":[["search",{state:"search"},"[data-act=privacy-search-commit]"],["activity",{state:"activity"},"[data-act=privacy-activity-commit]"],["delete",{state:"delete"},"#delete-current"],["clearing-search",{state:"search",response:"pending"},"[data-act=privacy-search-commit]","Clearing…"],["clearing-activity",{state:"activity",response:"pending"},"[data-act=privacy-activity-commit]","Clearing…"],["deleting",{state:"delete",response:"pending"},"#privacy-delete-continue","Continuing…"],["exporting",{response:"pending"},"[data-act=privacy-export]","Cancel export"]],
"account-devices":[["revoke",{state:"revoke"},"[data-act=device-confirm]"],["revoking",{state:"revoke",response:"pending"},"[data-act=device-confirm]","Revoking…"],["refreshing",{response:"pending"},"[data-act=device-refresh]","Refreshing…"],["empty",{state:"empty"},".devices-empty"],["error",{state:"error"},".error-message"]],
"stremio-sync":[["switch",{state:"synced",confirm:"switch",long:1},"#primaryAction"],["disconnect",{state:"synced",confirm:"disconnect",long:1},"#primaryAction"],["connect-fields",{state:"notConnected",long:1},"#primaryAction"]],
"extension-setup":[["required-long-title",{required:1,addon:"Comet | ElfHosted"},"#paste-input"],["typed-install",{},"#paste-input"]],
"tankoyomi-configuration":[["menu",{menu:"open"},"#default-selector"],["about",{tab:"about"},"#about-panel"],["empty",{language:"pt"},"#provider-list"]]
};
const kinds=cmd.kinds||["portrait","landscape","small","short"],only=cmd.pages||Object.keys(matrix),all=[];
for(const name of only){if(!matrix[name])continue;const dir=path.join(env.batch,"finished-mocks",qa.tasks[name]),dest=path.join(env.out,"after","states",name);fs.mkdirSync(dest,{recursive:true});const rows=[];
for(const [id,extra,target,pendingLabel]of matrix[name])for(const kind of kinds){
const stress=kind==="small"||kind==="short",q={...(stress?qa.stressFor(qa.tasks[name]):{}),...extra};if(q.state==="long"&&!extra.state&&name!=="account-profile")delete q.state;
const url=pathToFileURL(path.join(dir,name+".html")).href+"?"+qa.queryFor(qa.tasks[name],kind,q),row={page:name,id,kind,url,utc:new Date().toISOString(),assertions:[],inputs:[name+".html","app.js","style.css","../mock-support.js"].map(f=>({path:path.resolve(dir,f),sha256:hash(path.resolve(dir,f))}))};let p;
const check=(v,label,observed)=>{row.assertions.push({pass:!!v,label,...(observed!==undefined?{observed}:{})});if(!v)throw Error(label+": "+JSON.stringify(observed));};
try{
const opened=await qa.prepare(env.browser,url,...qa.dimensions[kind]);p=opened.page;row.errors=opened.errors;row.blockedRequests=opened.blocked;
if(id==="typed-install")await p.type("#paste-input","https://fixture.invalid/manifest.json");
if(id!=="menu")await p.$eval(target,e=>e.scrollIntoView({block:"center",inline:"nearest"}));
await p.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
row.metrics=await qa.measure(p,qa.roots[qa.tasks[name]]);
check(row.metrics.brokenImages.length===0,"all assets load",row.metrics.brokenImages);check(opened.errors.length===0,"no JavaScript errors",opened.errors);check(opened.blocked.length===0,"no network attempt",opened.blocked);
if(pendingLabel){
const result=await p.$eval(target,e=>({text:e.textContent.trim(),disabled:e.disabled}));check(result.text===pendingLabel,"source pending label",result);check(result.disabled===(pendingLabel!=="Cancel export"),"source pending action availability",result);
if(pendingLabel==="Cancel export"){await p.click(target);check(await p.$eval(target,e=>e.textContent.trim())==="Request export","cancel pending export restores idle label");await p.reload({waitUntil:"load"});await p.$eval(target,e=>e.scrollIntoView({block:"center"}));}
}
if(id==="menu"){const count=await p.$$("[data-default]").then(x=>x.length);check(count===4,"all four inline language entries present",count);const fit=await p.$eval("[data-default]:last-child",e=>{const r=e.getBoundingClientRect(),a=document.querySelector("#tank-scroll").getBoundingClientRect();return {fits:r.top>=a.top&&r.bottom<=a.bottom,top:r.top,bottom:r.bottom,areaTop:a.top,areaBottom:a.bottom};});check(fit.fits,"open menu last option visible",fit);}
row.pass=true;
}catch(e){row.pass=false;row.failure=e.stack;console.log("STATE_FAIL "+name+" "+id+" "+kind+" "+e.message);}
finally{if(p){const png=path.join(dest,id+"-"+kind+".png");await p.screenshot({path:png,captureBeyondViewport:false}).catch(()=>{});row.screenshot=path.relative(env.batch,png).replace(/\\/g,"/");await p.close();}}
rows.push(row);all.push(row);fs.writeFileSync(path.join(dest,"audit.json"),JSON.stringify({page:name,generatedUtc:new Date().toISOString(),cases:rows},null,2)+"\n");
}
fs.appendFileSync(path.join(env.batch,"CHECKPOINT.md"),"\n- "+new Date().toISOString()+": "+name+" expanded/pending state checkpoint: "+rows.filter(r=>r.pass).length+"/"+rows.length+" cases across "+kinds.join(",")+"; evidence/after/states/"+name+"/audit.json.\n");
console.log("STATE_PAGE "+name+" "+rows.filter(r=>r.pass).length+"/"+rows.length);
}
const summary={generatedUtc:new Date().toISOString(),cases:all.length,passed:all.filter(r=>r.pass).length,failures:all.filter(r=>!r.pass).map(r=>({page:r.page,id:r.id,kind:r.kind,failure:r.failure?.split("\n")[0]}))};fs.writeFileSync(path.join(env.out,"after","STATE-SUMMARY.json"),JSON.stringify(summary,null,2)+"\n");return summary;
};
})();