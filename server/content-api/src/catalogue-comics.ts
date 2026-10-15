import {query, text, integer, number, clamp, array, type Row, type CatalogueDb} from './catalogue-semantic.ts';
import {indexedRows} from './catalogue-lookup.ts';

const seriesKeys: Row = {gcdId:'gcd_id',title:'title',year:'year',yearEnded:'year_ended',publisher:'publisher',cover:'cover',downloads:'__downloads'};
const numeric = new Set(['gcdId','year','yearEnded','downloads','rank','issueCount','postId','yearStart','pages','parserVersion','canonicalIssueCount','coveredIssueCount','sourceCount','officialSourceCount','communitySourceCount']);
function mapped(r: Row, keys: Row): Row { const out:Row={}; for(const [key,column] of Object.entries(keys)) out[key]=numeric.has(key)?integer(r[column as string]):text(r[column as string]); return out; }
export function legacyRow(r:Row) { return mapped(r,seriesKeys); }
function curatedRow(r:Row, synopsis=false):Row {
 const out=mapped(r,{locgId:'locg_id',rank:'rank',title:'title',year:'year',slug:'slug',publisher:'publisher',cover:'cover'});
 out.genres=array(r.__genres).map(text).join(','); if(synopsis)out.synopsis=text(r.synopsis); return out;
}
const bool=(v:any)=>integer(v)!==0;
const mirrorRow=(r:Row)=>({url:text(r.url),host:text(r.host),label:text(r.label)});
const json=(v:any):Row=>{if(v&&typeof v==='object')return v;try{return JSON.parse(text(v))||{};}catch{return{};}};
async function exists(db:CatalogueDb,table:string) { const [r]=await query(db,"SELECT 1 AS present FROM sqlite_master WHERE name=? LIMIT 1",['lib_comics_'+table]);return !!r; }
async function ready(db:CatalogueDb) {const[r]=await query(db,'SELECT 1 AS present FROM lib_comics_curated_series LIMIT 1');return !!r;}
function coverageRow(r:Row):Row {
 const out=mapped(r,{canonicalIssueCount:'canonical_issue_count',coveredIssueCount:'covered_issue_count',availabilityState:'availability_state',sourceCount:'source_count',officialSourceCount:'official_source_count',communitySourceCount:'community_source_count',latestSourceDate:'latest_source_date',computedAt:'computed_at',parserVersion:'parser_version'});
 out.gcdSeriesId=r.gcd_series_id??null;out.coverageRatio=number(r.coverage_ratio);return out;
}
async function coverage(db:CatalogueDb,id:string) {if(!id||!await exists(db,'curated_series_coverage'))return{};const[r]=(await indexedRows(db,'comics','curated_series_coverage','key',id))??await query(db,'SELECT * FROM lib_comics_curated_series_coverage WHERE locg_id=?',[id]);return r?coverageRow(r):{};}
const escapeLike=(s:string)=>s.replace(/[\\%_]/g,'\\$&');
const labels:Row={available:'Available to read',complete:'Complete',near_complete:'Near Complete',partial:'Partial',bibliography_only:'Bibliography only'};

async function detail(db:CatalogueDb,id:string):Promise<Row> {
 if(!id||!await ready(db))return{};
 const[r]=(await indexedRows(db,'comics','curated_series','key',id))??await query(db,'SELECT * FROM den_comics_curated_series WHERE locg_id=?',[id]);if(!r)return{};
 const out=curatedRow(r,true);
 const editions=await query(db,'SELECT * FROM lib_comics_curated_edition WHERE locg_id=? ORDER BY id ASC',[id]);
 const sources=await exists(db,'curated_source')?await query(db,'SELECT * FROM lib_comics_curated_source WHERE curated_edition_id IN (SELECT value FROM json_each(?)) ORDER BY fan_made ASC,source_date DESC,post_id DESC',[JSON.stringify(editions.map(e=>integer(e.id)))]):[];
 const mirrors:Row={};
 if(sources.length&&await exists(db,'download_mirror'))for(const m of await query(db,'SELECT * FROM lib_comics_download_mirror WHERE post_id IN (SELECT value FROM json_each(?))',[JSON.stringify([...new Set(sources.map(s=>integer(s.post_id)))])]))(mirrors[integer(m.post_id)]??=[]).push(mirrorRow(m));
 out.editions=editions.map(e=>{
  const value=mapped(e,{title:'title',displayTitle:'display_title',format:'format',collects:'collects',isbn:'isbn',pages:'pages',published:'published',chid:'chid',cover:'cover',getcomicsPost:'getcomics_post',creators:'creators',description:'description'});value.available=bool(e.available);
  value.sources=sources.filter(s=>integer(s.curated_edition_id)===integer(e.id)).map(s=>{
   const source=mapped(s,{postId:'post_id',postUrl:'post_url',title:'source_title',date:'source_date',cover:'source_cover',kind:'source_kind',format:'source_format',attachmentMethod:'attachment_method',confidenceClass:'confidence_class',parsedSeriesText:'parsed_series_text',parserVersion:'parser_version'});
   source.fanMade=bool(s.fan_made);source.available=bool(s.available);source.mirrors=mirrors[integer(s.post_id)]||[];return source;
  });return value;
 });
 out.coverage=await coverage(db,id);return out;
}

