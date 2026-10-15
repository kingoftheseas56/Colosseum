// Full local publication integrity and asset-router proof. No remote requests.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,readdirSync,lstatSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
const [publication,reportPath,evidence]=process.argv.slice(2);
if(!publication||!reportPath||!evidence)throw Error('Usage: node projection/verify_public_static.mjs publication report.json evidence.json');
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const report=JSON.parse(readFileSync(reportPath)),entries=new Map(report.entries.map(e=>[e.path,e.bytes]));
const entry=JSON.parse(readFileSync(join(publication,'catalogue/current.json'))),prefix='/catalogue/'+entry.revision+'/';
assert.equal(entries.size,report.files);assert.equal(entry.revision,report.revision);
const paths=[];
function walk(dir,base=''){for(const e of readdirSync(dir,{withFileTypes:true})){assert.equal(e.isSymbolicLink(),false);if(e.isDirectory())walk(join(dir,e.name),base+'/'+e.name);else paths.push(base+'/'+e.name);}}
walk(publication);assert.equal(paths.length,entries.size);
const refs=new Set();let bytes=0,max=0,hashedFiles=0;
function references(value){
 if(typeof value==='string'&&(value.startsWith('/catalogue/')||value.startsWith('/assets/content/'))){assert.ok(value.startsWith(prefix)||value.startsWith('/assets/content/'));assert.ok(!value.includes('..'));refs.add(value);}
 else if(Array.isArray(value))value.forEach(references);
 else if(value&&typeof value==='object')Object.values(value).forEach(references);
}
for(const path of paths){
 assert.equal(lstatSync(join(publication,path)).isSymbolicLink(),false);
 const raw=readFileSync(join(publication,path));assert.equal(raw.length,entries.get(path),path);bytes+=raw.length;max=Math.max(max,raw.length);
 assert.ok(raw.length<=25*1024*1024,path);
 const digest=path.match(/(?:\/|index-)([a-f0-9]{64})\.(?:json|[a-z0-9]+)$/)?.[1];
 if(digest){assert.equal(sha(raw),digest,path);hashedFiles++;}
 if(path.endsWith('.json'))references(JSON.parse(raw));
}
for(const path of refs)assert.ok(entries.has(path),'missing reference '+path);
const indexRaw=readFileSync(join(publication,entry.index.path)),index=JSON.parse(indexRaw);
assert.equal(sha(indexRaw),entry.index.sha256);assert.equal(indexRaw.length,entry.index.bytes);
assert.equal(index.schemaVersion,1);assert.equal(index.version,1);assert.equal(index.revision,entry.revision);assert.equal(index.sourceRevision,entry.sourceRevision);
for(const d of Object.values(index.content.documents))assert.equal(sha(readFileSync(join(publication,d.path))),d.sha256);
assert.equal(bytes,report.totalBytes);assert.equal(max,report.largestFileBytes);assert.ok(paths.length<=20000);
const cfg=JSON.parse(readFileSync(new URL('../wrangler.jsonc',import.meta.url),'utf8').replace(/^\s*\/\/.*$/gm,''));
const mf=new Miniflare(convertV4MiniflareOptions({modules:true,compatibilityDate:cfg.compatibility_date,
 script:'export default {fetch(){return new Response("WORKER_INVOKED",{status:418,headers:{"X-Worker-Invoked":"yes"}})}}',
 assets:{directory:publication,binding:'ASSETS',run_worker_first:cfg.assets.run_worker_first,routerConfig:{has_user_worker:true}}}));
const routing=[];
try{
 const sample=['/catalogue/current.json',entry.index.path,Object.values(index.listBuckets)[0],Object.values(index.lookups)[0],Object.values(index.fixed)[0],index.search.manga.names[0].path,index.search.imdb.keys[0].path,index.content.manifest.path,index.content.bundle.path,Object.values(index.content.documents)[0].path,paths.find(p=>p.startsWith('/assets/content/'))];
 for(const path of sample){
  const r=await mf.dispatchFetch('https://public-static.invalid'+path),raw=Buffer.from(await r.arrayBuffer());
  assert.equal(r.status,200,path);assert.equal(r.headers.get('X-Worker-Invoked'),null,path);assert.deepEqual(raw,readFileSync(join(publication,path)));
  assert.match(r.headers.get('Cache-Control'),path.endsWith('current.json')?/max-age=60, must-revalidate/:/immutable/);assert.ok(r.headers.get('ETag'));
  const head=await mf.dispatchFetch('https://public-static.invalid'+path,{method:'HEAD'});assert.equal(head.status,200);assert.equal(await head.text(),'');assert.equal(head.headers.get('X-Worker-Invoked'),null);
  const conditional=await mf.dispatchFetch('https://public-static.invalid'+path,{headers:{'If-None-Match':r.headers.get('ETag')}});assert.equal(conditional.status,304,path);assert.equal(await conditional.text(),'');assert.equal(conditional.headers.get('X-Worker-Invoked'),null);
  routing.push({path,get:200,head:200,conditional:304,workerInvoked:false});
 }
 for(const path of ['/v1/guest/session','/v1/catalogue/mal/search','/internal/content/manifest.json','/internal/prebuilt/'+entry.sourceRevision+'/manifest.json','/admin/status','/healthz']){
  const r=await mf.dispatchFetch('https://public-static.invalid'+path);await r.text();assert.equal(r.status,418);assert.equal(r.headers.get('X-Worker-Invoked'),'yes');routing.push({path,workerInvoked:true});
 }
}finally{await mf.dispose();}
const result={revision:entry.revision,sourceRevision:entry.sourceRevision,files:paths.length,bytes,largestFileBytes:max,hashedFiles,verifiedReferences:refs.size,artworkFiles:report.artworkFiles,contentDocuments:report.contentDocuments,
 filesWithConservative135Artwork:paths.length+Math.max(0,135-report.artworkFiles),fileHeadroom:20000-paths.length,routing,publicGetHead304Checks:33,remoteRequests:0,d1Writes:0,deployed:false};
writeFileSync(evidence,JSON.stringify(result,null,2));console.log(JSON.stringify({...result,routing:undefined}));
