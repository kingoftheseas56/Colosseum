import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const path = new URL('../qml/feria/FeriaNativeMedia.js', import.meta.url);
const media = {};
if (fs.existsSync(path)) vm.runInNewContext(fs.readFileSync(path, 'utf8').replace(/^\.pragma library\s*/, ''), media);
assert.equal(typeof media.admit, 'function', 'Feria must admit explicit native playback candidates');
const candidate = {source:'https://cdn.example.test/video.mp4?token=abc',href:'https://example.test/watch',position:42,paused:false};
assert.equal(media.admit(candidate).ok, true);
assert.equal(media.admit({...candidate, encrypted:true}).ok, false, 'encrypted browser media stays in the browser');
assert.equal(media.admit({...candidate, source:'blob:https://example.test/id'}).ok, false, 'MSE/blob cannot be passed to mpv');
for (const source of ['file:///C:/private.txt','javascript:alert(1)','https://user:password@example.test/video','https://example.test/video\r\nX-Foo: bar','']) {
    assert.equal(media.admit({...candidate,source}).ok, false, 'reject unsafe or unavailable sources');
}
assert.equal(media.admit({...candidate,ad:true}).ok, false, 'advertisements cannot overwrite programme history');
assert.equal(media.admit({...candidate,position:-5}).position, 0);
assert.equal(media.admit({...candidate,position:Infinity}).position, 0);
assert.equal(media.admit({...candidate,source:'https://cdn.example.test/master.m3u8'}).ok, true);
const element={src:candidate.source,currentSrc:candidate.source,currentTime:42,duration:120,
    volume:1,muted:true,paused:false,playbackRate:1,isConnected:true,pause(){this.paused=true}};
const page={window:{},location:{href:candidate.href},document:{title:'Fixture',
    querySelector:()=>null,querySelectorAll:selector=>selector==='video'?[element]:[]}};
const probe=vm.runInNewContext(media.probe('handoff-a'),page);
assert.equal(probe.source,candidate.source,'literal probe executes in browser context');
assert.equal(probe.volume,0,'handoff preserves the website mute state');
assert.equal(vm.runInNewContext(media.pause('old-token'),page),false);
assert.equal(element.paused,false,'stale handoff cannot pause media');
assert.equal(vm.runInNewContext(media.pause('handoff-a'),page),true);
assert.equal(element.paused,true);
assert.equal(vm.runInNewContext(media.handBack('handoff-a',60,candidate.source),page),true);
assert.equal(element.currentTime,60,'handback transfers progress to the exact original element');
vm.runInNewContext(media.probe('handoff-b'),page);
element.currentSrc='https://cdn.example.test/replacement.mp4';
assert.equal(vm.runInNewContext(media.handBack('handoff-b',90,candidate.source),page),false);
assert.equal(element.currentTime,60,'a replacement video never receives the old progress');
console.log('FERIA_NATIVE_MEDIA_PASS');
