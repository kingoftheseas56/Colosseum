import schema from './source-schema.json';
import {contentDelivery} from './content-delivery';
import {catalogueMethod, catalogueMethods} from './catalogue-semantic.ts';
import {libraryReady} from './catalogue-readiness.ts';
import {prebuiltCatalogue} from './catalogue-prebuilt.ts';

type AppEnv = Cloudflare.Env & { ADMIN_TOKEN: string; SESSION_SECRET: string; LIBRARY?: D1Database; LIBRARY_REVISION?: string };
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
    if (semantic && claims.aud === 'colosseum-content-api') return (claims.scope === 'tankoban:read' || claims.scope === 'content:read') && claims.apiVersion === 1 && claims.sourceHash === tables.tankoban__series.sourceHash && Number.isInteger(claims.iat) && claims.iat <= now && Number.isInteger(claims.exp) && claims.exp > now && claims.exp - claims.iat === 300 && claims.exp <= now + 300;
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

async function guestGate(request: Request, env: AppEnv) {
  if (env.TANKOBAN_GUEST_ENABLED !== 'true' || !env.SESSION_SECRET || env.SESSION_SECRET.length < 32 || !env.GUEST_RATE_LIMITER) throw new Failure(503, 'GUEST_UNAVAILABLE');
  const ip = request.headers.get('CF-Connecting-IP');
  if (!ip) throw new Failure(503, 'GUEST_UNAVAILABLE');
  if (!(await env.GUEST_RATE_LIMITER.limit({ key: ip })).success) throw new Failure(429, 'RATE_LIMITED');
}
const tankobanChecked=new WeakSet<D1Database>();
async function tankobanReady(env: AppEnv) {
  if(tankobanChecked.has(env.CATALOG))return 0;
  const names = ['tankoban__series', 'tankoban__volumes'];
  if (tables[names[0]].sourceHash !== tables[names[1]].sourceHash) throw new Failure(503, 'CATALOGUE_NOT_READY');
  const rows = await env.CATALOG.prepare('SELECT name,ready,source_hash,expected_rows,imported_rows FROM _source_tables WHERE name IN (?,?)').bind(...names).all();
  for (const name of names) {
    const row = rows.results.find(r => r.name === name);
    if (!row || row.ready !== 1 || row.source_hash !== tables[name].sourceHash || row.expected_rows !== tables[name].rowCount || row.imported_rows !== tables[name].rowCount) throw new Failure(503, 'CATALOGUE_NOT_READY');
  }
  tankobanChecked.add(env.CATALOG);
  return rows.meta.rows_read??0;
}
function naturalKey(value: string) { return value.toLowerCase().replace(/\p{Nd}+/gu, run => run.padStart(12, '0')); }
async function tankoban(env: AppEnv, malId: number) {
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

// QVariantMap keys and JavaScript object insertion order share one cache key.
function canonicalArgs(value: any): any {
  if (Array.isArray(value)) return value.map(canonicalArgs);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(k=>[k,canonicalArgs(value[k])]));
  return value;
}
async function bodyTag(value: string | ArrayBuffer) {
  const bytes = typeof value === 'string' ? encoder.encode(value) : value;
  return '"'+Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))).map(v=>v.toString(16).padStart(2,'0')).join('')+'"';
}
function matchesTag(request: Request, tag: string) {
  const candidates=request.headers.get('If-None-Match')?.match(/(?:W\/)?"[^"]*"|\*/g) ?? [];
  return candidates.some(v=>v==='*'||v.replace(/^W\/\s*/,'')===tag.replace(/^W\/\s*/,''));
}
async function catalogueResponse(request: Request, source: Response): Promise<Response> {
  const headers=new Headers(source.headers);
  headers.set('Cache-Control','private, max-age=3600');headers.set('X-Content-Type-Options','nosniff');
  if(source.status===304)return new Response(null,{status:304,headers});
  let payload: BodyInit | null=source.body;
  if(!headers.has('ETag')){const bytes=await source.arrayBuffer();headers.set('ETag',await bodyTag(bytes));payload=bytes;}
  if(matchesTag(request,headers.get('ETag')!)){headers.delete('Content-Length');return new Response(null,{status:304,headers});}
  return new Response(payload,{status:source.status,headers});
}

