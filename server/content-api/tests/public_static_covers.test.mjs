import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,mkdirSync,readFileSync,writeFileSync,readdirSync,rmSync,existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import childProcess,{execFileSync} from 'node:child_process';
import {syncBuiltinESMExports} from 'node:module';
import {buildPublicCatalogue} from '../projection/public_static.mjs';
import {loadOwnedCovers,validateOwnedOrigin} from '../projection/cover_publication.mjs';
const sha=v=>createHash('sha256').update(v).digest('hex');
const original='https://source.example.org/cover.png',duplicate='https://source.example.org/alias.png',missing='https://source.example.org/unprepared.png';

function fixture(t,{unmappedLanding=false}={}){
 const root=mkdtempSync(join(tmpdir(),'owned-publication-'));t.after(()=>rmSync(root,{recursive:true,force:true}));
 const source=join(root,'source'),content=join(root,'content'),output=join(root,'out'),ownedCoverRoot=join(root,'owned'),ownedCoverManifest=join(root,'manifest.json');
 const write=(base,path,raw)=>{const target=join(base,path);mkdirSync(dirname(target),{recursive:true});writeFileSync(target,raw);};
 const revision='1'.repeat(64),prefix='/internal/prebuilt/'+revision+'/',entries=[];
 const put=value=>{const raw=JSON.stringify(value),path=prefix+'shared/'+sha(raw)+'.json';write(source,path,raw);entries.push({path,bytes:Buffer.byteLength(raw)});return path;};
 const cards=[{id:1,cover:original,poster:duplicate,source_cover:original,covers:[original,duplicate],coverCandidates:['https://fallback.example.org/cover']}];
 const landing=put({version:1,dataset:'comics',tab:'comics',queries:[{method:'shelf',args:[],result:unmappedLanding?[...cards,{id:2,cover:missing}]:cards}]});
 const deep=put([{id:3,cover:missing}]);
 const manifest={version:1,revision,fixed:{landing,deep},lists:{},listBuckets:{},lookups:{},search:{}};
 const raw=JSON.stringify(manifest);write(source,prefix+'manifest.json',raw);entries.push({path:prefix+'manifest.json',bytes:Buffer.byteLength(raw)});
 write(source,'static-catalogue-report.json',JSON.stringify({revision,date:'2026-10-16',year:'2026',files:entries.length,entries,listSelections:0}));
 const cm={schemaVersion:1,revision:'2'.repeat(64),documents:{},assets:{}};
 mkdirSync(join(content,'assets/content'),{recursive:true});
 write(content,'internal/content/manifest.json',JSON.stringify(cm));
 write(content,'internal/content/rpc-bundle.json',JSON.stringify({apiVersion:1,method:'bundle',revision:cm.revision,result:{manifest:cm,documents:{}}}));
 // Synthetic WebP bytes and an ordinary untrusted manifest: production verifier must check it.
 execFileSync(process.env.PYTHON||'python',['-c',String.raw`
from PIL import Image
import io,hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]); data=io.BytesIO(); Image.new('RGB',(12,18),'blue').save(data,'WEBP')
body=data.getvalue(); digest=hashlib.sha256(body).hexdigest(); key='covers/'+digest+'.webp'; (root/'covers').mkdir(parents=True); (root/key).write_bytes(body)
entry={'status':'complete','key':key,'sha256':digest,'bytes':len(body),'width':12,'height':18,'owned_url':'https://covers.example.org/'+key}
urls=sys.argv[3:]; manifest={'version':1,'origin':'https://covers.example.org','assets':{url:dict(entry,original_url=url) for url in urls}}
Path(sys.argv[2]).write_text(json.dumps(manifest))
`,ownedCoverRoot,ownedCoverManifest,original,duplicate]);
 return {root,source,content,output,previous:content,ownedCoverRoot,ownedCoverManifest,manifest};
}
function index(f,report){return JSON.parse(readFileSync(join(f.output,report.index.path)));}
function payload(f,path){return JSON.parse(readFileSync(join(f.output,path)));}

test('legacy mode retains exact revision contract and provider URLs',t=>{
 const f=fixture(t),r=buildPublicCatalogue({...f,ownedCoverManifest:undefined,ownedCoverRoot:undefined});
 const expected=sha(JSON.stringify({contract:'i05-static-v1',sourceManifestSha256:r.sourceManifestSha256,contentManifestSha256:r.contentManifestSha256,retainedArtworkSha256:r.retention.inventorySha256,date:r.date,year:r.year}));
 assert.equal(r.revision,expected);assert.equal(r.ownedCovers,undefined);
 assert.equal(payload(f,index(f,r).fixed.landing).queries[0].result[0].cover,original);
});

