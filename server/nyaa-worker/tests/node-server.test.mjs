import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { request as httpRequest } from 'node:http';
import test from 'node:test';
import { listenNodeServer } from '../src/node-server.mjs';

const fixture = readFileSync(new URL('./fixtures/nyaa-search.xml', import.meta.url), 'utf8');
const HASH = 'abcdef0123456789abcdef0123456789abcdef01';

async function withServer(options, action) {
  const host = await listenNodeServer({ host: '127.0.0.1', port: 0, ...options });
  try {
    return await action(`http://127.0.0.1:${host.address.port}`, host);
  } finally {
    await host.close();
  }
}

test('real HTTP adapter preserves health, manifest, and Nyaa search contracts', async () => {
  await withServer({ fetchImpl: async () => new Response(fixture) }, async base => {
    const health = await fetch(`${base}/healthz`);
    assert.equal(health.status, 200);
    assert.deepEqual(await health.json(), { ok: true, contractVersion: 1, revision: '1.0.0' });

    const manifest = await (await fetch(`${base}/manifest.json`)).json();
    assert.equal(manifest.id, 'colosseum.well.nyaa');
    assert.equal(manifest.contractVersion, 1);

    const response = await fetch(`${base}/v1/nyaa/search`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ query: 'adapter test' })
    });
    assert.equal(response.status, 200);
    const body = await response.json();
    assert.equal(body.contractVersion, 1);
    assert.equal(body.sourceId, 'nyaa');
    assert.equal(body.result[0].infoHash, HASH);
    assert.equal(body.result[0].torrentUrl, 'https://nyaa.si/download/1234567.torrent');
  });
});

test('real HTTP method, route, and content-type errors retain structured responses', async () => {
  await withServer({ fetchImpl: async () => { throw new Error('must not fetch'); } }, async base => {
    const method = await fetch(`${base}/v1/nyaa/search`);
    assert.equal(method.status, 405);
    assert.deepEqual(await method.json(), { contractVersion: 1, sourceId: 'nyaa', error: 'Method not allowed' });

    const type = await fetch(`${base}/v1/nyaa/search`, { method: 'POST', body: '{}' });
    assert.equal(type.status, 415);
    assert.deepEqual(await type.json(), { contractVersion: 1, sourceId: 'nyaa', error: 'Content-Type must be application/json' });

    const missing = await fetch(`${base}/missing`);
    assert.equal(missing.status, 404);
    assert.equal((await missing.json()).sourceId, 'nyaa');
  });
});

test('HTTP parser rejects headers above the configured 16 KiB limit', async () => {
  await withServer({ fetchImpl: async () => { throw new Error('must not fetch'); } }, async base => {
    const { port } = new URL(base);
    const status = await new Promise((resolve, reject) => {
      const request = httpRequest({
        host: '127.0.0.1', port: Number(port), path: '/healthz',
        headers: { 'x-oversized': 'x'.repeat(20 * 1024) }
      }, response => {
        response.resume();
        response.on('end', () => resolve(response.statusCode));
        response.on('error', reject);
      });
      request.on('error', reject);
      request.end();
    });
    assert.equal(status, 431);
  });
});

test('chunked oversized body is bounded by the shared 8 KiB handler validation', async () => {
  let upstreamCalls = 0;
  const body = JSON.stringify({ query: 'x', ignored: 'y'.repeat(9 * 1024) });
  await withServer({ fetchImpl: async () => { upstreamCalls += 1; return new Response(fixture); } }, async base => {
    const chunks = [body.slice(0, 3000), body.slice(3000, 7000), body.slice(7000)];
    const response = await fetch(`${base}/v1/nyaa/search`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: new ReadableStream({
        start(controller) {
          for (const chunk of chunks) controller.enqueue(new TextEncoder().encode(chunk));
          controller.close();
        }
      }),
      duplex: 'half'
    });
    assert.equal(response.status, 413);
    assert.deepEqual(await response.json(), { contractVersion: 1, sourceId: 'nyaa', error: 'Request body is too large' });
    assert.equal(upstreamCalls, 0);
  });
});

test('upstream timeout is returned through the Node HTTP adapter', async () => {
  await withServer({ timeoutMs: 10, fetchImpl: async () => new Promise(() => {}) }, async base => {
    const response = await fetch(`${base}/v1/nyaa/search`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ query: 'slow feed' })
    });
    assert.equal(response.status, 504);
    assert.deepEqual(await response.json(), { contractVersion: 1, sourceId: 'nyaa', error: 'Nyaa upstream request timed out' });
  });
});

test('slow chunked request body returns the shared body timeout error', async () => {
  await withServer({ bodyTimeoutMs: 15, fetchImpl: async () => { throw new Error('must not fetch'); } }, async base => {
    const { port } = new URL(base);
    const result = await new Promise((resolve, reject) => {
      const request = httpRequest({
        host: '127.0.0.1', port: Number(port), path: '/v1/nyaa/search', method: 'POST',
        headers: { 'content-type': 'application/json' }
      }, response => {
        const chunks = [];
        response.on('data', chunk => chunks.push(chunk));
        response.on('end', () => resolve({ status: response.statusCode, body: Buffer.concat(chunks).toString('utf8') }));
        response.on('error', reject);
      });
      request.on('error', reject);
      request.write('{"query":');
    });
    assert.equal(result.status, 408);
    assert.deepEqual(JSON.parse(result.body), { contractVersion: 1, sourceId: 'nyaa', error: 'Request body timed out' });
  });
});

test('graceful close drains an in-flight search before resolving', async () => {
  let release;
  let started;
  const startedPromise = new Promise(resolve => { started = resolve; });
  const upstream = new Promise(resolve => { release = resolve; });
  const host = await listenNodeServer({
    host: '127.0.0.1', port: 0,
    fetchImpl: async () => { started(); return upstream; }
  });
  const request = fetch(`http://127.0.0.1:${host.address.port}/v1/nyaa/search`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ query: 'drain test' })
  });

  try {
    await startedPromise;
    let closed = false;
    const closing = host.close().then(() => { closed = true; });
    await new Promise(resolve => setTimeout(resolve, 15));
    assert.equal(closed, false);
    release(new Response(`<rss><channel><item><title>Drain</title><link>https://nyaa.si/view/9</link><nyaa:infoHash>${HASH}</nyaa:infoHash></item></channel></rss>`));
    const response = await request;
    assert.equal(response.status, 200);
    assert.equal((await response.json()).result[0].title, 'Drain');
    await closing;
    assert.equal(closed, true);
  } finally {
    release?.(new Response(fixture));
    await host.close();
  }
});
