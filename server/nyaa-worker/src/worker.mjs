import { XMLParser, XMLValidator } from 'fast-xml-parser';

const CONTRACT_VERSION = 1;
const SOURCE_ID = 'nyaa';
const MAX_REQUEST_BYTES = 8 * 1024;
const MAX_UPSTREAM_BYTES = 1024 * 1024;
const MAX_QUERY_LENGTH = 512;
const MAX_RESULTS = 100;
const MAX_TITLE_LENGTH = 512;
const MAX_UPLOADER_LENGTH = 256;
const UPSTREAM_TIMEOUT_MS = 15_000;
const BODY_TIMEOUT_MS = 15_000;
const UPSTREAM_ORIGIN = 'https://nyaa.si';
const MAX_INT32 = 2_147_483_647;
const MAX_SAFE_SIZE = Number.MAX_SAFE_INTEGER;

const parser = new XMLParser({
  ignoreAttributes: true,
  removeNSPrefix: true,
  parseTagValue: false,
  trimValues: false,
  processEntities: {
    enabled: true,
    maxEntitySize: 128,
    maxExpansionDepth: 8,
    maxTotalExpansions: 2_000,
    maxExpandedLength: MAX_UPSTREAM_BYTES,
    maxEntityCount: 32
  },
  htmlEntities: false,
  ignoreDeclaration: true,
  maxNestedTags: 64
});

export const manifest = Object.freeze({
  id: 'colosseum.well.nyaa',
  version: '1.0.0',
  name: 'Nyaa',
  description: 'Colosseum Nyaa search metadata',
  resources: ['stream'],
  types: ['manga'],
  catalogs: [],
  idPrefixes: [],
  behaviorHints: { configurable: false },
  sources: [{ id: SOURCE_ID, name: 'Nyaa' }]
});

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
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

function errorResponse(error) {
  const status = Number.isInteger(error?.status) ? error.status : 502;
  const message = error instanceof HttpError ? error.message : 'Nyaa request failed';
  return jsonResponse({ contractVersion: CONTRACT_VERSION, sourceId: SOURCE_ID, error: message }, status);
}

function utf8Length(value) {
  return new TextEncoder().encode(value).byteLength;
}

function contentLength(headers) {
  const raw = headers?.get?.('content-length')?.trim();
  if (!raw || !/^\d+$/.test(raw)) return null;
  const parsed = Number(raw);
  return Number.isSafeInteger(parsed) ? parsed : null;
}

function withSignal(promise, signal) {
  if (!signal) return promise;
  if (signal.aborted) return Promise.reject(signal.reason || new Error('Aborted'));
  let onAbort;
  const aborted = new Promise((_, reject) => {
    onAbort = () => reject(signal.reason || new Error('Aborted'));
    signal.addEventListener('abort', onAbort, { once: true });
  });
  return Promise.race([promise, aborted]).finally(() => signal.removeEventListener('abort', onAbort));
}

async function readBoundedText(stream, maxBytes, signal, tooLargeMessage) {
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
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch (_) {
    throw new Error('Invalid UTF-8');
  }
}

async function readRequestBody(request, timeoutMs) {
  if (contentLength(request.headers) > MAX_REQUEST_BYTES)
    throw new HttpError(413, 'Request body is too large');
  const timeoutSignal = AbortSignal.timeout(timeoutMs);
  const signal = AbortSignal.any([request.signal, timeoutSignal]);
  try {
    return await readBoundedText(request.body, MAX_REQUEST_BYTES, signal, 'Request body is too large');
  } catch (error) {
    if (timeoutSignal.aborted) throw new HttpError(408, 'Request body timed out');
    if (error?.message === 'Request body is too large') throw new HttpError(413, error.message);
    throw new HttpError(400, 'Could not read request body');
  }
}

function parseRequestJson(text) {
  let body;
  try { body = JSON.parse(text); }
  catch (_) { throw new HttpError(400, 'Request body must be valid JSON'); }
  if (!body || typeof body !== 'object' || Array.isArray(body))
    throw new HttpError(400, 'Request body must be a JSON object');
  if (typeof body.query !== 'string' || !body.query.trim() || body.query.length > MAX_QUERY_LENGTH)
    throw new HttpError(400, `query must be a non-empty string of at most ${MAX_QUERY_LENGTH} characters`);
  return body.query;
}

