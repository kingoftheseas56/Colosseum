// extensions_store_contract_test.mjs — the Extensions Store's rules (qml/ExtensionsStoreApi.js and
// friends), checked offline against hand-made stremio-addons.net rows.
// Run: node tests/extensions_store_contract_test.mjs
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

let failures = 0;
function check(condition, message) {
  console.log((condition ? '  ok   ' : '  FAIL ') + message);
  if (!condition) failures++;
}

const api = {};
vm.createContext(api);
vm.runInContext(readFileSync('qml/ExtensionsStoreApi.js', 'utf8').replace('.pragma library', ''), api);

function row(name, { stars = 1, cats = [], resources = ['stream'], types = ['movie', 'series'], logo = '', hints = {} } = {}) {
  return {
    slug: name.toLowerCase().replace(/\W+/g, '-'), stars,
    manifestUrl: 'https://example.test/' + name + '/manifest.json', configureUrl: null,
    categories: cats.map(c => ({ name: c, slug: c })),
    manifest: { id: 'test.' + name, name, logo, description: name + ' does things. More text.', resources, types, behaviorHints: hints }
  };
}
const all = [
  row('Torrentio', { stars: 900, cats: ['movies', 'tv shows', 'torrents'] }),
  row('Comet | ElfHosted', { stars: 800, cats: ['movies', 'debrid support'] }),
  row('OpenSubtitles v3', { stars: 50, cats: ['subtitles'], resources: ['subtitles'] }),
  row('Streaming Catalogs', { stars: 40, cats: ['movies'], resources: ['catalog'] }),
  row('Anime Kitsu', { stars: 30, cats: ['anime', 'metadata'], resources: ['catalog', 'meta'] }),
  row('MediaFusion', { stars: 700, cats: ['live tv', 'movies', 'tv shows', 'anime'], logo: 'http://x.test/logo.png' }),
  row('USA TV', { stars: 600, cats: ['live tv', 'metadata'] }),
  row('Radio Box', { stars: 500, cats: ['radios'] }),
  row('Trakt Up Next', { stars: 400, cats: ['movies', 'tv shows'], resources: ['catalog'] }),
  row('Blue Film', { stars: 300, cats: ['nsfw', 'movies'] }),
  row('AnimeOnly', { stars: 20, cats: ['anime'] }),
  row('Setup Me', { stars: 10, cats: ['movies'], hints: { configurationRequired: true } })
].map(api.normalize).sort((x, y) => y.stars - x.stars);

const names = rows => rows.flatMap(r => r.items.map(a => a.name));
const shown = names(api.rows(all, false));

check(api.essentials(all).map(a => a.name).join('|') === api.ESSENTIALS.join('|'),
  'Essentials are the five ledger add-ons, in order');
check(!shown.some(n => api.ESSENTIALS.includes(n)), 'Essentials are not repeated in the rows below');
check(!shown.includes('USA TV') && !shown.includes('Radio Box'), 'Live TV and Radio add-ons are out');
check(shown.includes('MediaFusion'), 'a film source that also carries Live TV stays');
check(!shown.includes('Trakt Up Next'), 'tracker-sync add-ons are out');
check(!shown.includes('Blue Film') && names(api.rows(all, true)).includes('Blue Film'),
  'adult add-ons appear only when explicit content is on');
check(all.find(a => a.name === 'MediaFusion').logo.startsWith('https://'), 'http logos are upgraded to https');
check(all.find(a => a.name === 'Comet | ElfHosted').title === 'Comet', 'the ElfHosted suffix is dropped from titles');
check(all.find(a => a.name === 'Setup Me').setupRequired === true, 'configurationRequired marks an add-on as Set up');
check(all.find(a => a.name === 'Setup Me').configurable === true, 'configurationRequired also opens the setup page');
check(api.normalize(row('Opt Setup', { hints: { configurable: true } })).configurable === true,
  'an optional-setup add-on (Comet, MediaFusion) opens its setup page instead of a bare install');
const anime = api.rows(all, false).find(r => r.title === 'Anime');
check(anime && anime.items.some(a => a.name === 'AnimeOnly') && !anime.items.some(a => a.name === 'MediaFusion'),
  'the Anime row takes focused add-ons, not general sources');
