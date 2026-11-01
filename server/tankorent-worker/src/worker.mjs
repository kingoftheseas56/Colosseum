const CONTRACT_VERSION = 1;
const MAX_REQUEST_BYTES = 8 * 1024;
const MAX_RESPONSE_BYTES = 2 * 1024 * 1024;
const MAX_RESULT_BYTES = 1024 * 1024;
const MAX_UPSTREAM_SUBREQUESTS = 39;
const MAX_REDIRECTS = 3;
const MAX_DETAIL_CONCURRENCY = 4;
const SEARCH_DEADLINE_MS = 20_000;
const UPSTREAM_TIMEOUT_MS = 8_000;
const BODY_TIMEOUT_MS = 5_000;
const MAX_QUERY_LENGTH = 512;
const MAX_CATEGORY_LENGTH = 128;
const MAX_TITLE_LENGTH = 512;
const MAX_MAGNET_LENGTH = 8_192;
const MAX_CATEGORY_VALUE_LENGTH = 128;
const MAX_INT32 = 2_147_483_647;

const DEFAULT_TRACKERS = [
  'udp://tracker.opentrackr.org:1337/announce',
  'udp://open.stealth.si:80/announce',
  'udp://tracker.torrent.eu.org:451/announce',
  'udp://tracker.openbittorrent.com:6969/announce'
];

const SOURCE_LIST = [
  { id: 'piratebay', name: 'PirateBay', allowedHosts: ['apibay.org'], maxLimit: 300 },
  { id: 'torrentscsv', name: 'Torrents-CSV', allowedHosts: ['torrents-csv.com'], maxLimit: 100 },
  { id: 'exttorrents', name: 'ExtraTorrents', allowedHosts: ['extto.org'], maxLimit: 200 },
  { id: 'knaben', name: 'Knaben', allowedHosts: ['api.knaben.org'], maxLimit: 300 }
];

const SOURCES = new Map(SOURCE_LIST.map(source => [source.id, source]));

export const manifest = Object.freeze({
  id: 'colosseum.well.indexers',
  version: '1.0.2',
  name: 'Tankorent',
  description: 'Colosseum local-first torrent search metadata',
  resources: ['stream'],
  types: ['comic', 'book', 'audiobook'],
  catalogs: [],
  idPrefixes: [],
  behaviorHints: { configurable: true },
  sources: SOURCE_LIST.map(({ id, name }) => ({ id, name }))
});

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

class SourceError extends Error {
  constructor(message, health = 'unreachable', status = 502, fatal = false) {
    super(message);
    this.health = health;
    this.status = status;
    this.fatal = fatal;
  }
}

function jsonResponse(value, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store'
    }
  });
}

function errorResponse(sourceId, error, status) {
  const responseStatus = status || (error instanceof HttpError ? error.status : error?.status) || 502;
  const message = error instanceof HttpError || error instanceof SourceError
    ? error.message
    : 'Source request failed';
  const payload = { contractVersion: CONTRACT_VERSION, sourceId: sourceId || 'unknown', error: message };
  if (error instanceof SourceError) payload.health = error.health;
  return jsonResponse(payload, responseStatus);
}

function utf8Length(value) {
  return new TextEncoder().encode(value).byteLength;
}

function contentLength(headers) {
  const raw = headers?.get?.('content-length');
  if (!raw || !/^\d+$/.test(raw)) return null;
  const parsed = Number(raw);
  return Number.isSafeInteger(parsed) ? parsed : null;
}

function withSignal(promise, signal) {
  if (!signal) return promise;
  if (signal.aborted) return Promise.reject(signal.reason || new Error('Timed out'));
  let onAbort;
  const aborted = new Promise((_, reject) => {
    onAbort = () => reject(signal.reason || new Error('Timed out'));
    signal.addEventListener('abort', onAbort, { once: true });
  });
  return Promise.race([promise, aborted]).finally(() => signal.removeEventListener('abort', onAbort));
}

