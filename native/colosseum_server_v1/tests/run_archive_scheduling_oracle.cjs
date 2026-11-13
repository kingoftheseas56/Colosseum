'use strict';
// Observe the pinned named queue, not a reimplementation of its semantics.
const fs = require('fs'), vm = require('vm'), assert = require('assert');
const {extractModules} = require('../../../tools/server_lab/oracle/module_loader.cjs');
const source = extractModules(process.argv[2]);
assert.equal(source.sha256, '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
function queueHarness() {
    const immediate = [], module = {exports: {}};
    vm.runInNewContext('(' + source.modules[72].source + ')', {setImmediate: cb => immediate.push(cb)})(module, module.exports);
    const starts = [], answers = [], running = new Map();
    const queue = new module.exports((task, done) => { starts.push(task.label); running.set(task.id, {task, done}); }, 10);
    const push = (id, label, callback) => queue.push({id, label}, answer => { answers.push([label, answer]); callback?.(); });
    const flush = () => { while (immediate.length) immediate.shift()(); };
    const complete = id => { const {task, done} = running.get(id); running.delete(id); done(task.label); };
    return {starts, answers, push, flush, complete};
}
const duplicate = queueHarness();
duplicate.push('same', 'first'); duplicate.push('same', 'second'); duplicate.flush(); duplicate.complete('same'); duplicate.flush();
assert.deepEqual(duplicate.starts, ['first']); assert.deepEqual(duplicate.answers, [['first','first'],['second','first']]);
const saturated = queueHarness();
for (let i=0;i<12;i++) saturated.push('key'+i, ''+i);
saturated.flush(); assert.equal(saturated.starts.length, 10);
saturated.complete('key0'); saturated.flush(); assert.equal(saturated.starts[10], '10');
saturated.complete('key1'); saturated.flush(); assert.equal(saturated.starts[11], '11');
const boundary = queueHarness();
for(let i=0;i<10;i++) boundary.push('key'+i, ''+i);
boundary.push('key9', 'queued-duplicate'); boundary.flush(); boundary.complete('key9'); boundary.flush();
assert.equal(boundary.starts[10], 'queued-duplicate');
const reentrant = queueHarness();
reentrant.push('key', 'owner', () => reentrant.push('key', 'reentrant'));reentrant.flush();reentrant.complete('key');reentrant.flush();
assert.deepEqual(reentrant.answers, [['owner','owner']]); // Source loses callback appended during forEach.
const families = [];
(async () => {
    // Isolate the exported task wrapper; dependencies are not exercised here.
    // The actual source chooses the queue's limit and task ID, including URL/options.
    for(const [family,id,parse] of [['rar',514,272],['7zip',561,278],['zip',546,276],['tar',562,279],['tgz',563,280]]) {
        const observed = {family, id, concurrency: null, ids: []};
        function Queue(processor, concurrency) { observed.concurrency=concurrency; this.push=(task, cb)=>{observed.ids.push(task.id);cb(null);}; }
        const m={exports:{}};
        vm.runInNewContext('('+source.modules[id].source+')', {console:{error(){}}})(m,m.exports,k=>k===72?Queue:k===parse?req=>req.task:{});
        for(const selector of [0,1]) await m.exports({url:'/stream?key=shared&o='+selector,task:{query:{key:'shared'},opts:{fileIdx:selector}}});
        assert.equal(observed.concurrency,10);assert.deepEqual(observed.ids,['shared','shared']);families.push(observed);
    }
    fs.writeFileSync(process.argv[3],JSON.stringify({sha256:source.sha256,families,duplicate:{starts:duplicate.starts,answers:duplicate.answers},saturated:{starts:saturated.starts},boundary:{starts:boundary.starts},reentrant:{answers:reentrant.answers}},null,2));
    console.log('Pinned M72: coalescing, 10-active FIFO, saturated duplicate and reentrant callback observations; five archive task wrappers');
})().catch(error=>{console.error(error);process.exit(1);});
