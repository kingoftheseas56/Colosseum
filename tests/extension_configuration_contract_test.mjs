// Configuration-required extension contract.
//
// QML is not imported headlessly here. These checks pin the cross-layer contract:
// C++ preserves the Stremio flags, the page exposes them safely, and Configure
// resolves to the configured add-on endpoint rather than opening the manifest.
import { readFileSync } from 'node:fs';

const page = readFileSync('qml/ExtensionsPage.qml', 'utf8');
const sources = readFileSync('qml/ExtensionsSources.qml', 'utf8');
const catalogSource = readFileSync('qml/ExtensionsCatalog.js', 'utf8')
  .replace(/^\.pragma library\s*$/m, '');
const store = readFileSync('native/engine/ExtensionsStore.cpp', 'utf8');
const catalog = {};
new Function('module', catalogSource
  + '\nmodule.worldsFor = worldsFor;')(catalog);

let failures = 0;
function check(condition, message) {
  if (condition) console.log('  ok   ' + message);
  else {
    console.log('  FAIL ' + message);
    failures++;
  }
}

console.log('manifest metadata preservation');
check(store.includes('"configurable", "configurationRequired"'),
  'manifest slimming preserves both Stremio configuration flags');
check(page.includes('configurationRequired'),
  'ExtensionsPage exposes configuration-required state');
check(sources.includes('configurationRequired'),
  'ExtensionsSources exposes configuration-required state');
check(catalog.worldsFor({ manifest: {
  resources: [],
  behaviorHints: { configurationRequired: true }
} }).includes('theatre'),
  'resource-less configuration-required add-ons remain visible in Theatre');

console.log('safe configuration flow');
check(page.includes('function configureUrl(rawUrl)'),
  'ExtensionsPage has one configure endpoint resolver');
check(page.includes('root.configureUrl(entry.transportUrl)'),
  'Sources configure requests use the resolver');
check(page.includes('root.configureUrl('),
  'Installed configure requests use the resolver');
check(page.includes('final configured manifest URL'),
  'install sheet explains the configured-manifest handoff');
check(page.includes('openForUrl(item.url)'),
  'catalogue installs preview the manifest before installing');
check(page.includes('Configure ↗'),
  'configuration-required preview opens external Configure instead of installing the base manifest');
check(!/Qt\.openUrlExternally\(entry\.transportUrl\)/.test(page),
  'the raw manifest URL is not opened as the configure endpoint');

console.log('visible state');
check(page.includes('"Configure required"'),
  'ExtensionsPage gives configuration-required add-ons an actionable label');
check(sources.includes('"Configure required"'),
  'ExtensionsSources gives configuration-required add-ons an actionable label');

if (failures) {
  console.error('\n' + failures + ' configuration contract check(s) failed');
  process.exit(1);
}
console.log('\nAll configuration contract checks passed');
