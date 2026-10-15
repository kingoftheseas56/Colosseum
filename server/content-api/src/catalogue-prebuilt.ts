import {clamp,integer,text} from './catalogue-semantic.ts';
type Ref={path:string;first:string|number;last:string|number};
type List={parts:string[];length:number;chunkSize:number;shape:'array'|'mal'|'comics';period:string;notBefore?:string};
type Manifest={version:1;revision:string;lists:Record<string,List>;listBuckets?:Record<string,string>;search:Record<string,{names:Ref[];keys:Ref[]}>;fixed:Record<string,string>;lookups?:Record<string,string>;tankobanSourceHash?:string};
const cachesByAssets=new WeakMap<Fetcher,Map<string,{value:any;bytes:number}>>();
const MAX_CACHE_BYTES=12*1024*1024;
const canonical=(v:any):any=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
export const selectionKey=(dataset:string,method:string,args:any[])=>JSON.stringify([dataset,method,canonical(args)]);
const fold=(s:string)=>s.replace(/[A-Z]/g,c=>c.toLowerCase());
const lexical=(a:string,b:string)=>a<b?-1:a>b?1:0;
const descending=(a:any,b:any)=>a==null?(b==null?0:1):b==null?-1:Number(b)-Number(a);
export async function prebuiltCatalogue(assets:Fetcher,revision:string,dataset:string,method:string,args:any[]):Promise<{result:any}|null>{
 const prefix='/internal/prebuilt/'+revision+'/';let reads=0;
 async function get(path:string):Promise<any>{
  if(!path.startsWith(prefix)||path.includes('..'))throw Error('INVALID_PREBUILT_PATH');
  let cache=cachesByAssets.get(assets);if(!cache){cache=new Map();cachesByAssets.set(assets,cache);}
  const hit=cache.get(path);if(hit){cache.delete(path);cache.set(path,hit);return hit.value;}
  // Keep below the Free invocation subrequest ceiling, including readiness and
  // legacy fallback. No unbounded Promise.all or cross-request I/O promises.
  if(++reads>44)throw Error('PREBUILT_REQUEST_LIMIT');
  const response=await assets.fetch(new Request('https://assets.invalid'+path));
  if(response.status===404)return null;
  if(response.status!==200)throw Error('PREBUILT_ASSET_UNAVAILABLE');
  const reader=response.body?.getReader();if(!reader)throw Error('INVALID_PREBUILT_ASSET');
  const chunks:Uint8Array[]=[];let size=0;
  for(;;){const r=await reader.read();if(r.done)break;size+=r.value.byteLength;if(size>4*1024*1024){await reader.cancel();throw Error('PREBUILT_ASSET_TOO_LARGE');}chunks.push(r.value);}
  const bytes=new Uint8Array(size);let off=0;for(const chunk of chunks){bytes.set(chunk,off);off+=chunk.length;}
  const value=JSON.parse(new TextDecoder().decode(bytes));
  while([...cache.values()].reduce((n,e)=>n+e.bytes,0)+size>MAX_CACHE_BYTES&&cache.size)cache.delete(cache.keys().next().value!);
  cache.set(path,{value,bytes:size});return value;
 }
 const manifest:Manifest|null=await get(prefix+'manifest.json');if(!manifest)return null;
 if(manifest.version!==1||manifest.revision!==revision)throw Error('PREBUILT_REVISION_MISMATCH');
 async function bucketFor(key:string){const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(key));return new Uint8Array(digest)[0].toString(16).padStart(2,'0');}
 const fixedArgs=method==='discoverFilters'||method==='cachedDiscoverFilters'?[text(args[0]),dataset==='comics'?false:!!args[1]]:args;
 const fixed=manifest.fixed[selectionKey(dataset,method,fixedArgs)];if(fixed){const result=await get(fixed);if(result===null)throw Error('PREBUILT_ASSET_MISSING');return{result};}
 if(dataset==='comics'&&method==='curatedGenreShelves'){
  const path=manifest.fixed[selectionKey(dataset,method,[-1])];if(!path)return null;
  const full=await get(path);if(!Array.isArray(full))throw Error('PREBUILT_ASSET_MISSING');const n=integer(args[0]);
  return{result:full.map(r=>({...r,covers:r.covers.slice(0,n<0?undefined:n)}))};
 }
 if(manifest.lookups&&['series','downloadsFor','curatedSeries','curatedCoverage','curatedByNorm','curatedHasDownloadable','genreCount'].includes(method)){
  const lookupArgs=method==='series'||method==='downloadsFor'?[integer(args[0])]:method==='genreCount'?[text(args[0]),text(args[1])]:[method==='curatedByNorm'?text(args[0]).trim():text(args[0])];
  const key=selectionKey(dataset,method,lookupArgs),bucket=await bucketFor(key);
  const path=manifest.lookups[bucket];if(path){const values=await get(path);if(!values)throw Error('PREBUILT_ASSET_MISSING');if(Object.hasOwn(values,key))return{result:values[key]};}
  if(method==='genreCount')return{result:0};
  if(method==='curatedHasDownloadable')return{result:false};
  if(method==='downloadsFor')return{result:[]};
  if(dataset==='tankoban')return{result:{apiVersion:1,sourceHash:manifest.tankobanSourceHash,ready:true,seriesInfo:{},volumes:[]}};
  return{result:{}};
 }
 let selection:any[]|null=null,offset=0,limit=24,listMethod=method,catalog='';
 if(['mal','comics'].includes(dataset)&&['discoverPage','discoverPageFiltered'].includes(method)){
  let filters:any,explicit:any;
  if(method==='discoverPage'){catalog=text(args[0]);filters=args[1]||args[2]?[{axis:args[1],key:args[2]}]:[];explicit=args[3];offset=Math.max(0,integer(args[4]));limit=clamp(args[5]);}
  else{catalog=text(args[0]);filters=args[1];explicit=args[2];offset=Math.max(0,integer(args[3]));limit=clamp(args[4]);}
  const raw=(Array.isArray(filters)?filters:[]).map(f=>({axis:text(f?.axis),key:text(f?.key)}));
  const axes=dataset==='mal'?['','genre','demographic']:['','genre','publisher','format','availability'];
  const empty=()=>({result:{items:[],nextOffset:offset,exhausted:true,freshness:'bundled',...(dataset==='mal'?{fallbackCatalog:catalog==='trending'?'popular':''}:{})}});
  if(raw.some(f=>!axes.includes(f.axis)))return empty();
  const catalogs=dataset==='mal'?['popular','trending','top-rated','new-releases']:['popular','new-releases','most-stocked','all','recently-available','complete-runs','near-complete','community-collections'];
  if(!catalogs.includes(catalog))return empty();
  const normalized=raw.filter(f=>f.axis&&f.key);
  if(normalized.length<=1){selection=[catalog,normalized.map(f=>({...f,key:fold(f.key)})),!!explicit];listMethod='discoverPageFiltered';}
 }else if(dataset==='imdb'&&method==='titleCatalog'){
  selection=[args[0]&&typeof args[0]==='object'&&!Array.isArray(args[0])?args[0]:{}];offset=Math.max(0,integer(args[1]));limit=clamp(args[2],100);
 }else if(dataset==='mal'&&method==='animeCatalog'){
  selection=[args[0]&&typeof args[0]==='object'&&!Array.isArray(args[0])?args[0]:{}];offset=Math.max(0,integer(args[1]));limit=clamp(args[2]);
 }else if(dataset==='mal'&&method==='genreEntries'){
  selection=[text(args[0]),text(args[1]),text(args[2])==='score'?'score':'members'];limit=args[3]===undefined?24:integer(args[3]);
 }
 if(selection){
  const variants=[selection];
  if((dataset==='imdb'&&method==='titleCatalog')||(dataset==='mal'&&method==='animeCatalog')){
   const fallback=dataset==='imdb'?'votes':'members',q=selection[0];
   if(!text(q.order)||text(q.order)===fallback){const explicit={...q,order:fallback},implicit={...q};delete implicit.order;variants.push([explicit],[implicit]);}
  }
  let list:List|undefined;
  for(const variant of variants){const key=selectionKey(dataset,listMethod,variant);list=manifest.lists[key];const path=manifest.listBuckets?.[await bucketFor(key)];if(!list&&path){const values=await get(path);if(!values)throw Error('PREBUILT_ASSET_MISSING');list=values[key];}if(list)break;}
  if(list){
   const period=dataset==='comics'?String(new Date().getFullYear()):dataset==='mal'&&catalog==='new-releases'?new Date().toISOString().slice(0,10):'stable';
   // Annual Comics scores / daily MAL admission must never outlive their facts.
   if(list.period!=='stable'&&list.period!==period)return null;
   if(list.notBefore&&new Date().toISOString().slice(0,10)<list.notBefore)return null;
   const end=limit<0?list.length:Math.min(list.length,offset+limit),items:any[]=[];
   for(let n=Math.floor(offset/list.chunkSize);n<Math.ceil(end/list.chunkSize);n++){
    const part=await get(list.parts[n]);if(!Array.isArray(part))throw Error('PREBUILT_ASSET_MISSING');
    items.push(...part.slice(Math.max(0,offset-n*list.chunkSize),Math.min(list.chunkSize,end-n*list.chunkSize)));
   }
   if(list.shape==='array')return{result:items};
   if(list.shape==='comics'){const format=(selection[1] as any[]).find(f=>f.axis==='format');if(format){const originals=method==='discoverPage'?[{axis:text(args[1]),key:text(args[2])}]:(Array.isArray(args[1])?args[1]:[]);const key=text(originals.find(f=>f?.axis==='format')?.key);for(let i=0;i<items.length;i++)items[i]={...items[i],format:key};}}
   return{result:{items,nextOffset:offset+items.length,exhausted:list.shape==='comics'?offset+items.length>=list.length:items.length<limit,freshness:'bundled',...(list.shape==='mal'?{fallbackCatalog:catalog==='trending'?'popular':''}:{})}};
  }
  if(['mal','comics'].includes(dataset)&&listMethod==='discoverPageFiltered')return{result:{items:[],nextOffset:offset,exhausted:true,freshness:'bundled',...(dataset==='mal'?{fallbackCatalog:catalog==='trending'?'popular':''}:{})}};
  if(dataset==='mal'&&method==='genreEntries')return{result:[]};
 }
 const families=dataset==='imdb'?['imdb']:dataset==='mal'?(text(args[2])?[text(args[2])]:['anime','manga']):dataset==='comics'?['comics']:[];
 const search=method==='search'||method==='matchByTitle'||method==='exactMatches';
 if(search){
  const norm=dataset==='comics'?text(args[0]).trim():text(args[0]);if(!norm)return{result:[]};
  if(dataset==='mal'&&families.some(f=>!['anime','manga'].includes(f)))return{result:[]};
  const out:any[]=[];
  for(const family of families){
   const directory=manifest.search[family];if(!directory)return null;
   const exact=method!=='search',target=fold(norm),wildcard=/[%_]/.test(norm);
   const parts=dataset==='comics'||wildcard?directory.names:directory.names.filter(p=>fold(text(p.last))>=target&&fold(text(p.first))<=(exact?target:target+'\uffff'));
   const found=new Map<string,any[]>();
   for(const p of parts){const rows=await get(p.path);if(!Array.isArray(rows))throw Error('PREBUILT_ASSET_MISSING');
    for(const e of rows){const name=e[0] as string;
   // The deployed packed prefix directory uses literal normalized key ranges,
   // not SQL LIKE wildcards. Retain that contract for '%'/'_' and casing.
   const match=dataset==='comics'?(exact?fold(name)===target:(norm.split(/[^\p{L}\p{N}]+/u).filter(Boolean).every(w=>fold(name).includes(fold(w))))):exact?name===norm:name.startsWith(norm);
     if(match&&!(dataset==='comics'&&!exact&&!norm.split(/[^\p{L}\p{N}]+/u).filter(Boolean).length)&&(!exact||dataset==='comics'||integer(args[1])===0||e[5]===integer(args[1])))found.set(String(e[1]),e);
    }
   }
   const rows=[...found.values()];
   rows.sort((a,b)=>{
    if(dataset==='comics')return (exact?0:(fold(a[0])===target?0:fold(a[0]).startsWith(target)?1:2)-(fold(b[0])===target?0:fold(b[0]).startsWith(target)?1:2))||descending(a[6],b[6])||(exact?0:descending(a[5],b[5]))||a[7]-b[7];
    return (exact?0:(a[4]===norm?0:1)-(b[4]===norm?0:1))||descending(a[5],b[5])||descending(a[6],b[6])||(dataset==='imdb'?descending(a[7],b[7]):0)||(dataset==='imdb'?lexical(text(a[1]),text(b[1])):Number(a[1])-Number(b[1]));
   });
   const n=dataset==='comics'?(args[1]===undefined?30:integer(args[1])):clamp(args[1],dataset==='imdb'?100:20);
   for(const e of exact||n<0?rows:rows.slice(0,n)){const data=await get(e[2]);if(!Array.isArray(data)||!data[e[3]])throw Error('PREBUILT_ASSET_MISSING');out.push(data[e[3]].identity);}
  }
  return{result:out};
 }
 async function byId(family:string,id:any){
  const dir=manifest!.search[family];if(!dir)return undefined;
  const p=dir.keys.find(p=>p.first<=id&&p.last>=id);if(!p)return null;
  const keys=await get(p.path),entry=keys?.find((e:any[])=>e[0]===id);if(!entry)return null;
  if(entry[3])return entry[3];
  const rows=await get(entry[1]);if(!rows?.[entry[2]])throw Error('PREBUILT_ASSET_MISSING');return rows[entry[2]];
 }
 if(dataset==='mal'&&method==='mangaById'){const row=await byId('manga',integer(args[0]));if(row!==undefined)return{result:row?.normal??{}};}
 if(dataset==='imdb'&&method==='titlePresentation'){const row=await byId('imdb',text(args[0]));if(row!==undefined)return{result:row?{title:row.identity.title,type:row.identity.type}:{}};}
 if(dataset==='imdb'&&method==='titleFacts'){
  const result:Record<string,any>={},ids=Array.isArray(args[0])?args[0].map(text):[];
  for(let i=0;i<ids.length;i+=100){const found:{id:string;row:any}[]=[];for(const id of ids.slice(i,i+100)){const row=await byId('imdb',id);if(row===undefined)return null;if(row)found.push({id,row});}found.sort((a,b)=>(a.row.ordinal??0)-(b.row.ordinal??0));for(const {id,row}of found)result[id]={rating:row.identity.rating,votes:row.identity.votes,isAnime:row.identity.isAnime};}return{result};
 }
 return null;
}
