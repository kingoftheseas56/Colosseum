// Recover legacy precompute arguments from their canonical hashes and existing
// recipe owners. A filename alone is never treated as decoded arguments.
import {readFileSync,readdirSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v;
const sha=b=>createHash('sha256').update(b).digest('hex');
export function legacyRecipes(previous,qmlRoot){
 const candidates=new Map();
 const add=(dataset,method,args)=>{args=canonical(args);candidates.set(`/internal/catalogue/${dataset}/${method}/${sha(JSON.stringify(args))}.json`,{dataset,method,args});};
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
 const clean=s=>s.replace(/^\s*\.(?:pragma|import).*$/gm,'');
 const rules={},api={};vm.createContext(rules);vm.createContext(api);
 vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreCatalogRules.js'),'utf8')),rules);
 vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreApi.js'),'utf8')),api);
 const recipes=[...rules.MOVIE_ROWS(),...rules.SHOW_ROWS(),...rules.ANIME_ROWS(),...rules.MOVIE_DAILY_POOL];
 const limits=[-1,...Array.from({length:257},(_,i)=>i)];
 for(const row of recipes){
  const imdb=rules.indexQueriesFor(row.recipe);
  for(const q of imdb){add('imdb','titleCatalog',[q]);add('imdb','titleCatalog',[q,0]);for(const limit of limits)add('imdb','titleCatalog',[q,0,limit]);}
  const queries=row.recipe.kind==='animeTagAny'?(row.recipe.tags||[]).map(tag=>({order:'members',tag})):[api.animeQueryFor(row.recipe)].filter(Boolean);
  for(const q of queries){add('mal','animeCatalog',[q]);add('mal','animeCatalog',[q,0]);for(const limit of limits)add('mal','animeCatalog',[q,0,limit]);}
 }
 const rows=[],unknown=[];
 for(const dataset of readdirSync(join(previous,'internal/catalogue')))
  for(const method of readdirSync(join(previous,'internal/catalogue',dataset)))
   for(const name of readdirSync(join(previous,'internal/catalogue',dataset,method))){
    const path=`/internal/catalogue/${dataset}/${method}/${name}`,recipe=candidates.get(path);
    if(!recipe){unknown.push(path);continue;}
    const raw=readFileSync(join(previous,path)),body=JSON.parse(raw);
    if(body.apiVersion!==1||body.method!==method)throw Error('INVALID_LEGACY_ENVELOPE: '+path);
    rows.push({...recipe,path,sha256:sha(raw),bytes:raw.length,revision:body.revision});
   }
 return{recipes:rows.sort((a,b)=>a.path.localeCompare(b.path)),unknown};
}
if(process.argv[1]===fileURLToPath(import.meta.url)){
 const [previous,qmlRoot,output]=process.argv.slice(2);if(!output)throw Error('Usage: legacy_recipes.mjs previous-publication qml-root evidence.json');
 const result=legacyRecipes(previous,qmlRoot);writeFileSync(output,JSON.stringify(result,null,2));
 console.log(JSON.stringify({decoded:result.recipes.length,unknown:result.unknown}));
 if(result.unknown.length)process.exitCode=1;
}
