// warming_queue_test.mjs — the Stage 2 warming pick: given the modes already in the
// keep-alive stack and the target world list, choose the next world to pre-build, in
// target order, skipping ones already present; "" when everything is warmed. Loads the
// real .pragma library file (same pattern as tests/abb_parse_test.mjs).
import fs from 'fs';
let src = fs.readFileSync('qml/WarmingQueue.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.nextWarmMode=nextWarmMode;')(mod);

function fail(m) { console.log("FAIL " + m); process.exit(1); }

const targets = ["Tankoban", "Theatre", "Biblio"];
if (mod.nextWarmMode([], targets) !== "Tankoban") fail("empty stack -> first target");
if (mod.nextWarmMode(["Tankoban"], targets) !== "Theatre") fail("first warmed -> second");
if (mod.nextWarmMode(["Tankoban", "Theatre"], targets) !== "Biblio") fail("two warmed -> third");
if (mod.nextWarmMode(["Tankoban", "Theatre", "Biblio"], targets) !== "") fail("all warmed -> empty string");
// Target order is authoritative regardless of which are already present:
if (mod.nextWarmMode(["Theatre"], targets) !== "Tankoban") fail("skips present, keeps target order");
// Unknown modes already open (e.g. a genre page) don't confuse it:
if (mod.nextWarmMode(["Somewhere"], targets) !== "Tankoban") fail("ignores non-target present modes");
console.log("warming_queue_test: ALL PASS");
