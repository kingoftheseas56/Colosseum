import fs from 'node:fs';

const page = fs.readFileSync('qml/TankoyomiConfigurationPage.qml', 'utf8');
const extensions = fs.readFileSync('qml/ExtensionsPage.qml', 'utf8');
const main = fs.readFileSync('qml/Main.qml', 'utf8');
const manifest = JSON.parse(fs.readFileSync('extensions/tankoyomi/manifest.json', 'utf8'));
const store = fs.readFileSync('native/engine/ExtensionsStore.cpp', 'utf8');
let failures = 0;
const check = (ok, message) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures++;
};

check(manifest.behaviorHints?.configurable === true,
  'Tankoyomi advertises an in-app configuration surface');
check(store.includes('constexpr int kHouseDefaultsVersion = 13;'),
  'house defaults version advances for the configuration metadata migration');
check(store.includes('manifest("colosseum.well.tankoyomi", "Tankoyomi"')
      && store.includes('{}, true));'),
  'the seeded Tankoyomi row carries configurable metadata');
check(/if \(onlyMissing\s*&& m_items\.at\(at\)\.value\(QStringLiteral\("manifest"\)\)\.toMap\(\) != m\)/s.test(store),
  'house metadata refresh preserves the existing installed row and enabled state');

check(page.includes('objectName: "tankoyomiConfigurationPage"'),
  'configuration page has a stable automation identity');
check(page.includes('Configuration') && page.includes('About'),
  'configuration page exposes Configuration and About tabs');
check(page.includes('chapterLanguages') && page.includes('chapterDefaultLanguage')
      && page.includes('chapterProviders'),
  'page consumes the native language/default/provider projections');
const expectedCountryCodes = { en: 'GB', es: 'ES', pt: 'BR', fr: 'FR' };
for (const language of manifest.languages || []) {
  if (expectedCountryCodes[language.code]) {
    check(language.countryCode === expectedCountryCodes[language.code],
      `${language.code} manifest carries the approved country code`);
  }
}
check(page.includes('TankoyomiFlag') && page.includes('countryCode'),
  'page renders manifest-backed vector flags for language rows and detail header');
check(page.includes('allowedHosts') && page.includes('providerHost'),
  'page renders provider website identity from manifest host allowlists');
for (const method of [
  'setChapterDefaultLanguage', 'setChapterProviderEnabled',
  'moveChapterProviderUp', 'moveChapterProviderDown', 'resetChapterProviderOrder'
]) {
  check(page.includes(method), `page wires ${method}()`);
}
check(page.includes('Extensions.setEnabled') || page.includes('extensionsRef.setEnabled')
      || page.includes('source.setEnabled'),
  'master switch delegates to ExtensionsStore');
check(page.includes('same-language') || page.includes('Same language'),
  'page states the same-language-only fallback rule');
check(page.includes('KeyboardAction') && page.includes('focusEnabled')
      && page.includes('activeFocusOnTab'),
  'interactive controls expose keyboard actions and traversal');
check(!/WeebCentral|VoraToon|Kiryuu|Manga Night/.test(page),
  'page does not hard-code oracle demo provider inventory');

check(extensions.includes('configuringExtensionId'),
  'ExtensionsPage owns configuration subpage state');
check(extensions.includes('colosseum.well.tankoyomi')
      && extensions.includes('TankoyomiConfigurationPage'),
  'ExtensionsPage routes the Tankoyomi house row into the native subpage');
check(extensions.includes('function openConfiguration'),
  'ExtensionsPage exposes a testable configuration route');
check(/onConfigureRequested:[\s\S]{0,260}root\.openConfiguration\(entry\)/.test(extensions),
  'ExtensionsSources routes Tankoyomi Settings into the in-app page');
check(page.includes('../assets/addon-logos/tankoyomi.png')
      && page.includes('asynchronous: true') && page.includes('cache: true'),
  'configuration header uses the real bundled Tankoyomi logo');
check(page.includes('root.width < 1050') && page.includes('languagePanel.height + 18'),
  'configuration switches to a stacked layout below the desktop breakpoint');
check(extensions.includes('function requestEscape')
      && /case "extensions":[\s\S]{0,260}requestEscape\(\)/.test(main)
      && /requestEscape\(\)[\s\S]{0,260}closeExtensionsPage\(\)/.test(main),
  'Escape backs out of nested configuration before closing Extensions');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi configuration page contract');
