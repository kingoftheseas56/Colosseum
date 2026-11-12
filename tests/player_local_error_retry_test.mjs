import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const source=readFileSync(process.argv[2] || new URL('../qml/PlayerPage.qml',import.meta.url),'utf8');
function fixture() {
 const loads=[], calls=[];
 const mpv={position:0,duration:0,pause:false,loadSource:(url,headers)=>loads.push({url,headers})};
 const root={mediaLocalPath:'/tmp/valid.mp4',currentPlaybackUrl:'/tmp/valid.mp4',errored:true,starting:false,fileReady:false,
  playbackEnded:false, currentStreamIndex:-1,streamCandidates:[], activeDirectHeaders:{},
  recoveryPositionSec:43,recoveryResumePending:false,recoveryResumePaused:false,pendingSeekSec:-1,
  audioHandoffPaused:false,autoPausedInactive:false,resumeChoiceOpen:false,resumeChoiceSec:-1,
  mediaId:'vault:real',mediaTitle:'Original title',playbackQueue:['same','next'],
  wakeChrome:()=>calls.push('wake'),healAudio:()=>calls.push('heal'),cancelUpNext:()=>calls.push('cancel-next'),
  requestAudioPlayback(){this.audioHandoffPaused=false;calls.push('claim')},
  resetRecoveryWatch:()=>calls.push('reset-watch'),activityDiscontinuity:()=>calls.push('discontinuity')};
 const ctx={root,mpv,WatchPartySync:{active:false},streamWatchdog:{restart:()=>calls.push('watch')},Stream:{play:()=>calls.push('stream')}};
 for(const name of ['recoveryPauseIntent','prepareRecoveryResume','retryFailedLocalPlayback','retryCurrentStream','replayEndedPlayback','resumeFromMinimize','togglePlayPause','reloadActiveDirectSource']) {
  const m=source.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));
  if(m)root[name]=vm.runInNewContext('(function('+m[1]+'){'+m[2]+'\n})',ctx);
 }
 return {root,mpv,loads,calls,ctx};
}
for(const method of ['retryCurrentStream','togglePlayPause','resumeFromMinimize']) {
 test(method+' explicitly reloads failed local file once with existing identity and progress',()=>{
  const f=fixture(); f.root[method]();
  assert.equal(f.loads.length,1,'failed local file must be re-admitted, not only unpaused');
  assert.equal(f.loads[0].url,'/tmp/valid.mp4');
  assert.equal(Object.keys(f.loads[0].headers).length,0,'local retry must not carry provider headers');
  assert.equal(f.root.pendingSeekSec,43); assert.equal(f.root.recoveryResumePending,true);
  assert.equal(f.root.recoveryResumePaused,false,'explicit Play/reopen requests playback');
  assert.equal(f.root.errored,false); assert.equal(f.root.starting,true); assert.equal(f.root.playbackEnded,false);
  assert.equal(f.root.mediaId,'vault:real'); assert.equal(f.root.mediaTitle,'Original title');
  assert.deepEqual(f.root.playbackQueue,['same','next']);
  f.root[method](); assert.equal(f.loads.length,1,'repeated input while opening must not duplicate admission');
 });
 test(method+' does not claim room-controlled errored local playback',()=>{
  const f=fixture();f.ctx.WatchPartySync.active=true; f.root[method]();assert.equal(f.loads.length,0);
 });
}
test('healthy minimized local playback stays warm with no reload',()=>{
 const f=fixture();f.root.errored=false;f.root.fileReady=true;f.root.resumeFromMinimize();assert.equal(f.loads.length,0);
});
test('error with no local path does not use local retry',()=>{
 const f=fixture();f.root.mediaLocalPath='';f.root.resumeFromMinimize();assert.equal(f.loads.length,0);
});
test('explicit retry preserves a deliberately paused local position',()=>{
 const f=fixture();f.mpv.pause=true;f.root.retryCurrentStream();assert.equal(f.root.recoveryResumePaused,true);
});
test('explicit Play overrides a paused failed source',()=>{
 const f=fixture();f.mpv.pause=true;f.root.togglePlayPause();assert.equal(f.root.recoveryResumePaused,false);
});
test('local error message does not assert that an audio failure means a missing file',()=>{
 const match=source.match(/    function handlePlaybackFailure\(([^)]*)\) \{([\s\S]*?)\n    \}/);
 const root={mediaLocalPath:'/tmp/valid.mp4',recordProgress(){},wakeChrome(){}};
 vm.runInNewContext('(function('+match[1]+'){'+match[2]+'\n})',{root,streamWatchdog:{stop(){}}})('audio');
 assert.doesNotMatch(root.statusMsg,/moved or deleted/);assert.match(root.statusMsg,/audio output/);
});
for(const flag of ['autoPausedInactive','audioHandoffPaused']) {
 test('warm failed local reactivation resumes when '+flag+' caused pause',()=>{
  const f=fixture();f.mpv.pause=true;f.root[flag]=true;f.root.resumeFromMinimize();
  assert.equal(f.loads.length,1);assert.equal(f.root.recoveryResumePaused,false);
 });
}
test('warm failed local reactivation preserves deliberate user pause',()=>{
 const f=fixture();f.mpv.pause=true;f.root.resumeFromMinimize();assert.equal(f.root.recoveryResumePaused,true);
});
