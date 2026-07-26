// extension_page_wiring_contract.mjs — grep contracts over qml/ExtensionsPage.qml.
//
// WHY THIS EXISTS. On 2026-07-26 I changed Catalog.moveDestination from returning an
// index (-1 for none) to returning { id, index } (null for none), updated the JS unit
// test, and shipped. Every suite was green — and the reorder arrows were DEAD, because
// all three QML call sites still tested `>= 0`, which is false for an object. The unit
// tests exercised the module in isolation; nothing checked that the page still spoke the
// same contract. Hemanth found it on screen.
//
// A .qml file cannot be imported and run headlessly, so these are deliberately grep-level
// checks. They are cheap and they catch exactly one thing well: the page drifting away
// from an API the tests are still happily proving works.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const qml = readFileSync(path.join(here, '..', 'qml', 'ExtensionsPage.qml'), 'utf8');
const src = readFileSync(path.join(here, '..', 'qml', 'ExtensionsSources.qml'), 'utf8');
const js  = readFileSync(path.join(here, '..', 'qml', 'ExtensionsCatalog.js'), 'utf8');

let failed = 0;
const ok  = m => console.log(`  ok   ${m}`);
const bad = m => { console.log(`  FAIL ${m}`); failed++; };
const must    = (re, m) => re.test(qml) ? ok(m) : bad(m);
const mustNot = (re, m) => re.test(qml) ? bad(m) : ok(m);
const srcMust    = (re, m) => re.test(src) ? ok(m) : bad(m);
const srcMustNot = (re, m) => re.test(src) ? bad(m) : ok(m);

console.log('moveDestination returns an object — no call site may test it as a number');
{
  // Pin the JS side first, so this file fails loudly if the contract changes again.
  const returnsObject = /return costOther < costClicked \? \{ id: otherId, index: ia \}/.test(js);
  returnsObject ? ok('ExtensionsCatalog.js still returns { id, index }')
                : bad('ExtensionsCatalog.js contract changed — update this contract file too');

  const numericTest = /moveDestination\([^)]*\)\s*(>=|>|<|<=|===\s*-1|!==\s*-1)/s;
  mustNot(numericTest, 'no call site compares moveDestination(...) to a number');
  must(/moveDestination\([\s\S]{0,200}?\)\s*!==\s*null/, 'the enable bindings test !== null');
  must(/var m = Catalog\.moveDestination\([^)]*\);\s*\n\s*if \(m\) Extensions\.moveTo\(m\.id, m\.index\)/,
       'moveWell unpacks { id, index } and passes BOTH to moveTo');
}

console.log('\nthe reorder path uses the world-aware API, never the retired one');
mustNot(/Extensions\.move\(/, 'no caller of the retired Extensions.move(id, ±steps)');
must(/Extensions\.moveTo\(/, 'Extensions.moveTo is what the page calls');
must(/root\.moveWell\(irow\.modelData\.id,\s*-1\)/, 'the ▲ arrow goes through moveWell');
must(/root\.moveWell\(irow\.modelData\.id,\s*1\)/,  'the ▼ arrow goes through moveWell');

console.log('\nSources is world-agnostic; world tabs belong to Installed alone');
must(/ExtensionsSources \{/, 'the Sources pane is mounted');
must(/visible: root\.pane === "sources"/, 'it renders on the sources pane');
must(/readonly property bool worldTabsApply: pane === "installed"/,
     'world tabs apply to Installed alone');
must(/visible: root\.worldTabsApply/, 'the world tab strip is gated on that');
// The old three-worlds x three-panes grid is gone. Nothing may reintroduce a world gate on
// a pane — that is what made two of three panes decorative (A5's audit P0-6).
mustNot(/hasStore/, 'the retired hasStore world-gating is gone');
mustNot(/\{ key: "discover"/, 'Discover is no longer a pane');

console.log('\nSources survives universes, which fetch nothing and are never ranked');
srcMust(/worldOrder: \["theatre", "tankoban", "biblio", "universes"\]/,
        'universes is a world of its own, after the three media worlds');
srcMust(/visible: chainRow\.modelData\.key !== "universes"/,
        'universes never appear in the ask chain');
srcMust(/if \(rows\.length\) out\.push/, 'a world carrying nothing does not render');
srcMust(/if \(!row\.isWell\) return 0;/, 'only sources carry a rank');

console.log('\nSources speaks the same world-aware reorder contract');
srcMustNot(/moveDestination\([^)]*\)\s*(>=|>|<|<=|===\s*-1|!==\s*-1)/,
           'no numeric comparison of moveDestination');
srcMust(/if \(m\) Extensions\.moveTo\(m\.id, m\.index\)/, 'moveWell passes BOTH id and index');
srcMustNot(/Extensions\.move\(/, 'the retired ±steps API is absent');

console.log('\nthe descriptions Hemanth removed stay removed');
// Match the QUOTED string only — the same words survive in a code comment describing the
// grouping, and a comment is not something he can see.
mustNot(/"what fills the shelves[^"]*"/, 'no CATALOGUE subtitle rendered');
mustNot(/"what actually fetches[^"]*"/,  'no WELLS subtitle rendered');
mustNot(/groupSub/,                      'the groupSub property is gone entirely');
mustNot(/irow\.manifest\.description/,   'installed rows draw no description line');
srcMustNot(/manifest\.description/,      'Sources rows draw no description either');
// The one sub-line that survives is not a description — it is where else this source is
// asked and in what position. Its absence is what let a reorder cross worlds silently.
srcMust(/function tieFor\(entry, world\)/, 'the cross-world tie is still computed');

console.log('\nthe featured slab derives its state, never asserts it');
mustNot(/text: "built-in"/, 'the false "built-in" line is gone');
must(/featuredVerb\.isCore \? "Built-in"/, 'the slab carries the four-state verb in TEXT');
must(/readonly property bool isCore: root\.coreOf\(featuredVerb\.item\)/,
     'core is read off the installed entry, not the curated data that has no core field');
must(/readonly property bool isOn: root\.carried\(featuredVerb\.item\)/,
     'installed state is derived from carried()');
must(/onClicked: root\.installFromCard\(featuredVerb\.item\)/,
     'the slab offers a real install when the featured add-on is absent');

console.log('\nthe Installed count matches what that pane will draw');
must(/root\.worldTabsApply \? root\.countIn\(root\.world\)/,
     'it counts the world while a world is selectable');
must(/: root\.installedList\.length\)/,
     'and the whole roster when no world is in play');

console.log(failed === 0 ? '\nall green' : `\n${failed} FAILED`);
process.exit(failed === 0 ? 0 : 1);
