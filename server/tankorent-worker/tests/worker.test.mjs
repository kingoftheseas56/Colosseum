import test from 'node:test';
import assert from 'node:assert/strict';
import { createWorker, manifest, parseExtList } from '../src/worker.mjs';

const HASH_A = '0123456789abcdef0123456789abcdef01234567';
const HASH_B = '89abcdef0123456789abcdef0123456789abcdef';

function jsonResponse(value, status = 200, headers = {}) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'content-type': 'application/json', ...headers }
  });
}

function request(path, body, headers = {}) {
  return new Request(`https://worker.test${path}`, {
    method: body === undefined ? 'GET' : 'POST',
    headers: body === undefined ? headers : { 'content-type': 'application/json', ...headers },
    body: body === undefined ? undefined : JSON.stringify(body)
  });
}

async function responseBody(response) {
  return response.json();
}

test('health and manifest are stable public metadata routes', async () => {
  const worker = createWorker({ fetchImpl: async () => { throw new Error('unexpected provider request'); } });
  const health = await worker.fetch(request('/healthz'));
  assert.equal(health.status, 200);
  assert.deepEqual(await responseBody(health), { ok: true, contractVersion: 1, revision: manifest.version });

  const response = await worker.fetch(request('/manifest.json'));
  const value = await responseBody(response);
  assert.equal(value.id, 'colosseum.well.indexers');
  assert.equal(value.name, 'Tankorent');
  assert.deepEqual(value.resources, ['stream']);
  assert.deepEqual(value.types, ['comic', 'book', 'audiobook']);
  assert.equal(value.behaviorHints.configurable, true);
  assert.equal(value.contractVersion, 1);
  assert.deepEqual(value.sources.map(source => source.id), ['piratebay', 'torrentscsv', 'exttorrents', 'knaben']);
  assert.equal((await worker.fetch(request('/healthz', {}))).status, 405);
});

