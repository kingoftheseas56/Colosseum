'use strict';
// Differentially exercise the exact pinned M612 function, not a rewritten oracle.
const {extractModules} = require('../../../tools/server_lab/oracle/module_loader.cjs');
const vm = require('node:vm');
const {EventEmitter} = require('node:events');
const {execFileSync} = require('node:child_process');
const assert = require('node:assert/strict');
const [bundle, candidate] = process.argv.slice(2);
if (!bundle || !candidate) throw new Error('usage: compare_peer_search_duplicates.cjs pinned-server.js test_peer_search');
const extracted = extractModules(bundle);
assert.equal(extracted.sha256, '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
const source = extracted.modules.find(m => m.source.includes('x.numFoundUniq++') && m.source.includes('swarm.peerSearch = self')).source;
const instances=[];
class Source extends EventEmitter {constructor(){super();instances.push(this);}run(){++this.numRequests;}pause(){}}
const context=vm.createContext({setInterval:()=>1,clearInterval:()=>{},Date,console});
const moduleObject={exports:{}};
new vm.Script('('+source+')').runInContext(context)(moduleObject,moduleObject.exports,id=>id===4?{EventEmitter}:Source);
const swarm=new EventEmitter();swarm.infoHash='abc';const adds=[];swarm.add=addr=>adds.push(addr);
const search=new moduleObject.exports(['tracker:http://fixture','dht:abc'],swarm,{});
for(const [index,peer] of [[0,'1.2.3.4:5'],[1,'1.2.3.4:5'],[1,'2.3.4.5:6'],[0,'1.2.3.4:5']])instances[index].emit('peer',peer);
const expected={adds,stats:search.stats().map(x=>[x.numFound,x.numFoundUniq])};
const actual=JSON.parse(execFileSync(candidate,['K09-duplicate-trace'],{encoding:'utf8'}));
assert.deepEqual(actual,JSON.parse(JSON.stringify(expected)));
console.log('Pinned M612 duplicate observation trace matches:',JSON.stringify(expected));
