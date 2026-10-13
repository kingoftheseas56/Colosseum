import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { createHmac } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { Miniflare, convertV4MiniflareOptions } from 'miniflare';

const schema = JSON.parse(await readFile(new URL('../src/source-schema.json', import.meta.url), 'utf8'));
const admin = 'local-test-admin-secret';
const signingSecret = 'local-test-signing-secret-with-enough-length';

async function fixture(t) {
  const mf = new Miniflare(convertV4MiniflareOptions({
    modules: true, scriptPath: fileURLToPath(new URL('../dist/index.js', import.meta.url)),
    compatibilityDate: '2026-10-06',
    bindings: { ADMIN_TOKEN: admin, SESSION_SECRET: signingSecret, ENVIRONMENT: 'staging' },
    d1Databases: { CATALOG: 'catalog', METADATA: 'metadata' },
  }));
  t.after(() => mf.dispose());
  const catalog = await mf.getD1Database('CATALOG');
  const metadata = await mf.getD1Database('METADATA');
  for (const [db, directory] of [[catalog, 'catalog'], [metadata, 'metadata']]) {
    const files = (await readdir(new URL(`../migrations/${directory}/`, import.meta.url))).filter(file => file.endsWith('.sql')).sort();
    // D1 exec treats each newline as a statement boundary. Keep complete SQL on one line,
    // including trigger bodies, rather than splitting at their internal semicolons.
    for (const file of files) await db.exec((await readFile(new URL(`../migrations/${directory}/${file}`, import.meta.url), 'utf8')).replace(/\r?\n/g, ' '));
  }
  const request = (path, token, body) => mf.dispatchFetch(`https://test.invalid${path}`, {
    method: body === undefined ? 'GET' : 'POST',
    headers: { ...(token ? { Authorization: `Bearer ${token}` } : {}), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  return { request, catalog, metadata };
}

test('public health and authenticated short-lived read leases', async t => {
  const { request } = await fixture(t);
  assert.equal((await request('/healthz')).status, 200);
  assert.equal((await request('/v1/catalogues')).status, 401);
  assert.equal((await request('/v1/catalogues', admin)).status, 401);
  assert.equal((await request('/admin/session', 'incorrect', {})).status, 401);
  const response = await request('/admin/session', admin, {});
  assert.equal(response.status, 200);
  const lease = await response.json();
  assert.equal(typeof lease.token, 'string');
  assert.ok(lease.expiresAt);
  assert.equal((await request('/v1/catalogues', lease.token)).status, 200);
  const catalogue = await (await request('/v1/catalogues', lease.token)).json();
  assert.equal(catalogue.catalogues.length, Object.keys(schema).length);
  assert.ok(catalogue.catalogues.every(table => table.ready === false && table.importedRows === 0));
  assert.equal((await request('/v1/catalogues/imdb__meta', lease.token)).status, 503);
  assert.equal((await request('/admin/status', lease.token)).status, 401);
  assert.equal((await request('/v1/catalogues', `${lease.token}tampered`)).status, 401);
  const now = Math.floor(Date.now() / 1000);
  for (const claims of [
    { aud: 'content-api', scope: 'read', exp: now - 1 },
    { aud: 'content-api', scope: 'admin', exp: now + 60 },
    { aud: 'different-service', scope: 'read', exp: now + 60 },
    { aud: 'content-api', scope: 'read', exp: now + 3600 },
  ]) {
    const body = Buffer.from(JSON.stringify(claims)).toString('base64url');
    const token = `${body}.${createHmac('sha256', signingSecret).update(body).digest('base64url')}`;
    assert.equal((await request('/v1/catalogues', token)).status, 401);
  }
});

const chunk = (chunkId, rows) => ({ table: 'imdb__meta', sourceHash: schema.imdb__meta.sourceHash, chunkId, rows });

test('import replay, collision and exact completion preserve source truth', async t => {
  const { request, catalog } = await fixture(t);
  assert.equal((await request('/admin/complete', admin, { table: 'imdb__meta', sourceHash: schema.imdb__meta.sourceHash })).status, 409);
  const body = chunk('meta-1', [['one', 'value'], ['two', "O'Brien"]]);
  assert.equal((await request('/admin/import', admin, body)).status, 200);
  const budget = await catalog.prepare('SELECT reserved_writes FROM _import_budget').first();
  assert.equal((await request('/admin/import', admin, body)).status, 200);
  assert.deepEqual(await catalog.prepare('SELECT reserved_writes FROM _import_budget').first(), budget);
  assert.equal((await request('/admin/import', admin, chunk('meta-1', [['one', 'changed']]))).status, 409);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM imdb__meta').first('n'), 2);
  assert.equal((await request('/admin/complete', admin, { table: 'imdb__meta', sourceHash: schema.imdb__meta.sourceHash })).status, 200);
  assert.equal(await catalog.prepare("SELECT ready FROM _source_tables WHERE name='imdb__meta'").first('ready'), 1);
  const { token } = await (await request('/admin/session', admin, {})).json();
  const page = await (await request('/v1/catalogues/imdb__meta?limit=1', token)).json();
  assert.equal(page.items.length, 1);
  assert.equal(typeof page.nextCursor, 'number');
  const next = await (await request(`/v1/catalogues/imdb__meta?after=${page.nextCursor}&limit=1`, token)).json();
  assert.equal(next.items.length, 1);
  assert.equal(next.nextCursor, null);
  assert.equal(next.items[0].value, "O'Brien");
});

test('simultaneous chunks cannot exceed the manifest row count', async t => {
  const { request, catalog } = await fixture(t);
  const responses = await Promise.all([
    request('/admin/import', admin, chunk('race-a', [['a', 'value'], ['b', 'value']])),
    request('/admin/import', admin, chunk('race-b', [['c', 'value'], ['d', 'value']])),
  ]);
  assert.equal(responses.filter(r => r.status === 200).length, 1);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM imdb__meta').first('n'), 2);
  assert.equal(await catalog.prepare("SELECT imported_rows FROM _source_tables WHERE name='imdb__meta'").first('imported_rows'), 2);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM _import_chunks').first('n'), 1);
});

test('source-version mismatch blocks import, completion and catalogue readiness', async t => {
  const { request, catalog } = await fixture(t);
  await catalog.prepare("UPDATE _source_tables SET source_hash='different-source-version', ready=1 WHERE name='imdb__meta'").run();
  const { token } = await (await request('/admin/session', admin, {})).json();
  const listing = await (await request('/v1/catalogues', token)).json();
  assert.equal(listing.catalogues.find(table => table.name === 'imdb__meta').ready, false);
  const imported = await request('/admin/import', admin, chunk('wrong-version', [['one', 'value']]));
  assert.equal(imported.status, 409);
  assert.equal((await imported.json()).error, 'SOURCE_CHANGED');
  const completed = await request('/admin/complete', admin, { table: 'imdb__meta', sourceHash: schema.imdb__meta.sourceHash });
  assert.equal(completed.status, 409);
  assert.equal((await completed.json()).error, 'SOURCE_CHANGED');
  const page = await request('/v1/catalogues/imdb__meta', token);
  assert.equal(page.status, 503);
  assert.equal((await page.json()).error, 'CATALOGUE_NOT_READY');
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM imdb__meta').first('n'), 0);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM _import_chunks').first('n'), 0);
});

test('quota aborts source rows, ledger and count atomically', async t => {
  const { request, catalog } = await fixture(t);
  const day = new Date().toISOString().slice(0, 10);
  await catalog.prepare('INSERT INTO _import_budget(day,reserved_writes) VALUES (?,?)').bind(day, 59999).run();
  assert.equal((await request('/admin/import', admin, chunk('quota-test', [['one', 'value']]))).status, 429);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM imdb__meta').first('n'), 0);
  assert.equal(await catalog.prepare('SELECT COUNT(*) AS n FROM _import_chunks').first('n'), 0);
  assert.equal(await catalog.prepare("SELECT imported_rows FROM _source_tables WHERE name='imdb__meta'").first('imported_rows'), 0);
  assert.equal(await catalog.prepare('SELECT reserved_writes FROM _import_budget WHERE day=?').bind(day).first('reserved_writes'), 59999);
});

test('import validates table, source manifest, row width and chunk bounds', async t => {
  const { request } = await fixture(t);
  for (const body of [
    { ...chunk('invalid', [['one', 'value']]), table: 'imdb__meta; DROP TABLE imdb__meta' },
    { ...chunk('invalid', [['one', 'value']]), sourceHash: 'incorrect' },
    chunk('invalid', [['one']]),
    chunk('invalid', Array.from({ length: 33 }, (_, i) => [String(i), 'value'])),
  ]) assert.ok((await request('/admin/import', admin, body)).status >= 400);
  assert.equal((await request('/admin/import', admin, chunk('large', [['one', 'x'.repeat(300000)]]))).status, 413);
});

test('MusicBrainz enrichments are validated and cached locally', async t => {
  const { request } = await fixture(t);
  const { token } = await (await request('/admin/session', admin, {})).json();
  const mbid = '12345678-1234-1234-1234-123456789abc';
  const path = `/v1/enrichment/musicbrainz/artist/${mbid}`;
  assert.equal((await request(path, token)).status, 404);
  const body = { kind: 'artist', mbid, fetchedAt: new Date().toISOString(), sourceUrl: `https://musicbrainz.org/ws/2/artist/${mbid}?fmt=json`, payload: { id: mbid, name: 'Example' } };
  assert.equal((await request('/admin/enrichment', admin, body)).status, 200);
  assert.equal((await request(path, token)).status, 200);
  const cached = await (await request(path, token)).json();
  assert.deepEqual(cached.data, body.payload);
  assert.equal(cached.sourceUrl, body.sourceUrl);
  assert.equal(cached.fetchedAt, body.fetchedAt);
  assert.ok((await request('/admin/enrichment', admin, { ...body, sourceUrl: 'https://attacker.invalid/' })).status >= 400);
  assert.ok((await request('/admin/enrichment', admin, { ...body, mbid: 'bad-id' })).status >= 400);
  assert.equal((await request('/admin/enrichment', admin, { ...body, payload: { id: 'different-id' } })).status, 400);
  assert.equal((await request('/admin/enrichment', admin, { ...body, fetchedAt: 'not-a-date' })).status, 400);
  assert.equal((await request('/admin/enrichment', admin, { ...body, payload: null })).status, 400);
});
