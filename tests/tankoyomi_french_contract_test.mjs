import fs from 'node:fs';

const root = 'extensions/tankoyomi';
const manifest = JSON.parse(fs.readFileSync(`${root}/manifest.json`, 'utf8'));
const expected = [
  ['sushiscan-fr', 'languages/fr/sushiscan-fr.js'],
  ['lelscan-vf', 'languages/fr/lelscan-vf.js'],
  ['lelmanga', 'languages/fr/lelmanga.js'],
  ['mangamoins', 'languages/fr/mangamoins.js']
];
let failures = 0;
const check = (ok, msg) => { console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`); if (!ok) failures++; };
const fr = (manifest.languages || []).find(x => x.code === 'fr') || {};

check(fr.label === 'Français', 'French language is installed with the correct label');
check((fr.providers || []).map(x => x.id).join(',') === expected.map(x => x[0]).join(','),
  'French priority is SushiScan -> Lelscan-VF -> Lelmanga -> MangaMoins');
check(!(fr.providers || []).some(x => x.id === 'mangahub-fr'), 'failed MangaHub provider is not shipped');
for (const [id, entry] of expected) {
  const row = (fr.providers || []).find(x => x.id === id);
  check(!!row && row.entry === entry, `${id} is declared in the manifest`);
  check(!!row && Array.isArray(row.allowedHosts) && row.allowedHosts.length > 0, `${id} has a host allowlist`);
  check(fs.existsSync(`${root}/${entry}`), `${id} embedded provider exists`);
  if (!fs.existsSync(`${root}/${entry}`)) continue;
  const src = fs.readFileSync(`${root}/${entry}`, 'utf8');
  check(src.includes('searchSeries') && src.includes('getChapters') && src.includes('getPages'), `${id} exposes the provider contract`);
  check(!/^\s*(?:import|export)\s/m.test(src), `${id} has no ESM syntax`);
  check(!src.includes('async function') && !src.includes(' await '), `${id} avoids async/await`);
  check(!src.includes('fetch('), `${id} only uses the scoped Tankoyomi network context`);
}
if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi French package is embedded, scoped, and MangaHub-free');