'use strict';
const fs=require('node:fs'), {pathToFileURL}=require('node:url');
const vm=require('node:vm');
const {loadBundle}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const e=loadBundle(process.argv[2]);
if(e.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
// Execute the locked factory bodies with webpack's standard runtime helpers.
// The generic lab loader omits helpers required by the Matroska dependency tree.
const cache=new Map();
const context=vm.createContext({require,Buffer,process,console,setTimeout,clearTimeout,setInterval,clearInterval,setImmediate,clearImmediate});context.global=context;
function req(id){if(typeof id!=="number")return require(id);if(cache.has(id))return cache.get(id).exports;const m={exports:{}};cache.set(id,m);vm.runInContext('('+e.modules[id].source+')',context)(m,m.exports,req);return m.exports;}
req.o=(o,p)=>Object.prototype.hasOwnProperty.call(o,p);
req.d=(o,n,g)=>{if(!req.o(o,n))Object.defineProperty(o,n,{enumerable:true,get:g});};
req.r=o=>{Object.defineProperty(o,'__esModule',{value:true});};
req.n=m=>{const g=m&&m.__esModule?()=>m.default:()=>m;req.d(g,'a',g);return g;};
(async()=>{
 for (const c of JSON.parse(fs.readFileSync(process.argv[3]))) {
  let result;
  try {
   if(c.mode==='bundled')result=req(934);
   else if(c.mode==='group') {const input=JSON.parse(fs.readFileSync(c.file));let map=req(918)(input);result={segments:Array.from(map.entries()).map(([key,segment])=>({key,segment})),predicted:false};}
   else result={tracks:await req(c.mode==='mp4'?877:868)(c.file.startsWith('http')?c.file:pathToFileURL(c.file).href,c.indexes||[]),error:'',available:true};
  } catch(error){result={tracks:{},error:error.message,available:false};}
  process.stdout.write(JSON.stringify({name:c.name,result})+'\n');
 }
})();
