// addon_torrentio_honesty_test.mjs — the store's Remove/off-switch must actually
// silence Torrentio.
//
// The bug (found 2026-07-25): Torrentio ships SEEDED BUT REMOVABLE (core:false), yet
// several paths reached torrentio.strem.fun by hardcoded URL through Torrentio.js.
// Two of them (Main.qml resolveDownloadJob, TheatreWorld.qml playNextUp) never
// consulted the store at all; the player's three asked the store and then called
// Torrentio anyway as an unconditional floor. Net effect: removing or disabling
// Torrentio did NOT stop the app calling it, and any well ranked ABOVE it was ignored
// by the download and Next-Up paths.
//
// Every one of those fallbacks now asks AddonClient.torrentioEnabled() first. This
// pins that predicate, plus the ask-order/enabled contract the ladder depends on.
// Loads the real qml/AddonClient.js.
import fs from 'fs';

let src = fs.readFileSync('qml/AddonClient.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src +
  '\nmodule.torrentioEnabled=torrentioEnabled;' +
  '\nmodule.streamExtensions=streamExtensions;' +
  '\nmodule.TORRENTIO_ID=TORRENTIO_ID;')(mod);

let failures = 0;
function check(cond, msg) {
  if (cond) { console.log("  ok   " + msg); }
  else { console.log("  FAIL " + msg); failures++; }
}

const TID = mod.TORRENTIO_ID;
check(TID === "com.stremio.torrentio.addon",
      "torrentio id matches the seed in ExtensionsStore.cpp");

// --- the shapes ExtensionsStore.installed() actually hands QML ---
const torrentioManifest = {
  id: TID, name: "Torrentio", resources: ["stream"],
  types: ["movie", "series", "anime"], idPrefixes: ["tt", "kitsu"]
};
const otherWell = {
  id: "com.example.otherwell", name: "Other Well", resources: ["stream"],
  types: ["movie", "series"], idPrefixes: ["tt"]
};
const cinemeta = {
  id: "com.linvo.cinemeta", name: "Cinemeta", resources: ["catalog", "meta"],
  types: ["movie", "series"], idPrefixes: ["tt"]
};
const row = (manifest, enabled, core) =>
  ({ id: manifest.id, transportUrl: "https://x/manifest.json",
     enabled: enabled, core: !!core, manifest: manifest });

console.log("torrentioEnabled — the predicate every hardcoded fallback now asks");

check(mod.torrentioEnabled([row(torrentioManifest, true)]) === true,
      "installed and enabled -> true (normal seeded case, no behaviour change)");
check(mod.torrentioEnabled([row(torrentioManifest, false)]) === false,
      "installed but switched OFF -> false (the off-switch stops being cosmetic)");
check(mod.torrentioEnabled([]) === false,
      "removed entirely -> false (Remove stops being cosmetic)");
check(mod.torrentioEnabled([row(cinemeta, true, true), row(otherWell, true)]) === false,
      "other extensions installed, no Torrentio -> false");
check(mod.torrentioEnabled(null) === false,
      "null list -> false, never throws (harnesses run without an Extensions context)");
check(mod.torrentioEnabled(undefined) === false,
      "undefined list -> false, never throws");
check(mod.torrentioEnabled([{ id: TID }]) === false,
      "entry with no enabled field -> false (must be strictly enabled===true)");
check(mod.torrentioEnabled([null, row(torrentioManifest, true)]) === true,
      "a null entry in the list does not hide a real one");

console.log("streamExtensions — the ask-order contract the ladder relies on");

// A well ranked ABOVE Torrentio must come first: this is the half of the bug where
// the download and Next-Up paths ignored every other well outright.
const ordered = [row(otherWell, true), row(torrentioManifest, true)];
let asked = mod.streamExtensions(ordered, "movie", "tt0111161");
check(asked.length === 2, "both stream wells answer a movie ask");
check(asked[0].id === otherWell.id,
      "array order IS ask-order — the well above Torrentio is asked first");

// Disabled wells are never asked. A fallback that ignored this is exactly the bug.
let withOff = mod.streamExtensions([row(otherWell, false), row(torrentioManifest, true)],
                                   "movie", "tt0111161");
check(withOff.length === 1 && withOff[0].id === TID,
      "a disabled well is not asked at all");

// Catalogue-only extensions are not stream wells and must not be counted as one —
// otherwise a world with only a catalogue installed would look like it can fetch.
let catalogueOnly = mod.streamExtensions([row(cinemeta, true, true)], "movie", "tt0111161");
check(catalogueOnly.length === 0,
      "a catalogue-only extension is never mistaken for a stream well");

if (failures) { console.log("\nFAIL — " + failures + " check(s) failed"); process.exit(1); }
console.log("\nPASS — Torrentio honesty contract holds");
