"use strict";
const fs=require("fs"),path=require("path");
const cap=path.join(__dirname,"captures");
const expected={
 "extension-setup-portrait":[412,900],"extension-setup-landscape":[900,412],"extension-setup-desktop":[1280,700],
 "extension-setup-small":[354,792],"extension-setup-long-addon":[354,792],"extension-setup-short-keyboard":[792,354],
 "extension-setup-short-long":[792,354],"extension-setup-portrait-keyboard":[412,900],"extension-setup-closed":[412,900],
 "extension-setup-tablet":[800,1280],"extension-setup-tablet-landscape":[1280,800],
 "tankoyomi-configuration-portrait":[412,900],"tankoyomi-configuration-landscape":[900,412],"tankoyomi-configuration-desktop":[1280,700],
 "tankoyomi-configuration-small":[354,792],"tankoyomi-configuration-short":[792,354],
 "tankoyomi-configuration-menu-open":[412,900],"tankoyomi-configuration-about":[412,900],"tankoyomi-configuration-keyboard":[412,900],
 "tankoyomi-configuration-tablet":[800,1280],"tankoyomi-configuration-tablet-landscape":[1280,800],
 "tankoyomi-configuration-provider-portrait":[412,900],"tankoyomi-configuration-provider-landscape":[900,412],
 "tankoyomi-configuration-provider-short":[792,354],"tankoyomi-configuration-provider-small":[354,792],
 "extension-setup-short-keyboard-focused":[792,354]};
const captions=Object.entries(expected).map(([name,[width,height]])=>{
 const filename=path.join(cap,name+".png");
 if(!fs.existsSync(filename))return {name,exists:false,valid:false,expected:[width,height]};
 const bytes=fs.readFileSync(filename),sig="89504e470d0a1a0a";
 const valid=bytes.subarray(0,8).toString("hex")===sig && bytes.readUInt32BE(16)===width && bytes.readUInt32BE(20)===height;
 return {name,exists:true,valid,width:bytes.readUInt32BE(16),height:bytes.readUInt32BE(20),bytes:bytes.length};
});
const core={};
for(const type of ["extension-setup","tankoyomi-configuration"]){
 core[type]={};
 for(const variant of ["portrait","landscape","desktop"]){
  const p=path.join(cap,type,variant+".png"),name=type+"-"+variant+".png";
  const b=fs.existsSync(p)?fs.readFileSync(p):null;
  core[type][variant]={exists:!!b,exactDuplicate:!!b && b.equals(fs.readFileSync(path.join(cap,name)))};
 }
}
const interaction=JSON.parse(fs.readFileSync(path.join(cap,"interaction-audit.json"),"utf8"));
const render=JSON.parse(fs.readFileSync(path.join(cap,"render-audit.json"),"utf8"));
const result={kind:"capture-file-and-prior-evidence-reconciliation",explicitLimitations:["This script checks PNG headers, pixel dimensions, and byte identity of canonical per-page captures; it does not re-render.","The last 21-state rerun was interrupted after 13 successful zero-error screenshots because isolated Chrome stopped progressing. Original completed 19-state render audit is retained and covers both pages.","26 interactions were rerun after final setup and source-shell edits; they passed with no JavaScript errors."],expected:captions.length,presentAndDimensionCorrect:captions.filter(x=>x.valid).length,requiredCore:core,priorCompletedRenderReceipts:render.receipts.length,priorRenderEvidenceClean:render.receipts.every(r=>r.errors?.length===0 && (r.details?.assets||[]).every(x=>x.ok) && (r.details?.horizontalOutOfFrame||[]).length===0),interactionChecks:interaction.checks.length,interactionPassed:interaction.checks.filter(x=>x.pass).length,interactionErrors:interaction.errors,captures:captions};
fs.writeFileSync(path.join(cap,"capture-reconciliation.json"),JSON.stringify(result,null,2));
console.log(JSON.stringify({count:result.expected,valid:result.presentAndDimensionCorrect,core,priorRenderReceipts:result.priorCompletedRenderReceipts,priorClean:result.priorRenderEvidenceClean,checksPassed:result.interactionPassed,interactionErrors:result.interactionErrors}));
if(result.presentAndDimensionCorrect!==result.expected || Object.values(core).some(v=>Object.values(v).some(x=>!x.exists||!x.exactDuplicate)) ||result.interactionPassed!==result.interactionChecks)process.exitCode=1;
