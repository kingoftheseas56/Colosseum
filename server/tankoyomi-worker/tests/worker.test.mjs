import test from 'node:test';
import assert from 'node:assert/strict';
import { createWorker } from '../src/worker.mjs';
import { manifest as productionManifest, providers as productionProviders } from '../src/index.js';

const manifest = {
  id: 'colosseum.well.tankoyomi',
  version: '0.2.0',
  defaultLanguage: 'en',
  languages: [
    {
      code: 'en',
      providers: [{
        id: 'fixture',
        enabled: false,
        allowedHosts: ['catalog.example', 'cdn.catalog.example']
      }]
    }
  ]
};

function responseJson(value, status = 200, headers = {}) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'content-type': 'application/json', ...headers }
  });
}

function fixtureWorker(fetchImpl = async () => responseJson([])) {
  const calls = [];
  const provider = {
    async searchSeries(ctx, query) {
      calls.push(['searchSeries', query]);
      return ctx.fetchJson(`https://catalog.example/search?q=${encodeURIComponent(query)}`);
    },
    async getChapters(ctx, series) {
      calls.push(['getChapters', series]);
      return JSON.parse(await ctx.fetchText(`https://catalog.example/series/${encodeURIComponent(series.id)}`));
    },
    async getPages(_ctx, chapter) {
      calls.push(['getPages', chapter]);
      return [{ index: 0, url: `https://cdn.catalog.example/${encodeURIComponent(chapter.id)}.jpg` }];
    }
  };
  return {
    calls,
    worker: createWorker({
      manifest,
      providers: { 'en/fixture': provider },
      fetchImpl
    })
  };
}

async function post(worker, path, value, headers = {}) {
  return worker.fetch(new Request(`https://worker.example${path}`, {
    method: 'POST',
    headers: { 'content-type': 'application/json', ...headers },
    body: JSON.stringify(value)
  }));
}

test('health and manifest endpoints publish contract version and current manifest', async () => {
  const { worker } = fixtureWorker();
  const health = await worker.fetch(new Request('https://worker.example/healthz'));
  assert.equal(health.status, 200);
  assert.deepEqual(await health.json(), { ok: true, contractVersion: 1, revision: '0.2.0' });

  const manifestResponse = await worker.fetch(new Request('https://worker.example/manifest.json'));
  assert.equal(manifestResponse.status, 200);
  assert.deepEqual(await manifestResponse.json(), { ...manifest, contractVersion: 1 });
});

test('production bundle imports every manifest provider, including disabled registrations', () => {
  for (const language of productionManifest.languages) {
    for (const entry of language.providers || []) {
      const provider = productionProviders[`${language.code}/${entry.id}`];
      assert.ok(provider, `${language.code}/${entry.id} must be imported`);
      for (const method of ['searchSeries', 'getChapters', 'getPages'])
        assert.equal(typeof provider[method], 'function', `${language.code}/${entry.id}.${method}`);
    }
  }
});

test('registered disabled provider runs through the complete JSON handler and constrained fetch context', async () => {
  const upstreamCalls = [];
  const { worker, calls } = fixtureWorker(async (url, init) => {
    upstreamCalls.push([String(url), init]);
    if (String(url).includes('/search?')) return responseJson([{ id: 'series-1' }]);
    return responseJson([{ id: 'chapter-1' }]);
  });

  const search = await post(worker, '/v1/en/fixture/searchSeries', { query: 'One Piece' });
  assert.equal(search.status, 200);
  assert.deepEqual(await search.json(), { contractVersion: 1, result: [{ id: 'series-1' }] });

  const chapters = await post(worker, '/v1/en/fixture/getChapters', { series: { id: 'series-1' } });
  assert.equal(chapters.status, 200);
  assert.deepEqual(await chapters.json(), { contractVersion: 1, result: [{ id: 'chapter-1' }] });

  const pages = await post(worker, '/v1/en/fixture/getPages', { chapter: { id: 'chapter-1' } });
  assert.equal(pages.status, 200);
  assert.deepEqual((await pages.json()).result, [{ index: 0, url: 'https://cdn.catalog.example/chapter-1.jpg' }]);
  assert.deepEqual(calls.map(call => call[0]), ['searchSeries', 'getChapters', 'getPages']);
  assert.equal(upstreamCalls.length, 2);
  assert.ok(upstreamCalls.every(([, init]) => init.redirect === 'manual'));
  assert.equal(upstreamCalls.some(([url]) => url.includes('One%20Piece')), true);
});

test('canonical language and same-language provider registration are required', async () => {
  const { worker } = fixtureWorker();
  for (const path of [
    '/v1/fr/fixture/searchSeries',
    '/v1/EN/fixture/searchSeries',
    '/v1/en/missing/searchSeries'
  ]) {
    const response = await post(worker, path, { query: 'x' });
    assert.equal(response.status, 404, path);
    assert.equal((await response.json()).contractVersion, 1);
  }
});

