import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
const test = new URL('./feria_save_context_test.mjs', import.meta.url).pathname;
const variants = {
    advertised_capability: 'production-disabled PageSave must not advertise an enabled action',
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

const inputTest = new URL('./feria_save_controls_test.mjs', import.meta.url).pathname;
const inputMutant = spawnSync(process.execPath,[inputTest],{encoding:'utf8',env:{...process.env,FERIA_CONTEXT_CLICK_NEGATIVE:'1'}});
assert.notEqual(inputMutant.status,0,'retargeted context-button click was not caught');
assert.ok(inputMutant.stderr.includes('must dispatch the pressed context, never the replacement'),inputMutant.stderr);
for (const [variant, assertion] of [['owner','must retain the original context owner'],['kind','must retain the pressed semantic action kind']]) {
    const changed = spawnSync(process.execPath,[inputTest],{encoding:'utf8',env:{...process.env,FERIA_CONTEXT_CLICK_NEGATIVE:variant}});
    assert.notEqual(changed.status,0,`${variant} context receipt mutant was not caught`);
    assert.ok(changed.stderr.includes(assertion),changed.stderr);
    console.log(`PASS negative context-button ${variant}`);
}
const inputRestored = spawnSync(process.execPath,[inputTest],{encoding:'utf8',env:{...process.env,FERIA_CONTEXT_CLICK_NEGATIVE:''}});
assert.equal(inputRestored.status,0,inputRestored.stderr);
console.log('PASS negative context-button press/click retargeting');
console.log(inputRestored.stdout.trim());
