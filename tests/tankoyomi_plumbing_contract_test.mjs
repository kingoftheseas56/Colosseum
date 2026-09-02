import fs from 'node:fs';
import vm from 'node:vm';

const manifest = JSON.parse(fs.readFileSync('extensions/tankoyomi/manifest.json', 'utf8'));
const service = fs.readFileSync('native/engine/TankoyomiChapterService.cpp', 'utf8');
const header = fs.readFileSync('native/engine/TankoyomiChapterService.h', 'utf8');
const cmake = fs.readFileSync('native/CMakeLists.txt', 'utf8');
const qrc = fs.readFileSync('native/app_resources.qrc', 'utf8');
const routerSrc = fs.readFileSync('extensions/tankoyomi/tankoyomi.js', 'utf8');
let failures = 0;
function check(ok, message) {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures++;
}

const providers = (manifest.languages || []).flatMap(language =>
  (language.providers || []).filter(p => p.enabled !== false).map(provider => ({ language, provider })));
check(providers.length >= 6, 'manifest owns the full current provider roster');
for (const { language, provider } of providers) {
  check(Array.isArray(provider.allowedHosts) && provider.allowedHosts.length > 0,
    `${language.code}/${provider.id} declares a non-empty host allowlist`);
}

const providerIds = providers.map(x => x.provider.id);
check(providerIds.every(id => !service.includes(`QStringLiteral("${id}")`)),
  'chapter service contains no provider-specific registration ids');
check(!header.includes('m_zonatmo') && !header.includes('m_taiyo') && !header.includes('m_mangadexPt'),
  'chapter service owns no provider-specific pointer members');
check(service.includes('TankoyomiProviderRegistry'),
  'chapter service routes through the manifest provider registry');
check(cmake.includes('CONFIGURE_DEPENDS') && cmake.includes('extensions/tankoyomi'),
  'CMake discovers Tankoyomi resources dynamically');
check(cmake.includes('qt_add_resources'),
  'Tankoyomi resources use the Qt CMake resource API');
check(!qrc.includes('tankoyomi/languages/'),
  'app_resources.qrc does not enumerate provider scripts');

const sandbox = { module: { exports: {} }, exports: {}, globalThis: {} };
vm.runInNewContext(routerSrc, sandbox, { filename: 'tankoyomi.js' });
const router = Object.keys(sandbox.module.exports).length
  ? sandbox.module.exports : sandbox.globalThis.Tankoyomi;
check(router.normalizeLanguage('pt-BR') === 'pt', 'regional language normalizes to its base language');
check(router.resolveLanguage(manifest, '') === 'en', 'empty language uses the configured default');
check(router.resolveLanguage(manifest, 'de') === 'de',
  'an explicit unsupported language is not silently replaced by English');
check(router.providersForLanguage(manifest, 'de').length === 0,
  'unsupported explicit language has no cross-language provider fallback');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi provider addition is manifest-driven');