test('method and input validation reject malformed or oversized requests', async () => {
  const { worker } = fixtureWorker();
  const cases = [
    ['/v1/en/fixture/unknown', { query: 'x' }, 404],
    ['/v1/en/fixture/searchSeries', { query: '' }, 400],
    ['/v1/en/fixture/searchSeries', { query: 4 }, 400],
    ['/v1/en/fixture/getChapters', { series: [] }, 400],
    ['/v1/en/fixture/getPages', { chapter: null }, 400]
  ];
  for (const [path, body, status] of cases) {
    const response = await post(worker, path, body);
    assert.equal(response.status, status, path);
    const payload = await response.json();
    assert.equal(payload.contractVersion, 1);
    assert.equal(typeof payload.error, 'string');
  }

  const tooLarge = await post(worker, '/v1/en/fixture/searchSeries', { query: 'x'.repeat(70_000) });
  assert.equal(tooLarge.status, 413);
  const wrongType = await worker.fetch(new Request('https://worker.example/v1/en/fixture/searchSeries', {
    method: 'POST', body: 'not-json', headers: { 'content-type': 'text/plain' }
  }));
  assert.equal(wrongType.status, 415);
});

test('provider fetch follows only bounded redirects within that provider allowlist', async () => {
  const calls = [];
  const { worker } = fixtureWorker(async (url, init) => {
    calls.push(String(url));
    if (calls.length === 1) return new Response(null, {
      status: 302,
      headers: { location: 'https://pages.catalog.example/search-result' }
    });
    return responseJson([{ id: 'redirected' }]);
  });
  const response = await post(worker, '/v1/en/fixture/searchSeries', { query: 'redirect' });
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { contractVersion: 1, result: [{ id: 'redirected' }] });
  assert.deepEqual(calls, [
    'https://catalog.example/search?q=redirect',
    'https://pages.catalog.example/search-result'
  ]);

  const offsiteCalls = [];
  const blocked = fixtureWorker(async (url) => {
    offsiteCalls.push(String(url));
    return new Response(null, { status: 302, headers: { location: 'https://attacker.example/steal' } });
  });
  const blockedResponse = await post(blocked.worker, '/v1/en/fixture/searchSeries', { query: 'private' });
  assert.equal(blockedResponse.status, 502);
  assert.equal((await blockedResponse.json()).error, 'Provider request host is not allowed: attacker.example');
  assert.equal(offsiteCalls.length, 1);

  const redirectLoopCalls = [];
  const redirectLoop = fixtureWorker(async (url) => {
    redirectLoopCalls.push(String(url));
    return new Response(null, { status: 302, headers: { location: '/next' } });
  });
  const loopResponse = await post(redirectLoop.worker, '/v1/en/fixture/searchSeries', { query: 'loop' });
  assert.equal(loopResponse.status, 502);
  assert.equal(redirectLoopCalls.length, 6, 'initial request plus at most five followed redirects');
});

test('provider fetch rejects untrusted destinations, methods, and headers before network access', async () => {
  const observed = [];
  let target = { url: '' };
  const provider = {
    searchSeries(ctx) {
      return ctx.fetchText(target.url, target.options).then(value => [value]);
    },
    getChapters() { return []; },
    getPages() { return []; }
  };
  const worker = createWorker({
    manifest,
    providers: { 'en/fixture': provider },
    fetchImpl: async (...args) => { observed.push(args); return new Response('ok'); }
  });

  const invalid = [
    { url: 'http://catalog.example/data' },
    { url: 'https://catalog.example:444/data' },
    { url: 'https://user:pass@catalog.example/data' },
    { url: 'https://127.0.0.1/data' },
    { url: 'https://[::1]/data' },
    { url: 'https://badcatalog.example/data' },
    { url: 'https://catalog.example/data', options: { method: 'PATCH' } },
    { url: 'https://catalog.example/data', options: { headers: { Cookie: 'secret' } } }
  ];
  for (const current of invalid) {
    target = current;
    const response = await post(worker, '/v1/en/fixture/searchSeries', { query: 'x' });
    assert.equal(response.status, 502, current.url);
    if (current.url === 'https://badcatalog.example/data') {
      const payload = await response.json();
      assert.equal(payload.error, 'Provider request host is not allowed: badcatalog.example');
      assert.doesNotMatch(payload.error, /https?:\/\/|\?|private-query/);
    }
  }
  assert.equal(observed.length, 0);

  const maliciousManifest = structuredClone(manifest);
    maliciousManifest.languages[0].providers[0].allowedHosts.push('127.0.0.1', '::1', 'localhost');
  const privateLiteralWorker = createWorker({
    manifest: maliciousManifest,
    providers: { 'en/fixture': provider },
    fetchImpl: async (...args) => { observed.push(args); return new Response('unexpected'); }
  });
  target = { url: 'https://127.0.0.1/metadata' };
  const privateResponse = await post(privateLiteralWorker, '/v1/en/fixture/searchSeries', { query: 'x' });
  assert.equal(privateResponse.status, 502);
  assert.equal(observed.length, 0, 'private address literals stay blocked even when listed as allowed');
  target = { url: 'https://localhost/metadata' };
  const localhostResponse = await post(privateLiteralWorker, '/v1/en/fixture/searchSeries', { query: 'x' });
  assert.equal(localhostResponse.status, 502);
  assert.equal(observed.length, 0, 'localhost stays blocked even when listed as allowed');
});

