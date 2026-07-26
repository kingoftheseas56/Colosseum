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
const js  = readFileSync(path.join(here, '..', 'qml', 'ExtensionsCatalog.js'), 'utf8');

let failed = 0;
const ok  = m => console.log(`  ok   ${m}`);
const bad = m => { console.log(`  FAIL ${m}`); failed++; };
const must    = (re, m) => re.test(qml) ? ok(m) : bad(m);
const mustNot = (re, m) => re.test(qml) ? bad(m) : ok(m);

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

console.log('\nstore panes cannot render outside a world that has a store');
must(/visible: root\.pane === "discover" && root\.hasStore/, 'Discover is gated on hasStore');
must(/visible: root\.pane === "browse" && root\.hasStore/,   'Browse is gated on hasStore');
must(/visible: root\.pane === "installed" \|\| !root\.hasStore/,
     'Installed renders whenever there is no store, so no world is ever blank');
// The handler must compare `world` directly — reading the derived binding inside its own
// change handler is what made clicking Tankoban from Discover do nothing.
must(/onWorldChanged: if \(world !== "theatre"/,
     'onWorldChanged compares world directly, not the hasStore binding derived from it');

console.log('\nthe descriptions Hemanth removed stay removed');
// Match the QUOTED string only — the same words survive in a code comment describing the
// grouping, and a comment is not something he can see.
mustNot(/"what fills the shelves[^"]*"/, 'no CATALOGUE subtitle rendered');
mustNot(/"what actually fetches[^"]*"/,  'no WELLS subtitle rendered');
mustNot(/groupSub/,                      'the groupSub property is gone entirely');
mustNot(/irow\.manifest\.description/,   'installed rows draw no description line');

console.log('\nthe world-scoped count stays world-scoped');
must(/paneTab\.modelData\.label \+ root\.countIn\(root\.world\)/,
     'the Installed tab counts its own world, not the whole app');
mustNot(/paneTab\.modelData\.label \+ root\.installedList\.length/,
     'the global count does not come back');

console.log(failed === 0 ? '\nall green' : `\n${failed} FAILED`);
process.exit(failed === 0 ? 0 : 1);
