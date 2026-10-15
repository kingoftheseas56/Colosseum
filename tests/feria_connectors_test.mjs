import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
const c=vm.createContext({window:{}});vm.runInContext(fs.readFileSync(new URL('../qml/feria/FeriaConnectors.js',import.meta.url),'utf8'),c);
const dispatch=vm.runInContext('('+c.registrySource+')',c);
const sample={href:'https://music.youtube.com/watch?v=abcdefghijk',title:'Track',position:20,duration:300,paused:false,readyState:4};
let genericCalls=0;const media={};c.window.__feriaProbeMedia=media;
const generic=()=>{genericCalls++;return sample};
assert.equal(dispatch('youtube',()=>{throw Error('must not dispatch')},generic).outcome,'not-applicable');
assert.equal(dispatch('ytmusic',null,generic).outcome,'not-applicable');
assert.equal(dispatch('ytmusic',()=>({outcome:'observation',sample}),generic).outcome,'observation');
assert.equal(dispatch('ytmusic',()=>({outcome:'observation',sample:{...sample,identityProvider:'netflix'}}),generic).outcome,'hold');
for(const reason of ['ad','transition','identity-conflict','malformed-clock']){
 const before=genericCalls;assert.equal(dispatch('ytmusic',()=>({outcome:'hold',reason}),generic).outcome,'hold');assert.equal(genericCalls,before);
}
assert.equal(dispatch('ytmusic',()=>{throw Error('failure')},generic).outcome,'error');assert.equal(genericCalls,0);
assert.equal(dispatch('ytmusic',()=>({outcome:'error',reason:'unknown'}),generic).outcome,'error');assert.equal(genericCalls,0);
const proof={provider:'ytmusic',media,href:sample.href,title:sample.title,position:20,duration:300};
assert.equal(dispatch('ytmusic',()=>({outcome:'error',fallbackProof:proof}),generic).outcome,'observation');assert.equal(genericCalls,1);
assert.equal(dispatch('ytmusic',()=>({outcome:'error',fallbackProof:{...proof,position:21}}),generic).outcome,'error');
console.log('FERIA_CONNECTORS_PASS trusted dispatch, uncovered fallback, spoof, four holds, uncertain/error proof boundary');
