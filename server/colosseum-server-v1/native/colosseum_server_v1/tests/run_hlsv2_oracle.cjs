'use strict';
const fs=require('fs'),vm=require('vm'),{extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const e=extractModules(process.argv[2]);
const module865={exports:{}};
class Mutex {isLocked(){return false;} acquire(){return Promise.resolve(()=>{});} cancel(){}}
vm.runInNewContext('('+e.modules[865].source+')',{process:{env:{}},console})(module865,module865.exports,id=>({24:require('querystring'),437:{Mutex},106:{transcodeTrackConcurrency:2},440:{},432:()=>{},466:{},916:function(){}})[id]);
(async()=>{let rows=[];for(const options of [{mediaURL:'a',profile:['x','y'],maxWidth:['1200','900'],maxAudioChannels:[1,2],videoCodecs:[[1,2],true]},{mediaURL:'a',maxAudioChannels:1e20},{mediaURL:'a',maxAudioChannels:1e21},{mediaURL:'a',maxAudioChannels:0.000001},{mediaURL:'a',maxAudioChannels:1.0000000000000002},{},{mediaURL:''},{mediaURL:2},{mediaURL:'file:a',maxAudioChannels:0},{mediaURL:'file:a',maxAudioChannels:-2},{mediaURL:'file:a'},{mediaURL:'http://example/a?x=1&b=2',maxAudioChannels:2,forceTranscoding:true,videoCodecs:['h264','hevc'],audioCodecs:['aac'],profile:'x y',maxWidth:'1200'},{mediaURL:'file:a',maxAudioChannels:null,videoCodecs:[],audioCodecs:['a b',"a'b!"]}]){let row={options};try {let c=new module865.exports(options);row.query=(await c.status()).query;}catch(e){row.error=e.message;}rows.push(row);}fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2));console.log(JSON.stringify({bundle:e.sha256,module:865,hash:e.modules[865].sha256,cases:rows.length}));})();
// Additional exact factory differential: only process/source-data boundaries replaced.
async function converterCases(){
 const {EventEmitter}=require('events');
 let input,probeCalls=0;
 const cache={};
 const probeProcess=class{create(){const events=new EventEmitter();return {events,stream:{pipe(){const parser=new EventEmitter();setImmediate(()=>{probeCalls++;parser.emit('data',input);events.emit('close',0,null);});return parser;}},destroy(){events.emit('close',null,'SIGKILL');}}}};
 const overrides={24:require('querystring'),106:{transcodeTrackConcurrency:1},440:{},433:{ffprobe:probeProcess},862:{parse(){}},916:function(){throw Error('unexpected track converter');}};
 function load(id){if(id in overrides)return overrides[id];if(cache[id])return cache[id].exports;const m={exports:{}};cache[id]=m;vm.runInNewContext('('+e.modules[id].source+')',{process:{env:{}},console,Buffer,setTimeout,clearTimeout,Promise})(m,m.exports,load);return m.exports;}
 const Converter=load(865),rows=[];
 for(input of [{format:{format_name:'fixture',duration:'3'},streams:[{index:0,codec_type:'video',codec_name:'h264',width:100,height:50,r_frame_rate:'25/1',time_base:'1/1000'},{index:1,codec_type:'video',codec_name:'h264'},{index:2,codec_type:'audio',codec_name:'aac',channels:2,sample_rate:'48000'},{index:3,codec_type:'subtitle',codec_name:'subrip'},{index:4,codec_type:'subtitle',codec_name:'hdmv_pgs_subtitle'}]},{format:{format_name:'fixture'},streams:[{index:0,codec_type:'audio',codec_name:'aac'}]},{format:{format_name:'fixture',duration:'3'},streams:[{index:0,codec_type:'subtitle',codec_name:'subrip'}]}]){
  const mediaURL='fixture:test'+rows.length;const c=new Converter({mediaURL,maxAudioChannels:2});probeCalls=0;let results=[];
  for(const action of ['master','master','status','missing','status','destroy','master','status']){try{const value=action==='master'?await c.playlist('master'):action==='missing'?await c.playlist('audio999'):action==='status'?await c.status():c.destroy();results.push({action,...(value===undefined?{}:{value})});}catch(error){results.push({action,error:error.message});}}
  rows.push({input,mediaURL,results,probeCalls});
 }
 fs.writeFileSync(process.argv[3].replace('factory','converters'),JSON.stringify(rows,null,2));
}
converterCases().catch(e=>{console.error(e);process.exitCode=1;});
async function routeCases(){
 const cache={},routes=new Map();let param;
 class Router{param(name,fn){param=fn;return this;}get(path,fn){routes.set(path,fn);return this;}}
 const overrides={24:require('querystring'),100:Router,106:{transcodeConcurrency:4,transcodeTrackConcurrency:1},440:{},432:async()=>{throw Error('Probe process exited with code: 7');},916:function(){throw Error('unexpected converter');}};
 function load(id){if(id in overrides)return overrides[id];if(cache[id])return cache[id].exports;const m={exports:{}};cache[id]=m;vm.runInNewContext('('+e.modules[id].source+')',{process:{env:{}},console:{log(){},error(){}},Buffer,setTimeout,clearTimeout,setInterval(){},Promise})(m,m.exports,load);return m.exports;}
 new (load(853))({});let rows=[];
 const cases=[['/:id/:track.m3u8','/hlsv2/missing/master.m3u8',{id:'missing',track:'master'},{}],['/:id/destroy','/hlsv2/dead/destroy?mediaURL=fixture%3Atest',{id:'dead'},{mediaURL:'fixture:test'}],['/:id/:track.m3u8','/hlsv2/dead/master.m3u8',{id:'dead',track:'master'},{}],['/:id/:track/init.mp4','/hlsv2/dead/video0/init.mp4',{id:'dead',track:'video0'},{}],['/:id/:track/segment:sequenceNumber.:ext','/hlsv2/dead/video0/segment1.m4s',{id:'dead',track:'video0',sequenceNumber:'1',ext:'m4s'},{}],['/:id/burn','/hlsv2/dead/burn',{id:'dead'},{}],['/status','/hlsv2/status',{},{}],['/probe','/hlsv2/probe?mediaURL=fixture%3Afail',{}, {mediaURL:'fixture:fail'}],['/:id/:track.m3u8','/hlsv2/invalid/master.m3u8?mediaURL=fixture%3Atest&maxAudioChannels=0',{id:'invalid',track:'master'},{mediaURL:'fixture:test',maxAudioChannels:'0'}]];
 for(const [route,target,params,query]of cases){let body='',res={statusCode:200,setHeader(){},end(value){if(value!==undefined)body+=value;}};const req={params,query};let proceed=!params.id;if(params.id)await param(req,res,()=>{proceed=true;});if(proceed)await routes.get(route)(req,res,()=>{res.statusCode=404;});rows.push({target,status:res.statusCode,body:body?JSON.parse(body):null});}
 fs.writeFileSync(process.argv[3].replace('factory','routes'),JSON.stringify(rows,null,2));
}
routeCases().catch(e=>{console.error(e);process.exitCode=1;});
