import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';

const productPath = new URL('../qml/feria/FeriaPlayback.js', import.meta.url);
const testPath = new URL('./feria_presentation_test.mjs', import.meta.url);
const source = fs.readFileSync(productPath, 'utf8');
const output = process.env.FERIA_EVIDENCE_DIR || fs.mkdtempSync(path.join(os.tmpdir(), 'feria-presentation-controls-'));
fs.mkdirSync(output, {recursive: true});
const controls = [
    ['pending-ownership', 'ledger.resuming.forEach(function(record){revoke(record);record.resuming=false});ledger.resuming.clear()', 'void 0', 'superseded pending operations'],
    ['retired-frame', 'seen.indexOf(record.root)<0 || ', '', 'retired same-origin frame'],
    ['frame-document-at-settlement', 'binding.frame.contentDocument===binding.doc', 'true', 'frame document changes before settlement'],
    ['owned-pause', 'record.owned=false;ledger.paused.delete(record);++record.revision', '++record.revision', 'explicit pause on an already paused'],
    ['observed-playing', "allowed && !m.paused && !m.ended ? 'resumed' : 'blocked'", "allowed ? 'resumed' : 'blocked'", 'resolved play without observed playback'],
    ['rejected-play', "function(){settled(false)})", "function(){m.paused=false;settled(true)})", 'play rejection is observed'],
    ['pending-input', 'ledger.resuming.forEach(revoke)', 'void 0', 'page input during pending resume'],
    ['receipt-revision', 'record.revision!==revision || ', '', 'user pause while play is pending'],
    ['inaccessible-coverage', 'else ++result.coverage.inaccessibleFrames', 'else void 0', 'inaccessible frames are explicit'],
    ['bounded-coverage', 'result.coverage.truncated=true;return', 'return', 'bounded root inspection'],
    ['superseded-result', "' || ledger.serial!==' + Number(serial)", "' || false && ledger.serial!==' + Number(serial)", 'a superseded play Promise'],
];
const manifest = {baseline: '62057bfbdaa2305a05d2d3fb02b870bc9e826638',
    sourceSha256: crypto.createHash('sha256').update(source).digest('hex'), controls: []};
for (const [name, before, after, filter] of controls) {
    assert.equal(source.split(before).length - 1, 1, `${name}: mutation must match exactly once`);
    const mutant = source.replace(before, after), mutantPath = path.join(output, `${name}.js`);
    fs.writeFileSync(mutantPath, mutant);
    const red = spawnSync(process.execPath, [testPath.pathname], {encoding:'utf8',
        env: {...process.env, FERIA_PLAYBACK_SOURCE: mutantPath, FERIA_TEST_FILTER: filter}});
    fs.writeFileSync(path.join(output, `${name}-red.log`), red.stdout + red.stderr);
    assert.equal(red.status, 1, `${name}: mutation must fail its named assertion\n${red.stdout}${red.stderr}`);
    assert.match(red.stderr, /FAIL /);
    const green = spawnSync(process.execPath, [testPath.pathname], {encoding:'utf8',
        env: {...process.env, FERIA_PLAYBACK_SOURCE: productPath.pathname, FERIA_TEST_FILTER: filter}});
    fs.writeFileSync(path.join(output, `${name}-restored.log`), green.stdout + green.stderr);
    assert.equal(green.status, 0, `${name}: restored production must pass`);
    assert.match(green.stdout, /PASS /);
    manifest.controls.push({name, filter, mutationSha256:crypto.createHash('sha256').update(mutant).digest('hex'),
        negativeExit:red.status, restoredExit:green.status});
    console.log('DETECTED + RESTORED', name);
}
fs.writeFileSync(path.join(output, 'negative-controls.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log('Source identity and control output:', output);