async function readBoundedStream(stream, maxBytes, signal, tooLargeMessage) {
  if (!stream) return '';
  const reader = stream.getReader();
  const chunks = [];
  let total = 0;
  try {
    while (true) {
      const { done, value } = await withSignal(reader.read(), signal);
      if (done) break;
      const chunk = value instanceof Uint8Array ? value : new Uint8Array(value);
      total += chunk.byteLength;
      if (total > maxBytes) {
        await reader.cancel().catch(() => {});
        throw new Error(tooLargeMessage);
      }
      chunks.push(chunk);
    }
  } catch (error) {
    await reader.cancel().catch(() => {});
    throw error;
  } finally {
    try { reader.releaseLock(); } catch (_) {}
  }
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return new TextDecoder('utf-8').decode(bytes);
}

async function readRequestBody(request) {
  if (contentLength(request.headers) > MAX_REQUEST_BYTES)
    throw new HttpError(413, 'Request body is too large');
  try {
    return await readBoundedStream(
      request.body,
      MAX_REQUEST_BYTES,
      AbortSignal.timeout(BODY_TIMEOUT_MS),
      'Request body is too large'
    );
  } catch (error) {
    if (error instanceof HttpError) throw error;
    if (error?.message === 'Request body is too large') throw new HttpError(413, error.message);
    throw new HttpError(400, 'Could not read request body');
  }
}

function parseJson(text) {
  try { return JSON.parse(text); }
  catch (_) { throw new HttpError(400, 'Request body must be valid JSON'); }
}

function validateRequest(body, source) {
  if (!body || typeof body !== 'object' || Array.isArray(body))
    throw new HttpError(400, 'Request body must be a JSON object');
  if (typeof body.query !== 'string' || !body.query.trim() || body.query.length > MAX_QUERY_LENGTH)
    throw new HttpError(400, `query must be a non-empty string of at most ${MAX_QUERY_LENGTH} characters`);
  const limit = body.limit === undefined ? 30 : body.limit;
  if (!Number.isInteger(limit) || limit < 1 || limit > 1000)
    throw new HttpError(400, 'limit must be an integer between 1 and 1000');
  const categoryId = body.categoryId === undefined ? '' : body.categoryId;
  if (typeof categoryId !== 'string' || categoryId.length > MAX_CATEGORY_LENGTH)
    throw new HttpError(400, `categoryId must be a string of at most ${MAX_CATEGORY_LENGTH} characters`);
  return {
    query: body.query,
    limit: Math.min(limit, source.maxLimit),
    categoryId: categoryId.trim()
  };
}

function normalizedHost(hostname) {
  return String(hostname || '').toLowerCase().replace(/\.$/, '');
}

function isIpLiteral(hostname) {
  const host = String(hostname || '').replace(/^\[|\]$/g, '');
  return host.includes(':') || /^\d+(?:\.\d+){0,3}$/.test(host);
}

function validatedUrl(value, allowedHosts) {
  let url;
  try { url = value instanceof URL ? new URL(value.href) : new URL(value); }
  catch (_) { throw new SourceError('Provider requested an invalid URL'); }
  if (url.protocol !== 'https:') throw new SourceError('Provider requests must use HTTPS');
  if (url.port && url.port !== '443') throw new SourceError('Provider requests must use port 443');
  if (url.username || url.password) throw new SourceError('Provider request URL credentials are not allowed');
  const host = normalizedHost(url.hostname);
  if (isIpLiteral(url.hostname) || host === 'localhost' || host.endsWith('.localhost'))
    throw new SourceError('Provider request host is not allowed');
  if (!allowedHosts.includes(host)) throw new SourceError('Provider request host is not allowed');
  return url;
}

function sourceHealth(response) {
  const server = response.headers?.get?.('server') || '';
  const cfRay = response.headers?.get?.('cf-ray') || '';
  if (response.status === 429) return 'rate_limited';
  if (response.status === 403 && (/cloudflare/i.test(server) || cfRay)) return 'cloudflare_blocked';
  if (response.status === 401 || response.status === 403) return 'auth_required';
  return 'unreachable';
}