test('PirateBay forwards query/category and maps the native result fields', async () => {
  const seen = [];
  const worker = createWorker({ fetchImpl: async (input, init) => {
    seen.push({ url: new URL(input), init });
    return jsonResponse([{
      name: '  Category Test  ', info_hash: HASH_A.toUpperCase(), size: '4096', seeders: '12', leechers: '3',
      category: '201', added: '1700000000', id: '77'
    }]);
  } });
  const response = await worker.fetch(request('/v1/piratebay/search', {
    query: '  a title & another  ', limit: 5, categoryId: ' 201 '
  }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.equal(payload.sourceId, 'piratebay');
  assert.equal(seen[0].url.origin, 'https://apibay.org');
  assert.equal(seen[0].url.pathname, '/q.php');
  assert.equal(seen[0].url.searchParams.get('q'), '  a title & another  ');
  assert.equal(seen[0].url.searchParams.get('cat'), '201');
  assert.equal(seen[0].init.redirect, 'manual');
  assert.deepEqual(payload.result[0], {
    title: 'Category Test',
    magnetUri: `magnet:?xt=urn:btih:${HASH_A}&dn=Category%20Test&tr=udp%3A%2F%2Ftracker.bittor.pw%3A1337%2Fannounce&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A6969%2Fannounce`,
    sizeBytes: 4096,
    seeders: 12,
    leechers: 3,
    sourceName: 'PirateBay',
    sourceKey: 'piratebay',
    categoryId: '201',
    category: 'Video',
    infoHash: HASH_A,
    publishDate: '2023-11-14T22:13:20.000Z',
    detailsUrl: 'https://thepiratebay.org/description.php?id=77'
  });
});

test('Torrents-CSV ignores category and preserves its size/date mapping', async () => {
  let calledUrl;
  const worker = createWorker({ fetchImpl: async input => {
    calledUrl = new URL(input);
    return jsonResponse({ torrents: [{
      name: 'CSV title', infohash: HASH_B, size_bytes: 123, seeders: 8, leechers: 2, created_unix: 1700000100
    }] });
  } });
  const response = await worker.fetch(request('/v1/torrentscsv/search', {
    query: 'csv query', limit: 7, categoryId: '601'
  }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.equal(calledUrl.origin, 'https://torrents-csv.com');
  assert.equal(calledUrl.pathname, '/service/search');
  assert.equal(calledUrl.searchParams.get('q'), 'csv query');
  assert.equal(calledUrl.searchParams.get('size'), '7');
  assert.equal(calledUrl.searchParams.has('cat'), false);
  assert.deepEqual(payload.result[0], {
    title: 'CSV title',
    magnetUri: `magnet:?xt=urn:btih:${HASH_B}&dn=CSV%20title&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A6969%2Fannounce`,
    sizeBytes: 123,
    seeders: 8,
    leechers: 2,
    sourceName: 'Torrents-CSV',
    sourceKey: 'torrentscsv',
    categoryId: '',
    category: '',
    infoHash: HASH_B,
    publishDate: '2023-11-14T22:15:00.000Z',
    detailsUrl: ''
  });
});

test('Knaben keeps relevance request shape and only forwards positive numeric categories', async () => {
  let sent;
  const worker = createWorker({ fetchImpl: async (_input, init) => {
    sent = JSON.parse(init.body);
    return jsonResponse({ hits: [{
      title: 'Knaben result', hash: HASH_A.toUpperCase(), bytes: 99, seeders: 4, peers: 6,
      magnetUrl: `magnet:?xt=urn:btih:${HASH_A}&dn=Knaben&x-unsafe=drop&tr=javascript:bad&tr=udp://tracker.example/announce`, tracker: '1337x',
      details: 'https://tracker.example/details/1', date: '2024-01-02T03:04:05Z'
    }] });
  } });
  const response = await worker.fetch(request('/v1/knaben/search', {
    query: 'knaben title', limit: 301, categoryId: ' 12 '
  }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.deepEqual(sent, {
    search_type: 'score', search_field: 'title', query: 'knaben title', hide_unsafe: true,
    hide_xxx: true, size: 300, categories: [12]
  });
  assert.equal(Object.hasOwn(sent, 'order_by'), false);
  assert.equal(payload.result[0].sourceName, 'Knaben');
  assert.equal(payload.result[0].category, '1337x');
  assert.equal(payload.result[0].leechers, 6);
  assert.equal(payload.result[0].infoHash, HASH_A);
  assert.equal(payload.result[0].publishDate, '2024-01-02T03:04:05.000Z');
  assert.match(payload.result[0].magnetUri, new RegExp(`^magnet:\\?xt=urn:btih:${HASH_A}&dn=Knaben%20result&tr=udp%3A%2F%2Ftracker.example%2Fannounce$`));
  assert.equal(payload.result[0].magnetUri.includes('unsafe'), false);
  assert.equal(payload.result[0].magnetUri.includes('javascript'), false);
});

test('result fields are capped and numeric counts remain within the native safe range', async () => {
  const worker = createWorker({ fetchImpl: async () => jsonResponse([{
    name: 'T'.repeat(600), info_hash: HASH_A, size: '-2', seeders: '2147483648', leechers: '-4',
    category: `2${'x'.repeat(200)}`, added: '-1'
  }]) });
  const response = await worker.fetch(request('/v1/piratebay/search', { query: 'caps', limit: 2 }));
  const payload = await responseBody(response);
  const [row] = payload.result;
  assert.equal(row.title.length, 512);
  assert.equal(row.magnetUri.length <= 8192, true);
  assert.equal(row.categoryId.length, 128);
  assert.equal(row.category.length <= 128, true);
  assert.equal(row.sizeBytes, 0);
  assert.equal(row.seeders, 0);
  assert.equal(row.leechers, 0);
  assert.equal(row.publishDate, null);
});

test('ExtraTorrents keeps its known fallback routes, parses list rows and resolves details', async () => {
  const seen = [];
  const html = `<table><tbody>
    <tr class="has-books"><td><a href="/post-detail/book-1"><span class="name"><b>Book &amp; Volume</b></span></a></td><td>1.5 GiB</td><td>-</td><td>-</td><td>12</td><td>2</td></tr>
    <tr class="has-movie"><td><a href="/post-detail/book-2">Second title</a><a href="magnet:?xt=urn:btih:${HASH_B}&amp;dn=Second">download</a></td><td>512 MB</td><td>-</td><td>-</td><td>3</td><td>1</td></tr>
  </tbody></table>`;
  const worker = createWorker({ fetchImpl: async input => {
    const url = new URL(input);
    seen.push(url.href);
    if (url.pathname === '/browse/' && !url.searchParams.has('with_adult'))
      return new Response('unavailable', { status: 404 });
    if (url.pathname === '/browse/' && url.searchParams.get('with_adult') === '1')
      return new Response(html, { headers: { 'content-type': 'text/html' } });
    if (url.pathname === '/post-detail/book-1')
      return new Response(`<a href="magnet:?xt=urn:btih:${HASH_A}&amp;dn=Book">download</a>`);
    throw new Error(`unexpected URL ${url.href}`);
  } });
  const response = await worker.fetch(request('/v1/exttorrents/search', {
    query: 'space title', limit: 10, categoryId: ' 601 '
  }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.equal(seen.length, 3);
  const browse = new URL(seen[1]);
  assert.equal(browse.origin, 'https://extto.org');
  assert.equal(browse.pathname, '/browse/');
  assert.equal(browse.searchParams.get('q'), 'space title');
  assert.equal(browse.searchParams.get('with_adult'), '1');
  assert.equal(browse.searchParams.get('cat'), '601');
  assert.equal(payload.result.length, 2);
  assert.deepEqual(payload.result.map(row => [row.title, row.category, row.categoryId, row.sizeBytes, row.seeders, row.leechers]), [
    ['Book & Volume', 'Books', 'books', 1.5 * 1024 ** 3, 12, 2],
    ['Second title', 'Movies', 'movies', 512 * 1024 ** 2, 3, 1]
  ]);
  assert.equal(payload.result[0].infoHash, HASH_A);
  assert.equal(payload.result[1].infoHash, HASH_B);
  assert.equal(payload.result[0].detailsUrl, 'https://extto.org/post-detail/book-1');
});

test('ExtraTorrents parser rejects off-site detail links and keeps native title/category parsing', () => {
  const rows = parseExtList(`<table><tbody><tr class="has-anime">
    <td><a href="https://evil.example/post-detail/1"><span class="name"><b>Anime &lt;One&gt;</b></span></a></td>
    <td>2 GB</td><td>-</td><td>-</td><td>4</td>
  </tr></tbody></table>`);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].title, 'Anime <One>');
  assert.equal(rows[0].detailUrl, '');
  assert.equal(rows[0].category, 'Anime');
  const nonDetail = parseExtList(`<table><tbody><tr>
    <td><a href="/account"><span class="name"><b>Not a detail route</b></span></a></td>
    <td>2 GB</td><td>-</td><td>-</td><td>4</td>
  </tr></tbody></table>`);
  assert.equal(nonDetail[0].detailUrl, '');
});

test('ExtraTorrents preserves direct healthy rows when one detail request fails', async () => {
  const directHtml = `<table><tbody>
    <tr><td><a href="/post-detail/direct"><span class="name"><b>Direct row</b></span></a><a href="magnet:?xt=urn:btih:${HASH_A}">download</a></td><td>1 MB</td><td>-</td><td>-</td><td>5</td></tr>
    <tr><td><a href="/post-detail/missing"><span class="name"><b>Missing detail</b></span></a></td><td>1 MB</td><td>-</td><td>-</td><td>1</td></tr>
  </tbody></table>`;
  const worker = createWorker({ fetchImpl: async input => {
    const url = new URL(input);
    if (url.pathname === '/browse/') return new Response(directHtml);
    return new Response('temporarily unavailable', { status: 503 });
  } });
  const response = await worker.fetch(request('/v1/exttorrents/search', { query: 'partial rows', limit: 10 }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.deepEqual(payload.result.map(row => row.title), ['Direct row']);
});

test('ExtraTorrents detail fetches stay within four concurrent requests', async () => {
  let active = 0;
  let peakActive = 0;
  let detailsFetched = 0;
  const rows = Array.from({ length: 30 }, (_, index) => `<tr><td><a href="/post-detail/${index}"><span class="name"><b>Title ${index}</b></span></a></td><td>1 MB</td><td>-</td><td>-</td><td>1</td></tr>`).join('');
  const worker = createWorker({ fetchImpl: async input => {
    const url = new URL(input);
    if (url.pathname === '/browse/') return new Response(`<table><tbody>${rows}</tbody></table>`);
    active += 1;
    detailsFetched += 1;
    peakActive = Math.max(peakActive, active);
    await new Promise(resolve => setTimeout(resolve, 5));
    active -= 1;
    return new Response(`<a href="magnet:?xt=urn:btih:${HASH_A}">download</a>`);
  } });
  const response = await worker.fetch(request('/v1/exttorrents/search', { query: 'bounded detail concurrency', limit: 100 }));
  const payload = await responseBody(response);
  assert.equal(response.status, 200);
  assert.equal(payload.result.length, 30);
  assert.equal(detailsFetched, 30);
  assert.equal(peakActive, 4);
});

test('provider HTTP failures and malformed JSON are structured errors, never empty success', async () => {
  const limitedWorker = createWorker({ fetchImpl: async () => new Response('slow down', { status: 429 }) });
  const limited = await limitedWorker.fetch(request('/v1/piratebay/search', { query: 'title', limit: 5 }));
  assert.equal(limited.status, 429);
  assert.deepEqual(await responseBody(limited), {
    contractVersion: 1, sourceId: 'piratebay', error: 'Upstream returned HTTP 429', health: 'rate_limited'
  });

  const invalidWorker = createWorker({ fetchImpl: async () => new Response('{"not":"the expected array"}') });
  const invalid = await invalidWorker.fetch(request('/v1/piratebay/search', { query: 'title', limit: 5 }));
  assert.equal(invalid.status, 502);
  const body = await responseBody(invalid);
  assert.equal(body.sourceId, 'piratebay');
  assert.equal(body.health, 'unreachable');
  assert.equal(Object.hasOwn(body, 'result'), false);

  const largePayload = 'x'.repeat(2 * 1024 * 1024 + 1);
  const oversizedWorker = createWorker({ fetchImpl: async () => new Response(largePayload, {
    headers: { 'content-length': String(largePayload.length) }
  }) });
  const oversized = await oversizedWorker.fetch(request('/v1/piratebay/search', { query: 'title', limit: 5 }));
  assert.equal(oversized.status, 502);
  assert.equal((await responseBody(oversized)).error, 'Upstream response is too large');
});

test('redirect validation blocks HTTP downgrade and off-allowlist destinations', async () => {
  for (const location of ['http://apibay.org/redirected', 'https://evil.example/collect']) {
    const calls = [];
    const worker = createWorker({ fetchImpl: async input => {
      calls.push(String(input));
      return new Response(null, { status: 302, headers: { location } });
    } });
    const response = await worker.fetch(request('/v1/piratebay/search', { query: 'title', limit: 5 }));
    const body = await responseBody(response);
    assert.equal(response.status, 502);
    assert.equal(body.health, 'unreachable');
    assert.equal(calls.length, 1);
  }
});

test('ExtraTorrents redirect and detail work stays under the per-search subrequest budget', async () => {
  let calls = 0;
  const rows = Array.from({ length: 30 }, (_, index) => `<tr><td><a href="/post-detail/${index}"><span class="name"><b>Title ${index}</b></span></a></td><td>1 MB</td><td>-</td><td>-</td><td>1</td><td>0</td></tr>`).join('');
  const worker = createWorker({ fetchImpl: async input => {
    calls += 1;
    const url = new URL(input);
    if (url.pathname === '/browse/') return new Response(`<table><tbody>${rows}</tbody></table>`);
    return new Response(null, { status: 302, headers: { location: `https://extto.org${url.pathname}` } });
  } });
  const response = await worker.fetch(request('/v1/exttorrents/search', {
    query: 'bounded', limit: 100
  }));
  const body = await responseBody(response);
  assert.equal(response.status, 502);
  assert.equal(body.sourceId, 'exttorrents');
  assert.equal(body.health, 'unreachable');
  assert.ok(calls <= 39, `used ${calls} provider subrequests`);
});

test('route, content type, and bounded request validation fail before provider fetch', async () => {
  let fetches = 0;
  const worker = createWorker({ fetchImpl: async () => { fetches += 1; return jsonResponse([]); } });
  assert.equal((await worker.fetch(request('/v1/unknown/search', { query: 'x' }))).status, 404);
  assert.equal((await worker.fetch(new Request('https://worker.test/v1/piratebay/search', { method: 'POST', body: '{}' }))).status, 415);
  const invalid = await worker.fetch(request('/v1/piratebay/search', { query: 'x', limit: 0 }));
  assert.equal(invalid.status, 400);
  const tooLarge = await worker.fetch(new Request('https://worker.test/v1/piratebay/search', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ query: 'x'.repeat(9000) })
  }));
  assert.equal(tooLarge.status, 413);
  const tooLongQuery = await worker.fetch(request('/v1/piratebay/search', { query: 'q'.repeat(513) }));
  assert.equal(tooLongQuery.status, 400);
  assert.equal(fetches, 0);
  const boundary = await worker.fetch(request('/v1/piratebay/search', {
    query: 'q'.repeat(512), limit: 1, categoryId: 'c'.repeat(128)
  }));
  assert.equal(boundary.status, 200);
  assert.equal(fetches, 1);
});
