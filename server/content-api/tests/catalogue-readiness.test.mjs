import test from 'node:test';
import assert from 'node:assert/strict';
import {libraryReady} from '../src/catalogue-readiness.ts';
test('readiness caches only success, scopes binding/revision, never serves initially unready',async()=>{
 let calls=0,state={revision:'one',ready:0};
 const db={prepare(){calls++;return{async all(){return{results:[state],meta:{rows_read:1}}}}}};
 assert.equal(await libraryReady(db,'one'),false);assert.equal(await libraryReady(db,'one'),false);assert.equal(calls,2);
 state={revision:'one',ready:1};assert.equal(await libraryReady(db,'one'),true);assert.equal(calls,3);
 assert.equal(await libraryReady(db,'one'),true);assert.equal(calls,3); // zero reads on warmed static/cache paths
 assert.equal(await libraryReady(db,'two'),false);assert.equal(calls,4);
 state={revision:'two',ready:1};assert.equal(await libraryReady(db,'two'),true);assert.equal(calls,5);
 assert.equal(await libraryReady({prepare:db.prepare},'two'),true);assert.equal(calls,6);
});
