import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';

const storePath = 'worlds/extensions/Store.svelte';
const appPath = 'worlds/extensions/App.svelte';
const hostPath = 'qml/ExtensionsWorldPage.qml';

assert.ok(existsSync(storePath), 'Store.svelte must exist');
const store = readFileSync(storePath, 'utf8');
const app = readFileSync(appPath, 'utf8');
const host = readFileSync(hostPath, 'utf8');

for (const essential of ['Torrentio', 'Streaming Catalogs', 'OpenSubtitles v3', 'Anime Kitsu'])
  assert.ok(store.includes(essential), 'missing essential ' + essential);

for (const category of ['Movies', 'TV Shows', 'Metadata', 'Anime', 'Asian Drama', 'Bollywood'])
  assert.ok(store.includes(category), 'missing wide category banner ' + category);

assert.equal((store.match(/class="category-banner kind-/g) || []).length, 1,
  'Store must render categories through the reusable category-banner markup');
assert.match(store, /height:\s*210px/, 'category banners must be cinematic wide strips');
assert.match(store, /width:\s*100%/, 'category banners must span the content width');
assert.match(store, /class="hero-feature"/, 'Store must include the Stremio Addons-style featured hero');
assert.ok(store.includes('ThePirateBay+'), 'featured hero must show ThePirateBay+');
assert.match(store, /class="store-search"/, 'Store must have the top-right search field');
assert.match(store, /card-art/, 'Essentials must use Harbor-style faint art backdrops');
assert.match(store, /opacity:\s*\.1/, 'Harbor-style essential backdrop opacity must stay at 10%');
assert.ok(store.includes('colosseum-night-cc0-1920x1080.jpg'),
  'Store must carry Colosseum visual identity');

assert.match(app, /import Store from '.\/Store\.svelte'/, 'App must import Store');
assert.match(app, /tab === 'store'/, 'App must render Store in the third mode');
assert.ok(!app.includes('host.openStore()'), 'Store mode must no longer jump to the QML Store');
assert.ok(!host.includes('source: "ExtensionsPage.qml"'), 'Svelte Store must not fall through to the old QML Store');
assert.ok(!host.includes('function openStore()'), 'QML host must not expose the old QML Store route');

console.log('extensions_store_svelte_contract_test: PASS');