async function filters(db:CatalogueDb,axis:string):Promise<Row[]> {
 if(!await ready(db))return[];
 let rows:Row[];
 if(axis==='genre')rows=await query(db,'SELECT genre AS value,COUNT(*) AS total FROM lib_comics_curated_genre GROUP BY genre ORDER BY total DESC,genre ASC');
 else if(axis==='publisher')rows=await query(db,"SELECT publisher AS value,COUNT(*) AS total FROM lib_comics_curated_series WHERE publisher!='' GROUP BY publisher ORDER BY total DESC,publisher ASC");
 else if(axis==='format')rows=await query(db,"SELECT MIN(TRIM(format)) AS value,COUNT(DISTINCT locg_id) AS total FROM lib_comics_curated_edition WHERE TRIM(format)!='' GROUP BY LOWER(TRIM(format)) ORDER BY total DESC,LOWER(MIN(TRIM(format))) ASC");
 else if(axis==='availability'){
  if(!await exists(db,'curated_series_coverage'))return[];
  const out:Row[]=[];const[count]=await query(db,'SELECT COUNT(*) AS total FROM lib_comics_curated_series_coverage WHERE official_source_count>0');
  if(integer(count?.total)>0)out.push({key:'available',label:labels.available,count:integer(count.total)});
  const states=await query(db,"SELECT availability_state AS value,COUNT(*) AS total FROM lib_comics_curated_series_coverage WHERE availability_state IN ('complete','near_complete','partial','bibliography_only') GROUP BY availability_state ORDER BY total DESC,availability_state ASC");
  return out.concat(states.map(r=>({key:text(r.value),label:labels[text(r.value)]||text(r.value),count:integer(r.total)})));
 }else return[];
 return rows.map(r=>({key:text(r.value),label:text(r.value),count:integer(r.total)}));
}

