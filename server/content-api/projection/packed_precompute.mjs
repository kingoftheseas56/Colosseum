import {DatabaseSync} from 'node:sqlite';
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {catalogueMethod} from '../src/catalogue-semantic.ts';
const [artifact,output,extra] = process.argv.slice(2);
if(!artifact||!output)throw Error('Usage: node projection/packed_precompute.mjs packed.sqlite output [extra-requests.json]');
const revision=JSON.parse(readFileSync(join(artifact,'../packed-report.json'),'utf8')).revision;
const sqlite=new DatabaseSync(artifact,{readOnly:true});
const db={prepare(sql){return{bind(...args){return{async all(){return{success:true,results:sqlite.prepare(sql).all(...args)}}}}}}};
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
const requests=[];
const add=(dataset,method,args)=>requests.push({dataset,method,args});
for(const medium of ['anime','manga'])add('mal','genreCounts',[medium]);
for(const explicit of [false,true]){
 for(const axis of ['genre','demographic'])add('mal','discoverFilters',[axis,explicit]);
 for(const axis of ['genre','publisher','format','availability'])add('comics','discoverFilters',[axis,explicit]);
 for(const catalog of ['popular','top-rated','new-releases','trending'])add('mal','discoverPageFiltered',[catalog,[],explicit,0,24]);
 for(const catalog of ['popular','new-releases','most-stocked','all','recently-available','complete-runs','near-complete','community-collections'])add('comics','discoverPageFiltered',[catalog,[],explicit,0,24]);
}
add('comics','curatedRanked',[]);add('comics','curatedReady',[]);add('comics','curatedGenreShelves',[8]);
for(const[kind,arg]of [['stocked',''],['publisher','Marvel'],['publisher','DC'],['publisher','Image'],['decade','2020'],['decade','2010'],['deep',''],['fanmade','']])add('comics','shelf',[kind,arg,24]);
for(const key of ['omnibus','deluxe','graphic novel'])add('comics','discoverPageFiltered',['popular',[{axis:'format',key}],false,0,24]);
if(extra)requests.push(...JSON.parse(readFileSync(extra,'utf8')).map(r=>({dataset:r.dataset,method:r.method,args:r.args})));
mkdirSync(output,{recursive:true});const entries=[],seen=new Set();
for(const r of requests){
 // Current-date MAL admission and current-year Comics house scores remain live.
 if(r.dataset==='mal'&&r.method==='discoverPageFiltered'&&r.args[0]==='new-releases')continue;
 if(r.dataset==='comics'&&r.method==='discoverPageFiltered')continue;
 r.args=canonical(r.args);
 const hash=createHash('sha256').update(JSON.stringify(r.args)).digest('hex');const path=`/internal/catalogue/${r.dataset}/${r.method}/${hash}.json`;if(seen.has(path))continue;seen.add(path);
 const result=await catalogueMethod(db,r.dataset,r.method,r.args);const body=JSON.stringify({apiVersion:1,revision,method:r.method,result});const bytes=Buffer.byteLength(body);if(bytes>4*1024*1024)throw Error('Response exceeds cap: '+path);
 const target=join(output,path);mkdirSync(join(target,'..'),{recursive:true});writeFileSync(target,body);entries.push({...r,path,bytes,sha256:createHash('sha256').update(body).digest('hex')});
}
sqlite.close();writeFileSync(join(output,'precompute-report.json'),JSON.stringify({revision,entries,totalBytes:entries.reduce((a,e)=>a+e.bytes,0)},null,2));console.log(JSON.stringify({revision,files:entries.length,totalBytes:entries.reduce((a,e)=>a+e.bytes,0)}));