function parseCount(value) {
  const text = typeof value === 'string' ? value.trim() : '';
  if (!/^\d+$/.test(text)) return 0;
  const number = Number(text);
  return Number.isSafeInteger(number) && number <= MAX_INT32 ? number : 0;
}

function parseSizeBytes(value) {
  const text = fieldText(value).trim();
  const match = text.match(/^(\d+(?:\.\d+)?)\s*(B|bytes?|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)?$/i);
  if (!match) return 0;
  const quantity = Number(match[1]);
  if (!Number.isFinite(quantity) || quantity < 0) return 0;
  const unit = (match[2] || 'B').toLowerCase();
  const decimal = { b: 1, byte: 1, bytes: 1, kb: 1_000, mb: 1_000 ** 2, gb: 1_000 ** 3, tb: 1_000 ** 4, pb: 1_000 ** 5 };
  const binary = { kib: 1024, mib: 1024 ** 2, gib: 1024 ** 3, tib: 1024 ** 4, pib: 1024 ** 5 };
  const multiplier = decimal[unit] ?? binary[unit];
  if (!multiplier) return 0;
  const bytes = Math.round(quantity * multiplier);
  return Number.isSafeInteger(bytes) && bytes >= 0 && bytes <= MAX_SAFE_SIZE ? bytes : 0;
}

function fieldText(value) {
  if (Array.isArray(value)) {
    for (const item of value) {
      const text = fieldText(item);
      if (text) return text;
    }
    return '';
  }
  if (value && typeof value === 'object') {
    if (Object.hasOwn(value, '#text')) return fieldText(value['#text']);
    return '';
  }
  return typeof value === 'string' ? value : '';
}

function firstText(item, names) {
  for (const name of names) {
    const value = fieldText(item[name]).trim();
    if (value) return value;
  }
  return '';
}

function numericNyaaId(item) {
  const link = fieldText(item.link).trim();
  const downloadMatch = link.match(/^https:\/\/nyaa\.si\/download\/(\d+)\.torrent$/);
  if (downloadMatch) return downloadMatch[1];

  for (const name of ['link', 'guid']) {
    const value = fieldText(item[name]).trim();
    const viewMatch = value.match(/^https:\/\/nyaa\.si\/view\/(\d+)$/);
    if (viewMatch) return viewMatch[1];
  }
  return '';
}

function parseNyaaRss(xml) {
  if (/<!DOCTYPE\b/i.test(xml)) throw new HttpError(502, 'Nyaa returned unsupported XML');
  const valid = XMLValidator.validate(xml);
  if (valid !== true) throw new HttpError(502, 'Nyaa returned malformed RSS');

  let parsed;
  try { parsed = parser.parse(xml); }
  catch (_) { throw new HttpError(502, 'Nyaa returned malformed RSS'); }
  const channel = parsed?.rss?.channel;
  if (channel === '') return [];
  if (!channel || typeof channel !== 'object') throw new HttpError(502, 'Nyaa returned malformed RSS');
  const items = channel.item === undefined ? [] : Array.isArray(channel.item) ? channel.item : [channel.item];
  const results = [];

  for (const item of items) {
    if (!item || typeof item !== 'object' || Array.isArray(item)) continue;
    const title = firstText(item, ['title']).slice(0, MAX_TITLE_LENGTH);
    const uploader = firstText(item, ['uploader', 'author', 'creator', 'username']).slice(0, MAX_UPLOADER_LENGTH);
    const infoHash = firstText(item, ['infoHash', 'hash']).toLowerCase();
    const id = numericNyaaId(item);
    if (!title || !/^[a-f0-9]{40}$/.test(infoHash) || !id) continue;

    results.push({
      title,
      infoHash,
      uploader,
      seeders: parseCount(item.seeders),
      leechers: parseCount(item.leechers),
      sizeBytes: parseSizeBytes(item.size),
      torrentUrl: `${UPSTREAM_ORIGIN}/download/${id}.torrent`
    });
    if (results.length >= MAX_RESULTS) break;
  }
  return results;
}

