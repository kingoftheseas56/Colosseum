import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const chain = readFileSync('qml/ExtensionsChainPage.qml', 'utf8');
const house = readFileSync('qml/ExtensionsHousePage.qml', 'utf8');

const must = (text, re, msg) => assert.match(text, re, msg);

must(chain, /designHeight:\s*1312/, 'Chain canvas must match Svelte 92px frame + 1220px scroll');
must(chain, /displayFamily:\s*"Georgia"/, 'Chain display face must match Svelte Georgia');
must(chain, /uiFamily:\s*"Segoe UI"/, 'Chain UI face must match Svelte Segoe UI');
for (const pair of [['116','80'],['306','84'],['480','84'],['732','132'],['1044','150']])
  must(chain, new RegExp('y:\\s*' + pair[0] + '[\\s\\S]{0,180}?height:\\s*' + pair[1]), 'missing Chain row ' + pair.join('/'));
must(chain, /width:\s*stage\.width \* 0\.135/, 'Chain add-ons must use Svelte 13.5% width');
must(chain, /width:\s*stage\.width \* 0\.115/, 'Chain endpoints must use Svelte 11.5% width');
must(chain, /y:\s*15[\s\S]{0,140}?width:\s*230[\s\S]{0,80}?height:\s*46/, 'Chain picker must match compact Svelte picker');
must(chain, /text:\s*"Manage"/, 'Chain QML model must show the official Manage capsule');
must(chain, /opacity:\s*1\.0/, 'Gold icons must not be dimmed before colorization');

must(house, /interval:\s*7500/, 'House slideshow cadence must match Svelte');
must(house, /source:\s*"\.\.\/assets\/extensions\/house\/starwars-banner\.jpg"/, 'House must use the bundled Star Wars banner');
assert.ok(!house.includes('extensionBackground("com.colosseum.universe.starwars"'), 'House must not override the Svelte Star Wars banner dynamically');
must(house, /anchors\.bottomMargin:\s*root\.width < 1250 \? 28 : 42/, 'House tray vertical placement must match Svelte');
must(house, /height:\s*root\.width < 1250 \? 190 : 214/, 'House tray height must match Svelte');
must(house, /width:\s*root\.width < 1250 \? 104 : 122/, 'House app tile size must match Svelte');
must(house, /height:\s*root\.width < 1250 \? 104 : 122/, 'House app tile height must match Svelte');
must(house, /height:\s*30[\s\S]{0,120}?radius:\s*11/, 'House name pill must match Svelte');
assert.ok(house.includes('id: downloadButton') &&
          house.includes('width: root.width < 1250 ? 28 : 30') &&
          house.includes('height: width'),
          'House download button must match Svelte responsive sizing');
must(house, /y:\s*15[\s\S]{0,140}?width:\s*230[\s\S]{0,80}?height:\s*46/, 'House picker must match compact Svelte picker');
must(house, /text:\s*"Manage"/, 'House QML model must show the official Manage capsule');

console.log('extensions_qml_svelte_parity_contract_test: PASS');
