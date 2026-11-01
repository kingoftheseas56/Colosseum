'use strict';
const {loadBundle}=require('../../../tools/server_lab/oracle/module_loader.cjs');const e=loadBundle(process.argv[2]);if(e.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('wrong bundle');const mode=process.argv[3],url=process.argv[4];function done(o){console.log(JSON.stringify(o));}
if(mode==='fetch')e.require(732).retrieveSrt(url,(error,text)=>done({error:error?.message||'',text:text||'',tracks:text?e.require(791).parseString(text):[]}));
if(mode==='hash')e.require(792)({url},(error,result)=>done({result:result||{},error:error?.message||''}));
if(mode==='tracks'){console.log=()=>{};e.require(810).default(url,{maxBytesLimit:26214400}).then(tracks=>process.stdout.write(JSON.stringify({tracks,error:'',incomplete:false})+'\n')).catch(error=>process.stdout.write(JSON.stringify({tracks:[],error:String(error)})+'\n'));}