function upstreamUrl(query) {
  const url = new URL('/', UPSTREAM_ORIGIN);
  url.searchParams.set('page', 'rss');
  url.searchParams.set('c', '3_1');
  url.searchParams.set('s', 'seeders');
  url.searchParams.set('o', 'desc');
  url.searchParams.set('q', query);
  return url;
}

async function searchNyaa(query, fetchImpl, timeoutMs) {
  const signal = AbortSignal.timeout(timeoutMs);
  let response;
  try {
    response = await withSignal(fetchImpl(upstreamUrl(query).href, {
      method: 'GET',
      headers: { accept: 'application/rss+xml, application/xml;q=0.9, text/xml;q=0.8' },
      redirect: 'manual',
      signal
    }), signal);
  } catch (_) {
    if (signal.aborted) throw new HttpError(504, 'Nyaa upstream request timed out');
    throw new HttpError(502, 'Nyaa upstream request failed');
  }

  if (response.status >= 300 && response.status < 400) {
    await response.body?.cancel().catch(() => {});
    throw new HttpError(502, 'Nyaa redirects are not allowed');
  }
  if (!response.ok) {
    await response.body?.cancel().catch(() => {});
    throw new HttpError(502, `Nyaa returned HTTP ${response.status}`);
  }
  if (contentLength(response.headers) > MAX_UPSTREAM_BYTES) {
    await response.body?.cancel().catch(() => {});
    throw new HttpError(502, 'Nyaa response is too large');
  }

  let xml;
  try {
    xml = await readBoundedText(response.body, MAX_UPSTREAM_BYTES, signal, 'Nyaa response is too large');
  } catch (error) {
    if (signal.aborted) throw new HttpError(504, 'Nyaa upstream request timed out');
    if (error?.message === 'Nyaa response is too large') throw new HttpError(502, error.message);
    if (error?.message === 'Invalid UTF-8') throw new HttpError(502, 'Nyaa returned invalid UTF-8');
    throw new HttpError(502, 'Could not read Nyaa response');
  }
  return parseNyaaRss(xml);
}

export function createWorker({ fetchImpl = globalThis.fetch, timeoutMs = UPSTREAM_TIMEOUT_MS, bodyTimeoutMs = BODY_TIMEOUT_MS } = {}) {
  if (typeof fetchImpl !== 'function') throw new TypeError('A fetch implementation is required');
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new TypeError('timeoutMs must be positive');
  if (!Number.isFinite(bodyTimeoutMs) || bodyTimeoutMs <= 0) throw new TypeError('bodyTimeoutMs must be positive');

  async function fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/healthz') {
      if (request.method !== 'GET') return errorResponse(new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ok: true, contractVersion: CONTRACT_VERSION, revision: manifest.version });
    }
    if (url.pathname === '/manifest.json') {
      if (request.method !== 'GET') return errorResponse(new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ...manifest, contractVersion: CONTRACT_VERSION });
    }
    if (url.pathname !== '/v1/nyaa/search') return errorResponse(new HttpError(404, 'Route not found'));
    if (request.method !== 'POST') return errorResponse(new HttpError(405, 'Method not allowed'));
    if (!/^application\/json(?:\s*;|$)/i.test(request.headers.get('content-type') || ''))
      return errorResponse(new HttpError(415, 'Content-Type must be application/json'));

    try {
      const query = parseRequestJson(await readRequestBody(request, bodyTimeoutMs));
      const result = await searchNyaa(query, fetchImpl, timeoutMs);
      return jsonResponse({ contractVersion: CONTRACT_VERSION, sourceId: SOURCE_ID, result });
    } catch (error) {
      return errorResponse(error);
    }
  }

  return Object.freeze({ fetch });
}

export { CONTRACT_VERSION, MAX_QUERY_LENGTH, MAX_REQUEST_BYTES, MAX_RESULTS, MAX_UPSTREAM_BYTES, UPSTREAM_TIMEOUT_MS, parseNyaaRss };
