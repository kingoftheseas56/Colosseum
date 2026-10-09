import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const chainQ = readFileSync('qml/ExtensionsChainPage.qml', 'utf8');
const houseQ = readFileSync('qml/ExtensionsHousePage.qml', 'utf8');
const chainS = readFileSync('worlds/extensions/Chain.svelte', 'utf8');
const houseS = readFileSync('worlds/extensions/House.svelte', 'utf8');
const appS = readFileSync('worlds/extensions/App.svelte', 'utf8');

const has = (src, re, msg) => assert.match(src, re, msg);

// Authority sanity: make sure this test still points at the current Svelte geometry.
has(chainS, /root:\s*\[24, 80\].*world:\s*\[214, 84\].*base:\s*\[388, 84\].*addon:\s*\[640, 132\].*end:\s*\[952, 150\]/s,
    'Svelte Chain row authority changed; update parity test deliberately');
has(chainS, /const ADDON_W = 13\.5/, 'Svelte Chain addon width authority changed');
has(chainS, /const END_W = 11\.5/, 'Svelte Chain endpoint width authority changed');
has(appS, /top:15px.*height:46px/s, 'Svelte picker geometry authority changed');
has(houseS, /bottom:42px.*height:214px/s, 'Svelte House tray authority changed');
has(houseS, /\.app\{width:122px;height:122px/, 'Svelte House app-tile authority changed');

// QML Chain must match Svelte screen-space positions (92px frame offset + row top).
has(chainQ, /designHeight:\s*1312/, 'QML Chain canvas must match Svelte 92 + 1220 page height');
for (const [y, h, label] of [[116,80,'root'],[306,84,'world'],[480,84,'base'],[732,132,'addon'],[1044,150,'endpoint']])
    has(chainQ, new RegExp('y:\\s*' + y + '[\\s\\S]{0,180}height:\\s*' + h), 'QML Chain ' + label + ' row must match Svelte');
has(chainQ, /xPct:\s*0\.02[\s\S]*xPct:\s*0\.182[\s\S]*xPct:\s*0\.3525[\s\S]*xPct:\s*0\.512[\s\S]*xPct:\s*0\.674[\s\S]*xPct:\s*0\.836/,
    'QML Chain addon horizontal positions must match Svelte');
has(chainQ, /width:\s*stage\.width \* 0\.135/, 'QML Chain addon width must match Svelte 13.5%');
has(chainQ, /width:\s*stage\.width \* 0\.115/, 'QML Chain endpoint width must match Svelte 11.5%');
has(chainQ, /text:\s*"Chain"[\s\S]{0,180}font\.pixelSize:\s*14/, 'QML picker must use 14px Svelte-scale text');
has(chainQ, /id:\s*picker[\s\S]{0,160}height:\s*46/, 'QML picker must use 46px total height');
assert.ok(chainQ.includes('signal manageRequested()'), 'QML Chain needs Manage behavior seam');
assert.ok(chainQ.includes('text: "Manage"'), 'QML Chain needs Manage capsule');
for (const d of ['Comic source','Chapter source','Manga & books','Book source','Audiobook source','Video source'])
    assert.ok(chainQ.includes(d), 'QML Chain missing optional description: ' + d);

// House geometry and behavior parity.
has(houseQ, /id:\s*picker[\s\S]{0,160}y:\s*15[\s\S]{0,120}height:\s*46/, 'QML House picker must match Svelte picker');
has(houseQ, /anchors\.bottomMargin:\s*root\.width < 1250 \? 28 : 42/, 'QML House tray bottom must match Svelte desktop and narrow values');
has(houseQ, /height:\s*root\.width < 1250 \? 190 : 214/, 'QML House tray height must match Svelte desktop and narrow values');
has(houseQ, /width:\s*root\.width < 1250 \? 104 : 122[\s\S]{0,90}height:[\s\S]{0,90}radius:\s*root\.width < 1250 \? 18 : 21/, 'QML House app tiles must match Svelte responsive sizes');
has(houseQ, /height:\s*30[\s\S]{0,420}id:\s*nameText/, 'QML House name pills must match Svelte');
has(houseQ, /id:\s*downloadButton[\s\S]{0,420}width:\s*root\.width < 1250 \? 28 : 30[\s\S]{0,160}height:\s*width/, 'QML House download buttons must match Svelte responsive sizes');
assert.ok(!houseQ.includes('extensionBackground('), 'QML House must use the same bundled Universe art as Svelte');
assert.ok(houseQ.includes('signal manageRequested()'), 'QML House needs Manage behavior seam');
assert.ok(houseQ.includes('text: "Manage"'), 'QML House needs Manage capsule');
has(houseQ, /Extensions\.setEnabled\(extensionId, nextState\)/, 'QML House installs must call the same native store behavior');
has(houseQ, /activeFocusOnTab:\s*tileItem\.tileKind === "universes"/, 'Only Universes artwork tile should be focusable like Svelte');

console.log('extensions_qml_svelte_parity_test: PASS');
