// Staging-only, check-only by default. A reviewed deployment needs --deploy,
// pinned source/publication identities and an immediate live baseline match.
import {execFileSync} from 'node:child_process';
import {readFileSync,writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,join,sep} from 'node:path';
import {parseArgs} from 'node:util';

export function newestDeployment(rows){
 if(!Array.isArray(rows)||!rows.length)throw Error('BASELINE_EMPTY');
 const parsed=rows.map(row=>{
  if(typeof row.created_on!=='string'||!/^\d{4}-\d{2}-\d{2}T.*Z$/.test(row.created_on)||!Number.isFinite(Date.parse(row.created_on)))throw Error('BASELINE_INVALID_TIMESTAMP');
  return{row,time:Date.parse(row.created_on)};
 }).sort((a,b)=>b.time-a.time);
 if(parsed.length>1&&parsed[0].time===parsed[1].time&&parsed[0].row.id!==parsed[1].row.id)throw Error('BASELINE_AMBIGUOUS_TIMESTAMP');
 return parsed[0].row;
}
export function assertExpectedDeployment(rows,expectedVersion,expectedDeployment){
 if(!expectedVersion||!expectedDeployment)throw Error('BASELINE_EXPECTATIONS_REQUIRED');
 const row=newestDeployment(rows);
 if(!Array.isArray(row.versions)||row.versions.length!==1||row.versions[0].percentage!==100)throw Error('BASELINE_TRAFFIC');
 if(row.versions[0].version_id!==expectedVersion)throw Error('BASELINE_VERSION_CHANGED');
 if(row.id!==expectedDeployment)throw Error('BASELINE_DEPLOYMENT_CHANGED');
 return{id:row.id,created_on:row.created_on,versions:row.versions};
}
// Local identities only: this does NOT attest hosted D1 contents/readiness.
export function assertPublicationSource({configuredSource,publication,entry,legacyManifest}){
 const hash=/^[a-f0-9]{64}$/;
 if(!hash.test(configuredSource||'')||!hash.test(publication?.sourceRevision||'')||
    !hash.test(publication?.revision||'')||configuredSource!==publication.sourceRevision)
  throw Error('PUBLICATION_SOURCE_MISMATCH');
 if(entry?.sourceRevision!==publication.sourceRevision||entry?.revision!==publication.revision)
  throw Error('PUBLICATION_ENTRY_MISMATCH');
 if(legacyManifest?.revision!==configuredSource||legacyManifest?.publicRevision!==publication.revision)
  throw Error('LEGACY_PREPARED_MISMATCH');
 return{sourceRevision:configuredSource,publicRevision:publication.revision};
}
export async function guardedDeploy({expectedVersion,expectedDeployment,listDeployments,deploy,validatePublication,execute=false}){
 // Expensive local verification happens FIRST, so the live read is immediately
 // before deployment. There is no auto-retry on an unexpected baseline.
 if(execute){if(typeof validatePublication!=='function')throw Error('PUBLICATION_VALIDATOR_REQUIRED');await validatePublication();}
 const baseline=assertExpectedDeployment(await listDeployments(),expectedVersion,expectedDeployment);
 if(execute)await deploy();
 return{guardPassed:true,deployExecuted:execute,baseline};
}
async function main(){
 const {values}=parseArgs({options:{'expected-version':{type:'string'},'expected-deployment':{type:'string'},
  'expected-source':{type:'string'},'expected-publication':{type:'string'},assets:{type:'string'},
  'publication-report':{type:'string'},'previous-publication':{type:'string',multiple:true},evidence:{type:'string'},deploy:{type:'boolean',default:false}}});
 const cwd=fileURLToPath(new URL('../',import.meta.url)),repo=resolve(cwd,'../..');
 const cli=join(cwd,'node_modules/wrangler/bin/wrangler.js');
 const wrangler=args=>execFileSync(process.execPath,[cli,...args],{cwd,encoding:'utf8',timeout:300000,stdio:['ignore','pipe','pipe']});
 const config=JSON.parse(readFileSync(join(cwd,'wrangler.jsonc'),'utf8').replace(/^\s*\/\/.*$/gm,''));
 if(config.name!=='colosseum-content-api-staging'||config.account_id!=='69049ce787cd73028c878bf0953946f6')throw Error('STAGING_CONFIG_CHANGED');
 const git=args=>execFileSync('git',args,{cwd:repo,encoding:'utf8'}).trim();
 let deploymentOutput;
 const result=await guardedDeploy({expectedVersion:values['expected-version'],expectedDeployment:values['expected-deployment'],execute:values.deploy,
  validatePublication:async()=>{
   for(const key of ['expected-source','expected-publication','assets','publication-report','previous-publication','evidence'])if(!values[key])throw Error('DEPLOY_ARGUMENT_REQUIRED: '+key);
   if(git(['rev-parse','HEAD'])!==values['expected-source']||git(['status','--porcelain']))throw Error('SOURCE_CHANGED_OR_DIRTY');
   const publication=JSON.parse(readFileSync(values['publication-report'],'utf8'));
   if(publication.revision!==values['expected-publication'])throw Error('PUBLICATION_REVISION_CHANGED');
   // Validate before constructing the namespace path; never let a revision escape assets.
   if(!/^[a-f0-9]{64}$/.test(publication.sourceRevision||''))throw Error('PUBLICATION_SOURCE_MISMATCH');
   const entry=JSON.parse(readFileSync(join(values.assets,'catalogue/current.json'),'utf8'));
   let legacyManifest;
   try{legacyManifest=JSON.parse(readFileSync(join(values.assets,'internal/prebuilt',publication.sourceRevision,'manifest.json'),'utf8'));}
   catch{throw Error('LEGACY_PREPARED_MISMATCH');}
   assertPublicationSource({configuredSource:config.vars?.LIBRARY_REVISION,publication,entry,legacyManifest});
   if(resolve(values.evidence).startsWith(resolve(values.assets)+sep))throw Error('EVIDENCE_MUST_STAY_OUTSIDE_ASSETS');
   execFileSync(process.execPath,[join(cwd,'projection/verify_public_static.mjs'),values.assets,values['publication-report'],values.evidence+'.integrity.json',...values['previous-publication']],{cwd,stdio:['ignore','pipe','pipe']});
   if(git(['rev-parse','HEAD'])!==values['expected-source']||git(['status','--porcelain']))throw Error('SOURCE_CHANGED_DURING_VALIDATION');
  },
  listDeployments:async()=>JSON.parse(wrangler(['deployments','list','--config','./wrangler.jsonc','--name','colosseum-content-api-staging','--json'])),
  deploy:async()=>{deploymentOutput=wrangler(['deploy','--config','./wrangler.jsonc','--name','colosseum-content-api-staging','--assets',resolve(values.assets)]);}
 });
 if(values.evidence)writeFileSync(values.evidence,JSON.stringify({...result,checkedAt:new Date().toISOString()},null,2));
 if(deploymentOutput&&values.evidence)writeFileSync(values.evidence+'.deploy.log',deploymentOutput);
 console.log(JSON.stringify(result));
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url))main().catch(error=>{console.error('DEPLOY_REFUSED: '+error.message);process.exitCode=1;});
