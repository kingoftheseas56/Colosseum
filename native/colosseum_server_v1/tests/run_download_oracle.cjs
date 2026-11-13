// Executes only the locked M1284/M1285 functions, with Node builtins and a router adapter.
const fs=require('fs'),path=require('path'),os=require('os'),vm=require('vm'),{Writable}=require('stream');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const {spawnSync}=require('child_process');
const extracted=extractModules(process.argv[2]);
if(extracted.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('Oracle identity mismatch');
const root=fs.mkdtempSync(path.join(os.tmpdir(),'n07-oracle-'));fs.mkdirSync(path.join(root,'downloads'));fs.writeFileSync(path.join(root,'downloads','payload.bin'),'0123456789');
const record={id:'done',url:'http://127.0.0.1/file',status:'finished',fileName:'payload.bin',total:10,downloaded:10,progress:100,name:'payload.bin',speed:10};
fs.writeFileSync(path.join(root,'downManager.json'),JSON.stringify({done:record,paused:{...record,id:'paused',status:'paused'},missing:{...record,id:'missing',fileName:'absent'}}));
class Router {constructor(){this.handlers={}}get(p,cb){this.handlers[p]=cb;return this}}
let helper;
const dep=id=>({1:fs,5:path,3:require('stream'),68:{lookup:()=> 'application/octet-stream'},100:Router,194:root,1285:helper,6:require('url'),11:require('http'),20:require('https'),4:require('events')}[id]);
function load(id){const f=vm.runInThisContext('('+extracted.modules[id].source+')');const m={exports:{}};f(m,m.exports,dep);return m.exports}
helper=load(1285);const router=load(1284);
async function request(action,id,range,url){const req={query:id?{id}:{},headers:range?{range}:{}};if(url)req.query.url=url;const headers={};let code=200,body=[];const res=new Writable({write(chunk,enc,cb){body.push(Buffer.from(chunk));cb()}});Object.defineProperty(res,'statusCode',{get:()=>code,set:v=>code=v});res.setHeader=(k,v)=>headers[k.toLowerCase()]=String(v);res.writeHead=(c,h)=>{code=c;for(const [k,v]of Object.entries(h))res.setHeader(k,v)};let thrown;try{router.handlers['/'+action](req,res)}catch(e){thrown=e.code||e.message;res.end()}await new Promise(r=>res.on('finish',r));return {action,id:id||'',range:range||'',status:code,headers,body:Buffer.concat(body).toString(),...(thrown?{thrown}:{})};}
(async()=>{let out=[];for(const [action,id,range] of [['getAll'],['get'],['get','absent'],['add'],['pause','absent'],['resume','absent'],['remove','absent'],['stream','absent'],['pause','done'],['resume','done'],['stream','paused'],['stream','done'],['stream','done','bytes=2-5'],['stream','done','bytes=-3'],['stream','done','bytes=8-'],['stream','done','bytes=20-'],['stream','done','bytes=0-20'],['remove','done'],['get','done']])out.push(await request(action,id,range));const candidate=spawnSync(process.argv[3],['--differential'],{encoding:'utf8',timeout:30000});
if(candidate.status!==0)throw Error('Candidate failed: '+candidate.stderr);
const actual=JSON.parse(candidate.stdout);
function normalize(value){if(Array.isArray(value))return value.map(normalize);if(value&&typeof value==='object'){const result={};for(const key of Object.keys(value).sort()){let v=value[key];if(key==='body'&&value.headers?.['content-type']==='application/json')v=JSON.parse(v);result[key]=normalize(v);}return result;}return value;}
require('assert').deepStrictEqual(normalize(actual),normalize(out));
console.log('PASS N07 exact-oracle route differential: '+out.length+' cases');
const server=require('http').createServer((req,res)=>{const failed=req.url==='/fail';const data=failed?Buffer.from('oops'):Buffer.alloc(128*1024,'x');res.writeHead(failed?404:200,{'Content-Length':data.length,'Accept-Ranges':'bytes','Content-Disposition':'attachment; filename="payload.bin"','Connection':'close'});res.end(data);});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try {
 const live=[];
 for(const pathname of ['/file','/file','/fail']) {
  const added=JSON.parse((await request('add',null,null,'http://127.0.0.1:'+server.address().port+pathname)).body);
  const deadline=Date.now()+5000;let record;
  do {await new Promise(resolve=>setTimeout(resolve,5));record=JSON.parse((await request('get',added.id)).body);} while(!['finished','error'].includes(record.status)&&Date.now()<deadline);
  if(!['finished','error'].includes(record.status))throw Error('Source live transfer timed out');
  delete record.id;delete record.url;live.push(record);
 }
 const run=spawnSync(process.argv[3],['--live-differential'],{encoding:'utf8',timeout:30000});
 if(run.status!==0)throw Error('Candidate live transfer failed: '+run.stderr);
 require('assert').deepStrictEqual(normalize(JSON.parse(run.stdout)),normalize(live));
 console.log('PASS N07 exact-oracle live transfer differential: '+live.length+' cases');
} finally {await new Promise(resolve=>server.close(resolve));}

fs.rmSync(root,{recursive:true,force:true});})().catch(e=>{console.error(e);process.exit(1)});
