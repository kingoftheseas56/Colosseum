import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';

const qmlPath = 'qml/ExtensionsChainPage.qml';
assert.ok(existsSync(qmlPath), 'ExtensionsChainPage.qml must exist');

const qml = readFileSync(qmlPath, 'utf8');
const has = (re, message) => assert.match(qml, re, message);

has(/objectName:\s*"extensionsChainPage"/,
    'the QML surface must expose a stable page objectName');
has(/objectName:\s*"extensionsChainScroll"/,
    'the QML surface must expose a stable scroll objectName');
has(/\.\.\/assets\/extensions\/colosseum-night-cc0-1920x1080\.jpg/,
    'the QML surface must use the bundled CC0 Colosseum night photo');

for (const label of ['Chain', 'House', 'Store'])
    has(new RegExp('text:\\s*"' + label + '"'), 'picker must contain ' + label);

for (const medium of ['comics', 'manga', 'books', 'audiobook', 'tv', 'movies'])
    has(new RegExp('key:\\s*"' + medium + '"'), 'missing medium endpoint ' + medium);

for (const id of [
    'colosseum.well.getcomics.issues',
    'colosseum.well.tankoyomi',
    'colosseum.well.indexers',
    'colosseum.well.libgen',
    'colosseum.well.audiobookbay',
    'com.stremio.torrentio.addon'
])
    assert.ok(qml.includes(id), 'missing extension id ' + id);

has(/signal\s+sectionRequested\s*\(string section\)/,
    'section picker must expose routing intent');
has(/signal\s+addonToggleRequested\s*\(string extensionId, bool enabled\)/,
    'addon nodes must expose install/uninstall intent');
has(/component\s+LightPool\s*:/,
    'background needs per-medium local light pools');
has(/component\s+ChainWire\s*:/,
    'chain connections need a reusable QML wire component');
has(/SequentialAnimation\s*\{[\s\S]*?loops:\s*1/,
    'wire energizing animation must be one-shot');
assert.ok(!/Animation\.Infinite/.test(qml),
    'wire animations must settle instead of looping forever');

console.log('extensions_chain_qml_contract_test: PASS');
