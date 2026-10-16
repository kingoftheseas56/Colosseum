// Verified local cover map, loaded once per publication; never uploads image bytes.
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {BlockList,isIP} from 'node:net';

const sha=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
const fields=['cover','poster','source_cover'];

// URL normalizes decimal/octal/hex IPv4 and IDNA before these local-only checks.
const nonPublic=new BlockList();
for(const [address,prefix] of [['0.0.0.0',8],['10.0.0.0',8],['100.64.0.0',10],['127.0.0.0',8],['169.254.0.0',16],['172.16.0.0',12],['192.0.0.0',24],['192.0.2.0',24],['192.168.0.0',16],['198.18.0.0',15],['198.51.100.0',24],['203.0.113.0',24],['224.0.0.0',4],['240.0.0.0',4]])nonPublic.addSubnet(address,prefix,'ipv4');
const publicV6=new BlockList();publicV6.addSubnet('2000::',3,'ipv6');
for(const [address,prefix] of [['2001::',23],['2001:db8::',32],['2002::',16],['3fff::',20]])nonPublic.addSubnet(address,prefix,'ipv6');
export function validateOwnedOrigin(value){
 let origin;try{origin=new URL(value);}catch{throw Error('OWNED_COVERS_ORIGIN');}
 if(origin.protocol!=='https:'||origin.username||origin.password||origin.search||origin.hash||origin.pathname!=='/')throw Error('OWNED_COVERS_ORIGIN');
 const host=origin.hostname.replace(/^\[|\]$/g,'').replace(/\.$/,'').toLowerCase();
 if(['localhost','local','internal'].includes(host)||['.localhost','.local','.internal'].some(suffix=>host.endsWith(suffix)))throw Error('OWNED_COVERS_ORIGIN');
 const family=isIP(host);
 if(family===4&&nonPublic.check(host,'ipv4')||family===6&&(!publicV6.check(host,'ipv6')||nonPublic.check(host,'ipv6')))throw Error('OWNED_COVERS_ORIGIN');
 return origin;
}

/** No API accepts an unverified hand-written URL map. Python verifies every blob. */
export function loadOwnedCovers({manifest,root,requireOwnedLandingCovers=false}) {
 if(Boolean(manifest)!==Boolean(root))throw Error('OWNED_COVERS_INPUT_PAIR_REQUIRED');
 if(!manifest){if(requireOwnedLandingCovers)throw Error('OWNED_COVERS_INPUT_PAIR_REQUIRED');return null;}
 let verified;
 try {
  verified=JSON.parse(execFileSync(process.env.PYTHON||'python',[
   fileURLToPath(new URL('./cover_assets.py',import.meta.url)),
   'export-map','--manifest',manifest,'--root',root
  ],{encoding:'utf8',maxBuffer:64*1024*1024,stdio:['ignore','pipe','pipe']}));
 } catch(error){throw Error('OWNED_COVERS_VERIFICATION_FAILED: '+String(error.stderr||error.message));}
 validateOwnedOrigin(verified.origin);
 if(verified.version!==1)throw Error('OWNED_COVERS_ORIGIN');
 const urls=new Map(Object.entries(verified.urls).sort(([a],[b])=>a<b?-1:a>b?1:0));
 const owned=new Set(urls.values());
 const identity=sha({origin:verified.origin,urls:[...urls]});
 function strict(value) {
  if(Array.isArray(value)){value.forEach(strict);return;}
  if(!value||typeof value!=='object')return;
  for(const key of fields)if(typeof value[key]==='string'&&value[key]&&!urls.has(value[key])&&!owned.has(value[key]))throw Error('OWNED_COVERS_LANDING_UNMAPPED: '+value[key]);
  if(Array.isArray(value.covers))for(const url of value.covers)if(typeof url==='string'&&url&&!urls.has(url)&&!owned.has(url))throw Error('OWNED_COVERS_LANDING_UNMAPPED: '+url);
  Object.entries(value).filter(([key])=>key!=='coverCandidates').forEach(([,item])=>strict(item));
 }
 function transform(value) {
  if(Array.isArray(value))return value.map(transform);
  if(!value||typeof value!=='object')return value;
  // Comics ownership is enabled independently; Theatre/MAL keep their measured provider path.
  if(requireOwnedLandingCovers&&value.version===1&&value.dataset==='comics'&&typeof value.tab==='string'&&Array.isArray(value.queries))strict(value);
  const result=Object.fromEntries(Object.entries(value).map(([key,item])=>[key,transform(item)]));
  const originals=[];
  for(const key of fields)if(typeof value[key]==='string'&&urls.has(value[key])){result[key]=urls.get(value[key]);originals.push(value[key]);}
  // covers[] originals join card-level coverCandidates in source order; this
  // preserves the existing flat fallback contract without inventing nested policy.
  if(Array.isArray(value.covers))result.covers=value.covers.map(item=>{
   if(typeof item==='string'&&urls.has(item)){originals.push(item);return urls.get(item);}return transform(item);
  });
  if(originals.length){
   const previous=value.coverCandidates??[];
   if(!Array.isArray(previous)||previous.some(url=>typeof url!=='string'))throw Error('OWNED_COVERS_CANDIDATES');
   result.coverCandidates=[...new Set([...previous,...originals])];
  }
  return result;
 }
 return {identity,transform,report:{schemaVersion:1,origin:verified.origin,mappingSha256:identity,mappedUrls:urls.size,objects:verified.inventory.length,
  bytes:verified.inventory.reduce((sum,e)=>sum+e.bytes,0),inventory:verified.inventory,strictLandingCovers:requireOwnedLandingCovers,hostedVerificationPending:true,uploaded:false}};
}
