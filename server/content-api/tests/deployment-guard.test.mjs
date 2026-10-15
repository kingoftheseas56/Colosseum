import test from 'node:test';
import assert from 'node:assert/strict';
import {newestDeployment,assertExpectedDeployment,guardedDeploy} from '../projection/deployment_guard.mjs';
const version='3cee1485-8192-461e-9924-4157c1c79895',deployment='4b801c1b-62ef-4840-9f02-b8f4f1406d72';
const old={id:'35a54cf3-b139-49f1-84bb-f5e275c474d9',created_on:'2026-10-13T23:20:06.193020Z',versions:[{version_id:'a4c64608-4ab4-423a-84de-ec7492ae37a1',percentage:100}]};
const current={id:deployment,created_on:'2026-10-15T18:18:17.494399Z',versions:[{version_id:version,percentage:100}]};
test('oldest-first, newest-first and shuffled lists all select the newest timestamp',()=>{
 for(const rows of [[old,current],[current,old],[old,current,old]])assert.equal(newestDeployment(rows).id,deployment);
});
test('malformed, empty and ambiguous timestamp sets fail closed',()=>{
 for(const rows of [[],[{...current,created_on:'bad'}],[current,{...current,id:old.id}]])assert.throws(()=>newestDeployment(rows));
});
test('both version and deployment identity are required; mixed traffic is refused',()=>{
 assert.equal(assertExpectedDeployment([old,current],version,deployment).id,deployment);
 assert.throws(()=>assertExpectedDeployment([old,current],old.versions[0].version_id,deployment),/BASELINE_VERSION_CHANGED/);
 assert.throws(()=>assertExpectedDeployment([old,current],version,old.id),/BASELINE_DEPLOYMENT_CHANGED/);
 assert.throws(()=>assertExpectedDeployment([{...current,versions:[{version_id:version,percentage:50},{version_id:old.versions[0].version_id,percentage:50}]}],version,deployment),/BASELINE_TRAFFIC/);
});
test('wrong expected version prevents the deploy callback entirely',async()=>{
 let deploys=0;
 await assert.rejects(guardedDeploy({expectedVersion:old.versions[0].version_id,expectedDeployment:deployment,execute:true,
  validatePublication:async()=>{},listDeployments:async()=>[old,current],deploy:async()=>deploys++}),/BASELINE_VERSION_CHANGED/);
 assert.equal(deploys,0);
});
test('check-only never deploys; approved execution rechecks after local validation',async()=>{
 let deploys=0;const order=[];
 const args={expectedVersion:version,expectedDeployment:deployment,validatePublication:async()=>order.push('validate'),
  listDeployments:async()=>{order.push('list');return[old,current];},deploy:async()=>{order.push('deploy');deploys++;}};
 const check=await guardedDeploy(args);assert.equal(check.deployExecuted,false);assert.equal(deploys,0);
 order.length=0;await guardedDeploy({...args,execute:true});assert.deepEqual(order,['validate','list','deploy']);assert.equal(deploys,1);
});
