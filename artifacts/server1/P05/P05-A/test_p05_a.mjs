import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

const { extractModules, loadBundle, verifyModuleIndex } = await import('../../../../tools/server_lab/oracle/module_loader.cjs');

const bundle = `!function(modules){var __webpack_require__=function(id){var m={exports:{}};modules[id](m,m.exports,__webpack_require__);return m.exports};return __webpack_require__(0)}([function(module,exports,__webpack_require__){exports.value=__webpack_require__(1).value+Date.now()+Math.random()},function(module,exports){exports.value=7}]);`;

test('extracts webpack modules with byte-exact source spans and hashes', async () => {
  const root = await mkdtemp(join(tmpdir(), 'p05-a-'));
  const path = join(root, 'bundle.js');
  await writeFile(path, bundle, 'utf8');
  const result = extractModules(path);
  assert.equal(result.modules.length, 2);
  assert.deepEqual(result.modules.map((m) => m.id), [0, 1]);
  assert.equal(Buffer.compare(result.source, Buffer.from(bundle)), 0);
  assert.equal(result.modules[1].source, "function(module,exports){exports.value=7}");
});

test('executes modules with deterministic Date.now and Math.random injection', async () => {
  const root = await mkdtemp(join(tmpdir(), 'p05-a-'));
  const path = join(root, 'bundle.js');
  await writeFile(path, bundle, 'utf8');
  const runtime = loadBundle(path, { now: () => 1700000000000, random: () => 0.25 });
  assert.equal(runtime.require(0).value, 1700000000007.25);
});

test('verifies every extracted module against an index and rejects edited source', async () => {
  const root = await mkdtemp(join(tmpdir(), 'p05-a-'));
  const path = join(root, 'bundle.js');
  const indexPath = join(root, 'MODULE-INDEX.json');
  await writeFile(path, bundle, 'utf8');
  const extracted = extractModules(path);
  await writeFile(indexPath, JSON.stringify({ oracleSha256: extracted.sha256, modules: extracted.modules.map(({ id, start, end, sha256 }) => ({ id, start, end, sha256 })) }));
  assert.equal(verifyModuleIndex(path, indexPath).ok, true);
  await writeFile(indexPath, JSON.stringify({ oracleSha256: extracted.sha256, modules: [{ id: 0, start: 0, end: 1, sha256: 'bad' }] }));
  assert.throws(() => verifyModuleIndex(path, indexPath), /module index mismatch/i);
});

test('publishes five distinct deterministic mutation fixtures for P05-B TraceComparator', async () => {
  const casePath = new URL('../../../../tools/server_lab/cases/P05-A.json', import.meta.url);
  const definition = JSON.parse(await readFile(casePath, 'utf8'));
  const fixtures = definition.mutation_fixtures;
  assert.equal(fixtures.length, 5);
  assert.deepEqual(fixtures.map(({ mutation }) => mutation.kind), [
    'callback-removal',
    'duplicate-terminal-event',
    'priority-reorder',
    'null-replaces-omitted-key',
    'wrong-cancellation-generation',
  ]);
  assert.equal(new Set(fixtures.map(({ id }) => id)).size, fixtures.length);
  assert.equal(new Set(fixtures.map((fixture) => JSON.stringify(fixture))).size, fixtures.length);
  for (const fixture of fixtures) {
    assert.equal(fixture.case_id, 'P05-01');
    assert.ok(Array.isArray(fixture.baseline_trace) && fixture.baseline_trace.length > 0);
    assert.ok(Array.isArray(fixture.mutated_trace) && fixture.mutated_trace.length > 0);
    assert.notDeepEqual(fixture.baseline_trace, fixture.mutated_trace);
    assert.match(fixture.expected_rejection.identity, /\S/);
    assert.match(fixture.expected_rejection.path, /^trace\[/);
    assert.match(fixture.expected_rejection.rule, /\S/);
  }
  const byKind = new Map(fixtures.map((fixture) => [fixture.mutation.kind, fixture]));
  assert.equal(byKind.get('callback-removal').baseline_trace.length, byKind.get('callback-removal').mutated_trace.length + 1);
  assert.equal(byKind.get('callback-removal').baseline_trace[2].kind, 'callback.completed');
  assert.equal(byKind.get('duplicate-terminal-event').mutated_trace.length, byKind.get('duplicate-terminal-event').baseline_trace.length + 1);
  assert.equal(byKind.get('duplicate-terminal-event').mutated_trace[1].identity, byKind.get('duplicate-terminal-event').mutated_trace[2].identity);
  assert.deepEqual(byKind.get('priority-reorder').mutated_trace.slice(0, 2).map(({ identity }) => identity), ['track:audio-main', 'track:video-main']);
  assert.equal(Object.prototype.hasOwnProperty.call(byKind.get('null-replaces-omitted-key').baseline_trace[0].payload, 'contentLength'), false);
  assert.equal(byKind.get('null-replaces-omitted-key').mutated_trace[0].payload.contentLength, null);
  assert.notEqual(byKind.get('wrong-cancellation-generation').baseline_trace[1].generation, byKind.get('wrong-cancellation-generation').mutated_trace[1].generation);
  assert.equal(byKind.get('wrong-cancellation-generation').mutated_trace[1].payload.target_generation, 6);
  assert.deepEqual(definition.fixture_consumer, {
    worker: 'P05-B',
    interface: 'TraceComparator',
    runner: 'OracleModuleTraceRunner',
    execution_owner: 'P05-B',
  });
  assert.deepEqual(definition.trace_context.module_ids, [564, 730, 874]);
  assert.equal(definition.trace_context.oracle_sha256, '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
});

test('extracts the authenticated oracle without re-minifying assigned modules', async () => {
  const oracle = 'C:/b/Colosseum-Server-1.0-Planning-Pack/oracle/stremio-service-v4.21.1-server-bundle/server.js';
  const result = extractModules(oracle);
  assert.equal(result.sha256, '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f');
  assert.equal(result.modules.length, 1312);
  assert.deepEqual(result.modules.filter(({ id }) => [564, 730, 874].includes(id)).map(({ id, sha256 }) => ({ id, sha256 })), [
    { id: 564, sha256: '02c4ccd2ce56543271b541484b376ab7fd11a04fecef0bdd656808fe96fac4cf' },
    { id: 730, sha256: '337242535632cf933991eb152f2686a8384ab458c4db9b5d3e8d7df50d173146' },
    { id: 874, sha256: '13051e26dfc0998d2abab73f11e015580f009798c262927a5b29246394979df8' },
  ]);
});
