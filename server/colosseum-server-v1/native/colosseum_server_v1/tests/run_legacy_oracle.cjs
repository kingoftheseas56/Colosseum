'use strict';
const fs=require('fs'),vm=require('vm'),{extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const e=extractModules(process.argv[2]);
let instance, capture, subs={}; const cache={};
const queue={push(task,cb){cb(null,instance)}};
function load(id){if(cache[id])return cache[id].exports;const m={exports:{}};
 const overrides={35:f=>f,32:{spawn(program,args){capture.program=program;capture.args=args;return{}}},180:()=>{},669:{languageNames:{eng:'English',fra:'français'}},6:require('url'),305:{probeVideo(from,cb){cb(null,instance)}}};
 if(id in overrides)return overrides[id];cache[id]=m;
 vm.runInNewContext('('+e.modules[id].source+')',{process:{env:{}},console:{error(){}},Buffer,setTimeout:f=>f()})(m,m.exports,load);return m.exports;
}
const common=load(81);common.pipeProcToResp=()=>{};
load(668).init(queue,subs);load(673).init(queue);load(674).init(queue,subs,{emit(){}});load(675).init({ffmpeg:'ffmpeg',ffsplit:'ffsplit'},queue,{push:f=>f(()=>{})});load(729).init({ffmpeg:'ffmpeg',ffsplit:'ffsplit'},{});
common.serveFfmpeg=(args,type,res)=>{capture.program='ffmpeg';capture.args=args;res.setHeader('Content-Type',type);};
let rows=[];
const base={duration:12500,bitrate:123456,container:'mp4',streams:[{codec_type:'video',codec_name:'h264',stream:0,size:[1280,720],fps:25},{codec_type:'audio',codec_name:'aac',stream:1,lang:'eng',default:true}],qualities:[320,480,720],segmentsUniform:[{at:0,duration:6000},{at:6000,duration:6000},{at:12000,duration:500}],segmentsByFrame:[{at:0,duration:2000},{at:2000,duration:10500}]};base.videoStream=base.streams[0];
for(const action of ['master','multi','playlist','subtitle','segment','transcode','dlna','thumb'])for(const variant of [0,1,2,3]){
 instance=JSON.parse(JSON.stringify(base));capture={status:200,headers:{},body:''};subs.test=[{lang:'eng',url:'http://example.test/a b.srt',langName:'English'}];
 const req={url:'/file/movie/master.m3u8?x=1',search:'?x=1',headers:{host:'localhost:11470'},params:{from:'fixture.mp4'},query:{}};
 if(variant===1){req.query.waitForSubs='test';req.params.adaptiveBitrate=true;req.query.xdiscontinuity='0';}
 if(variant===2){req.params.stream='1';req.params.quality='o';req.params.seg='1tail';req.query.at='1.25';req.params.lang='eng';req.query.waitForSubs='test';}
 if(variant===3){req.params.quality='123';req.params.seg='99';req.query.at='bogus';instance.streams[0].codec_name='vp9';instance.streams[1].codec_name='opus';}
 if(action==='playlist'){req.url='/file/movie/stream-q-'+(req.params.quality||'720')+'.m3u8?x=1';if(!req.params.quality)req.params.quality='720';}
 if(action==='segment'||action==='transcode'){req.params.seg=req.params.seg||'0';req.params.quality=req.params.quality||(action==='transcode'?'480':'o');}
 const res={setHeader(k,v){capture.headers[k.toLowerCase()]=v},writeHead(n){capture.status=n},write(v){capture.body+=v},end(v){if(v)capture.body+=v},set statusCode(v){capture.status=v}};
 const mods={master:[668,'masterPlaylistMiddleware'],multi:[668,'masterMultiPlaylistMiddleware'],playlist:[673,'streamPlaylistMiddleware'],subtitle:[674,'subsPlaylistMiddleware'],segment:[675,'segmentMiddleware'],transcode:[675,'segmentTranscodeMiddleware'],dlna:[675,'DLNAMpegTtsMiddleware'],thumb:[729,'thumbMiddleware']};
 const input=JSON.parse(JSON.stringify(req));load(mods[action][0])[mods[action][1]](req,res,()=>{});if(capture.args)capture.args=capture.args.map(String);rows.push({action,variant,instance,input,subs:subs.test,expected:capture});
}
fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2));console.log(JSON.stringify({oracle:e.sha256,cases:rows.length}));
