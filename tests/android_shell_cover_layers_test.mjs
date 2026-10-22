// Regression: every surface named in Main.qml's shell-cover bindings exists.
//
// Nokia T20 (2026-10-22): fullWindowPageOpen referenced a removed
// `universeHallLayer`. With every earlier operand false the binding threw a
// ReferenceError and kept its previous value, so after search or a title page
// the Android world tab bar (Profile -> Extensions) stayed hidden for good.
//
// Run: node tests/android_shell_cover_layers_test.mjs
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const qml = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "qml", "Main.qml"), "utf8")
    .replace(/\r\n/g, "\n");
const ids = new Set([...qml.matchAll(/\bid:\s*([A-Za-z_]\w*)/g)].map((m) => m[1]));

let failures = 0;
for (const prop of ["fullWindowPageOpen", "immersiveSurfaceOpen"]) {
    const m = new RegExp(`readonly property bool ${prop}:([\\s\\S]*?)\\n\\n`).exec(qml);
    if (!m) {
        console.log("FAIL", `${prop} binding not found`);
        failures += 1;
        continue;
    }
    const names = [...new Set([...m[1].matchAll(/\b([A-Za-z_]\w*Layer)\b/g)].map((x) => x[1]))];
    const missing = names.filter((n) => !ids.has(n));
    console.log(missing.length ? "FAIL" : "PASS",
                `${prop}: ${names.length} layers referenced, missing: ${missing.join(", ") || "none"}`);
    failures += missing.length ? 1 : 0;
}
console.log(`ANDROID_SHELL_COVER_LAYERS ${failures ? "FAIL" : "PASS"} failures=${failures}`);
process.exit(failures ? 1 : 0);
