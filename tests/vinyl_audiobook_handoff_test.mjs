import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source=fs.readFileSync(new URL('../qml/AudiobookSession.qml',import.meta.url),'utf8');
function load(name,ctx){const m=source.match(new RegExp('    function '+name+'\\(([^)]*)\\) \\{([\\s\\S]*?)\\n    \\}'));assert.ok(m,name);return vm.runInNewContext('(function('+m[1]+'){'+m[2]+'\n})',ctx);}
let advances=0,completed=0;
const session={ready:true,multiFile:true,currentIndex:0,files:['a','b'],audioHandoffPaused:true,
 playIndex(){advances++;},activityNaturalEof(){completed++;}};
const eof=source.match(/        function onEndFile\(reason\) \{([\s\S]*?)\n        \}/);
const end=vm.runInNewContext('(function(reason){'+eof[1]+'\n})',{session});
end('eof');assert.equal(advances,0,'queued EOF after handoff must not advance/reclaim audio');
session.audioHandoffPaused=false;end('eof');assert.equal(advances,1,'ordinary multi-file EOF still advances');
session.currentIndex=1;end('eof');assert.equal(completed,1,'final EOF completes');
session.ready=false;end('eof');assert.equal(completed,1,'late EOF after stop ignored');
let requests=0,saves=0,loads=0;
const mpv={pause:false,loadFile(){loads++;}};
Object.assign(session,{ready:true,currentIndex:0,activityDiscontinuity(){},recordProgress(){saves++;},playbackRequested(){requests++;}});
const ctx={session,mpv};
session.requestAudioPlayback=load('requestAudioPlayback',ctx);
session.suspendForAudioHandoff=load('suspendForAudioHandoff',ctx);
session.playIndex=load('playIndex',ctx);
session.suspendForAudioHandoff();assert.equal(mpv.pause,true);assert.equal(requests,0);assert.equal(saves,1);
session.playIndex(1,true);assert.equal(loads,0,'late automatic advance cannot replace source');
session.playIndex(1);assert.equal(loads,1);assert.equal(requests,1);assert.equal(mpv.pause,false);
assert.equal(session.audioHandoffPaused,false);
assert.match(source,/if \(!session.ready\) return\n            if \(session.audioHandoffPaused\) mpv.pause = true/);
console.log('PASS actual audiobook handoff/EOF bodies: suspended and stopped callbacks, natural chapter/final EOF, explicit resume, load fence');
