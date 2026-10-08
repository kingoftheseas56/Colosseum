import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
const test = new URL('./feria_save_context_test.mjs', import.meta.url).pathname;
const variants = {
    context: 'new context must retire prior native target',
    document: 'same-URL new document must reject old context',
    identity: 'accountEpoch replacement rejects context',
    media: 'MediaCanSave is required even with enabled native action',
    serialization: 'initial page-save admission must serialize',
    frame: 'unqualified frame fidelity must fail closed',
    menu: 'resource native menu is replaced only when chrome controls exist',
    url: 'javascript:alert(1)'
};
for (const [variant, assertion] of Object.entries(variants)) {
    const run = spawnSync(process.execPath, [test], {encoding:'utf8',env:{...process.env,FERIA_SAVE_NEGATIVE:variant}});
    assert.notEqual(run.status,0,`${variant} mutant did not expose the missing fence`);
    assert.ok(run.stderr.includes(assertion),`${variant} failed for the wrong reason: ${run.stderr}`);
    console.log(`PASS negative ${variant}: ${assertion}`);
}
const restored = spawnSync(process.execPath,[test],{encoding:'utf8',env:{...process.env,FERIA_SAVE_NEGATIVE:''}});
assert.equal(restored.status,0,restored.stderr);
console.log(restored.stdout.trim());
