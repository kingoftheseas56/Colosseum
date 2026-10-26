import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import {
  createWorker,
  manifest,
  MAX_QUERY_LENGTH,
  MAX_REQUEST_BYTES,
  MAX_RESULTS,
  MAX_UPSTREAM_BYTES,
  UPSTREAM_TIMEOUT_MS,
  parseNyaaRss
} from '../src/worker.mjs';

const fixture = readFileSync(new URL('./fixtures/nyaa-search.xml', import.meta.url), 'utf8');
const HASH = 'abcdef0123456789abcdef0123456789abcdef01';

function request(path, body, headers = {}) {
  return new Request(`https://worker.test${path}`, {
    method: body === undefined ? 'GET' : 'POST',
    headers: body === undefined ? headers : { 'content-type': 'application/json', ...headers },
    body: body === undefined ? undefined : body
  });
}

function jsonRequest(path, value, headers = {}) {
  return request(path, JSON.stringify(value), headers);
}

async function readJson(response) {
  return response.json();
}

function rssItem(id, { title = `Item ${id}`, hash = HASH, size = '1 MiB', uploader = 'seed' } = {}) {
  return `<item><title>${title}</title><link>https://nyaa.si/view/${id}</link><dc:creator>${uploader}</dc:creator><nyaa:seeders>2</nyaa:seeders><nyaa:leechers>1</nyaa:leechers><nyaa:infoHash>${hash}</nyaa:infoHash><nyaa:size>${size}</nyaa:size></item>`;
}

function rss(items = '') {
  return `<rss xmlns:nyaa="https://nyaa.si/xmlns/nyaa" xmlns:dc="http://purl.org/dc/elements/1.1/"><channel>${items}</channel></rss>`;
}

test('health and manifest expose stable Nyaa service identity', async () => {
  const worker = createWorker({ fetchImpl: async () => { throw new Error('unexpected fetch'); } });
  const health = await worker.fetch(request('/healthz'));
  assert.equal(health.status, 200);
  assert.deepEqual(await readJson(health), { ok: true, contractVersion: 1, revision: manifest.version });

  const response = await worker.fetch(request('/manifest.json'));
  const body = await readJson(response);
  assert.equal(body.id, 'colosseum.well.nyaa');
  assert.equal(body.contractVersion, 1);
  assert.deepEqual(body.sources, [{ id: 'nyaa', name: 'Nyaa' }]);
  assert.equal((await worker.fetch(jsonRequest('/healthz', {}))).status, 405);
});

test('search uses fixed upstream query parameters and maps namespaced RSS fields', async () => {
  const query = 'Demon Slayer: 100% & #2?+ café';
  let sent;
  const worker = createWorker({ fetchImpl: async (input, init) => {
    sent = { url: new URL(input), init };
    return new Response(fixture, { headers: { 'content-type': 'application/rss+xml' } });
  } });
  const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query }));
  const body = await readJson(response);

  assert.equal(response.status, 200);
  assert.deepEqual(sent.url.searchParams.get('page'), 'rss');
  assert.deepEqual(sent.url.searchParams.get('c'), '3_1');
  assert.deepEqual(sent.url.searchParams.get('s'), 'seeders');
  assert.deepEqual(sent.url.searchParams.get('o'), 'desc');
  assert.deepEqual(sent.url.searchParams.get('q'), query);
  assert.equal(sent.url.origin, 'https://nyaa.si');
  assert.equal(sent.url.pathname, '/');
  assert.equal(sent.init.method, 'GET');
  assert.equal(sent.init.redirect, 'manual');
  assert.equal(sent.init.signal instanceof AbortSignal, true);
  assert.equal(body.sourceId, 'nyaa');
  assert.deepEqual(body.result, [{
    title: '[Group] A title & an extra scene',
    infoHash: 'abcdef0123456789abcdef0123456789abcdef01',
    uploader: 'uploader & team',
    seeders: 42,
    leechers: 7,
    sizeBytes: Math.round(1.5 * 1024 ** 3),
    torrentUrl: 'https://nyaa.si/download/1234567.torrent'
  }]);
});

test('size parser handles decimal and binary units and rejects unsafe values', () => {
  const rows = parseNyaaRss(rss([
    rssItem(1, { size: '1.5 GiB' }),
    rssItem(2, { size: '512 MB' }),
    rssItem(3, { size: '999999999999999999999 PiB' })
  ].join('')));
  assert.deepEqual(rows.map(row => row.sizeBytes), [Math.round(1.5 * 1024 ** 3), 512_000_000, 0]);
});

test('RSS rows are capped at 100', () => {
  const rows = parseNyaaRss(rss(Array.from({ length: MAX_RESULTS + 15 }, (_, index) => rssItem(index + 1)).join('')));
  assert.equal(rows.length, 100);
  assert.equal(rows[0].torrentUrl, 'https://nyaa.si/download/1.torrent');
  assert.equal(rows.at(-1).torrentUrl, 'https://nyaa.si/download/100.torrent');
});

test('empty RSS is a successful empty result', async () => {
  const worker = createWorker({ fetchImpl: async () => new Response(rss()) });
  const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'no matches' }));
  assert.equal(response.status, 200);
  assert.deepEqual(await readJson(response), { contractVersion: 1, sourceId: 'nyaa', result: [] });
});

