import test from 'node:test';import assert from 'node:assert/strict';import{readFile}from'node:fs/promises';import{createHash}from'node:crypto';import{fileURLToPath}from'node:url';import{Miniflare,convertV4MiniflareOptions}from'miniflare';
const schema=JSON.parse(await readFile(new URL('../src/source-schema.json',import.meta.url),'utf8')),revision='a'.repeat(64),secret='isolated-http-cache-test-secret-32-characters';
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
const hash=s=>createHash('sha256').update(s).digest('hex');
async function fixture(t,{preparedArgs=null,assetTag='"asset-version"',honorConditional=true}={}){
 const calls=[],assetPath=preparedArgs?'/internal/catalogue/imdb/titleCatalog/'+hash(JSON.stringify(canonical(preparedArgs)))+'.json':null;
 const prepared=JSON.stringify({apiVersion:1,revision,method:'titleCatalog',result:[{tt:'tt1',title:'Pinned'}]});
 const mf=new Miniflare(convertV4MiniflareOptions({modules:true,scriptPath:fileURLToPath(new URL('../dist/index.js',import.meta.url)),compatibilityDate:'2026-10-06',bindings:{ADMIN_TOKEN:'isolated-admin-token',SESSION_SECRET:secret,ENVIRONMENT:'staging',TANKOBAN_GUEST_ENABLED:'true',LIBRARY_REVISION:revision},ratelimits:{GUEST_RATE_LIMITER:{simple:{limit:1000,period:60},namespace_id:'1001'}},d1Databases:{CATALOG:'catalog-'+t.name,METADATA:'metadata-'+t.name,LIBRARY:'library-'+t.name},serviceBindings:{ASSETS:async request=>{
  calls.push({path:new URL(request.url).pathname,conditional:request.headers.get('If-None-Match')});if(new URL(request.url).pathname!==assetPath)return new Response('missing',{status:404});
  const headers={'Content-Type':'application/json',...(assetTag?{ETag:assetTag}:{})};if(honorConditional&&request.headers.get('If-None-Match')===assetTag)return new Response(null,{status:304,headers});return new Response(prepared,{headers});
 }}}));t.after(()=>mf.dispose());
 const catalog=await mf.getD1Database('CATALOG');await catalog.exec('CREATE TABLE _source_tables(name TEXT PRIMARY KEY,ready INTEGER,source_hash TEXT,expected_rows INTEGER,imported_rows INTEGER)');for(const name of ['tankoban__series','tankoban__volumes'])await catalog.prepare('INSERT INTO _source_tables VALUES(?,?,?,?,?)').bind(name,1,schema[name].sourceHash,schema[name].rowCount,schema[name].rowCount).run();
 const library=await mf.getD1Database('LIBRARY');await library.exec('CREATE TABLE _library_state(id INTEGER PRIMARY KEY,revision TEXT,ready INTEGER);CREATE TABLE lib_imdb_title(tt TEXT PRIMARY KEY,title TEXT,type TEXT)');await library.prepare('INSERT INTO _library_state VALUES(1,?,1)').bind(revision).run();await library.exec("INSERT INTO lib_imdb_title VALUES('tt1','Live title','movie')");
 const send=(method,args,token,tag)=>mf.dispatchFetch('https://cache-test.invalid/v1/catalogue/imdb/'+method,{method:'POST',headers:{'Content-Type':'application/json',...(token?{Authorization:'Bearer '+token}:{}),...(tag?{'If-None-Match':tag}:{})},body:JSON.stringify({args})});
 const issue=await mf.dispatchFetch('https://cache-test.invalid/v1/guest/session',{method:'POST',headers:{'Content-Type':'application/json','CF-Connecting-IP':'192.0.2.10'},body:JSON.stringify({appId:'colosseum',apiVersion:1})});assert.equal(issue.status,200);const token=(await issue.json()).token;
 return{send,token,library,calls,assetPath,prepared};
}
test('recursive canonical argument hashing reaches same prepared asset despite object insertion order',async t=>{
 const f=await fixture(t,{preparedArgs:[{votesMin:10,type:'movie',notGenre:['Animation']},0,24]});const args=[{notGenre:['Animation'],type:'movie',votesMin:10},0,24];const r=await f.send('titleCatalog',args,f.token);assert.equal(r.status,200);assert.equal(await r.text(),f.prepared);assert.equal(f.calls[0].path,f.assetPath);assert.equal(r.headers.get('ETag'),'"asset-version"');assert.match(r.headers.get('Cache-Control'),/^private/);
});
test('prepared If-None-Match reaches ASSETS and preserves ETag and empty 304',async t=>{
 const f=await fixture(t,{preparedArgs:[{},0,24]});const r=await f.send('titleCatalog',[{},0,24],f.token,'"asset-version"');assert.equal(r.status,304);assert.equal(await r.text(),'');assert.equal(r.headers.get('ETag'),'"asset-version"');assert.equal(f.calls[0].conditional,'"asset-version"');
});
test('authorization and revision readiness precede prepared conditional assets',async t=>{
 const f=await fixture(t,{preparedArgs:[{},0,24]});assert.equal((await f.send('titleCatalog',[{},0,24],null,'"asset-version"')).status,401);assert.equal(f.calls.length,0);await f.library.exec('UPDATE _library_state SET ready=0');assert.equal((await f.send('titleCatalog',[{},0,24],f.token,'"asset-version"')).status,503);assert.equal(f.calls.length,0);
});
test('prepared body gets deterministic hash ETag when assets provide none',async t=>{
 const f=await fixture(t,{preparedArgs:[{},0,24],assetTag:null,honorConditional:false});const tag='"'+hash(f.prepared)+'"';const r=await f.send('titleCatalog',[{},0,24],f.token,tag);assert.equal(r.status,304);assert.equal(r.headers.get('ETag'),tag);assert.equal(await r.text(),'');
});
test('dynamic and cached responses share body ETag; weak/list conditions and readiness remain valid',async t=>{
 const f=await fixture(t);const first=await f.send('titlePresentation',['tt1'],f.token);assert.equal(first.status,200);const body=await first.text(),tag='"'+hash(body)+'"';assert.equal(first.headers.get('ETag'),tag);await f.library.exec('DROP TABLE lib_imdb_title');
 const hit=await f.send('titlePresentation',['tt1'],f.token,'"other", W/'+tag);assert.equal(hit.status,304);assert.equal(await hit.text(),'');assert.equal(hit.headers.get('ETag'),tag);const mismatch=await f.send('titlePresentation',['tt1'],f.token,'"other"');assert.equal(mismatch.status,200);assert.equal(await mismatch.text(),body);assert.match(mismatch.headers.get('Cache-Control'),/^private/);await f.library.exec('UPDATE _library_state SET ready=0');assert.equal((await f.send('titlePresentation',['tt1'],f.token,tag)).status,503);
});
test('first dynamic wildcard 304 still populates complete 200 cache entry',async t=>{
 const f=await fixture(t);const r=await f.send('titlePresentation',['tt1'],f.token,'*');assert.equal(r.status,304);assert.equal(await r.text(),'');await f.library.exec('DROP TABLE lib_imdb_title');const hit=await f.send('titlePresentation',['tt1'],f.token);assert.equal(hit.status,200);assert.equal((await hit.json()).result.title,'Live title');
});
