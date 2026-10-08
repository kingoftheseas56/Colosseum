import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
const product = new URL('../qml/feria/FeriaQtWebEngine.qml', import.meta.url);
const tests = new URL('./feria_presentation_wrapper_test.mjs', import.meta.url);
const source = fs.readFileSync(product, 'utf8');
const output = process.env.FERIA_EVIDENCE_DIR || fs.mkdtempSync(path.join(os.tmpdir(), 'feria-wrapper-controls-'));
fs.mkdirSync(output, {recursive: true});
const controls = [
    ['pending-dispatch', 'presentationResult = {operationId: generation + ":" + serial,\n            outcome: "pending", reason: "script-requested"}', 'void 0', 'dispatch retires'],
    ['missing-result', 'if (!result) result = {operationId: generation + ":" + serial,\n            outcome: "failed", reason: "missing-script-result"}', 'if (!result) return', 'missing script result'],
    ['denied-dispatch', 'if (!evaluateJavaScript(Playback.presentationMedia(activitySuspended', 'if (evaluateJavaScript(Playback.presentationMedia(activitySuspended', 'denied script dispatch'],
    ['operation-callback', '|| serial !== presentationSerial) return', '|| false) return', 'old operation callback'],
    ['renderer-callback', 'if (closingSession || rendererLost || generation !== navigationGeneration', 'if (closingSession || generation !== navigationGeneration', 'renderer loss rejects'],
];
const manifest = {sourceSha256:crypto.createHash('sha256').update(source).digest('hex'),controls:[]};
for (const [name, before, after, assertion] of controls) {
    assert.equal(source.split(before).length - 1, 1, name);
    const mutant = source.replace(before, after), mutantPath = path.join(output, name + '.qml');
    fs.writeFileSync(mutantPath, mutant);
    const red = spawnSync(process.execPath,[tests.pathname], {encoding:'utf8',env:{...process.env,FERIA_WRAPPER_SOURCE:mutantPath}});
    fs.writeFileSync(path.join(output,name + '-red.log'),red.stdout+red.stderr);
    assert.equal(red.status,1); assert.ok(red.stderr.includes('FAIL ' + assertion), `${name}: expected assertion must fail`);
    const green = spawnSync(process.execPath,[tests.pathname], {encoding:'utf8',env:{...process.env,FERIA_WRAPPER_SOURCE:product.pathname}});
    fs.writeFileSync(path.join(output,name + '-restored.log'),green.stdout+green.stderr);
    assert.equal(green.status,0);
    manifest.controls.push({name,assertion,negativeExit:red.status,restoredExit:green.status,
        mutationSha256:crypto.createHash('sha256').update(mutant).digest('hex')});
    console.log('DETECTED + RESTORED',name);
}
fs.writeFileSync(path.join(output,'negative-controls.json'),JSON.stringify(manifest,null,2)+'\n');