function signalForDeadline(deadline) {
  const remaining = deadline - Date.now();
  if (remaining <= 0) throw new SourceError('Search deadline exceeded', 'unreachable', 502, true);
  return AbortSignal.timeout(Math.max(1, Math.min(UPSTREAM_TIMEOUT_MS, remaining)));
}

function sourceHeaders(extra = {}) {
  return new Headers({
    accept: 'application/json,text/html,*/*',
    'user-agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
    ...extra
  });
}

async function fetchText(source, input, { method = 'GET', body, headers = sourceHeaders() } = {}, fetchImpl, budget) {
  if (method !== 'GET' && method !== 'POST') throw new SourceError('Provider request method is invalid');
  if (method === 'GET' && body !== undefined) throw new SourceError('GET provider requests cannot have a body');
  if (body !== undefined && utf8Length(String(body)) > MAX_REQUEST_BYTES)
    throw new SourceError('Provider request body is too large');

  const originalUrl = validatedUrl(input, source.allowedHosts);
  const deadline = budget.deadline;
  let currentUrl = originalUrl;
  let currentMethod = method;
  let currentBody = body;
  let redirects = 0;

  while (true) {
    if (budget.used >= MAX_UPSTREAM_SUBREQUESTS)
      throw new SourceError('Provider request limit exceeded', 'unreachable', 502, true);
    const signal = signalForDeadline(deadline);
    budget.used += 1;
    let response;
    try {
      response = await withSignal(fetchImpl(currentUrl.href, {
        method: currentMethod,
        headers,
        body: currentBody,
        redirect: 'manual',
        signal
      }), signal);
    } catch (error) {
      if (Date.now() >= deadline)
        throw new SourceError('Search deadline exceeded', 'unreachable', 502, true);
      if (signal.aborted) throw new SourceError('Upstream request timed out');
      throw new SourceError('Upstream request failed');
    }

    if ([301, 302, 303, 307, 308].includes(response.status)) {
      if (redirects >= MAX_REDIRECTS) {
        await response.body?.cancel().catch(() => {});
        throw new SourceError('Upstream redirect limit exceeded');
      }
      const location = response.headers?.get?.('location');
      await response.body?.cancel().catch(() => {});
      if (!location) throw new SourceError('Upstream redirect did not provide a location');
      currentUrl = validatedUrl(new URL(location, currentUrl), source.allowedHosts);
      redirects += 1;
      if (response.status === 303 || ((response.status === 301 || response.status === 302) && currentMethod === 'POST')) {
        currentMethod = 'GET';
        currentBody = undefined;
      }
      continue;
    }

    if (!response.ok) {
      const health = sourceHealth(response);
      await response.body?.cancel().catch(() => {});
      throw new SourceError(`Upstream returned HTTP ${response.status}`, health, response.status === 429 ? 429 : 502);
    }
    if (contentLength(response.headers) > MAX_RESPONSE_BYTES) {
      await response.body?.cancel().catch(() => {});
      throw new SourceError('Upstream response is too large');
    }
    try {
      return await readBoundedStream(response.body, MAX_RESPONSE_BYTES, signal, 'Upstream response is too large');
    } catch (error) {
      if (Date.now() >= deadline)
        throw new SourceError('Search deadline exceeded', 'unreachable', 502, true);
      if (signal.aborted) throw new SourceError('Upstream request timed out');
      if (error?.message === 'Upstream response is too large') throw new SourceError(error.message);
      throw new SourceError('Could not read upstream response');
    }
  }
}

async function fetchJson(source, url, options, fetchImpl, budget) {
  const text = await fetchText(source, url, options, fetchImpl, budget);
  try { return JSON.parse(text); }
  catch (_) { throw new SourceError('Upstream returned invalid JSON'); }
}

