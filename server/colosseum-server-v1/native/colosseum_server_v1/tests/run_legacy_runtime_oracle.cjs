'use strict';
// Execute exact pinned legacy module 305 and the /probe handler extracted from
// module 564. Uses real child-process events and a loopback-only process fixture.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {loadBundle} = require('../../../tools/server_lab/oracle/module_loader.cjs');
const bundle = loadBundle(process.argv[2]);
assert.equal(bundle.sha256, '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
const fixture = path.resolve(process.argv[3] || path.join(__dirname, 'legacy-runtime-fixture.py'));
const output = process.argv[4];
const legacy = bundle.require(305);
const match = bundle.modules[564].source.match(/enginefs\.router\.get\("\/probe", \((function\(req, res, next\) \{[\s\S]*?)\)\), enginefs\.router\.get\("\/tracks\/:url"/);
assert.ok(match, 'find exact /probe handler in pinned module 564');
const handler = vm.runInNewContext('(' + match[1] + ')', {
    url: require('node:url'), HLS: legacy,
    enginefs: {baseUrlLocal: 'http://127.0.0.1:1'}, console: {error() {}}
});
const rows = [];
async function run(name, executable, source, expectedStatus, expectedBody) {
    legacy.init({ffmpeg: executable});
    const response = await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error('oracle deadline ' + name)), 25000);
        let status, headers;
        handler({url: '/probe?url=' + encodeURIComponent(source)}, {
            writeHead(code, values) { status = code; headers = values; },
            end(body) { clearTimeout(timeout); resolve({status, headers, body: body === undefined ? '' : String(body)}); }
        });
    });
    assert.equal(response.status, expectedStatus, name + ' status');
    assert.equal(response.body, expectedBody, name + ' body');
    rows.push({name, ...response});
    console.log('PASS pinned /probe ' + name + ': ' + JSON.stringify(response));
}
(async () => {
    await run('early missing executable', '', 'fixture://unused', 500, '');
    await run('spawn failure', fixture + '.missing', 'fixture://unused', 500, 'null');
    await run('zero-exit invalid probe', fixture, 'fixture://invalid-probe', 200, '{"streams":[]}');
    await run('20-second timeout', fixture, 'fixture://hang-probe-oracle', 500, 'null');
    const result = {oracle: bundle.sha256, modules: [305, 564], cases: rows};
    if (output) fs.writeFileSync(output, JSON.stringify(result, null, 2) + '\n');
    console.log(JSON.stringify({oracle: bundle.sha256, cases: rows.length}));
})().catch(error => { console.error(error); process.exitCode = 1; });
