// Full local publication integrity and asset-router proof. No remote requests.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,readdirSync,lstatSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {retainArtwork} from './public_static.mjs';
const [publication,reportPath,evidence,...previous]=process.argv.slice(2);
if(!publication||!reportPath||!evidence||!previous.length)throw Error('Usage: node projection/verify_public_static.mjs publication report.json evidence.json previous-publication [other-previous-publications...]');
const retention=retainArtwork({previous,output:publication,verifyOnly:true});
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const report=JSON.parse(readFileSync(reportPath)),entries=new Map(report.entries.map(e=>[e.path,e.bytes]));
assert.equal(report.retention.inventorySha256,retention.inventorySha256);assert.equal(report.retention.requiredFiles,retention.requiredFiles);
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
// The additive fine lookup directory must be sorted, disjoint, same-revision
// and cover exactly the coarse tankoban series key set it shadows.
const tankobanKeys=new Set();
for(const path of Object.values(index.lookups))for(const key of Object.keys(JSON.parse(readFileSync(join(publication,path))))){const[dataset,method]=JSON.parse(key);if(dataset==='tankoban'&&method==='series')tankobanKeys.add(key);}
if(index.lookupRanges!==undefined){
 assert.equal(index.lookupRanges.version,1);assert.ok(Array.isArray(index.lookupRanges.ranges));
 const fine=new Set();let previousLast=null;
 for(const ref of index.lookupRanges.ranges){
  assert.equal(typeof ref.first,'string');assert.equal(typeof ref.last,'string');assert.equal(typeof ref.path,'string');
  assert.ok(ref.first<=ref.last);if(previousLast!==null)assert.ok(previousLast<ref.first,'lookupRanges stay sorted and disjoint');previousLast=ref.last;
  assert.ok(ref.path.startsWith(prefix+'shared/')&&/^shared\/[a-f0-9]{64}\.json$/.test(ref.path.slice(prefix.length)));
  for(const key of Object.keys(JSON.parse(readFileSync(join(publication,ref.path))))){
   assert.ok(key>=ref.first&&key<=ref.last,'key inside its declared range');assert.ok(!fine.has(key),'no duplicate fine key');fine.add(key);
  }
 }
 assert.deepEqual([...fine].sort(),[...tankobanKeys].sort(),'fine ranges cover exactly the coarse tankoban keys');
}
const freeFileLimit=20000;
const qualificationFileLimit=report.audiobooks?freeFileLimit:18000;
assert.equal(bytes,report.totalBytes);assert.equal(max,report.largestFileBytes);
assert.ok(paths.length<=qualificationFileLimit);
let audiobookSamples=[],audiobookRows;
if(report.audiobooks){
 assert.equal(index.audiobooks?.version,1);
 assert.equal(index.audiobooks.path,report.audiobooks.root);
 const root=JSON.parse(readFileSync(join(publication,index.audiobooks.path)));
 assert.equal(root.date,report.audiobooks.date);
 const lookup=(field,visit)=>{
  const ranges=JSON.parse(readFileSync(join(publication,root.records[field])));
  let previous='';
  for(const [first,last,path] of ranges){
   assert.ok(first>previous&&first<=last&&entries.has(path),field+' ranges');
   const records=Object.entries(JSON.parse(readFileSync(join(publication,path))))
    .sort(([a],[b])=>a<b?-1:a>b?1:0);
   assert.equal(records[0][0],first);
   assert.equal(records.at(-1)[0],last);
   for(const [id,value] of records){
    assert.ok(id>previous,field+' keys');
    visit(id,value);
    previous=id;
   }
  }
  return ranges;
 };
 audiobookRows={works:0,recordings:0,series:0};
 const workRanges=lookup('work',(id,value)=>{
  assert.equal(value.id,id);
  assert.ok(Array.isArray(value.recordings));
  audiobookRows.works++;
  audiobookRows.recordings+=value.recordings.length;
  for(const rec of value.recordings)assert.equal(rec.workId,id);
 });
 lookup('series',(id,value)=>{
  assert.equal(value.id,id);
  audiobookRows.series++;
 });
 for(const [table,count] of Object.entries(audiobookRows))
  assert.equal(count,report.audiobooks.rows[table],table+' full coverage');
 audiobookSamples=[index.audiobooks.path,root.start,root.records.work,workRanges[0][2],root.search];
 for(const path of audiobookSamples)assert.ok(entries.has(path));
}
const cfg=JSON.parse(readFileSync(new URL('../wrangler.jsonc',import.meta.url),'utf8').replace(/^\s*\/\/.*$/gm,''));
const mf=new Miniflare(convertV4MiniflareOptions({modules:true,compatibilityDate:cfg.compatibility_date,
 script:'export default {fetch(){return new Response("WORKER_INVOKED",{status:418,headers:{"X-Worker-Invoked":"yes"}})}}',
 assets:{directory:publication,binding:'ASSETS',run_worker_first:cfg.assets.run_worker_first,routerConfig:{has_user_worker:true}}}));
const routing=[];
try{
 const sample=['/catalogue/current.json',entry.index.path,Object.values(index.listBuckets)[0],Object.values(index.lookups)[0],...(index.lookupRanges?.ranges??[]).slice(0,1).map(r=>r.path),Object.values(index.fixed)[0],index.search.manga.names[0].path,index.search.imdb.keys[0].path,index.content.manifest.path,index.content.bundle.path,Object.values(index.content.documents)[0].path,paths.find(p=>p.startsWith('/assets/content/')),...audiobookSamples];
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
 filesWithConservative135Artwork:paths.length+Math.max(0,135-report.artworkFiles),fileHeadroom:qualificationFileLimit-paths.length,
 freeFileHeadroom:freeFileLimit-paths.length,freeLargestFileHeadroom:25*1024*1024-max,
 ...(audiobookRows?{audiobookRows}:{}),routing,publicGetHead304Checks:3*routing.filter(r=>!r.workerInvoked).length,remoteRequests:0,d1Writes:0,deployed:false};
writeFileSync(evidence,JSON.stringify(result,null,2));console.log(JSON.stringify({...result,routing:undefined}));