function encodeComponent(value) {
  return encodeURIComponent(value).replace(/[!'()*]/g, char => `%${char.charCodeAt(0).toString(16).toUpperCase()}`);
}

function buildMagnet(infoHash, title, trackers = DEFAULT_TRACKERS) {
  const hash = canonicalInfoHash(infoHash);
  if (!hash) return '';
  let magnet = `magnet:?xt=urn:btih:${hash}&dn=${encodeComponent(title)}`;
  for (const tracker of trackers) magnet += `&tr=${encodeComponent(tracker.trim())}`;
  return magnet;
}

function validTracker(value) {
  if (typeof value !== 'string' || !value.trim() || value.length > 2_048 || /[\u0000-\u001f\u007f]/.test(value)) return false;
  try {
    const url = new URL(value);
    return ['udp:', 'http:', 'https:'].includes(url.protocol) && Boolean(url.hostname)
      && !url.username && !url.password;
  } catch (_) {
    return false;
  }
}

function normalizeMagnet(value, infoHash, title) {
  if (!infoHash || !title) return '';
  let trackers = [];
  if (typeof value === 'string' && value.length <= MAX_MAGNET_LENGTH && /^magnet:\?/i.test(value)
      && !/[\u0000-\u001f\u007f]/.test(value)) {
    try {
      const parsed = new URL(value);
      const matchingBtih = parsed.searchParams.getAll('xt')
        .some(xt => xt.trim().toLowerCase() === `urn:btih:${infoHash}`);
      if (matchingBtih) trackers = parsed.searchParams.getAll('tr').filter(validTracker).slice(0, 12);
    } catch (_) {
      trackers = [];
    }
  }
  return buildMagnet(infoHash, title, trackers.length ? trackers : DEFAULT_TRACKERS);
}

function canonicalInfoHash(value) {
  const hash = typeof value === 'string' ? value.trim().toLowerCase() : '';
  return /^[a-f0-9]{40}$/.test(hash) ? hash : '';
}

function textValue(value, maxLength = 12_000) {
  return typeof value === 'string' ? value.trim().slice(0, maxLength) : '';
}

function numericValue(value) {
  if (typeof value === 'string' && !/^[-+]?\d+(?:\.\d+)?$/.test(value.trim())) return 0;
  const number = Number(value);
  return Number.isSafeInteger(number) && number >= 0 ? number : 0;
}

function boundedNumericValue(value, maximum) {
  const number = numericValue(value);
  return number <= maximum ? number : 0;
}

function unixDate(value) {
  const seconds = numericValue(value);
  if (seconds <= 0) return null;
  const date = new Date(seconds * 1000);
  return Number.isNaN(date.getTime()) ? null : date.toISOString();
}

function isoDate(value) {
  const text = textValue(value, 100);
  if (!text) return null;
  const milliseconds = Date.parse(text);
  return Number.isFinite(milliseconds) ? new Date(milliseconds).toISOString() : null;
}

function categoryLabel(categoryId) {
  const first = String(categoryId || '')[0];
  return ({ '1': 'Audio', '2': 'Video', '3': 'Applications', '4': 'Games', '5': 'Porn', '6': 'Other' })[first] || '';
}

function resultRecord(fields) {
  const title = textValue(fields.title, MAX_TITLE_LENGTH);
  const infoHash = canonicalInfoHash(fields.infoHash);
  const magnetUri = normalizeMagnet(fields.magnetUri, infoHash, title);
  if (!title || !infoHash || !magnetUri) return null;
  return {
    title,
    magnetUri,
    sizeBytes: boundedNumericValue(fields.sizeBytes, Number.MAX_SAFE_INTEGER),
    seeders: boundedNumericValue(fields.seeders, MAX_INT32),
    leechers: boundedNumericValue(fields.leechers, MAX_INT32),
    sourceName: fields.sourceName,
    sourceKey: fields.sourceKey,
    categoryId: textValue(fields.categoryId, MAX_CATEGORY_VALUE_LENGTH),
    category: textValue(fields.category, MAX_CATEGORY_VALUE_LENGTH),
    infoHash,
    publishDate: fields.publishDate || null,
    detailsUrl: textValue(fields.detailsUrl, 2_000)
  };
}

function objectRow(value) {
  return value && typeof value === 'object' && !Array.isArray(value) ? value : null;
}

function parsePirateBayResponse(body) {
  if (!Array.isArray(body)) throw new SourceError('Upstream returned an unexpected response');
  const results = [];
  for (const value of body) {
    const row = objectRow(value);
    if (!row) continue;
    const title = textValue(row.name, MAX_TITLE_LENGTH);
    const infoHash = canonicalInfoHash(row.info_hash);
    if (!title || !infoHash) continue;
    const categoryId = textValue(row.category, MAX_CATEGORY_VALUE_LENGTH);
    const trackers = ['udp://tracker.bittor.pw:1337/announce', ...DEFAULT_TRACKERS];
    const id = textValue(row.id, 100);
    const detailsUrl = id ? `https://thepiratebay.org/description.php?id=${encodeComponent(id)}` : '';
    const result = resultRecord({
      title,
      magnetUri: buildMagnet(infoHash, title, trackers),
      sizeBytes: row.size,
      seeders: row.seeders,
      leechers: row.leechers,
      sourceName: 'PirateBay',
      sourceKey: 'piratebay',
      categoryId,
      category: categoryLabel(categoryId),
      infoHash,
      publishDate: unixDate(row.added),
      detailsUrl
    });
    if (result) results.push(result);
  }
  return results;
}

function parseTorrentsCsvResponse(body) {
  const torrents = Array.isArray(body) ? body : objectRow(body)?.torrents;
  if (!Array.isArray(torrents)) throw new SourceError('Upstream returned an unexpected response');
  const results = [];
  for (const value of torrents) {
    const row = objectRow(value);
    if (!row) continue;
    const title = textValue(row.name, MAX_TITLE_LENGTH);
    const infoHash = canonicalInfoHash(row.infohash);
    if (!title || !infoHash) continue;
    const result = resultRecord({
      title,
      magnetUri: buildMagnet(infoHash, title),
      sizeBytes: row.size_bytes,
      seeders: row.seeders,
      leechers: row.leechers,
      sourceName: 'Torrents-CSV',
      sourceKey: 'torrentscsv',
      categoryId: '',
      category: '',
      infoHash,
      publishDate: unixDate(row.created_unix),
      detailsUrl: ''
    });
    if (result) results.push(result);
  }
  return results;
}

function parseKnabenResponse(body) {
  const hits = objectRow(body)?.hits;
  if (!Array.isArray(hits)) throw new SourceError('Upstream returned an unexpected response');
  const results = [];
  for (const value of hits) {
    const row = objectRow(value);
    if (!row) continue;
    const title = textValue(row.title, MAX_TITLE_LENGTH);
    const infoHash = canonicalInfoHash(row.hash);
    if (!title || !infoHash) continue;
    const upstreamMagnet = textValue(row.magnetUrl, MAX_MAGNET_LENGTH + 1);
    const magnetUri = /^magnet:/i.test(upstreamMagnet) ? upstreamMagnet : buildMagnet(infoHash, title);
    const result = resultRecord({
      title,
      magnetUri,
      sizeBytes: row.bytes,
      seeders: row.seeders,
      leechers: row.peers,
      sourceName: 'Knaben',
      sourceKey: 'knaben',
      categoryId: '',
      category: textValue(row.tracker, 500),
      infoHash,
      publishDate: isoDate(row.date),
      detailsUrl: textValue(row.details, 2_000)
    });
    if (result) results.push(result);
  }
  return results;
}

function decodeHtmlEntities(value) {
  const named = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ' };
  return String(value || '').replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (entity, name) => {
    if (name[0] === '#') {
      const codePoint = name[1].toLowerCase() === 'x'
        ? Number.parseInt(name.slice(2), 16)
        : Number.parseInt(name.slice(1), 10);
      if (!Number.isInteger(codePoint) || codePoint <= 0 || codePoint > 0x10ffff) return entity;
      try { return String.fromCodePoint(codePoint); } catch (_) { return entity; }
    }
    return Object.hasOwn(named, name.toLowerCase()) ? named[name.toLowerCase()] : entity;
  });
}

function stripTags(value) {
  return decodeHtmlEntities(String(value || '').replace(/<[^>]*>/g, '')).trim();
}

function parseExtSize(value) {
  const match = String(value || '').match(/([\d.,]+)\s*(bytes?|[kmgt]i?b)/i);
  if (!match) return 0;
  const amount = Number(match[1].replace(/,/g, ''));
  if (!Number.isFinite(amount) || amount < 0) return 0;
  const unit = match[2].toLowerCase();
  const multiplier = unit.startsWith('k') ? 1024
    : unit.startsWith('m') ? 1024 ** 2
      : unit.startsWith('g') ? 1024 ** 3
        : unit.startsWith('t') ? 1024 ** 4 : 1;
  const size = Math.floor(amount * multiplier);
  return Number.isSafeInteger(size) ? size : 0;
}

function extCategory(cssClasses) {
  const map = {
    'has-movie': 'Movies', 'has-tv': 'TV', 'has-game': 'Games', 'has-music': 'Music',
    'has-app': 'Apps', 'has-documentary': 'Documentaries', 'has-anime': 'Anime',
    'has-books': 'Books', 'has-book': 'Books', 'has-other': 'Other', 'has-xxx': 'XXX'
  };
  for (const token of String(cssClasses || '').split(/\s+/)) if (map[token]) return map[token];
  return '';
}

function attrValue(tag, name) {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const match = String(tag || '').match(new RegExp(`\\b${escaped}\\s*=\\s*(["'])(.*?)\\1`, 'i'));
  return match ? decodeHtmlEntities(match[2]) : '';
}

function safeExtDetailsUrl(value) {
  if (!value) return '';
  try {
    const url = new URL(value, 'https://extto.org');
    if (!url.pathname.startsWith('/post-detail/')) return '';
    return validatedUrl(url, ['extto.org']).href;
  } catch (_) {
    return '';
  }
}

function parseExtList(html) {
  const tbody = String(html || '').match(/<tbody\b[^>]*>([\s\S]*?)<\/tbody>/i);
  if (!tbody) return [];
  const rows = [];
  for (const rowMatch of tbody[1].matchAll(/<tr\b[^>]*>([\s\S]*?)<\/tr>/gi)) {
    const rowHtml = rowMatch[1];
    const cells = [...rowHtml.matchAll(/<td\b[^>]*>([\s\S]*?)<\/td>/gi)].map(match => match[1]);
    if (cells.length < 5) continue;
    const firstCell = cells[0];
    let detailPath = '';
    for (const anchor of firstCell.matchAll(/<a\b[^>]*>/gi)) {
      const href = attrValue(anchor[0], 'href');
      if (safeExtDetailsUrl(href)) {
        detailPath = href;
        break;
      }
    }

    let title = '';
    const nameSpan = firstCell.match(/<span\b[^>]*class\s*=\s*(["'])[^"']*\bname\b[^"']*\1[^>]*>\s*<b\b[^>]*>([^<]+)<\/b>/i);
    if (nameSpan) title = stripTags(nameSpan[2]);
    if (!title && detailPath) {
      for (const anchor of firstCell.matchAll(/<a\b[^>]*>([\s\S]*?)<\/a>/gi)) {
        const href = attrValue(anchor[0], 'href');
        if (href === detailPath) {
          title = stripTags(anchor[1]);
          break;
        }
      }
    }
    if (!title) continue;

    let magnetUri = '';
    for (const anchor of rowHtml.matchAll(/<a\b[^>]*>/gi)) {
      const href = attrValue(anchor[0], 'href');
      if (/^magnet:\?/i.test(href)) { magnetUri = href; break; }
    }
    const rowClasses = [...rowMatch[0].matchAll(/\bclass\s*=\s*(["'])(.*?)\1/gi)].map(match => match[2]).join(' ');
    const category = extCategory(rowClasses);
    rows.push({
      title,
      detailUrl: safeExtDetailsUrl(detailPath),
      magnetUri,
      sizeBytes: parseExtSize(stripTags(cells[1])),
      seeders: numericValue(stripTags(cells[4])),
      leechers: cells[5] ? numericValue(stripTags(cells[5])) : 0,
      category
    });
  }
  return rows;
}

function extractMagnet(html) {
  for (const anchor of String(html || '').matchAll(/<a\b[^>]*>/gi)) {
    const href = attrValue(anchor[0], 'href');
    if (/^magnet:\?/i.test(href)) return href.slice(0, MAX_MAGNET_LENGTH + 1);
  }
  return '';
}

function extCandidates(query, categoryId) {
  const encodedQuery = encodeComponent(query);
  const category = categoryId ? `&cat=${encodeComponent(categoryId)}` : '';
  return [
    `https://extto.org/browse/?q=${encodedQuery}${category}`,
    `https://extto.org/browse/?q=${encodedQuery}&with_adult=1${category}`,
    `https://extto.org/advanced-search/?q=${encodedQuery}${category}`,
    `https://extto.org/search/?q=${encodedQuery}`,
    `https://extto.org/search?search=${encodedQuery}`,
    `https://extto.org/search/${encodedQuery}/1/`
  ];
}

async function searchExtTorrents(input, source, fetchImpl, budget) {
  let rows = [];
  let successfulListResponse = false;
  let lastFailure = null;
  for (const url of extCandidates(input.query, input.categoryId)) {
    try {
      const html = await fetchText(source, url, {}, fetchImpl, budget);
      successfulListResponse = true;
      rows = parseExtList(html);
      if (rows.length) break;
    } catch (error) {
      lastFailure = error;
    }
  }
  if (!rows.length) {
    if (successfulListResponse) return [];
    throw lastFailure instanceof SourceError ? lastFailure : new SourceError('All provider search routes failed');
  }

  const maxDetail = Math.min(rows.length, input.limit, 30);
  const needDetails = [];
  for (let index = 0; index < maxDetail; index += 1)
    if (!rows[index].magnetUri && rows[index].detailUrl) needDetails.push(index);

  let nextDetail = 0;
  let fatalDetailError = null;
  const detailErrors = [];
  async function detailWorker() {
    while (nextDetail < needDetails.length && !fatalDetailError) {
      const index = needDetails[nextDetail++];
      try {
        const html = await fetchText(source, rows[index].detailUrl, {}, fetchImpl, budget);
        rows[index].magnetUri = extractMagnet(html);
      } catch (error) {
        const sourceError = error instanceof SourceError ? error : new SourceError('Could not resolve provider detail page');
        if (sourceError.fatal) fatalDetailError = sourceError;
        else detailErrors.push(sourceError);
      }
    }
  }
  const workerCount = Math.min(MAX_DETAIL_CONCURRENCY, needDetails.length);
  await Promise.all(Array.from({ length: workerCount }, () => detailWorker()));
  if (fatalDetailError) throw fatalDetailError;
  const detailError = detailErrors[0] || null;

  const results = [];
  for (const row of rows) {
    if (results.length >= input.limit || !row.magnetUri) continue;
    const hashMatch = row.magnetUri.match(/btih:([a-f0-9]{40})/i);
    const infoHash = hashMatch ? canonicalInfoHash(hashMatch[1]) : '';
    const result = resultRecord({
      title: row.title,
      magnetUri: row.magnetUri,
      sizeBytes: row.sizeBytes,
      seeders: row.seeders,
      leechers: row.leechers,
      sourceName: 'ExtraTorrents',
      sourceKey: 'exttorrents',
      categoryId: row.category.toLowerCase().replace(/ /g, '_'),
      category: row.category,
      infoHash,
      publishDate: null,
      detailsUrl: row.detailUrl
    });
    if (result) results.push(result);
  }
  if (!results.length && detailError) throw detailError;
  return results;
}

async function searchSource(source, input, fetchImpl, budget) {
  if (source.id === 'piratebay') {
    const url = new URL('https://apibay.org/q.php');
    url.searchParams.set('q', input.query);
    if (input.categoryId) url.searchParams.set('cat', input.categoryId);
    return parsePirateBayResponse(await fetchJson(source, url, {}, fetchImpl, budget)).slice(0, input.limit);
  }
  if (source.id === 'torrentscsv') {
    const url = new URL('https://torrents-csv.com/service/search');
    url.searchParams.set('q', input.query);
    url.searchParams.set('size', String(input.limit));
    return parseTorrentsCsvResponse(await fetchJson(source, url, {}, fetchImpl, budget)).slice(0, input.limit);
  }
  if (source.id === 'exttorrents') return searchExtTorrents(input, source, fetchImpl, budget);
  if (source.id === 'knaben') {
    const body = {
      search_type: 'score',
      search_field: 'title',
      query: input.query,
      hide_unsafe: true,
      hide_xxx: true,
      size: input.limit
    };
    const category = Number.parseInt(input.categoryId, 10);
    if (/^\d+$/.test(input.categoryId) && category > 0) body.categories = [category];
    const response = await fetchJson(source, 'https://api.knaben.org/v1', {
      method: 'POST',
      headers: sourceHeaders({ 'content-type': 'application/json' }),
      body: JSON.stringify(body)
    }, fetchImpl, budget);
    return parseKnabenResponse(response).slice(0, input.limit);
  }
  throw new HttpError(404, 'Source is not available');
}

export function createWorker({ fetchImpl = globalThis.fetch } = {}) {
  if (typeof fetchImpl !== 'function') throw new TypeError('A fetch implementation is required');

  async function fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/healthz') {
      if (request.method !== 'GET') return errorResponse('unknown', new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ok: true, contractVersion: CONTRACT_VERSION, revision: manifest.version });
    }
    if (url.pathname === '/manifest.json') {
      if (request.method !== 'GET') return errorResponse('unknown', new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ...manifest, contractVersion: CONTRACT_VERSION });
    }

    const route = url.pathname.match(/^\/v1\/([^/]+)\/search$/);
    if (!route) return errorResponse('unknown', new HttpError(404, 'Route not found'));
    const sourceId = /^[a-z][a-z0-9]{0,31}$/.test(route[1]) ? route[1] : 'unknown';
    if (request.method !== 'POST') return errorResponse(sourceId, new HttpError(405, 'Method not allowed'));
    const source = SOURCES.get(sourceId);
    if (!source) return errorResponse(sourceId, new HttpError(404, 'Source is not available'));
    const contentType = request.headers.get('content-type') || '';
    if (!/^application\/json(?:\s*;|$)/i.test(contentType))
      return errorResponse(sourceId, new HttpError(415, 'Content-Type must be application/json'));

    try {
      const input = validateRequest(parseJson(await readRequestBody(request)), source);
      const budget = { used: 0, deadline: Date.now() + SEARCH_DEADLINE_MS };
      const result = await searchSource(source, input, fetchImpl, budget);
      if (!Array.isArray(result)) throw new SourceError('Provider result must be an array');
      const payload = { contractVersion: CONTRACT_VERSION, sourceId, result };
      const serialized = JSON.stringify(payload);
      if (utf8Length(serialized) > MAX_RESULT_BYTES)
        throw new SourceError('Provider result is too large');
      return jsonResponse(payload);
    } catch (error) {
      return errorResponse(sourceId, error);
    }
  }

  return Object.freeze({ fetch });
}

export { CONTRACT_VERSION, parseExtList, parseKnabenResponse, parsePirateBayResponse, parseTorrentsCsvResponse };