check(api.rows(all, false).every(r => Array.isArray(r.all) && r.all.length >= r.items.length),
  'every row keeps its full list for See all');

const sheet = readFileSync('qml/ExtensionsSetupSheet.qml', 'utf8');
const store = readFileSync('qml/ExtensionsStorePage.qml', 'utf8');
check(sheet.includes('WebEngineView') && sheet.includes('stremio://'),
  'setup opens inside the app and catches the stremio:// install link');
check(!/openUrlExternally/.test(sheet + store), 'nothing in the Store opens the outside browser');
// Android has no QtWebEngine: the Store must not name the WebEngine sheet statically (the whole
// page fails to load), and its own sheet hands setup to the phone's browser.
const phoneSheet = readFileSync('qml/ExtensionsSetupSheetAndroid.qml', 'utf8');
const houseHero = readFileSync('qml/+android/ExtensionsHouseHero.qml', 'utf8');
const addonLogo = readFileSync('qml/AddonLogo.qml', 'utf8');
const installedCard = readFileSync('qml/ExtensionsInstalledCard.qml', 'utf8');
check(!/^\s*ExtensionsSetupSheet\s*\{/m.test(store) && /Qt\.platform\.os === "android" \? "ExtensionsSetupSheetAndroid\.qml" : "ExtensionsSetupSheet\.qml"/.test(store),
  'the Store loads the setup sheet per platform, never as a static child');
check(!/QtWebEngine/.test(phoneSheet.replace(/^\s*\/\/.*$/gm, '')) && /openUrlExternally/.test(phoneSheet),
  'the Android sheet needs no WebEngine and opens setup in the browser');
check(/topBarBackOnly:\s*true/.test(store), 'the Store top bar carries only Back');
check(/preferManifestLogo:\s*true/.test(store) && /preferManifestLogo:\s*true/.test(installedCard)
    && /_usingManifestLogo: manifestLogo !== ""\s*&& \(preferManifestLogo \|\| _bundled === ""\)/.test(addonLogo),
  'URL previews and new installed cards use the same actual manifest logo');
check(/waitForArtworkResolution:\s*true/.test(store)
    && /installUrlPreviewReady:[\s\S]*?installPreviewLogo\.artworkResolved/.test(store)
    && /visible:\s*root\.installUrlPreviewReady/.test(store)
    && /root\.installUrlPreviewReady \? 488 : 358/.test(store)
    && /root\.installUrlPreviewReady\s*&&\s*!Extensions\.isInstalled/.test(store),
  'the URL preview card, expanded dialog, and install action share the artwork-ready state');
check(/artworkResolutionTimeoutMs:\s*3000/.test(addonLogo)
    && /onSourceChanged:\s*root\.waitForArtwork/.test(addonLogo)
    && /onStatusChanged:\s*root\.resolveArtwork/.test(addonLogo)
    && /logo\.status === Image\.Ready \|\| logo\.status === Image\.Error/.test(addonLogo)
    && /root\.waitForArtworkResolution && root\._artworkTimedOut/.test(addonLogo),
  'preview artwork waits for Image Ready or Error and falls back after a bounded timeout');
check(/sourceToggleMinimumTouchTarget:\s*44/.test(houseHero)
    && houseHero.includes('hero.sourceToggleMinimumTouchTarget / Math.max(0.01, appsRow.scale)')
    && /width:\s*Qt\.platform\.os === "android"/.test(houseHero)
    && /height:\s*Qt\.platform\.os === "android"/.test(houseHero),
  'House source toggles keep a 44 logical px Android touch target after tray scaling');

// Exercise the actual QML transaction functions in a small native-store stub. The shared
// installFinished(id, name) signal has no URL, so the Store must reject unrelated completions,
// preserve the selected instance switch, and retain the old row when cleanup cannot be saved.
function extractFunction(source, name) {
  const marker = `function ${name}(`;
  const start = source.indexOf(marker);
  if (start < 0) throw new Error(`Missing QML function ${name}`);
  const bodyStart = source.indexOf('{', start);
  let depth = 0, quote = '', escaped = false, lineComment = false, blockComment = false;
  for (let i = bodyStart; i < source.length; i++) {
    const c = source[i], next = source[i + 1];
    if (lineComment) { if (c === '\n') lineComment = false; continue; }
    if (blockComment) { if (c === '*' && next === '/') { blockComment = false; i++; } continue; }
    if (quote) {
      if (escaped) escaped = false;
      else if (c === '\\') escaped = true;
      else if (c === quote) quote = '';
      continue;
    }
    if (c === '/' && next === '/') { lineComment = true; i++; continue; }
    if (c === '/' && next === '*') { blockComment = true; i++; continue; }
    if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
    if (c === '{') depth++;
    else if (c === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`Unclosed QML function ${name}`);
}

const beginInstallSource = extractFunction(store, 'beginInstall');
const safeInstallMessageSource = extractFunction(store, 'safeInstallMessage');
const failInstallSource = extractFunction(store, 'failInstallOperation');
const finishInstallSource = extractFunction(store, 'finishInstallOperation');

function transactionHarness(rows, operation, options = {}) {
  const calls = { enabled: [], removed: [] };
  const context = {
    qsTr: value => value,
    Qt: { callLater: callback => callback() },
    setup: { open: true, status: '', addon: { manifestUrl: 'https://example.test/setup/manifest.json' } },
    Extensions: {
      normalizeUrl: value => String(value).trim(),
      installed: () => rows,
      setEnabledInstance(url, enabled) {
        calls.enabled.push([url, enabled]);
        if (!options.failEnable) {
          const target = rows.find(entry => entry.transportUrl === url);
          if (target) target.enabled = enabled;
        }
      },
      removeInstance(url) {
        calls.removed.push(url);
        if (!options.failRemove) {
          const index = rows.findIndex(entry => entry.transportUrl === url);
          if (index >= 0) rows.splice(index, 1);
        }
      }
    },
    root: {
      pendingInstallOperation: operation,
      profileGeneration: 0,
      setupGeneration: 1,
      urlInstallerGeneration: 0,
      installUrlOpen: false,
      installUrlInstalling: false,
      installUrlComplete: false,
      busy: {},
      failure: null,
      setBusy(slug, on) { if (on) this.busy[slug] = true; else delete this.busy[slug]; },
      failInstallOperation(op, message) { this.failure = { op, message }; },
      showInstallError(message) { this.installError = message; },
      takeKeyboardFocus() {}
    },
    calls
  };
  vm.createContext(context);
  const beginInstall = vm.runInContext(`(${beginInstallSource})`, context);
  const safeInstallMessage = vm.runInContext(`(${safeInstallMessageSource})`, context);
  const failInstall = vm.runInContext(`(${failInstallSource})`, context);
  const finishInstall = vm.runInContext(`(${finishInstallSource})`, context);
  context.root.safeInstallMessage = safeInstallMessage;
  context.root.failInstallOperation = failInstall;
  return { context, beginInstall, safeInstallMessage, failInstall, finishInstall, calls };
}

const singleFlight = transactionHarness([], null);
check(singleFlight.beginInstall.call(singleFlight.context.root,
  'https://example.test/first/manifest.json', { kind: 'store', expectedId: 'first.id' }) === true
  && singleFlight.beginInstall.call(singleFlight.context.root,
    'https://example.test/second/manifest.json', { kind: 'store', expectedId: 'second.id' }) === false
  && singleFlight.context.root.pendingInstallOperation.url === 'https://example.test/first/manifest.json',
  'Store install transactions are single-flight and keep their original URL correlation');

const targetUrl = 'https://example.test/configured/manifest.json';
const unrelated = transactionHarness(
  [{ id: 'same.id', transportUrl: targetUrl, enabled: false, installedAt: 12 }],
  { kind: 'setup', url: targetUrl, expectedId: 'same.id', profileGeneration: 0, targetInstalledAt: 12,
    replacingUrl: targetUrl, replacingEnabled: false, setupGeneration: 1 });
unrelated.finishInstall.call(unrelated.context.root, 'other.id');
check(unrelated.context.root.pendingInstallOperation !== null && unrelated.calls.enabled.length === 0
  && unrelated.calls.removed.length === 0,
  'an unrelated installFinished id cannot complete or mutate the pending target');

const sameUrl = transactionHarness(
  [{ id: 'same.id', transportUrl: targetUrl, enabled: true, installedAt: 13 }],
  { kind: 'setup', url: targetUrl, expectedId: 'same.id', profileGeneration: 0, targetInstalledAt: 12,
    replacingUrl: targetUrl, replacingEnabled: false, setupGeneration: 1 });
sameUrl.finishInstall.call(sameUrl.context.root, 'same.id');
check(sameUrl.context.Extensions.installed()[0].enabled === false
  && sameUrl.calls.enabled.length === 1 && sameUrl.calls.enabled[0][1] === false
  && sameUrl.calls.removed.length === 0,
  'same-URL reconfiguration preserves a disabled instance without removing it');

const differentUrl = transactionHarness(
  [
    { id: 'same.id', transportUrl: 'https://example.test/old/manifest.json', enabled: false, installedAt: 10 },
    { id: 'same.id', transportUrl: 'https://example.test/new/manifest.json', enabled: true, installedAt: 20 }
  ],
  { kind: 'setup', url: 'https://example.test/new/manifest.json', expectedId: 'same.id', profileGeneration: 0,
    targetInstalledAt: null, replacingUrl: 'https://example.test/old/manifest.json', replacingEnabled: false,
    setupGeneration: 1 });
differentUrl.finishInstall.call(differentUrl.context.root, 'same.id');
check(differentUrl.calls.enabled.length === 1 && differentUrl.calls.enabled[0][0].endsWith('/new/manifest.json')
  && differentUrl.calls.enabled[0][1] === false
  && differentUrl.context.Extensions.installed().length === 1
  && differentUrl.context.Extensions.installed()[0].transportUrl.endsWith('/new/manifest.json'),
  'different-URL reconfiguration preserves the selected switch and removes only that old instance');

const cleanupFailure = transactionHarness(
  [
    { id: 'same.id', transportUrl: 'https://example.test/old/manifest.json', enabled: false, installedAt: 10 },
    { id: 'same.id', transportUrl: 'https://example.test/new/manifest.json', enabled: true, installedAt: 20 }
  ],
  { kind: 'setup', url: 'https://example.test/new/manifest.json', expectedId: 'same.id', profileGeneration: 0,
    targetInstalledAt: null, replacingUrl: 'https://example.test/old/manifest.json', replacingEnabled: false,
    setupGeneration: 1 },
  { failRemove: true });
cleanupFailure.finishInstall.call(cleanupFailure.context.root, 'same.id');
check(cleanupFailure.context.Extensions.installed().length === 2
  && /previous one couldn't be removed/.test(cleanupFailure.context.setup.status)
  && cleanupFailure.context.setup.open,
  'failed replacement cleanup is visible and retains both configured instances');

const redactedFailure = transactionHarness([], {
  kind: 'setup', url: 'https://example.test/configured/manifest.json', expectedId: 'same.id',
  profileGeneration: 0, setupGeneration: 1
});
redactedFailure.context.root.safeInstallMessage = redactedFailure.safeInstallMessage;
redactedFailure.failInstall.call(redactedFailure.context.root, redactedFailure.context.root.pendingInstallOperation,
  'Service rejected https://private.example/private/path?token=secret-value');
check(redactedFailure.context.setup.status.includes('Service rejected')
  && !redactedFailure.context.setup.status.includes('secret-value')
  && !redactedFailure.context.setup.status.includes('/private/path'),
  'setup failures retain safe native details while redacting URL paths and query tokens');
check(/installUrlError\s*=\s*qsTr\("Couldn't preview it: %1"\)\.arg\(root\.safeInstallMessage\(reason\)\)/.test(store),
  'manifest preview failures also redact raw request URLs');

const normalInstall = transactionHarness(
  [{ id: 'ordinary.id', transportUrl: 'https://example.test/ordinary/manifest.json', enabled: false, installedAt: 21 }],
  { kind: 'store', url: 'https://example.test/ordinary/manifest.json', expectedId: 'ordinary.id',
    profileGeneration: 0, targetInstalledAt: null, slug: 'ordinary' });
normalInstall.finishInstall.call(normalInstall.context.root, 'ordinary.id');
check(normalInstall.context.Extensions.installed()[0].enabled === true
  && normalInstall.calls.enabled[0][1] === true,
  'a regular Store install turns on the exact successfully installed URL');

if (failures) { console.error('\n' + failures + ' Store contract check(s) failed'); process.exit(1); }
console.log('\nPASS — Extensions Store contract');
