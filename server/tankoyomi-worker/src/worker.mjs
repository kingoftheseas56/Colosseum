import { Buffer } from 'node:buffer';
import { createDecipheriv, createHash } from 'node:crypto';

const CONTRACT_VERSION = 1;
const MAX_REQUEST_BYTES = 64 * 1024;
const MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
const MAX_RESULT_BYTES = 1024 * 1024;
const MAX_REDIRECTS = 5;
const MAX_PROVIDER_SUBREQUESTS = 40;
const DEFAULT_TIMEOUT_MS = 15_000;
const MAX_TIMEOUT_MS = 30_000;
const REQUEST_BODY_TIMEOUT_MS = 10_000;
const ALLOWED_METHODS = new Set(['searchSeries', 'getChapters', 'getPages']);
const ALLOWED_HEADERS = new Set([
  'accept', 'authorization', 'content-type', 'hx-request', 'hx-target', 'origin',
  'referer', 'x-requested-with'
]);

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
  const status = error instanceof HttpError ? error.status : 502;
  const message = typeof error?.message === 'string' && error.message.trim()
    ? error.message.trim().slice(0, 300)
    : 'Provider request failed';
  return jsonResponse({ contractVersion: CONTRACT_VERSION, error: message }, status);
}

function byteLength(value) {
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
  if (signal.aborted) return Promise.reject(signal.reason || new Error('Upstream request timed out'));

  let onAbort;
  const aborted = new Promise((_, reject) => {
    onAbort = () => reject(signal.reason || new Error('Upstream request timed out'));
    signal.addEventListener('abort', onAbort, { once: true });
  });
  return Promise.race([promise, aborted]).finally(() => signal.removeEventListener('abort', onAbort));
}

