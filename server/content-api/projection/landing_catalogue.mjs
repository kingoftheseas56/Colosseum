// Additive fixed selections. No service routes, source rewriting or deployment.
import {readFileSync} from 'node:fs';
import {join} from 'node:path';
import vm from 'node:vm';
import {selectionKey} from '../src/catalogue-prebuilt.ts';

export function theatreLandingOwners(qmlRoot) {
 const clean=s=>s.replace(/^\s*\.(?:pragma|import).*$/gm,'');
 const rules={};vm.createContext(rules);
 vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreCatalogRules.js'),'utf8')),rules);
 const api={Rules:rules};vm.createContext(api);
 vm.runInContext(clean(readFileSync(join(qmlRoot,'TheatreApi.js'),'utf8')),api);
 return {rules,api};
}

// Keep original request identities (including pagination), not list selection aliases.
export function landingRequests(dataset,tab,{rules,api,date}) {
 const requests=new Map();
 const add=(method,args)=>requests.set(selectionKey(dataset,method,args),{method,args});
 if(dataset==='comics'&&tab==='comics') {
  add('curatedReady',[]);add('curatedRanked',[]);add('curatedGenreShelves',[8]);
  for(const [kind,arg] of [['stocked',''],['publisher','Marvel'],['publisher','DC'],['publisher','Image'],['decade','2020'],['decade','2010'],['deep',''],['fanmade','']])add('shelf',[kind,arg,24]);
  for(const [catalog,axis,key] of [['recently-available','',''],['complete-runs','',''],['near-complete','',''],['popular','format','omnibus'],['popular','format','deluxe'],['popular','format','graphic novel'],['community-collections','','']])add('discoverPageFiltered',[catalog,axis&&key?[{axis,key}]:[],false,0,24]);
 } else if(dataset==='imdb'&&(tab==='movies'||tab==='shows')) {
  let defs=rules.defaultRows(tab);
  if(tab==='movies')defs=defs.concat(rules.dailyRows(Date.parse(date+'T00:00:00Z'),6));
  for(const def of defs)for(const q of rules.indexQueriesFor(def.recipe))add('titleCatalog',[q,0,api.PREVIEW_ROW_CAP]);
 } else if(dataset==='mal'&&tab==='anime') {
  for(const def of rules.defaultRows(tab))for(const q of api.animeQueriesFor(def.recipe))add('animeCatalog',[q,0,api.PREVIEW_ROW_CAP]);
 } else throw Error('Unsupported landing '+dataset+'/'+tab);
 return [...requests.values()];
}

export async function publishLandingCatalogues({date,qmlRoot,queryFn,fixed,owners=theatreLandingOwners(qmlRoot)}) {
 if(!/^\d{4}-\d{2}-\d{2}$/.test(date)||new Date(date+'T00:00:00Z').toISOString().slice(0,10)!==date)throw Error('Invalid landing date');
 const nextDay=new Date(Date.parse(date+'T00:00:00Z')+86400000).toISOString().slice(0,10);
 const published=[];
 for(const [dataset,tab] of [['comics','comics'],['imdb','movies'],['imdb','shows'],['mal','anime']]) {
  const queries=[];
  for(const {method,args} of landingRequests(dataset,tab,{...owners,date})) {
   const result=await queryFn(dataset,method,args);
   if(result===undefined)throw Error('Missing landing result '+dataset+'/'+method);
   queries.push({method,args,result});
  }
  const bundle={version:1,dataset,tab,validFrom:date,validUntil:dataset==='comics'?String(Number(date.slice(0,4))+1)+'-01-01':nextDay,queries};
  if(Buffer.byteLength(JSON.stringify(bundle))>4*1024*1024)throw Error('Landing exceeds 4MiB: '+dataset+'/'+tab);
  fixed(dataset,'landing',[tab],bundle);published.push({dataset,tab,queries:queries.length});
 }
 return published;
}
