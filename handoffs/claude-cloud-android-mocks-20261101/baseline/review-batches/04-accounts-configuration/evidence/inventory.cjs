"use strict";
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const out=__dirname,base=path.resolve(out,'../../..');
const manifest=JSON.parse(fs.readFileSync(path.join(out,'../INPUTS.json'),'utf8').replace(/^\uFEFF/,''));
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const record=f=>{if(!fs.existsSync(f))return {path:path.relative(base,f).replace(/\\/g,'/'),exists:false};let s=fs.statSync(f);return {path:path.relative(base,f).replace(/\\/g,'/'),exists:true,bytes:s.size,modifiedUtc:s.mtime.toISOString(),sha256:hash(f)};};
const refs=new Set(['Theme.qml','Glass.qml','TopBar.qml','+android/TopBar.qml','PhoneShellHost.qml','PhoneWorldTabBar.qml','WideWorldRail.qml','HouseWindowControls.qml','BackAction.qml','account/AccountCenter.qml','account/AccountChoice.qml','account/AccountWelcomeChoice.qml','account/AccountPanelHeader.qml','account/AccountButton.qml','account/AccountAvatarGlyph.qml','account/AccountSecurityIcon.qml','account/AccountRecoveryIcon.qml','account/AccountDataPrivacyIcon.qml','account/AccountDeviceIcon.qml','account/AccountAuthoritativeSwitch.qml','ExtensionsSetupSheet.qml','TankoyomiFlag.qml']);
const result={batch:manifest.batch,startedUtc:new Date().toISOString(),root:base,approvedSource:manifest.references,scope:'Batch 4 only; inputs read-only; no app/runtime/device/service qualification',jobs:[],references:[],context:[]};
for(const job of manifest.jobs){
 const j={task:job.task,pages:job.pages,mockFiles:[],reports:[],existingCaptures:[],captureAudits:[]};
 for(const name of fs.readdirSync(job.output)){if(/\.(html|css|js|cjs)$/.test(name))j.mockFiles.push(record(path.join(job.output,name)));}
 for(const name of ['REPORT.json','FIDELITY.md']){const f=path.join(job.output,name),r=record(f);if(name.endsWith('.json')&&r.exists){try{JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));r.validJson=true;}catch(e){r.validJson=false;r.parseError=e.message;}}j.reports.push(r);}
 const cap=path.join(job.output,'captures');
 if(fs.existsSync(cap)){for(const n of fs.readdirSync(cap)){const f=path.join(cap,n);if(!fs.statSync(f).isFile())continue;if(n.endsWith('.png')){const b=fs.readFileSync(f);j.existingCaptures.push({...record(f),pngWidth:b.readUInt32BE(16),pngHeight:b.readUInt32BE(20)});}else if(n.endsWith('.json')){const r=record(f);try{const v=JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));r.topLevelKeys=Object.keys(v);r.hasHashBinding=/(?:sha256|inputHash|sourceHash)/i.test(JSON.stringify(v));}catch(e){r.parseError=e.message;}j.captureAudits.push(r);}}}
 job.sources.forEach(s=>refs.add(s));result.jobs.push(j);
}
for(const ref of ['desktop','alpha','android-drift'])for(const name of refs)result.references.push(record(path.join(base,'reference',ref,'qml',name)));
for(const name of ['ANDROID-PORT-CLASSIFICATION.md','VIEWPORTS.json','UI-VERIFICATION-AUDIT.md'])result.context.push(record(path.join(base,'context',name)));
fs.writeFileSync(path.join(out,'INPUT-VERSIONS.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({jobs:result.jobs.map(j=>({task:j.task,pages:j.pages.length,mockFiles:j.mockFiles.length,reports:j.reports.map(r=>({path:r.path,exists:r.exists,validJson:r.validJson,parseError:r.parseError})),existingCaptures:j.existingCaptures.length,audits:j.captureAudits.map(r=>({path:r.path,hasHashBinding:r.hasHashBinding,keys:r.topLevelKeys}))})),sourceRecords:result.references.length,output:path.join(out,'INPUT-VERSIONS.json')},null,2));