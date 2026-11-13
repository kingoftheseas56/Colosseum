'use strict';
// Exact pinned modules. Synthetic records only; no file discovery or network.
const fs=require('node:fs'),vm=require('node:vm'),{spawnSync}=require('node:child_process');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle=extractModules(process.argv[2]);
if(oracle.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
let fetchResponse=null,fetchedEntry=null;const cache=new Map();const now=1700000000000;class Clock extends Date{constructor(...args){super(...(args.length?args:[now]));}static now(){return now;}}
const context=vm.createContext({console:{log(){},error(){}},Date:Clock,Buffer,Promise,process:{nextTick:process.nextTick}});
function req(id){if(id===1)return fs;if(id===5)return require('node:path');if(id===0)return require('node:util');if(id===103)return ()=>{};if(id===501)return {indexParsedTorrent(t,cb){cb(null,fetchedEntry);}};if(id===34)return ()=>fetchResponse?Promise.resolve({json:async()=>fetchResponse}):Promise.reject(Error('offline fixture'));
if(cache.has(id))return cache.get(id).exports;const m={exports:{}};cache.set(id,m);vm.runInContext('('+oracle.modules[id].source+')',context)(m,m.exports,req);return m.exports;}
const movie={path:'/fixture/Alpha.mp4',name:'Alpha.mp4',length:30,parsedName:'Alpha',imdb_id:'tt123',type:'movie'};
const episode={path:'/fixture/Show.S01E02.mkv',name:'Show.S01E02.mkv',length:60,parsedName:'Show',imdb_id:'tt456',type:'series',season:1,episode:2,idx:4};
const entries=[{key:'one',entry:{itemId:'local:tt123',name:'Alpha.mp4',dateModified:'2023-01-01T00:00:00.000Z',files:[movie]}},{key:'two',entry:{itemId:'local:tt123',name:'duplicate',files:[{...movie,path:'/fixture/Other.mp4'}]}},{key:'episode',entry:{itemId:'local:tt456',name:'Show',files:[episode]}},{key:'torrent',entry:{itemId:'bt:abc',ih:'abc',name:'Bundle',sources:['dht:abc','tracker:https://tracker.example.test'],files:[{...movie,idx:8},episode,{path:'/fixture/unknown.mkv',name:'unknown.mkv',length:1,idx:9}]}},{key:'empty',entry:{files:[]}}];
const rows=[];const add=row=>rows.push(JSON.parse(JSON.stringify(row)));const Storage=req(1079),storage=new Storage({entryIndexes:['itemId']}),metaStorage=new Storage();for(const r of entries)storage.saveEntry(r.key,r.entry,()=>{});
function callback(fn,...args){return new Promise((resolve,reject)=>fn(...args,(e,v)=>e?reject(e):resolve(v)));}
(async()=>{
for(const disabled of [false,true])add({action:'manifest',disabled,entries,expected:req(disabled?1054:499)});
add({action:'catalog',entries,expected:await callback(req(1055),storage,metaStorage,{type:'other',id:'local'})});
for(const [type,id] of [['movie','tt123'],['series','tt456:1:2'],['series','tt456:1:3'],['movie','tt456:1:2'],['other','tt123'],['movie','unknown']])add({action:'stream',type,id,entries,expected:await callback(req(1078),storage,{type,id})});
for(const id of ['local:tt123','local:tt456','bt:abc','local:missing']){const before=JSON.parse(JSON.stringify(entries));add({action:'meta',id,now,entries:before,expected:await callback(req(1056),storage,metaStorage,'http://127.0.0.1:1',{id})||{}});}
metaStorage.saveEntry('local:tt123',{id:'local:tt123',type:'other',name:'Enriched'},()=>{});
add({action:'catalog',entries,metas:{'local:tt123':{id:'local:tt123',type:'other',name:'Enriched'}},expected:await callback(req(1055),storage,metaStorage,{})});
const records=[{name:'The Alpha & Omega (film)',type:'movie',year:'2021-2022',imdb_id:'tt1234567'},{name:'Example Show',type:'series',year:1999,imdb_id:'tt7654321'}];
for(const query of [{name:'The Alpha and Omega',type:'movie',year:2020},{name:'The Alpha and Omega',type:'movie',year:2018},{name:'Example Show',type:'series',year:2025},{name:'Wrong',type:'movie'},{name:'',type:'movie'}]){cache.delete(1068);fetchResponse={result:JSON.parse(JSON.stringify(records))};add({action:'recognize',records,query,expected:await callback(req(1068),query)||{}});}
for(const fail of [false,true]){
 const entry={itemId:'bt:remote',ih:'remote',name:'Remote',sources:null,files:[{idx:4,path:'Remote/clip.mkv',name:'clip.mkv',length:100}]};
 const torrent={infoHash:'remote',name:'Remote',files:entry.files};fetchResponse=fail?null:torrent;fetchedEntry=JSON.parse(JSON.stringify(entry));
 add({action:'resolveMeta',id:'bt:remote',now,entry,torrent,fail,expected:await callback(req(1056),storage,metaStorage,'http://127.0.0.1:1',{id:'bt:remote'})||{}});
}
for(const value of [undefined,null,0,'',false,true,'2020-2024','bad','-2020',2020.5])for(const qYear of [undefined,null,0,false,true,'bad','2020-2021',2020]){
 const record={name:'Optional',type:'movie'};if(value!==undefined)record.year=value;
 const query={name:'Optional',type:'movie'};if(qYear!==undefined)query.year=qYear;
 const records=[record];cache.delete(1068);fetchResponse={result:JSON.parse(JSON.stringify(records))};add({action:'recognize',records,query,expected:await callback(req(1068),query)||{}});
}
for(const record of [{name:'Optional',type:'movie',imdb_id:null},{name:'Optional',type:'movie',year:null},{name:'Optional 0',type:'movie',year:0},{name:'Optional true',type:'movie',year:true}]){
 const query={name:record.name,type:'movie',year:record.year};const records=[record];cache.delete(1068);fetchResponse={result:JSON.parse(JSON.stringify(records))};add({action:'recognize',records,query,expected:await callback(req(1068),query)||{}});
}
fetchResponse=null;
for(const entry of [{files:[]},{itemId:'local:tt1',files:[{path:'/fixture/unknown.mp4'}]},{itemId:'local:tt1',name:null,files:[{imdb_id:'tt1',type:'movie'}]}])add({action:'genericMeta',entry,expected:await req(502)(JSON.parse(JSON.stringify(entry)))});
fetchResponse={meta:{name:'Enriched',description:'Fixture',imdbRating:'8',secret:'discard',videos:[{}],type:'movie',id:'remote'}};
add({action:'enrich',entry:entries[0].entry,remote:fetchResponse,expected:await req(502)(entries[0].entry)});
fetchResponse=null;
fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2)+'\n');if(process.argv[4]){const result=spawnSync(process.argv[4],[process.argv[3]],{stdio:'inherit'});process.exitCode=result.status??1;}
})().catch(e=>{console.error(e);process.exitCode=1;});
