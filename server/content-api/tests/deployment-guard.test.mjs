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

// Root receiving regression: publishing a new graph with the old Worker source
// leaves native prepared->Worker fallback unable to pass expectedSource checks.
import * as guard from '../projection/deployment_guard.mjs';
const dataSource='4ee62650692b3ca915a27478c1da718726ae543c8f31768596c29509d6535b2e';
const publicRevision='847e23f6d627a2b5bbc9477a8bd0f279c4b47d4c368b3384767ae80cc3c3fa64';
const aligned=()=>({configuredSource:dataSource,
 publication:{sourceRevision:dataSource,revision:publicRevision},
 entry:{sourceRevision:dataSource,revision:publicRevision},
 legacyManifest:{revision:dataSource,publicRevision}});
test('source alignment accepts the exact Worker, entry and legacy prepared graph',()=>{
 assert.deepEqual(guard.assertPublicationSource(aligned()),{sourceRevision:dataSource,publicRevision});
});
test('old Worker source with a new publication is rejected before any deploy callback',async()=>{
 let lists=0,deploys=0;
 await assert.rejects(guardedDeploy({expectedVersion:version,expectedDeployment:deployment,execute:true,
  validatePublication:()=>guard.assertPublicationSource({...aligned(),configuredSource:'acef5f264d4e8ed13584d54fdcb1bd188e9bad99a27af315da679ce2b11a686a'}),
  listDeployments:async()=>{lists++;return[current];},deploy:async()=>{deploys++;}}),/PUBLICATION_SOURCE_MISMATCH/);
 assert.equal(lists,0);assert.equal(deploys,0);
});
test('missing or wrong-source legacy prepared namespace is rejected',()=>{
 for(const legacyManifest of [null,{}, {revision:'0'.repeat(64),publicRevision}, {revision:dataSource,publicRevision:'1'.repeat(64)}])
  assert.throws(()=>guard.assertPublicationSource({...aligned(),legacyManifest}),/LEGACY_PREPARED_MISMATCH/);
});
test('malformed and mixed public entry or report identities fail closed',()=>{
 for(const override of [{configuredSource:'../escape'}, {publication:{sourceRevision:dataSource,revision:'bad'}},
  {entry:{sourceRevision:'0'.repeat(64),revision:publicRevision}}, {entry:{sourceRevision:dataSource,revision:'0'.repeat(64)}}])
  assert.throws(()=>guard.assertPublicationSource({...aligned(),...override}),/PUBLICATION_(SOURCE|ENTRY)_MISMATCH/);
});

import {mkdtempSync,mkdirSync,writeFileSync,readFileSync,existsSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,dirname,basename} from 'node:path';
import {spawnSync,execFileSync} from 'node:child_process';
test('real guarded CLI refuses a source mismatch before invoking even the fake Wrangler',()=>{
 const root=mkdtempSync(join(tmpdir(),'colosseum-guard-'));
 try{
  const api=join(root,'server/content-api');
  const files={
   'server/content-api/projection/deployment_guard.mjs':readFileSync(new URL('../projection/deployment_guard.mjs',import.meta.url),'utf8'),
   'server/content-api/projection/verify_public_static.mjs':'// isolated fake integrity callback: no network\n',
   'server/content-api/node_modules/wrangler/bin/wrangler.js':`import {writeFileSync} from 'node:fs';writeFileSync('WRANGLER-INVOKED','yes');console.log(${JSON.stringify(JSON.stringify([current]))});`,
   'server/content-api/wrangler.jsonc':JSON.stringify({name:'colosseum-content-api-staging',account_id:'69049ce787cd73028c878bf0953946f6',vars:{LIBRARY_REVISION:'acef5f264d4e8ed13584d54fdcb1bd188e9bad99a27af315da679ce2b11a686a'}}),
   'assets/catalogue/current.json':JSON.stringify(aligned().entry),
   [`assets/internal/prebuilt/${dataSource}/manifest.json`]:JSON.stringify(aligned().legacyManifest),
   'publication.json':JSON.stringify(aligned().publication),
   'previous/retention-placeholder':'owned test fixture'
  };
  for(const [name,text] of Object.entries(files)){const target=join(root,name);mkdirSync(join(target,'..'),{recursive:true});writeFileSync(target,text);}
  const git=args=>execFileSync('git',args,{cwd:root,stdio:'ignore'});
  git(['init','-q']);git(['add','--force','--',...Object.keys(files)]);
  git(['-c','user.name=Fixture','-c','user.email=fixture@localhost','commit','-q','-m','isolated guard fixture']);
  const head=execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim();
  const run=spawnSync(process.execPath,[join(api,'projection/deployment_guard.mjs'),
   '--deploy','--expected-version',version,'--expected-deployment',deployment,
   '--expected-source',head,'--expected-publication',publicRevision,
   '--assets',join(root,'assets'),'--publication-report',join(root,'publication.json'),
   '--previous-publication',join(root,'previous'),'--evidence',join(root,'receipt.json')],
   {cwd:root,encoding:'utf8',timeout:15000});
  assert.equal(run.status,1);assert.match(run.stderr,/PUBLICATION_SOURCE_MISMATCH/);
  assert.equal(existsSync(join(api,'WRANGLER-INVOKED')),false);
 }finally{const target=resolve(root);assert.equal(dirname(target),resolve(tmpdir()));assert.ok(basename(target).startsWith('colosseum-guard-'));rmSync(target,{recursive:true,force:true});}
});
