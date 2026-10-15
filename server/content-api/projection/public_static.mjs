// Repackage the qualified I01 publication. No SQL rebuild, network or deployment.
import {readFileSync,writeFileSync,mkdirSync,existsSync,readdirSync,lstatSync} from 'node:fs';
import {join,dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';

const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const encode=value=>Buffer.from(JSON.stringify(value));
const hashPattern=/^[a-f0-9]{64}$/;
export const PUBLIC_HEADERS=`/catalogue/*
  X-Content-Type-Options: nosniff
  Access-Control-Allow-Origin: *
  X-Robots-Tag: noindex
/catalogue/:revision/*
  Cache-Control: public, max-age=31536000, immutable
/catalogue/current.json
  Cache-Control: public, max-age=60, must-revalidate
/assets/content/*
  Cache-Control: public, max-age=31536000, immutable
  X-Content-Type-Options: nosniff
`;

function file(root,path){
 const base=resolve(root),target=resolve(root,'.'+(path.startsWith('/')?path:'/'+path));
 if(!target.startsWith(base+'/')&&!target.startsWith(base+'\\'))throw Error('SOURCE_UNSAFE_PATH');
 // Refuse symbolic links, including parent links, before reading/copying.
 let cursor=target;while(cursor!==base){if(lstatSync(cursor).isSymbolicLink())throw Error('SOURCE_SYMLINK');cursor=dirname(cursor);}
 return readFileSync(target);
}
function walk(root,relative=''){
 return readdirSync(join(root,relative),{withFileTypes:true}).flatMap(e=>{
  if(e.isSymbolicLink())throw Error('SOURCE_SYMLINK');
  const p=relative+'/'+e.name;return e.isDirectory()?walk(root,p.slice(1)):['/'+p.replace(/^\//,'')];
 });
}

export function retainArtwork({previous,output,verifyOnly=false}){
 const roots=Array.isArray(previous)?previous:previous?[previous]:[];
 if(!roots.length)throw Error('RETENTION_BASELINE_REQUIRED');
 const entries=new Map();let copiedFiles=0;
 for(const root of roots){
  let result;
  try{result=JSON.parse(execFileSync(process.env.PYTHON||'python',[
   fileURLToPath(new URL('./publish_content.py',import.meta.url)),
   verifyOnly?'--verify-retained':'--retain-only','--retain-assets-from',root,'--output',output
  ],{encoding:'utf8',stdio:['ignore','pipe','pipe']}));}
  catch(error){throw Error('ARTWORK_RETENTION_FAILED: '+String(error.stderr||error.message));}
  copiedFiles+=result.copiedFiles;
  for(const entry of result.entries)entries.set(entry.path,entry);
 }
 const inventory=[...entries.values()].sort((a,b)=>a.path<b.path?-1:a.path>b.path?1:0);
 return{requiredFiles:inventory.length,copiedFiles,inventorySha256:sha(encode(inventory)),entries:inventory};
}

export function buildPublicCatalogue({source,content,output,previous,maxFiles=20000,maxFileBytes=25*1024*1024}){
 if(existsSync(output))throw Error('OUTPUT_EXISTS');
 if(!previous||(Array.isArray(previous)&&!previous.length))throw Error('RETENTION_BASELINE_REQUIRED');
 const reportBytes=file(source,'static-catalogue-report.json'),report=JSON.parse(reportBytes);
 if(!hashPattern.test(report.revision)||!/^\d{4}-\d{2}-\d{2}$/.test(report.date)||!/^[0-9]{4}$/.test(report.year))throw Error('SOURCE_INVALID_REPORT');
 const legacyPrefix='/internal/prebuilt/'+report.revision+'/',oldManifestPath=legacyPrefix+'manifest.json';
 const listed=new Map();
 for(const e of report.entries){
  if(!(e.path===oldManifestPath||new RegExp('^'+legacyPrefix+'shared/[a-f0-9]{64}\\.json$').test(e.path))||listed.has(e.path)||!Number.isSafeInteger(e.bytes)||e.bytes<0)throw Error('SOURCE_INVALID_ENTRY');
  listed.set(e.path,e.bytes);
 }
 if(listed.size!==report.files||!listed.has(oldManifestPath))throw Error('SOURCE_INVENTORY_MISMATCH');
 const manifestRaw=file(source,oldManifestPath),manifest=JSON.parse(manifestRaw);
 if(manifestRaw.length!==listed.get(oldManifestPath)||manifest.version!==1||manifest.revision!==report.revision)throw Error('SOURCE_MANIFEST_MISMATCH');
 const contentRaw=file(content,'internal/content/manifest.json'),cm=JSON.parse(contentRaw);
 if(cm.schemaVersion!==1||!hashPattern.test(cm.revision))throw Error('CONTENT_SCHEMA');
 const bundleRaw=file(content,'internal/content/rpc-bundle.json'),bundle=JSON.parse(bundleRaw),documents={};
 for(const [id,description]of Object.entries(cm.documents)){
  if(!/^[a-z][a-z0-9-]{0,100}$/.test(id))throw Error('CONTENT_DOCUMENT_ID');
  const raw=file(content,'internal/content/documents/'+id+'.json'),value=JSON.parse(raw);
  if(sha(raw)!==description.sha256||value.revision!==description.revision||value.schemaVersion!==1)throw Error('CONTENT_DOCUMENT_MISMATCH');
  documents[id]=raw.toString('utf8');
 }
 // Document object insertion order is not semantic, but exact document bytes are.
 const bundleDocuments=bundle.result?.documents;
 if(bundle.apiVersion!==1||bundle.method!=='bundle'||bundle.revision!==cm.revision||JSON.stringify(bundle.result?.manifest)!==JSON.stringify(cm)||!bundleDocuments||Object.keys(bundleDocuments).length!==Object.keys(documents).length||Object.entries(documents).some(([id,raw])=>bundleDocuments[id]!==raw))throw Error('CONTENT_BUNDLE_MISMATCH');
 const artwork=walk(join(content,'assets/content'));
 for(const path of artwork){
  if(!/^\/[a-f0-9]{64}\.[a-z0-9]+$/.test(path))throw Error('CONTENT_ARTWORK_PATH');
  const raw=file(content,'/assets/content'+path);if(sha(raw)!==path.slice(1,65))throw Error('CONTENT_ARTWORK_HASH');
 }
 for(const asset of Object.values(cm.assets)){
  if(!/^\/assets\/content\/[a-f0-9]{64}\.[a-z0-9]+$/.test(asset.path))throw Error('CONTENT_ARTWORK_PATH');
  const raw=file(content,asset.path);if(sha(raw)!==asset.sha256||raw.length!==asset.bytes)throw Error('CONTENT_ARTWORK_MISMATCH');
 }
 // Publication identity includes the exact source index, date policy and content.
 // LIBRARY's source revision remains unchanged for old authenticated clients.
 const retention=retainArtwork({previous,output});
 const revision=sha(encode({contract:'i05-static-v1',sourceManifestSha256:sha(manifestRaw),contentManifestSha256:sha(contentRaw),retainedArtworkSha256:retention.inventorySha256,date:report.date,year:report.year}));
 const publicPrefix='/catalogue/'+revision+'/',written=new Map(),converted=new Map(),loading=new Set();
 function write(path,raw){
  if(raw.length>maxFileBytes)throw Error('FREE_FILE_SIZE');
  if(written.has(path)){if(written.get(path)!==raw.length)throw Error('OUTPUT_COLLISION');return path;}
  if(written.size+1>maxFiles)throw Error('FREE_FILE_LIMIT');
  const target=join(output,path);mkdirSync(dirname(target),{recursive:true});writeFileSync(target,raw);written.set(path,raw.length);return path;
 }
 function putRaw(raw){return write(publicPrefix+'shared/'+sha(raw)+'.json',raw);}
 for(const item of retention.entries)write(item.path,file(output,item.path));
 function put(value){return putRaw(encode(value));}
 function rewrite(value){
  if(typeof value==='string'&&value.startsWith('/internal/prebuilt/'))return convert(value);
  if(Array.isArray(value))return value.map(rewrite);
  if(value&&typeof value==='object')return Object.fromEntries(Object.entries(value).map(([key,v])=>[key,rewrite(v)]));
  return value;
 }
 function convert(path){
  if(!listed.has(path)||path===oldManifestPath)throw Error('SOURCE_UNLISTED_REFERENCE');
  if(converted.has(path))return converted.get(path);
  if(loading.has(path))throw Error('SOURCE_REFERENCE_CYCLE');loading.add(path);
  const raw=file(source,path);
  if(raw.length!==listed.get(path)||sha(raw)!==path.slice(-69,-5))throw Error('SOURCE_HASH_MISMATCH');
  const result=put(rewrite(JSON.parse(raw)));converted.set(path,result);loading.delete(path);return result;
 }
 const prepared=rewrite(manifest);
 // Verify and retain every declared source file, including any deduplicated leaf.
 for(const path of listed.keys())if(path!==oldManifestPath)convert(path);
 const publicDocuments={};
 for(const [id,raw]of Object.entries(documents))publicDocuments[id]={...cm.documents[id],path:putRaw(Buffer.from(raw))};
 const contentManifestPath=putRaw(contentRaw),contentBundlePath=putRaw(bundleRaw);
 const index={...prepared,revision,schemaVersion:1,sourceRevision:report.revision,publicationDate:report.date,publicationYear:report.year,
  timePolicy:{comics:'publisher-year',malNewReleases:'not-before-no-future-titles',theatreRotation:'client-utc-day'},
  content:{manifest:{path:contentManifestPath,sha256:contentManifestPath.slice(-69,-5)},bundle:{path:contentBundlePath,sha256:contentBundlePath.slice(-69,-5)},documents:publicDocuments}};
 const indexRaw=encode(index),indexPath=write(publicPrefix+'index-'+sha(indexRaw)+'.json',indexRaw);
 write(oldManifestPath,encode({...prepared,publicRevision:revision}));
 // Preserve authenticated content routes and all provided immutable artwork URLs.
 write('/internal/content/manifest.json',contentRaw);write('/internal/content/rpc-bundle.json',bundleRaw);
 for(const [id,raw]of Object.entries(documents))write('/internal/content/documents/'+id+'.json',Buffer.from(raw));
 for(const path of artwork)write('/assets/content'+path,file(content,'/assets/content'+path));
 retainArtwork({previous,output,verifyOnly:true});
 write('/_headers',Buffer.from(PUBLIC_HEADERS));
 const entry={schemaVersion:1,revision,sourceRevision:report.revision,index:{path:indexPath,sha256:sha(indexRaw),bytes:indexRaw.length}};
 // Publish the entry last: failed validation never leaves a usable current pointer.
 write('/catalogue/current.json',encode(entry));
 return{schemaVersion:1,revision,sourceRevision:report.revision,index:entry.index,sourceReportSha256:sha(reportBytes),sourceManifestSha256:sha(manifestRaw),contentManifestSha256:sha(contentRaw),date:report.date,year:report.year,
  files:written.size,totalBytes:[...written.values()].reduce((a,b)=>a+b,0),largestFileBytes:Math.max(...written.values()),freeFileLimit:maxFiles,freeFileBytes:maxFileBytes,
  artworkFiles:[...written.keys()].filter(p=>p.startsWith('/assets/content/')).length,retention,contentDocuments:Object.keys(documents).length,listSelections:report.listSelections,sourceFilesReused:listed.size-1,d1Writes:0,deployed:false,
  entries:[...written].map(([path,bytes])=>({path,bytes}))};
}

if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const [source,content,output,evidence,...previous]=process.argv.slice(2);
 if(!source||!content||!output||!evidence||!previous.length)throw Error('Usage: node projection/public_static.mjs I01-publication content-publication fresh-output report-outside-output.json previous-publication [other-previous-publications...]');
 if(resolve(evidence).startsWith(resolve(output)))throw Error('REPORT_MUST_STAY_OUTSIDE_ASSETS');
 const result=buildPublicCatalogue({source,content,output,previous});writeFileSync(evidence,JSON.stringify(result,null,2));
 console.log(JSON.stringify({...result,entries:undefined,retention:{...result.retention,entries:undefined}}));
}