export default {
  async fetch(request: Request, env: AppEnv, ctx?: ExecutionContext): Promise<Response> {
    try {
      const url = new URL(request.url); const path = url.pathname;
      if (request.method === 'GET' && path === '/healthz') return json({ ok: true, environment: env.ENVIRONMENT });
      const semantic = path.match(/^\/v1\/tankoban\/series\/([1-9][0-9]*)$/);
      if (path.startsWith('/internal/')) return json({error:'NOT_FOUND'},404);
      const catalogueRpc = path.match(/^\/v1\/catalogue\/(imdb|mal|comics)\/([A-Za-z][A-Za-z0-9]{0,63})$/);
      const contentRpc = path === '/v1/catalogue/content/bundle' && request.method === 'POST';
      const contentGet = request.method === 'GET' && (path === '/v1/content/manifest' || /^\/v1\/content\/documents\/[a-z][a-z0-9-]{0,100}$/.test(path));
      if (request.method === 'POST' && path === '/v1/guest/session') {
        await guestGate(request, env);
        const input = await body(request);
        if (input.appId !== 'colosseum' || input.apiVersion !== 1 || Object.keys(input).length !== 2) throw new Failure(400, 'INVALID_GUEST_REQUEST');
        await tankobanReady(env);
        const iat = Math.floor(Date.now()/1000), exp = iat+300;
        const claims = base64(encoder.encode(JSON.stringify({aud:'colosseum-content-api',scope:'content:read',apiVersion:1,sourceHash:tables.tankoban__series.sourceHash,iat,exp})));
        const signature = base64(new Uint8Array(await crypto.subtle.sign('HMAC',await key(env.SESSION_SECRET),encoder.encode(claims))));
        return json({token:`${claims}.${signature}`,expiresAt:new Date(exp*1000).toISOString()});
      }
      if (semantic) await guestGate(request, env);
      const token = request.headers.get('Authorization')?.match(/^Bearer (.+)$/)?.[1] ?? '';
      const isAdmin = path.startsWith('/admin/');
      if (!(isAdmin ? await admin(token, env.ADMIN_TOKEN) : await lease(token, env.SESSION_SECRET, !!semantic || !!catalogueRpc || contentRpc || contentGet))) throw new Failure(401, 'UNAUTHORIZED');
      if (contentRpc || contentGet) {
        if (contentRpc) {
          const input = await body(request);
          if (Object.keys(input).length !== 1 || !Array.isArray(input.args) || input.args.length !== 0) throw new Failure(400,'INVALID_CONTENT_REQUEST');
        }
        const response = await contentDelivery(request, env.ASSETS);
        if (!response) throw new Failure(404,'NOT_FOUND');
        return response;
      }
      if (request.method === 'POST' && catalogueRpc) {
        const [,dataset,method] = catalogueRpc;
        if (!catalogueMethods[dataset]?.includes(method)) throw new Failure(400,'UNKNOWN_CATALOGUE_METHOD');
        const input = await body(request);
        if (Object.keys(input).length !== 1 || !Array.isArray(input.args) || input.args.length > 16
            || encoder.encode(JSON.stringify(input.args)).byteLength > 32768) throw new Failure(400,'INVALID_CATALOGUE_REQUEST');
        if (!env.LIBRARY || !env.LIBRARY_REVISION) throw new Failure(503,'CATALOGUE_NOT_READY');
        const cost={rowsRead:0,statements:0};
        const observe=(meta:D1Meta)=>{cost.rowsRead+=meta.rows_read??0;cost.statements++;};
        if (!await libraryReady(env.LIBRARY,env.LIBRARY_REVISION,observe)) throw new Failure(503,'CATALOGUE_NOT_READY');
        const record=(delivery:string)=>console.log(JSON.stringify({event:'catalogue-read',dataset,method,delivery,...cost}));
        const respond=async(delivery:string,response:Response)=>{
          record(delivery);const result=await catalogueResponse(request,response),headers=new Headers(result.headers);
          headers.set('X-Catalogue-Delivery',delivery);headers.set('X-Catalogue-D1-Rows',String(cost.rowsRead));
          return new Response(result.body,{status:result.status,headers});
        };
        const prebuilt=await prebuiltCatalogue(env.ASSETS,env.LIBRARY_REVISION,dataset,method,input.args);
        if(prebuilt){
          const value=JSON.stringify({apiVersion:1,revision:env.LIBRARY_REVISION,method,result:prebuilt.result});
          if(encoder.encode(value).byteLength>4*1024*1024)throw new Failure(503,'RESPONSE_TOO_LARGE');
          return respond('prebuilt',new Response(value,{headers:{'Content-Type':'application/json','ETag':await bodyTag(value)}}));
        }
        const fingerprint = (await bodyTag(JSON.stringify(canonicalArgs(input.args)))).slice(1,-1);
        const period=dataset==='comics'&&['discoverPage','discoverPageFiltered'].includes(method)?String(new Date().getFullYear()):dataset==='mal'&&['discoverPage','discoverPageFiltered'].includes(method)&&input.args[0]==='new-releases'?new Date().toISOString().slice(0,10):'stable';
        const staticUrl = new URL(`/internal/catalogue/${env.LIBRARY_REVISION}/${period}/${dataset}/${method}/${fingerprint}.json`,url.origin);
        const assetHeaders=new Headers();const conditional=request.headers.get('If-None-Match');if(conditional)assetHeaders.set('If-None-Match',conditional);
        const prepared = await env.ASSETS.fetch(new Request(staticUrl,{headers:assetHeaders}));
        if (prepared.status === 200 || prepared.status === 304) return respond('prepared',prepared);
        const cacheKey = new Request(new URL(`/internal-cache/catalogue/${env.LIBRARY_REVISION}/${period}/${dataset}/${method}/${fingerprint}`,url.origin));
        const hit = await caches.default.match(cacheKey);
        if (hit) return respond('edge',hit);
        let result: unknown;
        try { result = await catalogueMethod({prepare:sql=>env.LIBRARY!.prepare(sql),observe},dataset,method,input.args); }
        catch(error) { if (error instanceof Error && error.message === 'UNKNOWN_CATALOGUE_METHOD') throw new Failure(400,error.message); throw error; }
        const value = JSON.stringify({apiVersion:1,revision:env.LIBRARY_REVISION,method,result});
        if (encoder.encode(value).byteLength > 4*1024*1024) throw new Failure(503,'RESPONSE_TOO_LARGE');
        const response = new Response(value,{headers:{'Content-Type':'application/json','Cache-Control':'public, max-age=3600','X-Content-Type-Options':'nosniff','ETag':await bodyTag(value)}});
        if (ctx) ctx.waitUntil(caches.default.put(cacheKey,response.clone()));
        return respond('d1',response);
      }
      if (request.method === 'GET' && semantic) {
        const malId = Number(semantic[1]);
        if (!Number.isSafeInteger(malId) || malId > 2147483647) throw new Failure(400, 'INVALID_MAL_ID');
        const readinessRows=await tankobanReady(env);
        if(env.LIBRARY_REVISION){const prepared=await prebuiltCatalogue(env.ASSETS,env.LIBRARY_REVISION,'tankoban','series',[malId]);if(prepared){console.log(JSON.stringify({event:'catalogue-read',dataset:'tankoban',method:'series',delivery:'prebuilt',rowsRead:readinessRows}));const response=json(prepared.result);response.headers.set('X-Catalogue-Delivery','prebuilt');response.headers.set('X-Catalogue-D1-Rows',String(readinessRows));return response;}}
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