test('malformed RSS and DTD declarations fail closed without external resolution', async () => {
  for (const body of ['<rss><channel><item></channel></rss>', '<!DOCTYPE rss [<!ENTITY x SYSTEM "https://evil.example/secret">]><rss><channel/></rss>']) {
    let calls = 0;
    const worker = createWorker({ fetchImpl: async () => { calls += 1; return new Response(body); } });
    const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'unsafe XML' }));
    assert.equal(response.status, 502);
    const payload = await readJson(response);
    assert.equal(payload.contractVersion, 1);
    assert.equal(payload.sourceId, 'nyaa');
    assert.equal(typeof payload.error, 'string');
    assert.equal(Object.hasOwn(payload, 'result'), false);
    assert.equal(calls, 1);
  }
});

test('invalid API input and oversized request bodies return stable Nyaa errors before fetching', async () => {
  let calls = 0;
  const worker = createWorker({ fetchImpl: async () => { calls += 1; return new Response(rss()); } });

  const invalidJson = await worker.fetch(request('/v1/nyaa/search', '{'));
  assert.equal(invalidJson.status, 400);
  assert.equal((await readJson(invalidJson)).sourceId, 'nyaa');

  const tooLong = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'x'.repeat(MAX_QUERY_LENGTH + 1) }));
  assert.equal(tooLong.status, 400);
  assert.equal((await readJson(tooLong)).sourceId, 'nyaa');

  const tooLarge = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'x', ignored: 'x'.repeat(MAX_REQUEST_BYTES) }));
  assert.equal(tooLarge.status, 413);
  assert.equal((await readJson(tooLarge)).sourceId, 'nyaa');
  assert.equal(calls, 0);
});

test('query length boundary is accepted and query URLs cannot select an upstream host', async () => {
  let sentUrl;
  const worker = createWorker({ fetchImpl: async input => {
    sentUrl = new URL(input);
    return new Response(rss());
  } });
  const query = `https://evil.example/?q=${'x'.repeat(MAX_QUERY_LENGTH - 25)}`;
  const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query }));
  assert.equal(response.status, 200);
  assert.equal(sentUrl.origin, 'https://nyaa.si');
  assert.equal(sentUrl.searchParams.get('q'), query);
});

test('upstream failures and redirects produce structured errors and never follow', async () => {
  const failed = createWorker({ fetchImpl: async () => new Response('busy', { status: 503 }) });
  const failure = await failed.fetch(jsonRequest('/v1/nyaa/search', { query: 'failure' }));
  assert.equal(failure.status, 502);
  assert.deepEqual(await readJson(failure), {
    contractVersion: 1, sourceId: 'nyaa', error: 'Nyaa returned HTTP 503'
  });

  let calls = 0;
  const redirected = createWorker({ fetchImpl: async () => {
    calls += 1;
    return new Response(null, { status: 302, headers: { location: 'https://evil.example/steal' } });
  } });
  const redirect = await redirected.fetch(jsonRequest('/v1/nyaa/search', { query: 'redirect' }));
  assert.equal(redirect.status, 502);
  assert.equal((await readJson(redirect)).error, 'Nyaa redirects are not allowed');
  assert.equal(calls, 1);
});

test('upstream response is bounded to 1 MiB without relying on Content-Length', async () => {
  const tooLargeStream = new ReadableStream({
    start(controller) {
      controller.enqueue(new Uint8Array(MAX_UPSTREAM_BYTES + 1));
      controller.close();
    }
  });
  const worker = createWorker({ fetchImpl: async () => new Response(tooLargeStream) });
  const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'large feed' }));
  assert.equal(response.status, 502);
  assert.deepEqual(await readJson(response), {
    contractVersion: 1, sourceId: 'nyaa', error: 'Nyaa response is too large'
  });
});

test('upstream request deadline returns a stable timeout error', async () => {
  assert.equal(UPSTREAM_TIMEOUT_MS, 15_000);
  const worker = createWorker({ timeoutMs: 5, fetchImpl: async () => new Promise(() => {}) });
  const response = await worker.fetch(jsonRequest('/v1/nyaa/search', { query: 'slow upstream' }));
  assert.equal(response.status, 504);
  assert.deepEqual(await readJson(response), {
    contractVersion: 1, sourceId: 'nyaa', error: 'Nyaa upstream request timed out'
  });
});

test('uploader aliases prefer explicit uploader then author over namespace creator', () => {
  const row = parseNyaaRss(rss(`<item><title>Alias priority</title><link>https://nyaa.si/download/77.torrent</link><nyaa:uploader>preferred uploader</nyaa:uploader><dc:creator>creator fallback</dc:creator><nyaa:infoHash>${HASH.toUpperCase()}</nyaa:infoHash><nyaa:size>10 bytes</nyaa:size></item>`))[0];
  assert.equal(row.uploader, 'preferred uploader');
  assert.equal(row.torrentUrl, 'https://nyaa.si/download/77.torrent');
  const guidFallback = parseNyaaRss(rss(`<item><title>GUID fallback</title><link>https://nyaa.si/view/unsafe?x=1</link><guid>https://nyaa.si/view/78</guid><nyaa:infoHash>${HASH}</nyaa:infoHash><nyaa:size>10 bytes</nyaa:size></item>`))[0];
  assert.equal(guidFallback.torrentUrl, 'https://nyaa.si/download/78.torrent');
});

test('request body reading has a bounded wall time', async () => {
  const worker = createWorker({ bodyTimeoutMs: 5, fetchImpl: async () => { throw new Error('must not fetch'); } });
  const body = new ReadableStream({ start() {} });
  const request = new Request('https://worker.test/v1/nyaa/search', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body,
    duplex: 'half'
  });
  const response = await worker.fetch(request);
  assert.equal(response.status, 408);
  assert.deepEqual(await readJson(response), {
    contractVersion: 1, sourceId: 'nyaa', error: 'Request body timed out'
  });
});
