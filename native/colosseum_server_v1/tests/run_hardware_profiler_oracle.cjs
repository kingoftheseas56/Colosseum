'use strict';
const os=require('node:os'),path=require('node:path');
const fs=require('node:fs'),vm=require('node:vm'),crypto=require('node:crypto'),{spawnSync}=require('node:child_process');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle=extractModules(process.argv[2]);
if(oracle.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
function data(id){const m={exports:{}};vm.runInNewContext('('+oracle.modules[id].source+')')(m,m.exports);return m.exports;}
const profiles=data(193),samples=Object.entries(data(934)).map(([key,v])=>({path:'/samples/'+key+'.'+v.container,mime:v.mime,bytes:Buffer.from(v.data,'base64').length,sha256:crypto.createHash('sha256').update(Buffer.from(v.data,'base64')).digest('hex')}));
(async()=>{
 const rows=[];
 for(const input of [
  {name:'linux-none',platform:'linux'}, {name:'linux-one',platform:'linux',supported:['nvenc-linux']},
  {name:'linux-all',platform:'linux',supported:['qsv-linux','nvenc-linux','vaapi-renderD128']},
  {name:'mac-audio-failure',platform:'darwin',supported:['videotoolbox'],audioFail:true},
  {name:'mac-success',platform:'darwin',supported:['videotoolbox']},
  {name:'win-success',platform:'win32',supported:['qsv-win','amf']},
  {name:'rpi',platform:'rpi',supported:['v4l2m2m']},
  {name:'android-disabled',platform:'android'}, {name:'unknown',platform:'unknown'},
  {name:'ios-override',platform:'linux',ios:true}, {name:'fetch-failure',platform:'linux',networkFail:true},
  {name:'disabled-startup',platform:'linux',callback:false,initial:{transcodeHardwareAccel:false,allTranscodeProfiles:[]}},
  {name:'string-profile-startup',platform:'linux',callback:false,initial:{transcodeHardwareAccel:true,allTranscodeProfiles:'saved'}},
  {name:'known-startup',platform:'linux',callback:false,initial:{transcodeHardwareAccel:true,allTranscodeProfiles:['nvenc-linux']}},
  {name:'debug-forces-retest',platform:'linux',callback:false,debug:true,initial:{transcodeHardwareAccel:false,allTranscodeProfiles:['old']}},
  {name:'startup-initial-detect',platform:'linux',callback:false,supported:['qsv-linux']},
  {name:'port-default',platform:'linux',port:0},
 ]) {
  const row={callback:true,port:12471,supported:[],initial:{transcodeHardwareAccel:true,allTranscodeProfiles:[]},...input};
  let called=false,result;const requests=[],events=[];
  const current=()=>({transcodeHardwareAccel:settings.transcodeHardwareAccel,allTranscodeProfiles:settings.allTranscodeProfiles});
  const settings={...structuredClone(row.initial),extend(v){Object.assign(this,v)},save(cb){events.push({save:current()});cb();}};
  const fetch=url=>{requests.push(url);if(row.networkFail)return Promise.reject(Error('controlled network error'));const destroy=url.endsWith('/destroy');let ok=row.supported.some(p=>new URL(url).pathname.includes('-'+p+'-'));if(row.audioFail&&url.includes('-audio-'))ok=false;return Promise.resolve({ok:destroy||ok});};
  const m={exports:{}};
  vm.runInNewContext('('+oracle.modules[805].source+')',{process:{platform:row.platform,env:{...(row.debug?{HLS_DEBUG:'1'}:{}),...(row.ios?{IS_IOS:'1'}:{})}},console:{log(){}}})(m,m.exports,id=>({193:profiles,106:settings,414:{setOptionValues(){}},34:fetch})[id]);
  m.exports(row.port,row.callback?v=>{called=true;result=v;events.push({callback:v});}:undefined);
  await new Promise(resolve=>setImmediate(resolve));
  row.expected={requests,events,settings:current(),called,...(called?{result}:{})};rows.push(row);
 }
 fs.writeFileSync(process.argv[3],JSON.stringify({samples,profiles:rows},null,2)+'\n');
 console.log('Pinned M805/M934 fixtures: '+rows.length+' profiler cases + '+samples.length+' sample payloads');
 if(process.argv[4]){
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'server1-pinned-samples-'));
  try {
   for(const [key,v]of Object.entries(data(934))) {
    const bytes=Buffer.from(v.data,'base64');const name=key+'.'+v.container;
    const expected=samples.find(s=>s.path==='/samples/'+name);
    if(crypto.createHash('sha256').update(bytes).digest('hex')!==expected.sha256)throw Error('sample hash mismatch');
    fs.writeFileSync(path.join(directory,name),bytes);
   }
   const args=process.argv[5]==='--real'?[process.argv[6],process.argv[7],directory]:[process.argv[3],directory];
   const r=spawnSync(process.argv[4],args,{stdio:'inherit'});process.exitCode=r.status??1;
  } finally {fs.rmSync(directory,{recursive:true,force:true});}
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
