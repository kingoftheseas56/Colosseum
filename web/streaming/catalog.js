// Discovery only. A Stremio catalog is neither a provider entitlement nor a playback URL.
(() => {
const DEFAULT_MANIFEST = 'https://catalog.ers.pw/manifest.json';

const SERVICES = [
  { id: 'nfx', name: 'Netflix', mark: 'N', site: 'https://www.netflix.com/' },
  { id: 'amp', name: 'Prime Video', mark: 'P', site: 'https://www.primevideo.com/' },
  { id: 'hbm', name: 'HBO Max', mark: 'M', site: 'https://www.max.com/' },
  { id: 'dnp', name: 'Disney+', mark: 'D+', site: 'https://www.disneyplus.com/' },
  { id: 'atp', name: 'Apple TV+', mark: 'A', site: 'https://tv.apple.com/' }
];

function safeHttps(value) {
  try { const url = new URL(value); return url.protocol === 'https:' ? url.href : ''; }
  catch { return ''; }
}

async function getJson(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 12000);
  try {
    const response = await fetch(url, { signal: controller.signal, credentials: 'omit' });
    if (!response.ok) throw new Error(`Catalogue request failed (${response.status}).`);
    return await response.json();
  } finally { clearTimeout(timer); }
}

async function loadManifest(manifestUrl = DEFAULT_MANIFEST) {
  const url = safeHttps(manifestUrl);
  if (!url || !url.endsWith('/manifest.json')) throw new Error('A secure Stremio manifest URL is required.');
  const manifest = await getJson(url);
  if (!manifest || !Array.isArray(manifest.catalogs) || !manifest.resources?.some?.(resource => resource === 'catalog' || resource?.name === 'catalog')) {
    throw new Error('This addon does not expose Stremio catalogues.');
  }
  return { manifest, baseUrl: url.slice(0, -'manifest.json'.length) };
}

function normalizeMeta(meta, providerId, type) {
  if (!meta || !meta.id || !meta.name) return null;
  return {
    id: String(meta.id), providerId, type,
    title: String(meta.name),
    description: typeof meta.description === 'string' ? meta.description : '',
    year: String(meta.year || meta.releaseInfo || ''),
    genres: Array.isArray(meta.genres) ? meta.genres.filter(value => typeof value === 'string').slice(0, 3) : [],
    runtime: typeof meta.runtime === 'string' ? meta.runtime : '',
    image: safeHttps(meta.background) || safeHttps(meta.poster),
    poster: safeHttps(meta.poster)
  };
}

async function loadProviderCatalog(source, providerId) {
  const catalogs = source.manifest.catalogs.filter(item => item?.id === providerId && (item.type === 'movie' || item.type === 'series'));
  if (!catalogs.length) throw new Error('This service is not listed by the selected catalog addon.');
  const results = await Promise.allSettled(catalogs.map(async catalog => {
    const url = new URL(`catalog/${encodeURIComponent(catalog.type)}/${encodeURIComponent(providerId)}.json`, source.baseUrl);
    const json = await getJson(url.href);
    if (!Array.isArray(json.metas)) throw new Error('The catalog returned no title list.');
    const seen = new Set();
    const titles = json.metas.map(meta => normalizeMeta(meta, providerId, catalog.type)).filter(title => {
      if (!title || seen.has(title.id)) return false;
      seen.add(title.id); return true;
    });
    return { id: `${providerId}-${catalog.type}`, name: catalog.type === 'series' ? 'Series to discover' : 'Films to discover', type: catalog.type, titles };
  }));
  const shelves = results.filter(result => result.status === 'fulfilled').map(result => result.value);
  if (!shelves.length) throw results[0]?.reason || new Error('This service catalog is unavailable.');
  return shelves;
}

window.StreamingCatalog = Object.freeze({ DEFAULT_MANIFEST, SERVICES, loadManifest, loadProviderCatalog });
})();
