// Regression: a built-in add-on that ships switched off (Torrentio) keeps the
// one-tap "Install without setup" path in both setup sheets.
//
// Nokia T20 (2026-10-22): canSkip hid that button whenever the link was
// present, and a seeded-but-off Torrentio is present, so switching it on
// required the browser copy/paste setup. Runs the ACTUAL switchedOnAt() and
// canSkip expression from each sheet. Run: node tests/extensions_setup_one_tap_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repo = join(dirname(fileURLToPath(import.meta.url)), "..");
const URL = "https://torrentio.strem.fun/manifest.json";
let failures = 0;
function check(cond, label) {
    console.log(cond ? "PASS" : "FAIL", label);
    if (!cond) failures += 1;
}

for (const file of ["qml/ExtensionsSetupSheetAndroid.qml", "qml/ExtensionsSetupSheet.qml"]) {
    const qml = readFileSync(join(repo, file), "utf8").replace(/\r\n/g, "\n");
    const fn = /function switchedOnAt\(url\) \{([\s\S]*?)\n    \}\n/.exec(qml);
    const expr = /readonly property bool canSkip:([\s\S]*?)\n    (?:readonly|signal|\/\/|function|property)/.exec(qml);
    if (!fn || !expr) {
        check(false, `${file}: switchedOnAt/canSkip boundary`);
        continue;
    }
    const canSkip = (enabled, setupRequired = false) => {
        const rows = enabled === null ? [] : [{ id: "com.stremio.torrentio.addon", transportUrl: URL, enabled }];
        const Extensions = { installed: () => rows, normalizeUrl: (u) => u };
        const sheet = { open: true, addon: { manifestUrl: URL, setupRequired } };
        sheet.switchedOnAt = new Function("url", "Extensions", fn[1]).bind(null);
        const wrapped = { ...sheet, switchedOnAt: (u) => new Function("url", "Extensions", fn[1])(u, Extensions) };
        return new Function("sheet", "Extensions", `return (${expr[1]});`)(wrapped, Extensions);
    };
    check(canSkip(false) === true, `${file}: seeded but switched off -> one-tap install offered`);
    check(canSkip(null) === true, `${file}: not present -> one-tap install offered`);
    check(canSkip(true) === false, `${file}: already switched on -> no redundant install`);
    check(canSkip(false, true) === false, `${file}: setup-required add-on -> no skip`);
}
console.log(`EXTENSIONS_SETUP_ONE_TAP ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
