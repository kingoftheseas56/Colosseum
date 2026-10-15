import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
function load(name){const c=vm.createContext({});vm.runInContext(fs.readFileSync(new URL('../qml/feria/'+name,import.meta.url),'utf8'),c);return c}
const P=load('FeriaPlayback.js'),C=load('FeriaConnectors.js'),Y=load('FeriaYouTubeMusic.js');
let tests=0;
function test(name,fn){fn();tests++;console.log('PASS '+name)}
function fixture() {
 const f={now:1000,id:'abcdefghijk',title:'Track',href:'https://music.youtube.com/watch?v=abcdefghijk&list=queue',
   link:'https://music.youtube.com/watch?v=abcdefghijk',clock:'0:20 / 5:00',ad:false,queries:0,genericScans:0};
 const listeners={};
 f.media={currentTime:20,duration:300,paused:false,readyState:4,ended:false,playbackRate:1,currentSrc:'a.mp4',isConnected:true,
   addEventListener:(name,fn)=>{(listeners[name]??=[]).push(fn)}};
 f.fire=name=>(listeners[name]||[]).forEach(fn=>fn());
 f.meta={title:'Track',artist:'Artist',album:'Album',artwork:[{src:'https://example.test/art.jpg'}]};
 const player={querySelector:()=>f.media,getVideoData:()=>({video_id:f.id,title:f.title}),
   getPlayerResponse:()=>({videoDetails:{videoId:f.responseId||f.id,title:f.title,lengthSeconds:'300'}})};
 const document={title:'YouTube Music',querySelector(selector){
  f.queries++;
  if(selector==='#movie_player')return player;
  if(selector.includes('advertisement'))return f.ad?{}:null;
  if(selector.startsWith('ytmusic-player-bar a'))return f.link?{href:f.link}:null;
  if(selector==='.ytmusic-player-bar.time-info')return f.clock?{textContent:f.clock}:null;
  return null;
 },querySelectorAll(selector){f.genericScans++;return selector==='video,audio'?[f.media]:[]}};
 f.page=vm.createContext({URL,Date:{now:()=>f.now},Math,window:{},document,
   location:{get href(){return f.href}},navigator:{mediaSession:{metadata:f.meta}}});
 f.freshMetadata=()=>{f.meta={...f.meta};f.page.navigator.mediaSession.metadata=f.meta};
 f.observe=(provider='ytmusic')=>{f.now+=250;return vm.runInContext(P.mediaObservation('listen',provider,C.script(Y.source),1),f.page)};
 f.settle=()=>{let x=f.observe();if(x.outcome==='hold')x=f.observe();assert.equal(x.outcome,'observation',x.reason);return x.sample};
 return f;
}
test('stable qualified ID and canonical queue-free destination',()=>{const f=fixture(),s=f.settle();assert.equal(s.providerMediaId,'abcdefghijk');assert.equal(s.href,'https://music.youtube.com/watch?v=abcdefghijk');assert.equal(s.position,20);assert.equal(s.artist,'Artist');assert.equal(s.album,'Album');assert.equal(s.artwork,'https://example.test/art.jpg')});
test('title and queue churn preserve ID and player generation',()=>{const f=fixture(),a=f.settle();f.href+='&si=tracking';f.title='Track renamed';f.meta.title=f.title;const b=f.settle();assert.equal(a.providerMediaId,b.providerMediaId);assert.equal(a.mediaGeneration,b.mediaGeneration)});
test('same title different IDs are separate after a real media lifecycle',()=>{const f=fixture(),a=f.settle();f.id='0123456789_';f.href='https://music.youtube.com/watch?v='+f.id;f.link=f.href;f.fire('loadedmetadata');f.freshMetadata();const b=f.settle();assert.notEqual(a.providerMediaId,b.providerMediaId);assert.notEqual(a.mediaGeneration,b.mediaGeneration)});
test('same URL reused element waits for coherent new media boundary',()=>{const f=fixture();f.href='https://music.youtube.com/playlist?list=queue';f.settle();f.id='0123456789_';f.link='https://music.youtube.com/watch?v='+f.id;assert.equal(f.observe().reason,'await-media-transition');f.fire('loadedmetadata');f.freshMetadata();assert.equal(f.settle().providerMediaId,f.id)});
test('foreign host carrying v is held without generic fallback',()=>{const f=fixture();f.href='https://evil.test/watch?v=abcdefghijk';assert.equal(f.observe().outcome,'hold');assert.equal(f.genericScans,0)});
test('native provider context controls selection',()=>{const f=fixture();const x=f.observe('youtube');assert.equal(x.outcome,'observation');assert.equal(x.sample.providerMediaId,undefined);assert(f.genericScans>0)});
for(const clock of ['1:99 / 2:00','bad / 5:00','9:00 / 5:00','0:20 / -1:00'])test('malformed clock '+clock,()=>{const f=fixture();f.clock=clock;assert.equal(f.observe().outcome,'hold');assert.equal(f.genericScans,0)});
test('ad is held and later coherent observation releases its own hold',()=>{const f=fixture();f.settle();f.ad=true;assert.equal(f.observe().reason,'ad');assert.equal(f.genericScans,0);f.ad=false;assert.equal(f.observe().outcome,'observation')});
test('old URL and new player/link metadata are withheld',()=>{const f=fixture();f.settle();f.id='0123456789_';f.link='https://music.youtube.com/watch?v='+f.id;f.title='Next';f.meta.title='Next';assert.equal(f.observe().reason,'identity-conflict');assert.equal(f.genericScans,0)});
test('new URL with old player identity is withheld',()=>{const f=fixture();f.settle();f.href='https://music.youtube.com/watch?v=0123456789_';assert.equal(f.observe().reason,'identity-conflict')});
test('metadata title conflict is withheld',()=>{const f=fixture();f.meta.title='Previous';assert.equal(f.observe().reason,'metadata-transition')});
for(const position of [NaN,Infinity,-1,301])test('invalid exact position '+position,()=>{const f=fixture();f.media.currentTime=position;assert.equal(f.observe().reason,'invalid-exact-time')});
test('paused and playing state come from selected qualified player',()=>{const f=fixture();f.media.paused=true;assert.equal(f.settle().paused,true)});
test('selected connector uses bounded selectors, no full DOM scans',()=>{const f=fixture();for(let i=0;i<100;i++)f.observe();assert.equal(f.genericScans,0);console.log('SCALE 100 samples: targeted DOM queries='+f.queries+' generic scans='+f.genericScans)});
console.log('FERIA_YOUTUBE_MUSIC_PASS '+tests);

test('ID-bearing artwork cannot supply previous-track metadata',()=>{const f=fixture();f.meta.artwork=[{src:'https://i.ytimg.com/vi/0123456789_/default.jpg'}];assert.equal(f.observe().reason,'metadata-transition')});
test('same-title new track waits for fresh MediaSession metadata',()=>{const f=fixture();f.settle();f.id='0123456789_';f.href='https://music.youtube.com/watch?v='+f.id;f.link=f.href;f.fire('loadedmetadata');assert.equal(f.observe().reason,'metadata-transition');f.freshMetadata();assert.equal(f.settle().providerMediaId,f.id)});
console.log('FERIA_YOUTUBE_MUSIC_TOTAL_PASS '+tests);
