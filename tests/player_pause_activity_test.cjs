const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../qml/PlayerPage.qml'), 'utf8').replace(/\r\n/g, '\n');
const body = source.match(/onPauseChanged:\s*\{([\s\S]*?)\n        \}\n        onSpeedChanged:/)[1];
const handler = new Function('root', 'mpv', 'activityTracker', body);
for (const pause of [true, false]) {
 const calls = [];
 const root = {starting:false, fileReady:true};
 for (const name of ['sendTraktPlayback','wakeChrome','syncPowerInhibit','detectStubStream','syncWatchPartyPlayerObservation','activityDiscontinuity'])
  root[name] = (...args) => calls.push([name,...args]);
 const tracker={playbackStateChanged: (...args)=>calls.push(['state',...args])};
 handler(root,{pause,position:3.25,duration:12},tracker);
 assert.deepEqual(calls.find(c=>c[0]==='state'),['state',!pause,3250,12000]);
 assert(calls.some(c=>c[0]==='activityDiscontinuity'));
 assert(calls.some(c=>c[0]==='syncPowerInhibit'));
}
console.log('Player pause/resume activity regression: PASS');