async function readBoundedStream(stream, limit, signal) {
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
      if (total > limit) {
        await reader.cancel().catch(() => {});
        throw new Error('Upstream response is too large');
      }
      chunks.push(chunk);
    }
  } catch (error) {
    await reader.cancel().catch(() => {});
    if (signal?.aborted) throw new Error('Upstream request timed out');
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

async function readBoundedRequest(request) {
  if (contentLength(request.headers) > MAX_REQUEST_BYTES)
    throw new HttpError(413, 'Request body is too large');
  try {
    return await readBoundedStream(request.body, MAX_REQUEST_BYTES, AbortSignal.timeout(REQUEST_BODY_TIMEOUT_MS));
  } catch (error) {
    if (error instanceof HttpError) throw error;
    if (error?.message === 'Upstream request timed out')
      throw new HttpError(408, 'Request body timed out');
    if (error?.message === 'Upstream response is too large')
      throw new HttpError(413, 'Request body is too large');
    throw new HttpError(400, 'Could not read request body');
  }
}

function parseRequestBody(text) {
  try {
    return JSON.parse(text);
  } catch (_) {
    throw new HttpError(400, 'Request body must be valid JSON');
  }
}

function validateArguments(method, body) {
  if (!body || typeof body !== 'object' || Array.isArray(body))
    throw new HttpError(400, 'Request body must be a JSON object');
  if (method === 'searchSeries') {
    if (typeof body.query !== 'string' || !body.query.trim() || body.query.length > 500)
      throw new HttpError(400, 'query must be a non-empty string of at most 500 characters');
    return body.query.trim();
  }
  const key = method === 'getChapters' ? 'series' : 'chapter';
  const value = body[key];
  if (!value || typeof value !== 'object' || Array.isArray(value))
    throw new HttpError(400, `${key} must be a JSON object`);
  return value;
}

function ipLiteralHost(hostname) {
  const host = String(hostname || '').replace(/^\[|\]$/g, '');
  // URL canonicalizes shortened, hexadecimal, and integer IPv4 forms before
  // exposing hostname. Reject all address literals, including public ones,
  // to keep the provider transport limited to named hosts.
  return host.includes(':') || /^\d+(?:\.\d+){0,3}$/.test(host);
}

function normalizedHost(hostname) {
  return String(hostname || '').toLowerCase().replace(/\.$/, '');
}

function validatedUrl(value, allowedHosts) {
  let url;
  try {
    url = new URL(value);
  } catch (_) {
    throw new Error('Provider requested an invalid URL');
  }
  if (url.protocol !== 'https:') throw new Error('Provider requests must use HTTPS');
  if (url.port && url.port !== '443') throw new Error('Provider requests must use port 443');
  if (url.username || url.password) throw new Error('Provider request URL credentials are not allowed');
  const host = normalizedHost(url.hostname);
  if (ipLiteralHost(url.hostname)) throw new Error('Provider request IP addresses are not allowed');
  if (host === 'localhost' || host.endsWith('.localhost'))
    throw new Error(`Provider request host is not allowed: ${host}`);
  const allowed = [...allowedHosts].some(entry => host === entry || host.endsWith(`.${entry}`));
  if (!allowed) throw new Error(`Provider request host is not allowed: ${host || '[invalid]'}`);
  return url;
}

function normalizedHeaders(value) {
  const headers = new Headers();
  if (value == null) return headers;
  const pairs = value instanceof Headers
    ? (() => { const out = []; value.forEach((v, k) => out.push([k, v])); return out; })()
    : Array.isArray(value) ? value : Object.entries(value);
  for (const pair of pairs) {
    if (!Array.isArray(pair) || pair.length < 2) throw new Error('Provider headers are invalid');
    const name = String(pair[0]).toLowerCase();
    const headerValue = String(pair[1]);
    if (!ALLOWED_HEADERS.has(name)) throw new Error(`Provider header is not allowed: ${name}`);
    if (headerValue.length > 4096 || /[\r\n]/.test(headerValue))
      throw new Error(`Provider header value is invalid: ${name}`);
    headers.set(name, headerValue);
  }
  return headers;
}

function normalizedMethod(value) {
  const method = String(value || 'GET').toUpperCase();
  if (method !== 'GET' && method !== 'POST') throw new Error('Provider method must be GET or POST');
  return method;
}

function normalizedBody(method, body) {
  if (body == null) return undefined;
  if (method !== 'POST') throw new Error('GET requests cannot include a body');
  if (typeof body !== 'string' && !(body instanceof Uint8Array) && !(body instanceof ArrayBuffer))
    throw new Error('Provider request body must be text or bytes');
  const bytes = typeof body === 'string' ? byteLength(body) : body.byteLength;
  if (bytes > MAX_REQUEST_BYTES) throw new Error('Provider request body is too large');
  return body;
}

function base64Bytes(value, maxLength) {
  if (typeof value !== 'string' || value.length > maxLength
      || !/^[A-Za-z0-9+/_-]*={0,2}$/.test(value) || value.length % 4 === 1)
    throw new Error('Base64 input is invalid or too large');
  const normalized = value.replace(/-/g, '+').replace(/_/g, '/');
  const bytes = Buffer.from(normalized, 'base64');
  if (bytes.length === 0 && value.length > 0) throw new Error('Base64 input is invalid');
  return bytes;
}

function decodeBase64(value) {
  return base64Bytes(value, 16 * 1024).toString('utf8');
}

function decryptCryptoJs(ciphertextBase64, saltHex, password) {
  if (typeof password !== 'string' || password.length > 1024)
    throw new Error('CryptoJS password is invalid');
  if (typeof saltHex !== 'string' || !/^[a-f\d]{16}$/i.test(saltHex))
    throw new Error('CryptoJS salt is invalid');
  const ciphertext = base64Bytes(ciphertextBase64, MAX_RESPONSE_BYTES * 2);
  if (ciphertext.length === 0 || ciphertext.length % 16 !== 0)
    throw new Error('CryptoJS ciphertext is invalid');

  const salt = Buffer.from(saltHex, 'hex');
  const passwordBytes = Buffer.from(password, 'utf8');
  let material = Buffer.alloc(0);
  let previous = Buffer.alloc(0);
  while (material.length < 48) {
    previous = createHash('md5')
      .update(previous)
      .update(passwordBytes)
      .update(salt)
      .digest();
    material = Buffer.concat([material, previous]);
  }

  try {
    const decipher = createDecipheriv('aes-256-cbc', material.subarray(0, 32), material.subarray(32, 48));
    return Buffer.concat([decipher.update(ciphertext), decipher.final()]).toString('utf8');
  } catch (_) {
    throw new Error('CryptoJS AES decryption failed');
  }
}

function allowedHostSet(providerEntry) {
  const hosts = new Set();
  for (const value of Array.isArray(providerEntry?.allowedHosts) ? providerEntry.allowedHosts : []) {
    if (typeof value !== 'string' || !value.trim()) continue;
    const host = normalizedHost(value.trim());
    if (host && !host.includes('/') && !host.includes(':') && !host.includes('*')) hosts.add(host);
  }
  return hosts;
}

function createProviderContext(providerEntry, fetchImpl, budget) {
  const allowedHosts = allowedHostSet(providerEntry);
  async function requestText(input, options = {}) {
    if (typeof input !== 'string' && !(input instanceof URL))
      throw new Error('Provider URL must be a string or URL');
    const originalUrl = String(input);
    const method = normalizedMethod(options?.method);
    const body = normalizedBody(method, options?.body);
    const headers = normalizedHeaders(options?.headers);
    const timeoutValue = Number(options?.timeoutMs);
    const timeoutMs = Number.isFinite(timeoutValue) && timeoutValue > 0
      ? Math.min(MAX_TIMEOUT_MS, Math.max(1, Math.floor(timeoutValue)))
      : DEFAULT_TIMEOUT_MS;
    const controllerSignal = AbortSignal.timeout(timeoutMs);
    let currentUrl = validatedUrl(originalUrl, allowedHosts);
    let currentMethod = method;
    let currentBody = body;
    let redirects = 0;

    while (true) {
      if (budget.used >= MAX_PROVIDER_SUBREQUESTS) {
        budget.exhausted = true;
        throw new Error('Provider request limit exceeded');
      }
      budget.used++;
      let response;
      try {
        response = await withSignal(fetchImpl(currentUrl.href, {
          method: currentMethod,
          headers,
          body: currentBody,
          redirect: 'manual',
          signal: controllerSignal
        }), controllerSignal);
      } catch (error) {
        if (controllerSignal.aborted) throw new Error('Upstream request timed out');
        throw new Error(error?.message === 'fetch failed' ? 'Upstream request failed' : (error?.message || 'Upstream request failed'));
      }

      if ([301, 302, 303, 307, 308].includes(response.status)) {
        if (redirects >= MAX_REDIRECTS) {
          await response.body?.cancel().catch(() => {});
          throw new Error('Upstream redirect limit exceeded');
        }
        const location = response.headers?.get?.('location');
        await response.body?.cancel().catch(() => {});
        if (!location) throw new Error('Upstream redirect did not provide a location');
        currentUrl = validatedUrl(new URL(location, currentUrl).href, allowedHosts);
        redirects++;
        if (response.status === 303 || ((response.status === 301 || response.status === 302) && currentMethod === 'POST')) {
          currentMethod = 'GET';
          currentBody = undefined;
        }
        if (normalizedHost(currentUrl.hostname) !== normalizedHost(new URL(originalUrl).hostname))
          headers.delete('authorization');
        continue;
      }

      if (!response.ok) {
        await response.body?.cancel().catch(() => {});
        throw new Error(`Upstream returned HTTP ${response.status}`);
      }
      if (contentLength(response.headers) > MAX_RESPONSE_BYTES) {
        await response.body?.cancel().catch(() => {});
        throw new Error('Upstream response is too large');
      }
      return readBoundedStream(response.body, MAX_RESPONSE_BYTES, controllerSignal);
    }
  }

  return Object.freeze({
    fetchText: requestText,
    fetchJson: async (url, options) => {
      const text = await requestText(url, options);
      try {
        return JSON.parse(text);
      } catch (_) {
        throw new Error('Upstream returned invalid JSON');
      }
    },
    decodeBase64,
    decryptCryptoJs
  });
}

function providerFrom(providers, key) {
  if (providers instanceof Map) return providers.get(key);
  return providers?.[key];
}

export function createWorker({ manifest, providers = {}, fetchImpl = globalThis.fetch } = {}) {
  if (!manifest || !Array.isArray(manifest.languages)) throw new TypeError('A Tankoyomi manifest is required');
  if (typeof fetchImpl !== 'function') throw new TypeError('A fetch implementation is required');

  async function fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/healthz') {
      if (request.method !== 'GET') return errorResponse(new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ok: true, contractVersion: CONTRACT_VERSION, revision: String(manifest.version || 'unknown') });
    }
    if (url.pathname === '/manifest.json') {
      if (request.method !== 'GET') return errorResponse(new HttpError(405, 'Method not allowed'));
      return jsonResponse({ ...manifest, contractVersion: CONTRACT_VERSION });
    }

    const segments = url.pathname.split('/').slice(1);
    if (segments.length !== 4 || segments[0] !== 'v1')
      return errorResponse(new HttpError(404, 'Route not found'));
    if (request.method !== 'POST') return errorResponse(new HttpError(405, 'Method not allowed'));

    const [, languageCode, providerId, method] = segments;
    if (!ALLOWED_METHODS.has(method)) return errorResponse(new HttpError(404, 'Provider method not found'));
    const language = manifest.languages.find(item => item?.code === languageCode);
    if (!language) return errorResponse(new HttpError(404, 'Canonical language not found'));
    const providerEntry = (Array.isArray(language.providers) ? language.providers : [])
      .find(item => item?.id === providerId);
    if (!providerEntry) return errorResponse(new HttpError(404, 'Provider is not registered for this language'));

    const provider = providerFrom(providers, `${languageCode}/${providerId}`);
    if (!provider || typeof provider[method] !== 'function')
      return errorResponse(new HttpError(404, 'Provider implementation is unavailable'));
    const contentType = request.headers.get('content-type') || '';
    if (!/^application\/json(?:\s*;|$)/i.test(contentType))
      return errorResponse(new HttpError(415, 'Content-Type must be application/json'));

    try {
      const bodyText = await readBoundedRequest(request);
      const input = validateArguments(method, parseRequestBody(bodyText));
      const budget = { used: 0, exhausted: false };
      const context = createProviderContext(providerEntry, fetchImpl, budget);
      const result = await provider[method](context, input);
      if (budget.exhausted) throw new Error('Provider request limit exceeded');
      if (!Array.isArray(result)) throw new Error('Provider result must be an array');
      const payload = { contractVersion: CONTRACT_VERSION, result };
      const serialized = JSON.stringify(payload);
      if (byteLength(serialized) > MAX_RESULT_BYTES)
        throw new Error('Provider result is too large');
      return new Response(serialized, {
        status: 200,
        headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' }
      });
    } catch (error) {
      if (error instanceof HttpError) return errorResponse(error);
      return errorResponse(error);
    }
  }

  return Object.freeze({ fetch });
}

export { CONTRACT_VERSION };