async function discover(db:CatalogueDb,args:any[],single:boolean,publication=false):Promise<Row> {
 let[catalog,active,_explicit,offset,limit]=args;
 if(single){catalog=args[0];active=args[1]||args[2]?[{axis:args[1],key:args[2]}]:[];offset=args[4];limit=args[5];}
 catalog=text(catalog);const off=Math.max(0,integer(offset)),lim=clamp(limit,24);
 const pack=(items:Row[],total:number)=>({items,nextOffset:off+items.length,exhausted:off+items.length>=total,freshness:'bundled'});
 if(!['popular','new-releases','most-stocked','all','recently-available','complete-runs','near-complete','community-collections'].includes(catalog)||!await ready(db))return pack([],0);
 const hasCoverage=await exists(db,'curated_series_coverage');
 if(['recently-available','complete-runs','near-complete','community-collections'].includes(catalog)&&!hasCoverage)return pack([],0);
 const conditions:string[]=[],binds:any[]=[];let formatKey='';
 for(const f of Array.isArray(active)?active:[]){const axis=text(f?.axis),key=text(f?.key);
  if(!['','genre','publisher','format','availability'].includes(axis)||(axis==='availability'&&!hasCoverage))return pack([],0);
  if(!axis||!key)continue;
  if(axis==='genre'||axis==='format'){conditions.push('EXISTS(SELECT 1 FROM json_each(s.'+(axis==='genre'?'__genres':'__formats')+') f WHERE LOWER('+ (axis==='format'?'TRIM(f.value)':'f.value') +')=LOWER(?))');binds.push(key);if(axis==='format')formatKey=key;}
  else if(axis==='publisher'){conditions.push('LOWER(s.publisher)=LOWER(?)');binds.push(key);}
  else if(key==='available')conditions.push("COALESCE(json_extract(s.__coverage,'$.official_source_count'),0)>0");
  else {conditions.push("LOWER(json_extract(s.__coverage,'$.availability_state'))=LOWER(?)");binds.push(key);}
 }
 const raw=await query(db,'SELECT s.* FROM den_comics_curated_series s'+(conditions.length?' WHERE '+conditions.join(' AND '):''),binds);
 let rows:Row[]=raw.map(r=>{const c=json(r.__coverage);return {...r,c,edCount:integer(r.__editionCount),availCount:integer(r.__availableCount),maxYear:integer(r.__publishedMaxYear),score:0,components:{} as Row};});
 if(catalog==='recently-available')rows=rows.filter(r=>integer(r.c.official_source_count)>0&&text(r.c.latest_source_date)!=='');
 else if(catalog==='complete-runs')rows=rows.filter(r=>r.c.availability_state==='complete');
 else if(catalog==='near-complete')rows=rows.filter(r=>r.c.availability_state==='near_complete');
 else if(catalog==='community-collections')rows=rows.filter(r=>integer(r.c.community_source_count)>0);
 const ranks=rows.filter(r=>r.rank!=null).map(r=>integer(r.rank));const minRank=Math.min(...ranks),maxRank=Math.max(...ranks);
 for(const r of rows){const ranked=r.rank!=null;const popularity=!ranked||!ranks.length?0:maxRank===minRank?1:1-(integer(r.rank)-minRank)/(maxRank-minRank);
  const recency=r.maxYear>0?Math.min(1,Math.max(0,(r.maxYear-1980)/Math.max(1,new Date().getFullYear()-1980))):0.5;
  const metadata=[r.cover,r.synopsis,r.publisher,array(r.__genres).join(',')].filter(v=>text(v)!=='').length/4*0.05;
  const fraction=r.edCount>0?r.availCount/r.edCount:0;
  r.components={popularity:(ranked?0.65:0)*popularity,availability:(ranked?0.20:0.20+0.65*(0.20/0.30))*fraction,recency:(ranked?0.10:0.10+0.65*(0.10/0.30))*recency,metadata};
  r.score=r.components.popularity+r.components.availability+r.components.recency+metadata;
 }
 const lexical=(a:any,b:any)=>text(a)<text(b)?-1:text(a)>text(b)?1:0;
 const canon=(a:Row,b:Row)=>lexical(a.norm_title,b.norm_title)||integer(a.year)-integer(b.year)||lexical(a.locg_id,b.locg_id);
 rows.sort((a,b)=>{
  if(catalog==='popular')return b.score-a.score||canon(a,b);
  if(catalog==='most-stocked')return b.edCount-a.edCount||b.score-a.score||(b.edCount?b.availCount/b.edCount:0)-(a.edCount?a.availCount/a.edCount:0)||canon(a,b);
  if(catalog==='new-releases')return b.maxYear-a.maxYear||b.score-a.score||canon(a,b);
  if(catalog==='recently-available')return lexical(b.c.latest_source_date,a.c.latest_source_date)||b.score-a.score||canon(a,b);
  if(catalog==='complete-runs'||catalog==='near-complete')return number(b.c.coverage_ratio)-number(a.c.coverage_ratio)||b.score-a.score||canon(a,b);
  if(catalog==='community-collections')return integer(b.c.community_source_count)-integer(a.c.community_source_count)||lexical(b.c.latest_source_date,a.c.latest_source_date)||canon(a,b);
  return canon(a,b);
 });
 return pack((publication?rows:rows.slice(off,off+lim)).map(r=>({locgId:text(r.locg_id),title:text(r.title),year:integer(r.year),publisher:text(r.publisher),cover:text(r.cover),genres:array(r.__genres).map(text).join(','),format:formatKey,availability:hasCoverage?integer(r.c.official_source_count)>0:r.availCount>0,availabilityState:text(r.c.availability_state),coverageRatio:number(r.c.coverage_ratio),latestSourceDate:text(r.c.latest_source_date),sourceDepth:integer(r.c.source_count),officialSourceCount:integer(r.c.official_source_count),communitySourceCount:integer(r.c.community_source_count),houseScore:r.score,houseComponents:r.components,explicit:false})),rows.length);
}

// Publish-time only; no RPC allowlist entry. Share the exact scoring/tie owner.
export async function comicsPreparedList(db:CatalogueDb,args:any[]):Promise<Row[]>{return (await discover(db,args,false,true)).items;}

