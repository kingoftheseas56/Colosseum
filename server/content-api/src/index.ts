import schema from './source-schema.json';

interface Env { CATALOG: D1Database; METADATA: D1Database; ADMIN_TOKEN: string; SESSION_SECRET: string; ENVIRONMENT: string; TANKOBAN_GUEST_ENABLED?: string; GUEST_RATE_LIMITER?: RateLimit }
type Table = { columns: string[]; rowCount: number; sourceHash: string; writeWeight: number };
const tables: Record<string, Table> = schema;
const encoder = new TextEncoder();
const kinds = new Set(['artist', 'release', 'release-group', 'recording', 'work', 'label']);
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
class Failure extends Error { constructor(public status: number, public code: string) { super(code); } }
function json(value: unknown, status = 200) { return Response.json(value, { status, headers: { 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' } }); }
function base64(bytes: Uint8Array) { return btoa(String.fromCharCode(...bytes)).replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', ''); }
function decode(value: string) { return Uint8Array.from(atob(value.replaceAll('-', '+').replaceAll('_', '/')), c => c.charCodeAt(0)); }
async function key(secret: string) { return crypto.subtle.importKey('raw', encoder.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign', 'verify']); }
async function digest(value: string) { return base64(new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(value)))); }
async function admin(token: string, secret: string) {
  if (!secret || !token) return false;
  const k = await key(secret);
  const signature = await crypto.subtle.sign('HMAC', k, encoder.encode(secret));
  return crypto.subtle.verify('HMAC', k, signature, encoder.encode(token));
}
async function lease(token: string, secret: string, semantic = false) {
  if (!secret || token.length > 2048) return false;
  try {
    const [body, sig, extra] = token.split('.');
    if (!body || !sig || extra) return false;
    if (!await crypto.subtle.verify('HMAC', await key(secret), decode(sig), encoder.encode(body))) return false;
    const claims = JSON.parse(new TextDecoder().decode(decode(body)));
    const now = Math.floor(Date.now() / 1000);
    if (semantic && claims.aud === 'colosseum-content-api') return claims.scope === 'tankoban:read' && claims.apiVersion === 1 && claims.sourceHash === tables.tankoban__series.sourceHash && Number.isInteger(claims.iat) && claims.iat <= now && Number.isInteger(claims.exp) && claims.exp > now && claims.exp - claims.iat === 300 && claims.exp <= now + 300;
    return claims.aud === 'content-api' && claims.scope === 'read' && Number.isInteger(claims.exp) && claims.exp > now && claims.exp <= now + 1800;
  } catch { return false; }
}
async function body(request: Request): Promise<any> {
  const reader = request.body?.getReader();
  if (!reader) throw new Failure(400, 'BODY_REQUIRED');
  const chunks: Uint8Array[] = []; let size = 0;
  for (;;) {
    const next = await reader.read(); if (next.done) break;
    size += next.value.byteLength;
    if (size > 262144) { await reader.cancel(); throw new Failure(413, 'BODY_TOO_LARGE'); }
    chunks.push(next.value);
  }
  const data = new Uint8Array(size); let offset = 0;
  for (const chunk of chunks) { data.set(chunk, offset); offset += chunk.length; }
  try { const parsed = JSON.parse(new TextDecoder().decode(data)); if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) throw 0; return parsed; }
  catch { throw new Failure(400, 'INVALID_JSON'); }
}
function tableFor(name: unknown, sourceHash: unknown) {
  if (typeof name !== 'string' || !Object.hasOwn(tables, name)) throw new Failure(400, 'UNKNOWN_TABLE');
  const table = tables[name];
  if (sourceHash !== table.sourceHash) throw new Failure(409, 'SOURCE_CHANGED');
  return table;
}
function metadataKey(kind: unknown, mbid: unknown) {
  if (typeof kind !== 'string' || !kinds.has(kind) || typeof mbid !== 'string' || !uuid.test(mbid)) throw new Failure(400, 'INVALID_MUSICBRAINZ_ID');
}

async function guestGate(request: Request, env: Env) {
  if (env.TANKOBAN_GUEST_ENABLED !== 'true' || !env.SESSION_SECRET || env.SESSION_SECRET.length < 32 || !env.GUEST_RATE_LIMITER) throw new Failure(503, 'GUEST_UNAVAILABLE');
  const ip = request.headers.get('CF-Connecting-IP');
  if (!ip) throw new Failure(503, 'GUEST_UNAVAILABLE');
  if (!(await env.GUEST_RATE_LIMITER.limit({ key: ip })).success) throw new Failure(429, 'RATE_LIMITED');
}
async function tankobanReady(env: Env) {
  const names = ['tankoban__series', 'tankoban__volumes'];
  if (tables[names[0]].sourceHash !== tables[names[1]].sourceHash) throw new Failure(503, 'CATALOGUE_NOT_READY');
  const rows = await env.CATALOG.prepare('SELECT name,ready,source_hash,expected_rows,imported_rows FROM _source_tables WHERE name IN (?,?)').bind(...names).all();
  for (const name of names) {
    const row = rows.results.find(r => r.name === name);
    if (!row || row.ready !== 1 || row.source_hash !== tables[name].sourceHash || row.expected_rows !== tables[name].rowCount || row.imported_rows !== tables[name].rowCount) throw new Failure(503, 'CATALOGUE_NOT_READY');
  }
}
function naturalKey(value: string) { return value.toLowerCase().replace(/\p{Nd}+/gu, run => run.padStart(12, '0')); }
async function tankoban(env: Env, malId: number) {
  await tankobanReady(env);
  const info = await env.CATALOG.prepare('SELECT volume_count,count_basis FROM tankoban__series WHERE mal_id=?').bind(malId).first<{volume_count:number;count_basis:string}>();
  let seriesInfo: object = {}; const volumes: {number:string;cover:string;name:string}[] = [];
  if (info) {
    if (!Number.isInteger(info.volume_count) || info.volume_count < 0 || info.volume_count > 2000 || typeof info.count_basis !== 'string') throw new Failure(503, 'CATALOGUE_INVALID');
    seriesInfo = {volumeCount:info.volume_count,countBasis:info.count_basis};
    // Bound row count and aggregate UTF-8 bytes in SQLite before fetching baked facts.
    const total = await env.CATALOG.prepare("SELECT COUNT(*) AS n,coalesce(SUM(length(CAST(number AS BLOB))+length(CAST(coalesce(cover_url,'') AS BLOB))+length(CAST(coalesce(name,'') AS BLOB))),0) AS bytes FROM tankoban__volumes WHERE mal_id=?").bind(malId).first<{n:number;bytes:number}>();
    if (!total || total.n > 2000 || total.bytes > 524288) throw new Failure(503, 'RESPONSE_TOO_LARGE');
    const baked = await env.CATALOG.prepare('SELECT number,cover_url,name FROM tankoban__volumes WHERE mal_id=? ORDER BY number LIMIT 2001').bind(malId).all();
    if (total.n !== baked.results.length || baked.results.length > 2000) throw new Failure(503, 'RESPONSE_TOO_LARGE');
    const overlay = new Map<string,{number:string;cover:string;name:string}>();
    for (const row of baked.results) {
      if (typeof row.number !== 'string' || (row.cover_url !== null && typeof row.cover_url !== 'string') || (row.name !== null && typeof row.name !== 'string')) throw new Failure(503, 'CATALOGUE_INVALID');
      overlay.set(row.number,{number:row.number,cover:(row.cover_url as string) ?? '',name:(row.name as string) ?? ''});
    }
    for (let n=1;n<=info.volume_count;n++) { const number=String(n); volumes.push(overlay.get(number) ?? {number,cover:'',name:''}); overlay.delete(number); }
    volumes.push(...overlay.values());
    if (volumes.length > 2000) throw new Failure(503, 'RESPONSE_TOO_LARGE');
    volumes.sort((a,b) => {const x=naturalKey(a.number),y=naturalKey(b.number); return x<y?-1:x>y?1:0;});
  }
  const value = {apiVersion:1,sourceHash:tables.tankoban__series.sourceHash,ready:true,seriesInfo,volumes};
  if (encoder.encode(JSON.stringify(value)).byteLength > 524288) throw new Failure(503, 'RESPONSE_TOO_LARGE');
  return json(value);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      const url = new URL(request.url); const path = url.pathname;
      if (request.method === 'GET' && path === '/healthz') return json({ ok: true, environment: env.ENVIRONMENT });
      const semantic = path.match(/^\/v1\/tankoban\/series\/([1-9][0-9]*)$/);
      if (request.method === 'POST' && path === '/v1/guest/session') {
        await guestGate(request, env);
        const input = await body(request);
        if (input.appId !== 'colosseum' || input.apiVersion !== 1 || Object.keys(input).length !== 2) throw new Failure(400, 'INVALID_GUEST_REQUEST');
        await tankobanReady(env);
        const iat = Math.floor(Date.now()/1000), exp = iat+300;
        const claims = base64(encoder.encode(JSON.stringify({aud:'colosseum-content-api',scope:'tankoban:read',apiVersion:1,sourceHash:tables.tankoban__series.sourceHash,iat,exp})));
        const signature = base64(new Uint8Array(await crypto.subtle.sign('HMAC',await key(env.SESSION_SECRET),encoder.encode(claims))));
        return json({token:`${claims}.${signature}`,expiresAt:new Date(exp*1000).toISOString()});
      }
      if (semantic) await guestGate(request, env);
      const token = request.headers.get('Authorization')?.match(/^Bearer (.+)$/)?.[1] ?? '';
      const isAdmin = path.startsWith('/admin/');
      if (!(isAdmin ? await admin(token, env.ADMIN_TOKEN) : await lease(token, env.SESSION_SECRET, !!semantic))) throw new Failure(401, 'UNAUTHORIZED');
      if (request.method === 'GET' && semantic) {
        const malId = Number(semantic[1]);
        if (!Number.isSafeInteger(malId) || malId > 2147483647) throw new Failure(400, 'INVALID_MAL_ID');
        return await tankoban(env, malId);
      }
      if (request.method === 'POST' && path === '/admin/session') {
        // Preserve the staging-only broad read lease; guest leases above remain semantic-only.
        if (env.ENVIRONMENT !== 'staging') throw new Failure(404, 'NOT_FOUND');
        const exp = Math.floor(Date.now() / 1000) + 1800;
        const claims = base64(encoder.encode(JSON.stringify({ aud: 'content-api', scope: 'read', exp })));
        const signature = base64(new Uint8Array(await crypto.subtle.sign('HMAC', await key(env.SESSION_SECRET), encoder.encode(claims))));
        return json({ token: `${claims}.${signature}`, expiresAt: new Date(exp * 1000).toISOString() });
      }
      if (request.method === 'GET' && (path === '/admin/status' || path === '/v1/catalogues')) {
        const rows = await env.CATALOG.prepare('SELECT name,source_hash,expected_rows,imported_rows,ready FROM _source_tables ORDER BY name').all();
        if (isAdmin) return json({ tables: rows.results, budget: (await env.CATALOG.prepare('SELECT day,reserved_writes FROM _import_budget ORDER BY day DESC LIMIT 7').all()).results });
        const imported = new Map(rows.results.map(row => [row.name, row]));
        return json({ catalogues: Object.entries(tables).map(([name, t]) => ({ name, sourceHash: t.sourceHash, expectedRows: t.rowCount, importedRows: imported.get(name)?.imported_rows ?? 0, ready: imported.get(name)?.ready === 1 && imported.get(name)?.source_hash === t.sourceHash && imported.get(name)?.expected_rows === t.rowCount })) });
      }
      if (request.method === 'POST' && path === '/admin/import') {
        const input = await body(request); const table = tableFor(input.table, input.sourceHash);
        if (typeof input.chunkId !== 'string' || !/^[a-zA-Z0-9_.:-]{1,200}$/.test(input.chunkId) || !Array.isArray(input.rows) || input.rows.length < 1 || input.rows.length > 32 || !input.rows.every((row: unknown) => Array.isArray(row) && row.length === table.columns.length && row.every(v => v === null || typeof v === 'string' || (typeof v === 'number' && Number.isFinite(v))))) throw new Failure(400, 'INVALID_CHUNK');
        const hash = await digest(JSON.stringify([input.table, input.sourceHash, input.rows]));
        const existing = await env.CATALOG.prepare('SELECT payload_hash FROM _import_chunks WHERE id=?').bind(input.chunkId).first();
        if (existing) { if (existing.payload_hash !== hash) throw new Failure(409, 'CHUNK_CONFLICT'); return json({ ok: true, replayed: true }); }
        const state = await env.CATALOG.prepare('SELECT imported_rows,ready,source_hash,expected_rows FROM _source_tables WHERE name=?').bind(input.table).first<{ imported_rows: number; ready: number; source_hash: string; expected_rows: number }>();
        if (state && (state.source_hash !== table.sourceHash || state.expected_rows !== table.rowCount)) throw new Failure(409, 'SOURCE_CHANGED');
        if (state?.ready || (state?.imported_rows ?? 0) + input.rows.length > table.rowCount) throw new Failure(409, 'TABLE_FULL');
        const day = new Date().toISOString().slice(0, 10);
        const statements = [
          env.CATALOG.prepare('INSERT INTO _import_chunks(id,payload_hash,day,estimated_writes) VALUES(?,?,?,?)').bind(input.chunkId, hash, day, input.rows.length * table.writeWeight + 10),
          env.CATALOG.prepare('INSERT OR IGNORE INTO _source_tables(name,source_hash,expected_rows) VALUES(?,?,?)').bind(input.table, table.sourceHash, table.rowCount),
          ...input.rows.map((row: any[]) => env.CATALOG.prepare(`INSERT INTO "${input.table}" (${table.columns.map(c => `"${c}"`).join(',')}) VALUES(${table.columns.map(() => '?').join(',')})`).bind(...row)),
          env.CATALOG.prepare('UPDATE _source_tables SET imported_rows=imported_rows+? WHERE name=?').bind(input.rows.length, input.table),
        ];
        await env.CATALOG.batch(statements);
        return json({ ok: true, replayed: false });
      }
      if (request.method === 'POST' && path === '/admin/complete') {
        const input = await body(request); const table = tableFor(input.table, input.sourceHash);
        const state = await env.CATALOG.prepare('SELECT imported_rows,source_hash,expected_rows FROM _source_tables WHERE name=?').bind(input.table).first<{ imported_rows: number; source_hash: string; expected_rows: number }>();
        if (state && (state.source_hash !== table.sourceHash || state.expected_rows !== table.rowCount)) throw new Failure(409, 'SOURCE_CHANGED');
        if (state?.imported_rows !== table.rowCount) throw new Failure(409, 'IMPORT_INCOMPLETE');
        const count = await env.CATALOG.prepare(`SELECT COUNT(*) AS n FROM "${input.table}"`).first<{ n: number }>();
        if (count?.n !== table.rowCount) throw new Failure(409, 'ROW_COUNT_MISMATCH');
        await env.CATALOG.prepare('UPDATE _source_tables SET ready=1 WHERE name=?').bind(input.table).run();
        return json({ ok: true, ready: true });
      }
      const catalogue = path.match(/^\/v1\/catalogues\/([a-z_]+)$/);
      if (request.method === 'GET' && catalogue) {
        const name = catalogue[1]; if (!Object.hasOwn(tables, name)) throw new Failure(404, 'NOT_FOUND');
        const ready = await env.CATALOG.prepare('SELECT ready,source_hash,expected_rows FROM _source_tables WHERE name=?').bind(name).first();
        if (ready?.ready !== 1 || ready.source_hash !== tables[name].sourceHash || ready.expected_rows !== tables[name].rowCount) throw new Failure(503, 'CATALOGUE_NOT_READY');
        const after = Number(url.searchParams.get('after') ?? 0); const limit = Number(url.searchParams.get('limit') ?? 25);
        if (!Number.isSafeInteger(after) || after < 0 || !Number.isInteger(limit) || limit < 1 || limit > 100) throw new Failure(400, 'INVALID_PAGE');
        const result = await env.CATALOG.prepare(`SELECT rowid AS _cursor,* FROM "${name}" WHERE rowid>? ORDER BY rowid LIMIT ?`).bind(after, limit + 1).all();
        const more = result.results.length > limit; const items = result.results.slice(0, limit);
        return json({ items, nextCursor: more ? items.at(-1)?._cursor : null });
      }
      if (request.method === 'POST' && path === '/admin/enrichment') {
        const input = await body(request); metadataKey(input.kind, input.mbid);
        const expected = `https://musicbrainz.org/ws/2/${input.kind}/${input.mbid}`;
        if (!input.payload || input.payload.id !== input.mbid || typeof input.sourceUrl !== 'string' || input.sourceUrl.split('?')[0] !== expected || typeof input.fetchedAt !== 'string' || !Number.isFinite(Date.parse(input.fetchedAt))) throw new Failure(400, 'INVALID_ENRICHMENT');
        await env.METADATA.prepare('INSERT INTO musicbrainz_entities(kind,mbid,fetched_at,source_url,payload_json) VALUES(?,?,?,?,?) ON CONFLICT(kind,mbid) DO UPDATE SET fetched_at=excluded.fetched_at,source_url=excluded.source_url,payload_json=excluded.payload_json').bind(input.kind, input.mbid, input.fetchedAt, input.sourceUrl, JSON.stringify(input.payload)).run();
        return json({ ok: true });
      }
      const enrichment = path.match(/^\/v1\/enrichment\/musicbrainz\/([^/]+)\/([^/]+)$/);
      if (request.method === 'GET' && enrichment) {
        const [, kind, mbid] = enrichment; metadataKey(kind, mbid);
        const row = await env.METADATA.prepare('SELECT fetched_at,source_url,payload_json FROM musicbrainz_entities WHERE kind=? AND mbid=?').bind(kind, mbid).first<{ fetched_at: string; source_url: string; payload_json: string }>();
        if (!row) throw new Failure(404, 'ENRICHMENT_NOT_CACHED');
        return json({ provider: 'musicbrainz', kind, mbid, fetchedAt: row.fetched_at, sourceUrl: row.source_url, data: JSON.parse(row.payload_json) });
      }
      throw new Failure(404, 'NOT_FOUND');
    } catch (error) {
      if (error instanceof Failure) return json({ error: error.code }, error.status);
      if (error instanceof Error && error.message.includes('IMPORT_DAILY_BUDGET')) return json({ error: 'IMPORT_DAILY_BUDGET' }, 429);
      if (error instanceof Error && error.message.includes('TABLE_FULL')) return json({ error: 'TABLE_FULL' }, 409);
      return json({ error: 'SERVICE_UNAVAILABLE' }, 503);
    }
  },
};
