import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash,createHmac} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';

const schema=JSON.parse(await readFile(new URL('../src/source-schema.json',import.meta.url),'utf8'));
const revision='a'.repeat(64),secret='isolated-content-test-signing-secret-32-characters';
const args=['Batman',30];
const fingerprint=createHash('sha256').update(JSON.stringify(args)).digest('hex');
const preparedPath=`/internal/catalogue/comics/search/${fingerprint}.json`;
const prepared={apiVersion:1,revision,method:'search',result:[{gcdId:123,title:'Pinned Batman',year:1940,yearEnded:0,publisher:'DC',cover:'https://assets.example/a.png',downloads:4}]};
const document={schemaVersion:1,revision,data:{title:'Curated content'}};
const manifest={schemaVersion:1,revision,documents:{'universe-dcau':{revision,sha256:'b'.repeat(64)}},assets:{}};
const bundle={apiVersion:1,revision,method:'bundle',result:{manifest,documents:{'universe-dcau':JSON.stringify(document)}}};

async function fixture(t,{withLibrary=true}={}){
 const assetCalls=[];
 const content=new Map([[preparedPath,JSON.stringify(prepared)],['/internal/content/manifest.json',JSON.stringify(manifest)],['/internal/content/documents/universe-dcau.json',JSON.stringify(document)],['/internal/content/rpc-bundle.json',JSON.stringify(bundle)]]);
 const mf=new Miniflare(convertV4MiniflareOptions({
  modules:true,scriptPath:fileURLToPath(new URL('../dist/index.js',import.meta.url)),compatibilityDate:'2026-10-06',
  bindings:{ADMIN_TOKEN:'isolated-admin-token',SESSION_SECRET:secret,ENVIRONMENT:'staging',TANKOBAN_GUEST_ENABLED:'true',LIBRARY_REVISION:revision},
  ratelimits:{GUEST_RATE_LIMITER:{simple:{limit:1000,period:60},namespace_id:'1001'}},
  d1Databases:{CATALOG:'isolated-catalog',METADATA:'isolated-metadata',...(withLibrary?{LIBRARY:'isolated-library'}:{})},
  serviceBindings:{ASSETS:async request=>{
   const path=new URL(request.url).pathname;assetCalls.push(path);
   if(!content.has(path))return new Response('not found',{status:404});
   if(request.headers.get('If-None-Match')==='"fixture-revision"')return new Response(null,{status:304,headers:{ETag:'"fixture-revision"'}});
   return new Response(content.get(path),{headers:{'Content-Type':'application/json',ETag:'"fixture-revision"'}});
  }},
 }));
 t.after(()=>mf.dispose());
 const catalog=await mf.getD1Database('CATALOG');
 await catalog.exec('CREATE TABLE _source_tables(name TEXT PRIMARY KEY,ready INTEGER,source_hash TEXT,expected_rows INTEGER,imported_rows INTEGER)');
 for(const name of ['tankoban__series','tankoban__volumes'])await catalog.prepare('INSERT INTO _source_tables VALUES(?,?,?,?,?)').bind(name,1,schema[name].sourceHash,schema[name].rowCount,schema[name].rowCount).run();
 const library=withLibrary?await mf.getD1Database('LIBRARY'):null;
 if(library){await library.exec('CREATE TABLE _library_state(id INTEGER PRIMARY KEY,revision TEXT,ready INTEGER)');await library.prepare('INSERT INTO _library_state VALUES(1,?,1)').bind(revision).run();}
 const request=(path,token,body,headers={})=>mf.dispatchFetch('https://content-test.invalid'+path,{method:body===undefined?'GET':'POST',headers:{'CF-Connecting-IP':'192.0.2.10',...(token?{Authorization:'Bearer '+token}:{}),...(body===undefined?{}:{'Content-Type':'application/json'}),...headers},...(body===undefined?{}:{body:JSON.stringify(body)})});
 const issue=async()=>{const response=await request('/v1/guest/session',null,{appId:'colosseum',apiVersion:1});assert.equal(response.status,200);return(await response.json()).token;};
 return{request,issue,library,catalog,assetCalls};
}

