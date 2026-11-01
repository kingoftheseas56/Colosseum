(function extraBefore(){
"use strict";
const fs=require("fs"),path=require("path"),crypto=require("crypto"),{pathToFileURL}=require("url"),qa=require("./qa-core.cjs");
exports.run=async(env,cmd)=>{
const out=path.join(env.out,"before","expanded-examples");fs.mkdirSync(out,{recursive:true});const rows=[];
for(const [name,extra,target]of [["extension-setup",{required:1,addon:"Comet | ElfHosted"},"#paste-input"],["tankoyomi-configuration",{menu:"open"},"#default-selector"]])for(const kind of ["landscape","short"]){
const dir=path.join(env.base,"outputs","14-extension-setup"),url=pathToFileURL(path.join(dir,name+".html")).href+"?"+qa.queryFor("14-extension-setup",kind,{...(kind==="short"?qa.stressFor("14-extension-setup"):{}),...extra});const row={page:name,kind,url,utc:new Date().toISOString(),sourceFiles:[name+".html","app.js","style.css"].map(f=>({path:path.join(dir,f),sha256:crypto.createHash("sha256").update(fs.readFileSync(path.join(dir,f))).digest("hex")}))};
const x=await qa.prepare(env.browser,url,...qa.dimensions[kind]);try{await x.page.$eval(target,e=>e.scrollIntoView({block:"center"}));row.metrics=await qa.measure(x.page,"#viewport");row.errors=x.errors;const png=path.join(out,name+"-"+kind+".png");await x.page.screenshot({path:png,captureBeyondViewport:false});row.screenshot=path.relative(env.batch,png).replace(/\\/g,"/");}finally{await x.page.close();}rows.push(row);
}
fs.writeFileSync(path.join(out,"audit.json"),JSON.stringify({cases:rows},null,2)+"\n");return {examples:rows.length};
};
})();
