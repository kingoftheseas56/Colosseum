import fs from 'node:fs';
import vm from 'node:vm';

const root = 'extensions/tankoyomi';
const manifestPath = `${root}/manifest.json`;
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(fs.existsSync(manifestPath), 'Tankoyomi manifest exists');
check(fs.existsSync(`${root}/tankoyomi.js`), 'Tankoyomi router exists');

let manifest = {};
if (fs.existsSync(manifestPath)) {
  manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
}

check(manifest.id === 'colosseum.well.tankoyomi', 'stable extension id');
check(manifest.defaultLanguage === 'en', 'English is the default language');
check(manifest.fallbackPolicy === 'same-language-only', 'never silently crosses languages');
const languages = manifest.languages || [];
check(languages.map(x => x.code).join(',') === 'en,es,pt,fr', 'language order is English, Spanish, Portuguese, then French');
check(languages.map(x => x.label).join('|') === 'English|Espa\u00f1ol|Portugu\u00eas (Brasil)|Fran\u00e7ais',
  'language labels preserve their intended UTF-8 text');

const en = languages.find(x => x.code === 'en') || {};
const es = languages.find(x => x.code === 'es') || {};
const pt = languages.find(x => x.code === 'pt') || {};
const fr = languages.find(x => x.code === 'fr') || {};
check((en.providers || []).map(x => x.id).join(',') === 'weebcentral',
  'English routes only to WeebCentral');
check((es.providers || []).map(x => x.id).join(',') === 'zonatmo,niadd',
  'Spanish routes ZonaTMO first, NiAdd second');
check((pt.providers || []).map(x => x.id).join(',') === 'taiyo,manga-online,manga-night',
  'Portuguese routes Taiyo, Manga Online, then Manga Night');
check((pt.providers || [])[0]?.name === 'Taiy\u014d', 'Taiyo display name preserves its intended UTF-8 text');
check((fr.providers || []).map(x => x.id).join(',') === 'sushiscan-fr,lelscan-vf,lelmanga,mangamoins',
  'French routes SushiScan, Lelscan-VF, Lelmanga, then MangaMoins');

function loadScript(file) {
  const sandbox = { module: { exports: {} }, exports: {}, globalThis: {} };
  const src = fs.readFileSync(`${root}/${file}`, 'utf8');
  vm.runInNewContext(src, sandbox, { filename: file });
  return Object.keys(sandbox.module.exports).length
    ? sandbox.module.exports
    : sandbox.globalThis.TankoyomiProvider || sandbox.globalThis.Tankoyomi;
}

for (const language of languages) {
  for (const declared of language.providers || []) {
    check(fs.existsSync(`${root}/${declared.entry}`), `${declared.id} provider script exists`);
    if (!fs.existsSync(`${root}/${declared.entry}`)) continue;
    const provider = loadScript(declared.entry);
    check(provider.id === declared.id, `${declared.id} id matches manifest`);
    check(provider.language === language.code, `${declared.id} stays inside ${language.code}`);
    for (const fn of ['searchSeries', 'getChapters', 'getPages']) {
      check(typeof provider[fn] === 'function', `${declared.id} exposes ${fn}()`);
    }
  }
}

if (fs.existsSync(`${root}/tankoyomi.js`)) {
  const router = loadScript('tankoyomi.js');
  check(typeof router.providersForLanguage === 'function', 'router exposes providersForLanguage()');
  check(typeof router.resolveLanguage === 'function', 'router exposes resolveLanguage()');
  check(router.resolveLanguage(manifest, 'es') === 'es', 'router accepts installed Spanish');
  check(router.resolveLanguage(manifest, 'pt-BR') === 'pt', 'router normalizes pt-BR to Portuguese');
  check(router.resolveLanguage(manifest, 'fr-FR') === 'fr', 'router normalizes fr-FR to installed French');
  check(router.providersForLanguage(manifest, 'fr').map(x => x.id).join(',') === 'sushiscan-fr,lelscan-vf,lelmanga,mangamoins',
    'router preserves French provider priority');
  check(router.resolveLanguage(manifest, 'de') === 'de', 'explicit unsupported language stays unsupported');
  check(router.providersForLanguage(manifest, 'de').length === 0, 'unsupported language never crosses to English providers');
  check(router.providersForLanguage(manifest, 'es').map(x => x.id).join(',') === 'zonatmo,niadd',
    'router preserves Spanish provider priority');
  check(router.providersForLanguage(manifest, 'pt-BR').map(x => x.id).join(',') === 'taiyo,manga-online,manga-night',
    'router preserves Portuguese provider priority');
}

if (failures) process.exit(1);
console.log('\nPASS â€” Tankoyomi language/provider contract');
