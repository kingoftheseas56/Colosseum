import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';

const qmlPath = 'qml/ExtensionsHousePage.qml';
assert.ok(existsSync(qmlPath), 'ExtensionsHousePage.qml must exist');

const qml = readFileSync(qmlPath, 'utf8');
const has = (re, message) => assert.match(qml, re, message);

has(/objectName:\s*"extensionsHousePage"/, 'House page needs a stable objectName');
has(/interval:\s*7500/, 'Universe slideshow must retain the 7.5 second cadence');

for (const label of ['Chain', 'House', 'Store'])
    has(new RegExp('text:\\s*"' + label + '"'), 'picker must contain ' + label);

assert.ok(qml.includes('root.extensionBackground("com.colosseum.universe.starwars"'),
    'Star Wars House slot must use the installed universe extension banner');
assert.ok(qml.includes('../assets/extensions/house/starwars-banner.jpg'),
    'Star Wars House slot must keep the exact seeded universe banner locally');
assert.ok(!qml.includes('../assets/universes/star-wars/hoth.jpg'),
    'House must not substitute one Star Wars era environment for the universe banner');
assert.ok(!/StarWarsGalaxySystem\s*\{/.test(qml),
    'House must show the Star Wars banner, not embed the full Star Wars universe page');
assert.ok(!qml.includes('starwars-vader.jpg'),
    'retired Darth Vader still must not remain in the QML House page');
for (const art of ['dcau.jpg', 'cosmere.jpg'])
    assert.ok(qml.includes(art), 'missing House wallpaper ' + art);

has(/Glass\s*\{[\s\S]*?id:\s*trayGlass[\s\S]*?backdrop:\s*wallpaperLayer/,
    'House tray must use the shared real-backdrop Glass material');
has(/id:\s*trayGlass[\s\S]*?blurAmount:\s*1\.0/,
    'House tray must use full real blur, not a translucent fake');
// Preserve the current QML film and sizing; the older HTML parity numbers
// describe an earlier revision. Navigation and manifest fallback run in QtTest.
has(/id:\s*trayGlass[\s\S]*?blurMax:\s*34/,
    'House tray must retain the current shared material ceiling');
has(/id:\s*trayGlass[\s\S]*?tint:\s*0\.00/,
    'House tray must retain its separately drawn gradient film');
has(/Glass\s*\{[\s\S]*?id:\s*pickerGlass[\s\S]*?backdrop:\s*wallpaperLayer/,
    'House picker must use the same real-backdrop Glass material');
has(/id:\s*tray[\s\S]*?height:\s*root\.width\s*<\s*1250\s*\?\s*190\s*:\s*214/,
    'House shelf must retain its responsive QML sizing');
has(/anchors\.bottomMargin:\s*root\.width\s*<\s*1250\s*\?\s*28\s*:\s*42/,
    'House shelf must float above the bottom instead of sitting on it');
has(/id:\s*appTile[\s\S]*?width:\s*root\.width\s*<\s*1250\s*\?\s*104\s*:\s*122[\s\S]*?height:\s*root\.width\s*<\s*1250\s*\?\s*104\s*:\s*122/,
    'House extension cards must retain the responsive QML sizes');
has(/id:\s*downloadButton[\s\S]*?width:\s*root\.width\s*<\s*1250\s*\?\s*28\s*:\s*30[\s\S]*?height:\s*width/,
    'House download controls must retain the responsive square sizes');
assert.ok(qml.includes('Qt.rgba(250/255,250/255,248/255,0.97)'),
    'House tiles must use the slightly translucent HTML ivory instead of opaque white');
assert.ok(qml.includes('#efc15a'),
    'House selected pill must use the HTML gold');
assert.ok(!qml.includes('Inter-Regular.otf'), 'House parity uses Segoe UI, not Inter');
assert.ok(qml.includes('tileKind: "universes"'),
    'Tributes tile must retain its Universe destination');
assert.ok(qml.includes('../assets/icons/download.svg'),
    'download controls must use the real download glyph');

for (const name of [
    'Tributes', 'Grand Database', 'Nyaa', 'Tankoyomi',
    'GetComics', 'Tankorent', 'LibGen', 'AudioBookBay'
])
    assert.ok(qml.includes(name), 'missing House tile ' + name);

has(/signal\s+sectionRequested\s*\(string section\)/,
    'House picker must expose section routing');
has(/signal\s+installRequested\s*\(string extensionId, bool installed\)/,
    'download buttons must expose install intent');
has(/signal\s+universesRequested\s*\(\)/,
    'Universes tile must route to its future configuration surface');

assert.ok(!/One Piece/.test(qml), 'House slideshow should only carry the current three universes');

console.log('extensions_house_qml_contract_test: PASS');