test('upstream response reads are size bounded and timed out', async () => {
  const large = fixtureWorker(async () => new Response('x'.repeat(8 * 1024 * 1024 + 1)));
  const largeResponse = await post(large.worker, '/v1/en/fixture/searchSeries', { query: 'large' });
  assert.equal(largeResponse.status, 502);
  assert.match((await largeResponse.json()).error, /response is too large/i);

  const tooLargeResult = createWorker({
    manifest,
    providers: {
      'en/fixture': {
        searchSeries() { return ['x'.repeat(1_048_577)]; },
        getChapters() { return []; },
        getPages() { return []; }
      }
    },
    fetchImpl: async () => responseJson([])
  });
  const resultResponse = await post(tooLargeResult, '/v1/en/fixture/searchSeries', { query: 'large result' });
  assert.equal(resultResponse.status, 502);
  assert.match((await resultResponse.json()).error, /result is too large/i);

  const provider = {
    searchSeries(ctx) { return ctx.fetchText('https://catalog.example/slow', { timeoutMs: 5 }).then(() => []); },
    getChapters() { return []; },
    getPages() { return []; }
  };
  const timed = createWorker({
    manifest,
    providers: { 'en/fixture': provider },
    fetchImpl: () => new Promise(() => {})
  });
  const timeoutResponse = await post(timed, '/v1/en/fixture/searchSeries', { query: 'slow' });
  assert.equal(timeoutResponse.status, 502);
  assert.match((await timeoutResponse.json()).error, /timed out/i);

  const hangingBody = createWorker({
    manifest,
    providers: { 'en/fixture': provider },
    fetchImpl: async () => new Response(new ReadableStream({
      pull() { return new Promise(() => {}); }
    }))
  });
  const hangingBodyResponse = await post(hangingBody, '/v1/en/fixture/searchSeries', { query: 'slow body' });
  assert.equal(hangingBodyResponse.status, 502);
  assert.match((await hangingBodyResponse.json()).error, /timed out/i);
});

test('provider subrequests have a hard ceiling and cannot return a partial success', async () => {
  const provider = {
    async searchSeries(ctx) {
      for (let i = 0; i < 41; i++) {
        try { await ctx.fetchText(`https://catalog.example/page/${i}`); }
        catch (_) { /* Simulate a provider that would otherwise hide the exhausted request. */ }
      }
      return [];
    },
    getChapters() { return []; },
    getPages() { return []; }
  };
  let upstreamCalls = 0;
  const worker = createWorker({
    manifest,
    providers: { 'en/fixture': provider },
    fetchImpl: async () => { upstreamCalls++; return new Response('ok'); }
  });
  const response = await post(worker, '/v1/en/fixture/searchSeries', { query: 'many pages' });
  assert.equal(response.status, 502);
  assert.match((await response.json()).error, /request limit exceeded/i);
  assert.equal(upstreamCalls, 40);
});

test('runtime exposes Base64 and CryptoJS OpenSSL AES helpers with a fixed vector', async () => {
  const provider = {
    searchSeries(ctx) {
      return [
        ctx.decodeBase64('aGVsbG8='),
        ctx.decryptCryptoJs('p5N7stIT78bbYlO7r1sFPA==', '0102030405060708', 'test password')
      ];
    },
    getChapters() { return []; },
    getPages() { return []; }
  };
  const worker = createWorker({ manifest, providers: { 'en/fixture': provider } });
  const response = await post(worker, '/v1/en/fixture/searchSeries', { query: 'crypto' });
  assert.equal(response.status, 200);
  assert.deepEqual((await response.json()).result, ['hello', 'hello crypto']);
});

test('request data is not written to the console', async () => {
  const { worker } = fixtureWorker();
  const previous = console.log;
  const logged = [];
  console.log = (...values) => logged.push(values.join(' '));
  try {
    await post(worker, '/v1/en/fixture/searchSeries', { query: 'private-query-123' });
  } finally {
    console.log = previous;
  }
  assert.deepEqual(logged, []);
});
