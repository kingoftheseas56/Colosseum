// Actual bundled Worker in local workerd. Old responses are oracle inputs only;
// ASSETS serves the NEW publication, with no catalogue tables in the local D1.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {legacyRecipes} from './legacy_recipes.mjs';
import schema from '../src/source-schema.json' with {type:'json'};
const [previous,publication,qmlRoot,evidence,flag]=process.argv.slice(2);
if(!evidence)throw Error('Usage: verify_legacy_recipes.mjs previous publication qml-root report.json [--negative-control]');
const decoded=legacyRecipes(previous,qmlRoot);assert.equal(decoded.unknown.length,0,'Every recipe must be decoded');assert.equal(decoded.recipes.length,88);
const entry=JSON.parse(readFileSync(join(publication,'catalogue/current.json'))),revision=entry.sourceRevision;
const negative=flag==='--negative-control',rows=[];let assetReads=0;
const mf=new Miniflare(convertV4MiniflareOptions({modules:true,
 scriptPath:fileURLToPath(new URL('../dist/index.js',import.meta.url)),compatibilityDate:'2026-10-06',
 bindings:{SESSION_SECRET:'local-legacy-recipe-qualification-secret32',ENVIRONMENT:'staging',TANKOBAN_GUEST_ENABLED:'true',LIBRARY_REVISION:revision},
 ratelimits:{GUEST_RATE_LIMITER:{simple:{limit:1000,period:60},namespace_id:'1001'}},
 d1Databases:{CATALOG:'legacy-recipe-catalog',LIBRARY:'legacy-recipe-library',METADATA:'legacy-recipe-metadata'},
 serviceBindings:{ASSETS:async request=>{
  assetReads++;const path=new URL(request.url).pathname;
  if(negative&&path.startsWith('/internal/prebuilt/'))return new Response('',{status:404});
  try{return new Response(readFileSync(join(publication,path)),{headers:{'Content-Type':'application/json'}});}
  catch(error){if(error.code==='ENOENT')return new Response('',{status:404});throw error;}
 }}}));
try{
 const catalog=await mf.getD1Database('CATALOG'),library=await mf.getD1Database('LIBRARY');
 await catalog.exec('CREATE TABLE _source_tables(name TEXT PRIMARY KEY,ready INTEGER,source_hash TEXT,expected_rows INTEGER,imported_rows INTEGER)');
 for(const name of ['tankoban__series','tankoban__volumes'])await catalog.prepare('INSERT INTO _source_tables VALUES(?,?,?,?,?)').bind(name,1,schema[name].sourceHash,schema[name].rowCount,schema[name].rowCount).run();
 await library.exec('CREATE TABLE _library_state(id INTEGER PRIMARY KEY,revision TEXT,ready INTEGER)');
 await library.prepare('INSERT INTO _library_state VALUES(1,?,1)').bind(revision).run();
 const guest=await mf.dispatchFetch('https://legacy.invalid/v1/guest/session',{method:'POST',headers:{'Content-Type':'application/json','CF-Connecting-IP':'192.0.2.10'},body:JSON.stringify({appId:'colosseum',apiVersion:1})});
 assert.equal(guest.status,200);const {token}=await guest.json();
 for(const recipe of negative?decoded.recipes.slice(0,1):decoded.recipes){
  const expected=JSON.parse(readFileSync(join(previous,recipe.path)));
  const r=await mf.dispatchFetch(`https://legacy.invalid/v1/catalogue/${recipe.dataset}/${recipe.method}`,{method:'POST',headers:{Authorization:'Bearer '+token,'Content-Type':'application/json'},body:JSON.stringify({args:recipe.args})});
  const actual=await r.json();let mismatch=null;
  try{assert.equal(r.status,200);assert.deepEqual(actual,expected);}catch(error){mismatch=String(error.message).slice(0,1800);}
  rows.push({...recipe,status:r.status,delivery:r.headers.get('X-Catalogue-Delivery'),d1Rows:r.headers.get('X-Catalogue-D1-Rows'),equal:!mismatch,mismatch});
 }
}finally{await mf.dispose();}
const counts={prebuilt:0,prepared:0,d1:0,edge:0,unknown:0,mismatch:0};
for(const r of rows){counts[r.delivery in counts?r.delivery:'unknown']++;if(!r.equal)counts.mismatch++;}
const result={publication:entry.revision,sourceRevision:revision,recipes:rows.length,counts,rows,negativeControl:negative,assetReads,
 localD1:'Only readiness control tables; business catalogue tables deliberately absent. Any SQL fallback fails rather than using fabricated catalogue rows.',remoteRequests:0,remoteD1Writes:0,deployed:false};
writeFileSync(evidence,JSON.stringify(result,null,2));console.log(JSON.stringify({...result,rows:undefined}));
if(counts.mismatch)process.exitCode=1;