test('Unauthenticated prepared catalogue, content RPC and content GET never reach static assets',async t=>{
 const f=await fixture(t);
 for(const[path,body]of [['/v1/catalogue/comics/search',{args}],['/v1/catalogue/content/bundle',{args:[]}],['/v1/content/manifest',undefined],['/v1/content/documents/universe-dcau',undefined]]){
  const response=await f.request(path,null,body);assert.equal(response.status,401,path);assert.deepEqual(await response.json(),{error:'UNAUTHORIZED'});
 }
 assert.deepEqual(f.assetCalls,[]);
});

test('Issued guest scope authorizes semantic prepared response only, hides internal paths and rejects forged leases',async t=>{
 const f=await fixture(t),token=await f.issue();
 const response=await f.request('/v1/catalogue/comics/search',token,{args});assert.equal(response.status,200);assert.deepEqual(await response.json(),prepared);assert.match(response.headers.get('Cache-Control'),/^private/);
 assert.deepEqual(f.assetCalls,[preparedPath]);
 for(const path of ['/v1/catalogues','/v1/catalogues/comics__series','/admin/status'])assert.equal((await f.request(path,token)).status,401,path);
 for(const path of [preparedPath,'/internal/content/manifest.json','/internal/content/rpc-bundle.json'])assert.equal((await f.request(path,token)).status,404,path);
 assert.deepEqual(f.assetCalls,[preparedPath]);
 const claims=JSON.parse(Buffer.from(token.split('.')[0],'base64url'));
 for(const override of [{scope:'admin'},{exp:claims.iat-1},{sourceHash:'stale'},{aud:'other'}]){
  const body=Buffer.from(JSON.stringify({...claims,...override})).toString('base64url');const invalid=body+'.'+createHmac('sha256',secret).update(body).digest('base64url');
  assert.equal((await f.request('/v1/catalogue/comics/search',invalid,{args})).status,401);
 }
 assert.deepEqual(f.assetCalls,[preparedPath]);
});

test('Prepared responses remain behind library presence, readiness and exact revision gates',async t=>{
 for(const scenario of ['missing-binding','no-state','not-ready','wrong-revision']){
  const f=await fixture(t,{withLibrary:scenario!=='missing-binding'}),token=await f.issue();
  if(scenario==='no-state')await f.library.exec('DELETE FROM _library_state');
  if(scenario==='not-ready')await f.library.exec('UPDATE _library_state SET ready=0');
  if(scenario==='wrong-revision')await f.library.prepare('UPDATE _library_state SET revision=?').bind('b'.repeat(64)).run();
  const response=await f.request('/v1/catalogue/comics/search',token,{args});assert.equal(response.status,503,scenario);assert.deepEqual(await response.json(),{error:'CATALOGUE_NOT_READY'});assert.deepEqual(f.assetCalls,[]);
 }
});

test('Malformed semantic and content arguments fail before SQL catalogue access or static lookup',async t=>{
 const f=await fixture(t),token=await f.issue();
 // No semantic catalogue tables exist in this fixture: SQL access would fail with 503.
 for(const input of [{args:{}},{args:[],sql:'DROP TABLE series'},{args:Array(17).fill(0)},{args:['x'.repeat(32769)]}])assert.equal((await f.request('/v1/catalogue/comics/search',token,input)).status,400);
 assert.equal((await f.request('/v1/catalogue/comics/unknownMethod',token,{args:[]})).status,400);
 for(const input of [{args:[1]},{args:{ }},{args:[],extra:true}])assert.equal((await f.request('/v1/catalogue/content/bundle',token,input)).status,400);
 assert.deepEqual(f.assetCalls,[]);
});

test('Authenticated content bundle, manifest and document preserve exact payload and private caching',async t=>{
 const f=await fixture(t),token=await f.issue();
 for(const[path,input,expected]of [['/v1/catalogue/content/bundle',{args:[]},bundle],['/v1/content/manifest',undefined,manifest],['/v1/content/documents/universe-dcau',undefined,document]]){
  const response=await f.request(path,token,input);assert.equal(response.status,200);assert.deepEqual(await response.json(),expected);assert.match(response.headers.get('Cache-Control'),/^private/);assert.equal(response.headers.get('X-Content-Type-Options'),'nosniff');
 }
 const missing=await f.request('/v1/content/documents/missing-key',token);assert.equal(missing.status,503);assert.deepEqual(await missing.json(),{error:'CONTENT_NOT_AVAILABLE'});
 const unchanged=await f.request('/v1/content/manifest',token,undefined,{'If-None-Match':'"fixture-revision"'});assert.equal(unchanged.status,304);assert.equal(await unchanged.text(),'');
});
