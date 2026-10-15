// extensions_catalog_test.mjs — the community-browse row mapping (_rowFrom/_mapList).
// Root cause of the "addons won't install (404)" bug (2026-07-24): the stremio-addons.net
// v0 API returns transportUrl=null, `url` = the human DIRECTORY page (…/addons/<slug>),
// and the actual installable manifest in `manifestUrl`. Installing `url` 404s because
// ExtensionsStore.normalizeUrl appends /manifest.json to a directory page. The install
// URL must therefore prefer manifestUrl over url. Loads the real .pragma library file.
import fs from 'fs';
let src = fs.readFileSync('qml/ExtensionsCatalog.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src + '\nmodule._mapList=_mapList;module._rowFrom=_rowFrom;')(mod);

function fail(m) { console.log("FAIL " + m); process.exit(1); }

// The real stremio-addons.net v0 entry shape (confirmed live 2026-07-24).
const apiEntry = {
  manifest: { name: "One Pace Addon", id: "com.onepace.fedew",
              description: "One Pace", resources: ["catalog", "meta", "stream"] },
  transportUrl: null,
  url: "https://stremio-addons.net/addons/one-pace-addon",
  manifestUrl: "https://fedew04.github.io/OnePaceStremio/manifest.json"
};
let row = mod._rowFrom(apiEntry, 0);
if (!row) fail("row should map");
if (row.url !== "https://fedew04.github.io/OnePaceStremio/manifest.json")
  fail("install url must be the real manifestUrl, got: " + row.url);

// transportUrl, when the registry does provide it, still wins (Stremio official shape).
let withTransport = mod._rowFrom({
  manifest: { name: "X", id: "x" },
  transportUrl: "https://x.example/manifest.json",
  manifestUrl: "https://y.example/manifest.json",
  url: "https://dir.example/addons/x"
}, 0);
if (withTransport.url !== "https://x.example/manifest.json")
  fail("transportUrl must win, got: " + withTransport.url);

// Only `url` present (no transport/manifest) → last-resort fallback must still work.
let onlyUrl = mod._rowFrom({ manifest: { name: "Z", id: "z" }, url: "https://z.example/manifest.json" }, 0);
if (onlyUrl.url !== "https://z.example/manifest.json")
  fail("url fallback when nothing else, got: " + onlyUrl.url);

// End-to-end through _mapList with the real API envelope { addons:[...] }.
let rows = mod._mapList({ addons: [apiEntry], pagination: {} });
if (!rows || rows.length !== 1) fail("_mapList should return 1 row, got " + (rows && rows.length));
if (rows[0].url !== "https://fedew04.github.io/OnePaceStremio/manifest.json")
  fail("_mapList row url must be manifestUrl, got: " + rows[0].url);

// ---- adult entries follow the global showExplicit preference (2026-08-15) ----
// Was an unconditional wall; it now mirrors ContentPreferences.showExplicit like every
// other surface. Off must still filter (the old contract); on must pass them through.
const adultEntry = {
  manifest: { name: "Adult Addon", id: "com.adult.x" },
  manifestUrl: "https://adult.example/manifest.json",
  nsfw: true
};

let offRows = mod._mapList({ addons: [apiEntry, adultEntry] });          // omitted = off
if (offRows.length !== 1) fail("showExplicit off must filter adult, got " + offRows.length);
if (offRows[0].name === "Adult Addon") fail("adult entry leaked with preference off");

let offExplicitFalse = mod._mapList({ addons: [apiEntry, adultEntry] }, false);
if (offExplicitFalse.length !== 1) fail("explicit=false must filter adult, got " + offExplicitFalse.length);

let onRows = mod._mapList({ addons: [apiEntry, adultEntry] }, true);
if (onRows.length !== 2) fail("showExplicit on must pass adult through, got " + onRows.length);
if (!onRows.some(r => r.url === "https://adult.example/manifest.json"))
  fail("adult entry must be installable when preference is on");

// A category-tagged adult entry follows the same rule (not just the nsfw flag).
const catAdult = { manifest: { name: "Cat Adult", id: "c" }, manifestUrl: "https://c.example/manifest.json",
                   categories: ["Adult"] };
if (mod._mapList({ addons: [catAdult] }).length !== 0) fail("category adult must filter when off");
if (mod._mapList({ addons: [catAdult] }, true).length !== 1) fail("category adult must pass when on");

// Null entries are dropped in BOTH modes — _rowFrom dereferences entry.manifest, so a
// null surviving the open path would throw rather than render.
if (mod._mapList({ addons: [null, apiEntry] }, true).length !== 1) fail("null entry must be dropped when on");
if (mod._mapList({ addons: [null, apiEntry] }, false).length !== 1) fail("null entry must be dropped when off");

console.log("extensions_catalog_test: ALL PASS");
