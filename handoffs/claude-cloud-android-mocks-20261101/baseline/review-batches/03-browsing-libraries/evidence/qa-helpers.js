(async()=>{
globalThis.qaResults={};globalThis.qaPage=null;globalThis.qaChecks=[];globalThis.qaEvidence=[];
globalThis.qaStart=(job,name)=>{qaPage={job,name};qaChecks=[];qaEvidence=[];};
globalThis.qaCheck=async(name,fn)=>{try{const v=await fn();qaChecks.push({name,pass:typeof v==='object'?!!v.pass:!!v,...(typeof v==='object'?{detail:v}:{}),time:new Date().toISOString()});}catch(e){qaChecks.push({name,pass:false,error:String(e)});}return qaChecks[qaChecks.length-1];};
globalThis.qaShot=async(tag)=>{const r=await shot(qaPage.job,qaPage.name,'after',tag);qaEvidence.push(path.relative(B,r.path));return r;};
globalThis.qaSave=()=>{const d={...qaPage,checks:qaChecks,evidence:qaEvidence,completedAt:new Date().toISOString(),pass:qaChecks.every(c=>c.pass)};qaResults[qaPage.name]=d;fs.writeFileSync(path.join(B,'evidence','qa-'+path.basename(qaPage.name,'.html')+'.json'),JSON.stringify(d,null,2));fs.appendFileSync(path.join(B,'CHECKPOINT.md'),'\n- '+d.completedAt+' '+qaPage.name+': '+qaChecks.filter(x=>x.pass).length+'/'+qaChecks.length+' focused checks passed. '+(d.pass?'Interaction pass complete.':'Specific failures remain in per-page QA JSON.')+'\n');return {page:d.name,checks:d.checks.length,failed:d.checks.filter(x=>!x.pass)};};
globalThis.qaRect=async(sel)=>page.$eval(sel,e=>{const r=e.getBoundingClientRect();return{x:r.x,y:r.y,w:r.width,h:r.height,right:r.right,bottom:r.bottom,visible:r.width>0&&r.height>0&&getComputedStyle(e).visibility!=='hidden',client:e.clientHeight,scroll:e.scrollHeight};});
globalThis.qaBounds=async(sel)=>{const r=await qaRect(sel);return{pass:r.visible&&r.x>=-1&&r.y>=-1&&r.right<=page.viewport().width+1&&r.bottom<=page.viewport().height+1,...r};};
globalThis.qaScrollBottom=async(sel)=>{await page.$eval(sel,e=>e.scrollTo({top:e.scrollHeight,behavior:'instant'}));await delay(200);return page.$eval(sel,e=>({pass:e.scrollTop>0&&Math.abs(e.scrollHeight-e.clientHeight-e.scrollTop)<3,scrollTop:e.scrollTop,scrollHeight:e.scrollHeight,clientHeight:e.clientHeight}));};
globalThis.qaClick=async(sel)=>{await page.click(sel);await delay(130);};
return {ready:true};
})()