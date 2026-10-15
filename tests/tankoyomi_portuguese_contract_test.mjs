import fs from 'node:fs';

const manifest = JSON.parse(fs.readFileSync('extensions/tankoyomi/manifest.json', 'utf8'));
const service = fs.readFileSync('native/engine/TankoyomiChapterService.cpp', 'utf8');
const registry = fs.readFileSync('native/engine/TankoyomiProviderRegistry.cpp', 'utf8');
const cmake = fs.readFileSync('native/CMakeLists.txt', 'utf8');
const providers = [
  ['taiyo', 'extensions/tankoyomi/languages/pt/taiyo.js'],
  ['manga-night', 'extensions/tankoyomi/languages/pt/manga-night.js'],
  ['manga-online', 'extensions/tankoyomi/languages/pt/manga-online.js']
];
let failures = 0;
function check(ok, message) { console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`); if (!ok) failures++; }

for (const [id, path] of providers) {
  check(fs.existsSync(path), `${id} PT provider exists`);
  if (!fs.existsSync(path)) continue;
  const src = fs.readFileSync(path, 'utf8');
  check(src.includes('searchSeries') && src.includes('getChapters') && src.includes('getPages'),
    `${id} exposes the Tankoyomi contract`);
  check(!src.includes('async function') && !src.includes(' await '), `${id} avoids unsupported async/await`);
  check(!/^export\s/m.test(src), `${id} is loadable as an embedded script`);
  check(!src.includes('fetch('), `${id} uses the scoped Tankoyomi network context`);
}
const pt = (manifest.languages || []).find(x => x.code === 'pt') || {};
check((pt.providers || []).map(x => x.id).join(',') === 'taiyo,manga-online,manga-night',
  'PT provider priority is Taiyo -> Manga Online -> Manga Night');
check((pt.providers || []).every(x => Array.isArray(x.allowedHosts) && x.allowedHosts.length > 0),
  'every PT provider declares its network capability allowlist');
check(cmake.includes('GLOB_RECURSE TANKOYOMI_RESOURCE_FILES CONFIGURE_DEPENDS'),
  'PT scripts are embedded by automatic Tankoyomi resource discovery');
check(service.includes('m_registry.providersForLanguage'),
  'Portuguese routing uses the same generic manifest provider chain');
check(!service.includes('fetchPortuguese') && !service.includes('m_taiyo'),
  'Portuguese needs no provider-specific C++ registration');
check(service.includes('TankoyomiIdentity::qualifyChapter'),
  'Portuguese chapters receive generic qualified identities');
check(registry.includes('normalizeLanguage'), 'native registry normalizes pt-BR to base-language pt');

if (failures) process.exit(1);
console.log('\nPASS â€” Tankoyomi PT-BR package is manifest-driven and capability-scoped');
