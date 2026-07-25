// addon_logos_house_wells_test.mjs — the house wells must resolve to a real bundled
// mark, not silently fall back to the letter square.
//
// Hemanth's note (2026-07-25): "all the icons are just letter, not the official looking
// icons ... every website has their iconography, we just need to make those into the
// addon icon." The letter square in AddonLogo.qml is the FAILURE case; a typo'd filename
// in AddonLogos.js looks identical to "we ship no logo", so nothing at runtime tells you
// the mark is missing. This test closes that gap: every matcher must resolve to a file
// that exists on disk, and both roles of a two-role site must share one mark.
//
// Loads the real qml/AddonLogos.js and checks against the real assets directory.
import fs from 'fs';
import path from 'path';

let src = fs.readFileSync('qml/AddonLogos.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule.logoFor=logoFor;module.TABLE=TABLE;module.DIR=DIR;')(mod);

const ASSETS = 'assets/addon-logos';
let failures = 0;
const ok  = m => console.log("  ok   " + m);
const bad = m => { console.log("  FAIL " + m); failures++; };

// AddonLogos.js paths are relative to qml/AddonLogo.qml — "../assets/addon-logos/x.png".
const onDisk = p => fs.existsSync(path.join(ASSETS, path.basename(p)));

console.log("every matcher in the table points at a file that exists");
const missing = mod.TABLE.filter(e => !fs.existsSync(path.join(ASSETS, e.file)));
if (missing.length) bad("missing assets: " + missing.map(e => e.file).join(", "));
else ok(`all ${mod.TABLE.length} bundled logos present in ${ASSETS}`);

console.log("house wells and catalogues resolve to their site mark");
// [ id, display name, expected file ]
const cases = [
  ["colosseum.well.weebcentral.pages",  "WeebCentral",       "weebcentral.png"],
  ["colosseum.catalogue.weebcentral",   "WeebCentral",       "weebcentral.png"],
  ["colosseum.well.getcomics.issues",   "GetComics",         "getcomics.png"],
  ["colosseum.catalogue.getcomics",     "GetComics",         "getcomics.png"],
  ["colosseum.well.libgen",             "LibGen",            "libgen.ico"],
  ["colosseum.well.audiobookbay",       "AudioBookBay",      "audiobookbay.png"],
  ["colosseum.catalogue.applebooks",    "Apple Books",       "applebooks.ico"],
  ["colosseum.well.nyaa",               "Nyaa",              "nyaa.png"],
  ["piratebay",                         "PirateBay",         "thepiratebay.png"],
  ["knaben",                            "Knaben",            "knaben.ico"],
];
for (const [id, name, want] of cases) {
  const got = mod.logoFor(id, name);
  if (!got) { bad(`${name} (${id}) got the letter square, expected ${want}`); continue; }
  if (path.basename(got) !== want) { bad(`${name} -> ${path.basename(got)}, expected ${want}`); continue; }
  if (!onDisk(got)) { bad(`${name} -> ${want} but that file is not on disk`); continue; }
  ok(`${name.padEnd(13)} -> ${want}`);
}

console.log("a two-role site shares ONE mark across both roles");
const pairs = [["colosseum.catalogue.weebcentral", "colosseum.well.weebcentral.pages", "WeebCentral"],
               ["colosseum.catalogue.getcomics",   "colosseum.well.getcomics.issues",  "GetComics"]];
for (const [a, b, n] of pairs) {
  const la = mod.logoFor(a, n), lb = mod.logoFor(b, n);
  if (la && la === lb) ok(`${n}: catalogue row and well row draw the same mark`);
  else bad(`${n}: roles disagree — ${la || "(letter)"} vs ${lb || "(letter)"}`);
}

console.log("the deliberate letter squares stay letters (never a wrong-site logo)");
// Our own composite is not a website and has no iconography to borrow. ExtTorrents
// publishes only a 73x29 wordmark, Torrents-CSV publishes nothing — a banner squeezed
// into a square plate reads worse than a letter, so the shape gate rejects them.
for (const [id, name] of [["colosseum.well.torrentindexers", "Torrent Indexers"],
                          ["exttorrents", "ExtTorrents"],
                          ["torrentscsv", "Torrents-CSV"]]) {
  const got = mod.logoFor(id, name);
  if (got) bad(`${name} unexpectedly matched ${path.basename(got)} — a wrong-site logo is worse than a letter`);
  else ok(`${name.padEnd(17)} -> letter square, by design`);
}

if (failures) { console.log("\nFAIL — " + failures + " check(s) failed"); process.exit(1); }
console.log("\nPASS — every house well draws its own site mark");