export async function comicsMethod(db:CatalogueDb,method:string,args:any[]):Promise<any>{
 if(method==='curatedReady')return ready(db);
 if(method==='series'){const[r]=(await indexedRows(db,'comics','series','key',integer(args[0])))??await query(db,'SELECT * FROM den_comics_series WHERE gcd_id=?',[integer(args[0])]);return r?{...legacyRow(r),issueCount:integer(r.issue_count),synopsis:text(r.synopsis),kinds:text(r.__kinds),latestPost:text(r.__latestPost)}:{};}
 if(method==='search'||method==='exactMatches'){
  const t=text(args[0]).trim();if(!t)return[];
  if(method==='exactMatches')return(await query(db,'SELECT * FROM den_comics_series WHERE LOWER(title)=LOWER(?) ORDER BY __downloads DESC',[t])).map(legacyRow);
  const words=t.split(/[^\p{L}\p{N}]+/u).filter(Boolean);if(!words.length)return[];
  return(await query(db,'SELECT * FROM den_comics_series WHERE '+words.map(()=>"title LIKE ? ESCAPE '\\'").join(' AND ')+" ORDER BY CASE WHEN LOWER(title)=LOWER(?) THEN 0 WHEN title LIKE ? ESCAPE '\\' THEN 1 ELSE 2 END,__downloads DESC,year DESC LIMIT ?",[...words.map(w=>'%'+escapeLike(w)+'%'),t,escapeLike(t)+'%',args[1]===undefined?30:integer(args[1])])).map(legacyRow);
 }
 if(method==='downloadsFor')return(await query(db,'SELECT * FROM den_comics_download WHERE series_id=? ORDER BY date DESC,post_id DESC',[integer(args[0])])).map(r=>({...mapped(r,{postId:'post_id',title:'title',link:'link',date:'date',kind:'kind',method:'method',yearStart:'year_start'}),fanMade:bool(r.fan_made),mirrors:array(r.__mirrors).map(mirrorRow)}));
 if(method==='shelf'){
  const kind=text(args[0]),arg=text(args[1]);let where='1=1';const binds:any[]=[];
  if(kind==='publisher'){where='publisher=?';binds.push(arg);}else if(kind==='decade'){if(!/^[+-]?\d+$/.test(arg.trim()))return[];where='year BETWEEN ? AND ?';binds.push(integer(arg),integer(arg)+9);}else if(kind==='deep')where='__downloads>=10';else if(kind==='fanmade')where='__fanMade=1';else if(kind!=='stocked')return[];
  return(await query(db,'SELECT * FROM den_comics_series WHERE '+where+' ORDER BY __downloads DESC,year DESC LIMIT ?',[...binds,integer(args[2])])).map(legacyRow);
 }
 if(method==='curatedRanked')return(await query(db,'SELECT * FROM den_comics_curated_series ORDER BY rank ASC')).map(r=>curatedRow(r));
 if(method==='curatedSeries')return detail(db,text(args[0]));
 if(method==='curatedCoverage')return coverage(db,text(args[0]));
 if(method==='curatedByNorm'){const norm=text(args[0]).trim();if(!norm)return{};const[r]=await query(db,'SELECT locg_id,title,cover,publisher FROM lib_comics_curated_series WHERE norm_title=? ORDER BY rank ASC LIMIT 1',[norm]);return r?mapped(r,{locgId:'locg_id',title:'title',cover:'cover',publisher:'publisher'}):{};}
 if(method==='curatedGenreShelves'){
  const groups=await query(db,'SELECT genre,COUNT(*) AS total FROM lib_comics_curated_genre GROUP BY genre ORDER BY total DESC,genre ASC');
  const ranked=await query(db,"SELECT cover,__genres FROM den_comics_curated_series WHERE cover!='' ORDER BY rank ASC");
  return groups.map(g=>({name:text(g.genre),count:integer(g.total),covers:ranked.flatMap(r=>array(r.__genres).filter(v=>v===g.genre).map(()=>text(r.cover))).slice(0,integer(args[0])<0?undefined:integer(args[0]))}));
 }
 if(method==='curatedHasDownloadable'){
  const id=text(args[0]);if(!id||!await ready(db))return false;
  if(await exists(db,'curated_source')){const editions=await query(db,'SELECT id FROM lib_comics_curated_edition WHERE locg_id=?',[id]);const[r]=await query(db,"SELECT 1 AS present FROM lib_comics_curated_source WHERE curated_edition_id IN (SELECT value FROM json_each(?)) AND available=1 AND fan_made=0 AND confidence_class IN ('exact','strong') LIMIT 1",[JSON.stringify(editions.map(e=>integer(e.id)))]);return !!r;}
  const[r]=await query(db,"SELECT 1 AS present FROM lib_comics_curated_edition WHERE locg_id=? AND available=1 AND getcomics_post!='' LIMIT 1",[id]);return !!r;
 }
 if(method==='discoverFilters')return filters(db,text(args[0]));
 if(method==='discoverPage'||method==='discoverPageFiltered')return discover(db,args,method==='discoverPage');
 throw new Error('UNKNOWN_CATALOGUE_METHOD');
}
