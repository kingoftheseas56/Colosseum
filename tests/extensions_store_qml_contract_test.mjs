import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';

const qmlPath = 'qml/ExtensionsStorePage.qml';
assert.ok(existsSync(qmlPath), 'ExtensionsStorePage.qml must exist');

const qml = readFileSync(qmlPath, 'utf8');
const has = (re, message) => assert.match(qml, re, message);

has(/objectName:\s*"extensionsStorePage"/, 'Store page needs a stable objectName');
for (const label of ['Chain', 'House', 'Store'])
    assert.ok(qml.includes('"' + label + '"'), 'mode picker must contain ' + label);

has(/placeholderText:\s*"Search extensions"/,
    'Store must keep the top-right search bar');

has(/text:\s*"COMMUNITY EXTENSIONS · LIVE CATALOGUE"/,
    'Store intro must keep the editorial eyebrow');
has(/Find the exact piece[\s\S]*your library is missing\./,
    'Store intro must port the Stremio Addons hero headline');
assert.ok(qml.includes('ThePirateBay+'), 'Store intro must feature ThePirateBay+');
assert.ok(qml.includes('thepiratebay-feature-bg.jpg'), 'Store must bundle the featured PirateBay art');
assert.ok(qml.includes('thepiratebay-logo.png'), 'Store must bundle the featured PirateBay logo');

has(/text:\s*"Essentials"/, 'Store homepage must include Essentials');
for (const name of ['Torrentio', 'Streaming Catalogs', 'OpenSubtitles v3', 'Anime Kitsu'])
    assert.ok(qml.includes(name), 'missing essential ' + name);

has(/component\s+EssentialCard\s*:/,
    'Essentials must be a reusable Harbor-inspired component');
has(/opacity:\s*0\.10/,
    'Harbor-style essentials must use the addon art as a faint backdrop');
has(/columns:\s*2/,
    'Essentials must use Harbor-style paired rows instead of one long strip');

for (const category of ['Movies','TV Shows','Metadata','Anime','Asian Drama','Bollywood'])
    assert.ok(qml.includes(category), 'missing homepage category banner ' + category);

const categoryRows = (qml.match(/ListElement \{ modelTitle:/g) || []).length;
assert.equal(categoryRows, 6, 'Store homepage must have exactly six category banners');

has(/component\s+CategoryBanner\s*:/,
    'Store categories must be rendered as reusable wide banners');
has(/height:\s*210/,
    'category banners must be cinematic widescreen strips');
has(/width:\s*categoryColumn\.width/,
    'each category banner must run from one side of the content column to the other');
assert.ok(!qml.includes('columns: 2\n                columnSpacing: 18\n                rowSpacing: 18'),
    'category banners must not be tiled side by side');

has(/signal\s+categoryRequested\s*\(string category\)/,
    'category banners must route to category pages');
has(/signal\s+essentialRequested\s*\(string slug\)/,
    'essentials must route to extension details');
has(/signal\s+searchRequested\s*\(string query\)/,
    'search box must expose search intent');

assert.ok(!qml.includes('Live TV'), 'Live TV belongs on the homepage widget, not Store');
assert.ok(!qml.includes('"Music"'), 'Music belongs on the homepage widget, not Store');
assert.ok(!qml.includes('"Radios"'), 'Radios belongs on the homepage widget, not Store');

console.log('extensions_store_qml_contract_test: PASS');
