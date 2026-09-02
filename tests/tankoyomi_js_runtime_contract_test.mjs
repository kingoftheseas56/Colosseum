import fs from 'node:fs';

const h = fs.readFileSync('native/engine/TankoyomiScriptProvider.h', 'utf8');
const cpp = fs.readFileSync('native/engine/TankoyomiScriptProvider.cpp', 'utf8');
const service = fs.readFileSync('native/engine/TankoyomiChapterService.cpp', 'utf8');
const registry = fs.readFileSync('native/engine/TankoyomiProviderRegistry.cpp', 'utf8');
const identity = fs.readFileSync('native/engine/TankoyomiIdentity.cpp', 'utf8');
const cmake = fs.readFileSync('native/CMakeLists.txt', 'utf8');
const qrc = fs.readFileSync('native/app_resources.qrc', 'utf8');
const providerPaths = [
  'extensions/tankoyomi/languages/en/weebcentral.js',
  'extensions/tankoyomi/languages/es/zonatmo.js',
  'extensions/tankoyomi/languages/es/niadd.js',
  'extensions/tankoyomi/languages/pt/taiyo.js',
  'extensions/tankoyomi/languages/pt/manga-night.js',
  'extensions/tankoyomi/languages/pt/manga-online.js'
];
let failures = 0;
const check = (ok, msg) => { console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`); if (!ok) failures++; };

check(h.includes('QJSEngine'), 'runtime uses the smaller QJSEngine sandbox');
check(!cpp.includes('new Promise'), 'runtime avoids QJSEngine Promise scheduling');
check(cpp.includes('__tankoyomiCallbacks'), 'runtime owns per-call success/error callbacks');
check(cpp.includes('fetchText') && cpp.includes('fetchJson'), 'runtime exposes scoped fetchText/fetchJson');
check(h.includes('QStringList allowedHosts'), 'runtime receives a manifest origin allowlist');
check(cpp.includes('allowedHosts.contains'), 'runtime rejects undeclared network hosts');
check(cpp.includes('setTransferTimeout'), 'provider metadata requests have a finite timeout');
for (const path of providerPaths) {
  const src = fs.readFileSync(path, 'utf8');
  check(!src.includes('async function') && !src.includes(' await '), `${path} avoids unsupported async/await`);
}
check(cmake.includes('GLOB_RECURSE TANKOYOMI_RESOURCE_FILES CONFIGURE_DEPENDS'),
  'CMake auto-discovers Tankoyomi provider resources');
check(cmake.includes('qt_add_resources(colosseum "tankoyomi_resources"'),
  'auto-discovered providers are embedded through Qt resources');
check(!qrc.includes('tankoyomi/languages/'),
  'base app QRC no longer hardcodes provider scripts');
check(service.includes('m_registry.providersForLanguage'),
  'service obtains same-language priority from the manifest registry');
check(service.includes('descriptor.resourcePath') && service.includes('descriptor.allowedHosts'),
  'service constructs provider runtimes from manifest capabilities');
check(service.includes('TankoyomiIdentity::qualifyChapter'),
  'catalogue rows receive generic qualified chapter identities');
check(service.includes('TankoyomiIdentity::parseChapter'),
  'page routing decodes the generic qualified identity');
check(identity.includes('Base64UrlEncoding'),
  'opaque provider chapter state is filesystem-safe and versionable');
check(registry.includes('same-language-only'),
  'native registry enforces the no-cross-language fallback policy');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi provider runtime is manifest-driven and capability-scoped');
