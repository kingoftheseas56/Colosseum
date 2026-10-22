// Regression: the Extensions store treats an add-on as installed only when it
// is present AND switched on.
//
// Nokia T20 (2026-10-22): Torrentio ships seeded but switched off. The store
// counted the seeded row as installed, so its card looked the same before and
// after Set up and users could not tell whether streams were available.
//
// Runs the ACTUAL isInstalled() body from qml/ExtensionsStorePage.qml with a
// fake Extensions object. Run: node tests/extensions_store_installed_state_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repo = join(dirname(fileURLToPath(import.meta.url)), "..");
const page = readFileSync(join(repo, "qml", "ExtensionsStorePage.qml"), "utf8").replace(/\r\n/g, "\n");
const card = readFileSync(join(repo, "qml", "ExtensionsStoreCard.qml"), "utf8").replace(/\r\n/g, "\n");
const m = /function isInstalled\(a\) \{([\s\S]*?)\n    \}\n/.exec(page);
if (!m) throw new Error("isInstalled boundary not found");
const isInstalled = new Function("a", "Extensions", m[1]);

const TORRENTIO = { id: "com.stremio.torrentio.addon", manifestUrl: "https://torrentio.strem.fun/manifest.json" };
function store(rows) {
    return {
        installed: () => rows,
        isInstalled: (key) => rows.some((r) => r.id === key || r.transportUrl === key),
        normalizeUrl: (u) => String(u).trim(),
    };
}

let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}

check(isInstalled(TORRENTIO, store([{ id: TORRENTIO.id, transportUrl: TORRENTIO.manifestUrl, enabled: false }])) === false,
      "seeded but switched-off add-on is not installed");
check(isInstalled(TORRENTIO, store([{ id: TORRENTIO.id, transportUrl: TORRENTIO.manifestUrl, enabled: true }])) === true,
      "switched-on add-on is installed");
check(isInstalled(TORRENTIO, store([
    { id: TORRENTIO.id, transportUrl: TORRENTIO.manifestUrl, enabled: false },
    { id: "configured.instance", transportUrl: TORRENTIO.manifestUrl, enabled: true },
])) === true, "an enabled instance at the same link counts as installed");
check(isInstalled(TORRENTIO, store([])) === false, "absent add-on is not installed");
check(isInstalled(null, store([])) === false, "no add-on is not installed");
check(/card\.installed\s*\n\s*\? "✓ Installed"/.test(card),
      "phone card shows the desktop badge wording once installed");

console.log(`EXTENSIONS_STORE_INSTALLED_STATE ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
