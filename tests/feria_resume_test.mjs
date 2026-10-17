// Actual Feria coordinator and generated page probe, modeled browser/media only.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const load = name => { const c=vm.createContext({});vm.runInContext(fs.readFileSync(new URL('../qml/feria/'+name,import.meta.url),'utf8'),c);return c };
const R=load('FeriaResume.js'), P=load('FeriaPlayback.js');
let count=0;
function test(name,fn){fn();++count;console.log('PASS '+name)}
const sample=(position, extra={})=>({position,duration:1000,paused:false,readyState:4,rate:1,mediaGeneration:'1',...extra});
test('intent protects before readiness',()=>{let s=R.begin(1,'A',600,0);assert(s.blocked);assert.equal(R.tick(s,sample(20,{readyState:1}),'A',100).allow,false);assert.equal(s.target,600)});
test('effective target clamp settles only on later samples',()=>{let s=R.begin(1,'A',600,0);assert.equal(R.tick(s,sample(20,{duration:300}),'A',100).seek,299);assert(s.blocked);assert.equal(R.tick(s,sample(299,{duration:300}),'A',1100).allow,false);assert.equal(R.tick(s,sample(299,{duration:300}),'A',2100).allow,true)});
test('deadline stops retries but retains bookmark protection',()=>{let s=R.begin(1,'A',600,0);let d=R.tick(s,sample(20,{readyState:1}),'A',16000);assert(d.unavailable);assert(s.blocked);assert.equal(R.tick(s,sample(20),'A',17000).seek,null);assert(s.blocked)});
console.log('FERIA_RESUME_PASS '+count);
for (const rate of [1,2,0.75,4]) test('ordinary '+rate+'x progression settles without user inference',()=>{
 let s=R.begin(1,'A',600,0);assert.equal(R.tick(s,sample(20,{rate}),'A',100).seek,600);
 assert.equal(R.tick(s,sample(600+rate*2,{rate}),'A',2100).allow,false);
 assert.equal(R.tick(s,sample(600+rate*4,{rate}),'A',4100).allow,true);
});
test('ad and late metadata do not expose startup progress',()=>{
 let s=R.begin(1,'A',600,0);for(const [at,extra] of [[100,{readyState:0}],[2100,{ad:true}],[4100,{readyState:2}]]){
 let d=R.tick(s,sample(20,extra),'A',at);assert.equal(d.seek,null);assert.equal(d.allow,false)}
 assert.equal(R.tick(s,sample(20),'A',6100).seek,600);
});
for(const snaps of [1,3]) test(snaps+' snap-backs re-seek then settle',()=>{
 let s=R.begin(1,'A',600,0);let now=100;assert.equal(R.tick(s,sample(20),'A',now).seek,600);
 for(let i=0;i<snaps;i++){now+=1000;assert.equal(R.tick(s,sample(20),'A',now).seek,600)}
 assert.equal(R.tick(s,sample(601),'A',now+1000).allow,false);
 assert.equal(R.tick(s,sample(602),'A',now+2000).allow,true);
});
test('bounded failure never emits last startup sample',()=>{
 let s=R.begin(1,'A',600,0);for(let i=1;i<=8;i++){let d=R.tick(s,sample(20),'A',i*1000);assert.equal(d.allow,false)}
 assert.equal(s.tries,4);assert(s.blocked);
});
test('after deadline later proven effective-target settlement can release',()=>{
 let s=R.begin(1,'A',600,0);R.tick(s,null,'',16000);assert(s.blocked);
 assert.equal(R.tick(s,sample(600),'A',18000).allow,false);
 assert.equal(R.tick(s,sample(602),'A',20000).allow,true);
});
test('reused player generation cancels target rather than inheriting it',()=>{
 let s=R.begin(1,'A',600,0);R.tick(s,sample(20),'A',100);
 assert.equal(R.tick(s,sample(20,{mediaGeneration:'2'}),'A',1100).seek,null);assert.equal(s.target,0);assert(s.blocked);
});
test('same URL, different qualified key owns a fresh generation',()=>{
 let s=R.begin(1,'ytmusic\nabcdefghijk',600,0);R.tick(s,sample(20),'ytmusic\nabcdefghijk',100);
 let d=R.tick(s,sample(10,{mediaGeneration:'2'}),'ytmusic\n0123456789_',1100);assert.equal(d.seek,null);assert(d.allow);assert.equal(s.target,0);
});
test('URL/navigation cancellation cannot apply previous request',()=>{
 let s=R.begin(1,'A',600,0);R.tick(s,sample(20),'A',100);s=R.invalidate(s,2);
 assert.equal(s.target,0);assert.equal(R.tick(s,sample(20),'B',1000).seek,null);assert.equal(s.blocked,false);
});
test('explicit cancellation is token checked; fresh start is deliberate',()=>{
 let s=R.begin(7,'A',600,0);assert.equal(R.cancel(s,6,true),s);
 s=R.cancel(s,7,false);assert(s.blocked);assert.equal(R.tick(s,sample(20),'A',1000).allow,false);
 s=R.cancel(s,s.token,true);assert.equal(R.tick(s,sample(20),'A',2000).allow,true);
});
test('generated seek targets the selected live generation; stale request cannot seek',()=>{
 const m={tagName:'VIDEO',duration:1000,currentTime:20,paused:false,readyState:4,playbackRate:1,isConnected:true,currentSrc:'a.mp4'};
 const page=vm.createContext({window:{},location:{href:'https://provider.test/watch/1'},navigator:{},document:{title:'A',querySelector:()=>null,querySelectorAll:s=>s==='video,audio'?[m]:[]}});
 const connector='function(){return {outcome:"not-applicable"}}';
 const frame=vm.runInContext(P.mediaObservation('watch','youtube',connector,1),page);
 assert.equal(vm.runInContext(P.seekMedia(600,frame.sample,'watch','youtube',connector,1),page),true);assert.equal(m.currentTime,600);
 m.currentSrc='b.mp4';m.currentTime=20;
 assert.equal(vm.runInContext(P.seekMedia(600,frame.sample,'watch','youtube',connector,1),page),false);assert.equal(m.currentTime,20);
 page.window.__feriaRequestToken=2;
 assert.equal(vm.runInContext(P.seekMedia(600,frame.sample,'watch','youtube',connector,1),page),false);
});
console.log('FERIA_HOSTILE_RESUME_PASS '+count);
test('paused startup is not sought, but later paused settlement is safe',()=>{
 let s=R.begin(1,'A',600,0);assert.equal(R.tick(s,sample(20,{paused:true}),'A',100).seek,null);
 assert.equal(R.tick(s,sample(20,{duration:300}),'A',200).seek,299);
 assert.equal(R.tick(s,sample(299,{duration:300,paused:true}),'A',1200).allow,false);
 assert.equal(R.tick(s,sample(300,{duration:300,paused:true,ended:true}),'A',2200).allow,true);
});
test('qualified request matching handles queue, fragment and encoded ID',()=>{
 assert.equal(P.mediaKey('ytmusic','https://music.youtube.com/watch?v=%61bcdefghijk&list=q#/x'),'ytmusic\nabcdefghijk');
 assert.notEqual(P.mediaKey('ytmusic','https://evil.test/watch?v=abcdefghijk'),'ytmusic\nabcdefghijk');
});
console.log('FERIA_RESUME_FINAL_PASS '+count);
test('generated probe distinguishes late discovery from reused same-source lifecycle',()=>{
 let media=null;const events={};const m={tagName:'VIDEO',duration:1000,currentTime:20,paused:false,readyState:4,playbackRate:1,isConnected:true,currentSrc:'a.mp4',addEventListener:(name,fn)=>events[name]=fn};
 const page=vm.createContext({window:{},location:{href:'https://provider.test/watch/1'},navigator:{},document:{title:'A',querySelector:()=>null,querySelectorAll:s=>s==='video,audio'&&media?[media]:[]}});
 const connector='function(){return {outcome:"not-applicable"}}';const poll=()=>vm.runInContext(P.mediaObservation('watch','youtube',connector,1),page);
 const waiting=poll();media=m;const discovered=poll();assert.equal(waiting.sample.mediaGeneration,discovered.sample.mediaGeneration);
 events.loadedmetadata();const replacement=poll();assert.notEqual(discovered.sample.mediaGeneration,replacement.sample.mediaGeneration);
 assert.equal(vm.runInContext(P.seekMedia(600,discovered.sample,'watch','youtube',connector,1),page),false);
});
console.log('FERIA_RESUME_TOTAL_PASS '+count);

test('generic tracking-only SPA URL noise does not replace media generation',()=>{
 const m={tagName:'VIDEO',duration:1000,currentTime:20,paused:false,readyState:4,playbackRate:1,currentSrc:'a.mp4'};
 const page=vm.createContext({URL,window:{},location:{href:'https://provider.test/watch?id=1'},navigator:{},document:{title:'A',querySelector:()=>null,querySelectorAll:s=>s==='video,audio'?[m]:[]}});
 const connector='function(){return {outcome:"not-applicable"}}';const poll=()=>vm.runInContext(P.mediaObservation('watch','youtube',connector,1),page);
 const a=poll();page.location.href+='&utm_source=feed&trackId=1';const b=poll();assert.equal(a.sample.mediaGeneration,b.sample.mediaGeneration);
});
console.log('FERIA_RESUME_COMPLETE_PASS '+count);