test('verified covers transform before shared hashes, preserve all fallback originals, and stay external',t=>{
 const f=fixture(t),r=buildPublicCatalogue({...f,requireOwnedLandingCovers:true}),i=index(f,r),card=payload(f,i.fixed.landing).queries[0].result[0];
 const owned=JSON.parse(readFileSync(f.ownedCoverManifest)).assets[original].owned_url;
 assert.equal(card.cover,owned);assert.equal(card.poster,owned);assert.deepEqual(card.covers,[owned,owned]);
 assert.deepEqual(card.coverCandidates,['https://fallback.example.org/cover',original,duplicate]);
 assert.equal(payload(f,i.fixed.deep)[0].cover,missing);
 assert.equal(r.ownedCovers.mappedUrls,2);assert.equal(r.ownedCovers.objects,1);assert.equal(r.ownedCovers.inventory.length,1);
 assert.equal(r.ownedCovers.hostedVerificationPending,true);assert.equal(r.ownedCovers.uploaded,false);
 assert.equal(existsSync(join(f.output,'covers')),false);
 for(const path of Object.values(i.fixed)){const bytes=readFileSync(join(f.output,path));assert.equal(sha(bytes),path.slice(-69,-5));}
});

test('changed owned origin/map changes publication identity while repeated maps remain deterministic',t=>{
 const f=fixture(t),one=buildPublicCatalogue(f);
 const two=buildPublicCatalogue({...f,output:join(f.root,'second')});assert.equal(one.revision,two.revision);
 const manifest=JSON.parse(readFileSync(f.ownedCoverManifest));manifest.origin='https://new-covers.example.org';
 for(const e of Object.values(manifest.assets))e.owned_url=manifest.origin+'/'+e.key;
 writeFileSync(f.ownedCoverManifest,JSON.stringify(manifest));
 const three=buildPublicCatalogue({...f,output:join(f.root,'third')});assert.notEqual(one.revision,three.revision);
 assert.notEqual(one.ownedCovers.mappingSha256,three.ownedCovers.mappingSha256);
});

test('corrupt asset, unsafe child key, unmatched options and HTTP origin fail closed',t=>{
 for(const mode of ['corrupt','path','origin','pair']){
  const f=fixture(t),manifest=JSON.parse(readFileSync(f.ownedCoverManifest));
  if(mode==='corrupt'){const e=manifest.assets[original];writeFileSync(join(f.ownedCoverRoot,e.key),Buffer.alloc(e.bytes));}
  if(mode==='path')manifest.assets[original].key='../escape.webp';
  if(mode==='origin')manifest.origin='http://covers.example.org';
  writeFileSync(f.ownedCoverManifest,JSON.stringify(manifest));
  assert.throws(()=>buildPublicCatalogue({...f,...(mode==='pair'?{ownedCoverRoot:undefined}:{})}),/OWNED_COVERS_/);
  assert.equal(existsSync(join(f.output,'catalogue/current.json')),false);
 }
});

test('strict landing check rejects mixed origins; optional relaxed mode remains honest',t=>{
 const f=fixture(t,{unmappedLanding:true});
 assert.throws(()=>buildPublicCatalogue({...f,requireOwnedLandingCovers:true}),/OWNED_COVERS_LANDING_UNMAPPED/);
 assert.equal(existsSync(join(f.output,'catalogue/current.json')),false);
 const output=join(f.root,'relaxed'),r=buildPublicCatalogue({...f,output});
 const view={...f,output};assert.equal(payload(view,index(view,r).fixed.landing).queries[0].result[1].cover,missing);
});

test('verified map loader runs once for the complete publication graph',t=>{
 const f=fixture(t);
 const execute=childProcess.execFileSync;let verificationCalls=0;
 childProcess.execFileSync=function(command,args,...options){if(args?.[1]==='export-map')verificationCalls++;return execute(command,args,...options);};
 syncBuiltinESMExports();
 try{buildPublicCatalogue(f);assert.equal(verificationCalls,1);}
 finally{childProcess.execFileSync=execute;syncBuiltinESMExports();}
 const loaded=loadOwnedCovers({manifest:f.ownedCoverManifest,root:f.ownedCoverRoot});
 const first=loaded.transform({cover:original}),second=loaded.transform({cover:duplicate});
 assert.equal(first.cover,second.cover);assert.deepEqual(first.coverCandidates,[original]);
 assert.equal(typeof loaded.identity,'string');
});

test('owned origin boundary rejects URL-normalized local/nonpublic aliases without DNS',()=>{
 for(const origin of ['https://localhost','https://x.localhost','https://printer.local','https://127.1','https://2130706433','https://0x7f000001','https://0177.0.0.1','https://%31%32%37.0.0.1','https://[::1]','https://[fc00::1]','https://[fe80::1]','https://[::ffff:127.0.0.1]','https://192.168.1.2','https://100.64.0.1'])assert.throws(()=>validateOwnedOrigin(origin),/OWNED_COVERS_ORIGIN/,origin);
 for(const origin of ['https://covers.example.org','https://8.8.8.8','https://[2606:4700:4700::1111]'])assert.equal(validateOwnedOrigin(origin).protocol,'https:');
});

// A Comics-only rollout must not force unmeasured Theatre artwork into owned storage.
test('strict Comics landing ownership leaves Theatre and MAL provider artwork unchanged',t=>{
 const f=fixture(t);
 const loaded=loadOwnedCovers({manifest:f.ownedCoverManifest,root:f.ownedCoverRoot,requireOwnedLandingCovers:true});
 for(const dataset of ['imdb','mal']){
  const bundle={version:1,dataset,tab:'provider-tab',queries:[{method:'example',args:[],result:[{cover:missing}]}]};
  assert.deepEqual(loaded.transform(bundle),bundle);
 }
});
