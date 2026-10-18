// Controlled synthetic catalogue, shaped from existing static-publication.test.mjs.
// No live/provider completeness claims. Stock public publisher remains unchanged.
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {join,dirname,resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';
const [sourceArg,workArg]=process.argv.slice(2),repo=resolve(sourceArg),work=resolve(workArg);
const {buildPublicCatalogue}=await import(pathToFileURL(join(repo,'server/content-api/projection/public_static.mjs')));
const hash=b=>createHash('sha256').update(b).digest('hex'),enc=JSON.stringify;
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
const key=(d,m,a)=>enc([d,m,canonical(a)]);
const source=join(work,'synthetic-source'),revision=hash('Colosseum QA controlled synthetic prepared catalogue v1'),prefix='/internal/prebuilt/'+revision+'/',entries=new Map();
function write(base,path,raw){let file=join(base,path);mkdirSync(dirname(file),{recursive:true});writeFileSync(file,raw);}
function put(value){let raw=enc(value),path=prefix+'shared/'+hash(raw)+'.json';write(source,path,raw);entries.set(path,Buffer.byteLength(raw));return path;}
const comic={locgId:'137131',title:'The Amazing Spider-Man',cover:''};
const manga={id:13,malId:13,title:'One Piece',cover:''};
const fixed={},listBuckets={},bucketLists={},lookups={};
function fix(d,m,a,value){fixed[key(d,m,a)]=put(value);}
function list(d,m,a,items,shape){const k=key(d,m,a),bucket=hash(k).slice(0,2);(bucketLists[bucket]??={})[k]={parts:items.length?[put(items)]:[],length:items.length,chunkSize:256,shape,period:'stable'};}
for(const explicit of [false,true]){
 for(const c of ['popular','new-releases','most-stocked','all','recently-available','complete-runs','near-complete','community-collections'])list('comics','discoverPageFiltered',[c,[],explicit],[comic],'comics');
 for(const c of ['popular','trending','top-rated','new-releases'])list('mal','discoverPageFiltered',[c,[],explicit],[manga],'mal');
 for(const d of ['mal','comics'])for(const axis of ['genre','demographic','publisher','format','availability'])for(const m of ['discoverFilters','cachedDiscoverFilters'])fix(d,m,[axis,explicit],[]);
}
fix('comics','curatedReady',[],true);fix('comics','curatedRanked',[],[comic]);fix('comics','curatedGenreShelves',[-1],[]);
const landingQueries=[{method:'curatedReady',args:[],result:true},{method:'curatedRanked',args:[],result:[comic]},{method:'curatedGenreShelves',args:[8],result:[]},{method:'discoverPageFiltered',args:['popular',[],false,0,24],result:{items:[comic],nextOffset:1,exhausted:true,freshness:'bundled'}}];
fix('comics','landing',['comics'],{version:1,dataset:'comics',tab:'comics',validFrom:'2026-10-18',validUntil:'2027-01-01',queries:landingQueries});
list('imdb','titleCatalog',[{}],[],'array');
const mr=put([{identity:manga}]),mn=put([['one piece',13,mr,0,'one piece',1997,0,0]]),mk=put([[13,mr,0]]);
// Existing legitimate title; absent ratings remain null. Never manufacture ratings.
const ik=put([['tt0076759',null,0,{identity:{id:'tt0076759',title:'Star Wars: Episode IV - A New Hope',rating:null,votes:null,isAnime:false},ordinal:0}]]);
const lk=key('comics','curatedSeries',['137131']);lookups[hash(lk).slice(0,2)]=put({[lk]:{locgId:'137131',title:comic.title,editions:[]}});
for(const [bucket,value]of Object.entries(bucketLists))listBuckets[bucket]=put(value);
const manifest={version:1,revision,lists:{},fixed,listBuckets,lookups,search:{manga:{names:[{path:mn,first:'one piece',last:'one piece'}],keys:[{path:mk,first:13,last:13}]},imdb:{names:[],keys:[{path:ik,first:'tt0076759',last:'tt0076759'}]}}};
const raw=enc(manifest),mp=prefix+'manifest.json';write(source,mp,raw);entries.set(mp,Buffer.byteLength(raw));
write(source,'static-catalogue-report.json',enc({revision,date:'2026-10-18',year:'2026',files:entries.size,listSelections:Object.values(bucketLists).reduce((n,v)=>n+Object.keys(v).length,0),source:{mal:{sha256:hash('synthetic-source-not-live')}},entries:[...entries].map(([path,bytes])=>({path,bytes}))}));
for(const label of ['A','B']){
 const report=buildPublicCatalogue({source,content:join(work,'content-'+label),previous:label==='A'?join(work,'content-A'):join(work,'publication-A'),output:join(work,'publication-'+label)});
 write(work,'publication-'+label+'-report.json',JSON.stringify(report,null,2));
 console.log(label,report.revision,report.files,report.totalBytes);
}
write(work,'fixture-scope.json',JSON.stringify({synthetic:true,liveCoverage:false,comicIdentityEvidence:'b1-native/ci-run-39129013913/evidence/b1-evidence/runtime/deep-comics-series-1.json',comicFields:'Only locgId/title retained. Unknown cover/year omitted or empty. No new provider identity.',name:'Colosseum QA Publication 1.1.9',runtimeTested:false},null,2));
