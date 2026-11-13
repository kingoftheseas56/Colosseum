'use strict';
const fs=require('fs'),vm=require('vm'),cp=require('child_process'),EventEmitter=require('events');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const x=extractModules(process.argv[2]);if(x.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('Wrong oracle');
const load=(id,deps)=>{const m={exports:{}};vm.runInNewContext('('+x.modules[id].source+')',{Buffer,console:{log(){}},URL,Promise,setTimeout,clearTimeout})(m,m.exports,n=>{if(!(n in deps))throw Error('Unexpected dependency '+n);return deps[n]});return m.exports};
const rows=[];
const stderr='Input #0, matroska, from file:\n  Duration: 00:01:12.34, start: 0, bitrate: 500 kb/s\n  Stream #0:0(eng): Video: h264 (High), yuv420p(progressive), 320x240, 25 fps (default)\n  Stream #0:1(eng): Audio: aac (LC), 48000 Hz, stereo, fltp (default)\n';
const helpers=load(234,{32:{spawn(){const p={stderr:new EventEmitter()};queueMicrotask(()=>{p.stderr.emit('data',stderr);p.stderr.emit('end')});return p;}},34:()=>{},68:{},1:{},5:require('path'),6:require('url')});
(async()=>{
 rows.push({kind:'info',input:stderr,expected:await helpers.getVideoInfo('ffmpeg','file:///tmp/demo')});
 for(const input of ['01:02:03.04','00:00:01.123','nonsense','prefix 2:3:4x5 suffix'])rows.push({kind:'hms',input,expected:helpers.HMSToSec(input)});
 const base=rows[0].expected;
 const variants=[base,{duration:0,streams:base.streams},{duration:40,streams:[{id:'0:0',type:'Video',codec:'hevc'},{id:'0:1',type:'Audio',codec:'ac3',channels:'5.1',default:true}]},{duration:15,streams:[{id:'0:2',type:'Audio',codec:'aac',channels:'stereo',default:true}]}];
 for(const info of variants)for(const query of [{video:'file:///tmp/demo%20movie.mkv'},{video:'http://127.0.0.1/a',time:'12tail',fmp4:'0',flagRe:'1',audioTrack:'0:1'},{video:'file:///tmp/test',time:'99',audioTrack:'missing'},{video:'file:///tmp/test',subtitles:'http://127.0.0.1/subs.srt'}]){
  let args;const router={param(){},get(){},all(){}};
  const Casting=load(942,{1:{},5:require('path'),475:{},32:{spawn(_,a){args=a;return {stdout:{},kill(){}}}},34:()=>{},179:()=>{},944:{},6:require('url'),234:{getVideoInfo:()=>Promise.resolve(info),getRemoteFileMeta:()=>Promise.resolve({length:100})},945:function(){this.devices={}},946:{},951:{},960:{},996:{},1006:{},100:()=>router});
  const cast=new Casting({ffmpeg:'ffmpeg'});const subtitleFile=query.subtitles?'/tmp/subs.srt':'';cast.makeSubs=()=>Promise.resolve(subtitleFile||undefined);const headers={};cast.transcode({query,headers:{},method:'GET',once(){}},{writeHead(_,h){Object.assign(headers,h)},end(){},setHeader(){}});await new Promise(r=>setImmediate(r));rows.push({kind:'args',query,info,subtitleFile,expected:args.map(String)});
 }
 const mdns=load(951,{4:EventEmitter,952:()=>({decode:txt=>Object.fromEntries(txt.map(x=>x.split('=')))}),954:()=>({on(){},query(){}}),0:require('util')});
 const response={answers:[{name:'_googlecast._tcp.local',type:'PTR',data:'Living Room._googlecast._tcp.local'}],additionals:[{type:'SRV',data:{target:'fixture.local',port:8009}},{type:'TXT',data:['fn=Fixture TV']},{type:'A',data:'127.0.0.1'}]};let expected;const instance=new mdns();instance.on('device',d=>expected=d);instance.onResponse(response);rows.push({kind:'mdns',input:response,expected});
 const SSDP=load(960,{6:require('url'),4:EventEmitter,235:{promisify:()=>()=>{}},188:{parseString(){}},34:()=>{},991:{Client:function(){this.on=()=>{}}},0:require('util')});
 for(const [deviceType,type] of [['urn:schemas-upnp-org:device:MediaRenderer:1','tv'],['urn:dial-multiscreen-org:device:dial:1','chromecast'],['unsupported','']]){const instance=new SSDP();let expected={};instance.on('device',d=>expected=d);const location='http://127.0.0.1:1234/description.xml';const device={deviceType:[deviceType],UDN:['uuid:fixture-id'],friendlyName:['Fixture & TV']};instance.normalizeObject(location,{root:{device:[device]}});const xml='<root><device><deviceType>'+deviceType+'</deviceType><UDN>uuid:fixture-id</UDN><friendlyName>Fixture &amp; TV</friendlyName></device></root>';rows.push({kind:'ssdp',location,xml,expected});}
 const Player=load(482,{479:{},6:require('url'),24:require('querystring')});
 for(const params of [{},{paused:'false',stop:'1',source:'x'},{paused:'true',time:'99'},{source:''},{subtitlesDelay:'2',subtitlesSize:'3'},{volume:'0.2',audioTrack:'0:1'},{formats:'1'},{time:'12'},{volume:['0.2','0.3']}]){const p=new Player();p.methodInvoke=(method,args)=>{rows.push({kind:'player',query:params,expected:{method,arguments:args,status:p.mediaStatus}})};p.middleware({url:'/player?'+require('querystring').stringify(params),body:{}},{},()=>{});}
 const Dlna=load(1006,{6:{},24:require('querystring'),235:{promisifyAll:x=>x},34:{},475:{},32:{},179:{},1007:function(){this.on=()=>{}},0:require('util'),234:{},482:Player});
 for(const input of [{TransportState:'PLAYING',CurrentTrackPosition:'12.3',CurrentVolume:'25'},{TransportState:'NO_MEDIA_PRESENT',CurrentTrackPosition:'0',CurrentVolume:0},{TransportState:'PAUSED_PLAYBACK'},{TransportState:'UNKNOWN'}]){const p=new Dlna({},{});p._updateStatus(input);rows.push({kind:'dlna',input,expected:p.mediaStatus});}
 const Cast=load(996,{479:{},234:{},24:require('querystring'),235:Promise,997:{Client:function(){this.on=()=>{}}},0:require('util'),482:Player});
 for(const input of [{playerState:'PLAYING',currentTime:12},{playerState:'NO_MEDIA_PRESENT',currentTime:0},{playerState:'PAUSED',volume:{level:0.3}},{applications:[{statusText:'Ready To Cast'}]}]){const p=new Cast({},{});rows.push({kind:'chromecast',input,expected:p._conformStatus(input)});}
 fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2));if(process.argv[4]){const p=cp.spawnSync(process.argv[4],[process.argv[3]],{stdio:'inherit'});process.exitCode=p.status;}
})().catch(e=>{console.error(e);process.exitCode=1});
