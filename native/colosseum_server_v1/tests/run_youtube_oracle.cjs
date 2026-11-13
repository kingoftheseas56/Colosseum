'use strict';
// Lawful synthetic HTTP pages. Runs the exact pinned resolver, not a model of its outputs.
const fs=require('node:fs'),vm=require('node:vm'),{spawnSync}=require('node:child_process');
const {extractModules}=require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle=extractModules(process.argv[2]);
if(oracle.sha256!=='405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f')throw Error('oracle hash mismatch');
const player='var Z={rv:function(a){a.reverse()},sl:function(a,b){return a.slice(b)},sp:function(a,b){a.splice(0,b)},sw:function(a,b){var c=a[0];a[0]=a[b%a.length];a[b]=c}};function D(a){a=a.split("");Z.sw(a,2);Z.rv(a,0);Z.sp(a,1);a=Z.sl(a,1);return a.join("")}';
const origin='https://www.youtube.com',watch=origin+'/watch?v=abcdefghijk&hl=en',base=origin+'/s/player/test/base.js';
function html(p,script=true){p={videoDetails:{videoId:'abcdefghijk',title:'synthetic fixture',thumbnail:{thumbnails:[]}},...p};if(p.videoDetails&&!p.videoDetails.thumbnail)p.videoDetails.thumbnail={thumbnails:[]};return '<script>var ytInitialPlayerResponse = '+JSON.stringify(p)+';</script><script>var ytInitialData = {};</script>'+(script?'<script src="/s/player/test/base.js" name="player_ias/base"></script>':'');}
function pages(p,script=player){return {[watch]:html({videoDetails:{videoId:'abcdefghijk',title:'synthetic fixture'},...p}),[base]:script};}
const rows=[];
function add(name,streamingData,extra={}){rows.push({name,id:'abcdefghijk',pages:pages({streamingData}),...extra});}
add('progressive-highest',{formats:[{itag:18,url:'https://media.example.test/video?x=1'},{itag:22,url:'https://media.example.test/high?x=2'}]});
add('audio-only-excluded',{adaptiveFormats:[{itag:140,url:'https://media.example.test/audio'}]});
add('video-only-excluded',{adaptiveFormats:[{itag:137,url:'https://media.example.test/video'}]});
add('content-length-priority',{formats:[{itag:18,url:'https://media.example.test/low',contentLength:'42'},{itag:22,url:'https://media.example.test/high'}]});
add('cipher-decipher',{formats:[{itag:22,signatureCipher:new URLSearchParams({url:'https://media.example.test/clip?foo=bar',s:'abcdef',sp:'sig'}).toString()}]});
add('old-cipher-default-signature',{formats:[{itag:18,cipher:new URLSearchParams({url:'https://media.example.test/clip',s:'abcdef'}).toString()}]});
add('format-overrides',{formats:[{itag:18,url:'https://media.example.test/one',mimeType:'video/webm; codecs="vp9, opus"',audioBitrate:128,qualityLabel:'1080p',height:1080}]});
add('unknown-format',{formats:[{itag:9999,url:'https://media.example.test/u',mimeType:'video/test; codecs="x, y"',qualityLabel:'480p',audioBitrate:50}]});
add('duplicate-url-last-wins',{formats:[{itag:18,url:'https://media.example.test/same'},{itag:22,url:'https://media.example.test/same'}]});
add('url-form-encoding',{formats:[{itag:18,url:'https://media.example.test/clip?x=hello%2520world&tilde=~&ratebypass=no&ratebypass=maybe'}]});
add('empty-formats',{formats:[]});
add('bad-player-script',{formats:[{itag:18,url:'https://media.example.test/clip'}]},{pages:pages({streamingData:{formats:[{itag:18,url:'https://media.example.test/clip'}]}},'// changed player has no old actions')});
rows.push({name:'private-video',id:'abcdefghijk',pages:pages({playabilityStatus:{status:'LOGIN_REQUIRED',reason:'Private video',messages:['This is a private video']}})});
rows.push({name:'removed-video',id:'abcdefghijk',pages:pages({playabilityStatus:{status:'ERROR',reason:'Video unavailable'}})});
rows.push({name:'invalid-id',id:'bad',pages:{}});
rows.push({name:'empty-id',id:'',pages:{}});
add('long-id-truncates',{formats:[{itag:18,url:'https://media.example.test/clip'}]},{id:'abcdefghijklmnop'});
const manifest=origin+'/manifest/hls_variant/test';add('hls-manifest',{hlsManifestUrl:manifest},{pages:{...pages({streamingData:{hlsManifestUrl:manifest}}),[manifest]:'#EXTM3U\nhttps://media.example.test/manifest/hls_playlist/itag/18/low\nhttps://media.example.test/manifest/hls_playlist/itag/22/high\n'}});
const dash=origin+'/manifest/dash/test';add('dash-last-representation',{dashManifestUrl:dash},{pages:{...pages({streamingData:{dashManifestUrl:dash}}),[dash]:'<MPD><Period><AdaptationSet mimeType="video/mp4"><Representation id="18" bandwidth="100" codecs="avc1, mp4a" width="640" height="360" frameRate="30"/><Representation id="22" bandwidth="200" codecs="avc1, mp4a" width="1280" height="720" frameRate="30"/></AdaptationSet></Period></MPD>'}});
add('hls-over-live-progressive',{formats:[{itag:22,url:'https://media.example.test/source/yt_live_broadcast/a'},{itag:18,url:'https://media.example.test/manifest/hls_playlist/low'}]});
const pbj=watch+'&pbj=1';rows.push({name:'watch-json-fallback',id:'abcdefghijk',pages:{[watch]:'<html>No legacy player here</html>',[pbj]:JSON.stringify([{playerResponse:{videoDetails:{thumbnail:{thumbnails:[]}},streamingData:{formats:[{itag:18,url:'https://media.example.test/fallback'}]}}},{player:{assets:{js:'/s/player/test/base.js'}}}]),[base]:player}});
rows.push({name:'embed-player-fallback',id:'abcdefghijk',pages:{[watch]:html({streamingData:{formats:[{itag:18,url:'https://media.example.test/embed'}]}},false),[origin+'/embed/abcdefghijk?hl=en']:'{"jsUrl":"/s/player/test/base.js"}',[base]:player}});
const infoUrl=new URL(origin+'/get_video_info');for(const [k,v] of Object.entries({video_id:'abcdefghijk',c:'TVHTML5',cver:'7',eurl:'https://youtube.googleapis.com/v/abcdefghijk',ps:'default',gl:'US',hl:'en',html5:'1'}))infoUrl.searchParams.set(k,v);
rows.push({name:'get-video-info-fallback',id:'abcdefghijk',pages:{[watch]:'<html>{"jsUrl":"/s/player/test/base.js"}</html>',[pbj]:'{}',[infoUrl.toString()]:new URLSearchParams({player_response:JSON.stringify({videoDetails:{thumbnail:{thumbnails:[]}},streamingData:{formats:[{itag:18,url:'https://media.example.test/legacy-info'}]}})}).toString(),[base]:player}});
function routeCallback(path) {
 const source=oracle.modules[564].source,start=source.indexOf('enginefs.router.get("'+path+'", ');let p=source.indexOf(',',start)+1;while(/\s/.test(source[p]))p++;let depth=0,quote='',escape=false;
 for(let i=p;i<source.length;i++){const c=source[i];if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote='';continue;}if(c==='"'||c==="'"||c==='`'){quote=c;continue;}if(c==='(')depth++;else if(c===')'&&!--depth)return source.slice(p,i+1);}throw Error('missing route');
}
function routeResult(result,json){const response={status:200,headers:{},body:'',writeHead(s,h={}){this.status=s;this.headers=h;},end(b=''){this.body=b;}};vm.runInNewContext(routeCallback(json?'/yt/:id.json':'/yt/:id'),{console:{error(){}},getYt(id,cb){cb(result.error?Error(result.error):null,result.error?undefined:result.format);}})({params:{id:'abcdefghijk'}},response);return response;}
function fresh(row){
 const cache=new Map();const sandbox={URL,URLSearchParams,Buffer,console,process:{...process,env:{...process.env,YTDL_NO_UPDATE:'1'}},setTimeout,clearTimeout,setInterval,clearInterval,setImmediate,clearImmediate};sandbox.global=sandbox;sandbox.require=require;
 const context=vm.createContext(sandbox),realRequire=require('node:module').createRequire(oracle.bundlePath);
 function req(id){if(typeof id==='string')return realRequire(id);if(cache.has(id))return cache.get(id).exports;const m={exports:{}};cache.set(id,m);vm.runInContext('('+oracle.modules[id].source+')',context)(m,m.exports,req);return m.exports;}
 const utils=req(108);utils.checkForUpdates=()=>{};
 const miniget=req(156);miniget.defaultOptions.maxRetries=0;
 utils.exposedMiniget=(url)=>{url=String(url);const {PassThrough}=require('node:stream');const stream=new PassThrough();stream.on("error",()=>{});stream.text=async()=>{if(!(url in row.pages))throw Error('Missing fixture: '+url);return row.pages[url];};process.nextTick(()=>{if(url in row.pages)stream.end(row.pages[url]);else stream.destroy(Error('Missing fixture: '+url));});return stream;};
 return {info:req(936),formats:req(470),sig:req(473)};
}
(async()=>{
 if(process.env.SERVER1_YT_PUBLIC_FIXTURE){rows.length=0;rows.push(JSON.parse(fs.readFileSync(process.env.SERVER1_YT_PUBLIC_FIXTURE,'utf8')));}
 for(const row of rows){const {info,formats,sig}=fresh(row);let result;try {const inf=await info.getInfo('http://www.youtube.com/watch?v='+row.id,{downloadURL:true});result={format:formats.chooseFormat(inf.formats,{filter:'audioandvideo'}),error:''};}catch(e){result={format:{},error:e.message};}row.expected=result;row.routes={json:routeResult(result,true),redirect:routeResult(result,false)};row.tokenScript=player;row.actions=sig.extractActions(player);row.deciphered=sig.decipher(row.actions,'abcdef');}
 fs.writeFileSync(process.argv[3],JSON.stringify(rows,null,2)+'\n');
 if(process.argv[4]){const r=spawnSync(process.argv[4],[process.argv[3]],{stdio:'inherit'});process.exitCode=r.status??1;}
})().catch(e=>{console.error(e);process.exitCode=1;});
