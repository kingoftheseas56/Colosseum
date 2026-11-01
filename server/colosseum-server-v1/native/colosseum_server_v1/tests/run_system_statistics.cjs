'use strict';

// Test-only JavaScript: evaluate the sys payload expression from the exact
// locked bundle. The production implementation and backend remain native C++.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const { execFileSync } = require('node:child_process');
const vm = require('node:vm');

const [oracle, candidate, backend] = process.argv.slice(2);
if (!oracle || !candidate || !backend) {
    throw new Error('usage: node run_system_statistics.cjs ORACLE CANDIDATE BACKEND');
}
const source = fs.readFileSync(oracle, 'utf8');
assert.equal(crypto.createHash('sha256').update(source).digest('hex'),
    '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f',
    'only the locked v4.21.1 oracle is accepted');
const sourceMatch = source.match(/stats\.sys = (\{\s*loadavg: os\.loadavg\(\),\s*cpus: os\.cpus\(\)\s*\})/);
assert(sourceMatch, 'locked sys statistics expression not found');
const expression = new vm.Script(`(${sourceMatch[1]})`);
function evaluate(backend) {
    return JSON.parse(JSON.stringify(expression.runInNewContext({ os: backend })));
}
function run(executable, mode) {
    return execFileSync(executable, [mode], { encoding: 'utf8', timeout: 30000, maxBuffer: 8 * 1024 * 1024 }).trim();
}
function shape(value) {
    assert.deepEqual(Object.keys(value), ['loadavg', 'cpus']);
    assert(Array.isArray(value.loadavg));
    assert.equal(value.loadavg.length, 3);
    const number = (value) => {
        assert.equal(typeof value, 'number');
        assert(Number.isFinite(value) && value >= 0);
    };
    value.loadavg.forEach(number);
    assert(Array.isArray(value.cpus));
    for (const cpu of value.cpus) {
        assert.deepEqual(Object.keys(cpu), ['model', 'speed', 'times']);
        assert.equal(typeof cpu.model, 'string');
        number(cpu.speed);
        assert(Number.isInteger(cpu.speed));
        assert.deepEqual(Object.keys(cpu.times), ['user', 'nice', 'sys', 'idle', 'irq']);
        for (const time of Object.values(cpu.times)) {
            number(time);
            assert(Number.isInteger(time));
        }
    }
}

const cpus = [
    { model: 'CPU "first"\n\u00ae', speed: 3200, times: { user: 11, nice: 22, sys: 33, idle: 44, irq: 55 } },
    { model: '', speed: 0, times: { user: Number(9007199254740993n), nice: 0, sys: 1,
        idle: Number(18446744073709551615n), irq: 2 } },
];
for (const mode of ['--fixture', '--empty', '--failure', '--windows']) {
    const fixtureCpus = structuredClone(cpus);
    if (mode === '--windows') fixtureCpus[0].times.nice = 0;
    const expected = evaluate({
        loadavg: () => mode === '--windows' ? [0, 0, 0] : [1.25, 2.5, 3.75],
        cpus: () => ['--empty', '--failure'].includes(mode) ? [] : fixtureCpus,
    });
    const actual = JSON.parse(run(backend, mode));
    shape(actual);
    assert.deepEqual(actual, expected, `deterministic source mismatch ${mode}`);
}

const candidateUv = run(candidate, '--uv-version');
assert.equal(candidateUv, process.versions.uv,
    'live differential requires the same libuv version; use a matching Node test runtime');
let cpuCount = 0;
let changedSpeeds = 0;
let maximumSpeedDeltaMhz = 0;
const samples = 5;
for (let sample = 0; sample < samples; ++sample) {
    const before = evaluate(os);
    const actual = JSON.parse(run(candidate, '--json'));
    const after = evaluate(os);
    shape(actual);
    assert.equal(actual.cpus.length, before.cpus.length, 'CPU count differs from source');
    assert.equal(actual.cpus.length, after.cpus.length, 'CPU count changed during sample');
    cpuCount = actual.cpus.length;
    for (let average = 0; average < 3; ++average) {
        assert(actual.loadavg[average] >= Math.min(before.loadavg[average], after.loadavg[average])
            && actual.loadavg[average] <= Math.max(before.loadavg[average], after.loadavg[average]),
        `load average ${average} not bracketed by source readings`);
    }
    for (let cpu = 0; cpu < actual.cpus.length; ++cpu) {
        const current = actual.cpus[cpu];
        assert.equal(current.model, before.cpus[cpu].model, `CPU ${cpu} model/order`);
        assert.equal(current.model, after.cpus[cpu].model, `CPU ${cpu} model changed`);
        for (const [field, time] of Object.entries(current.times)) {
            // Kernel accounting can be refreshed by different readers one tick
            // apart. No arbitrary percentage/elapsed-time tolerance is used.
            const tickAllowanceMs = 10;
            assert(time >= before.cpus[cpu].times[field] - tickAllowanceMs
                && time <= after.cpus[cpu].times[field] + tickAllowanceMs,
            `CPU ${cpu} ${field} not bracketed by source samples`);
        }
        // Frequency scaling is instantaneous and non-monotonic. Verify its
        // exact mapping in the deterministic cases, and report live differences
        // rather than pretending separately sampled MHz must be equal.
        if (current.speed !== before.cpus[cpu].speed) ++changedSpeeds;
        maximumSpeedDeltaMhz = Math.max(maximumSpeedDeltaMhz,
            Math.abs(current.speed - before.cpus[cpu].speed));
    }
}
console.log(JSON.stringify({
    result: 'passed', deterministicCases: 4, liveSamples: samples, cpuCount,
    node: process.version, libuv: candidateUv, platform: process.platform,
    changedSpeeds, maximumSpeedDeltaMhz,
    qualification: 'native live readings compared against locked source on this host; intended Stremio runtime and other platforms are not qualified',
}));
