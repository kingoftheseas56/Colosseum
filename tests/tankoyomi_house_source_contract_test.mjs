import fs from 'node:fs';

const store = fs.readFileSync('native/engine/ExtensionsStore.cpp', 'utf8');
const catalog = fs.readFileSync('qml/ExtensionsCatalog.js', 'utf8');
const packageManifest = JSON.parse(fs.readFileSync('extensions/tankoyomi/manifest.json', 'utf8'));
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(store.includes('constexpr int kHouseDefaultsVersion = 13;'),
  'house defaults version bumps for Tankoyomi migration');
check(store.includes('add("colosseum.well.tankoyomi", "colosseum://well/tankoyomi"'),
  'Tankoyomi is seeded as the Tankoban chapter well');
check(store.includes('manifest("colosseum.well.tankoyomi", "Tankoyomi"'),
  'Tankoyomi owns the visible source row');
const tankSeedAt = store.indexOf('add("colosseum.well.tankoyomi"');
const tankSeedEnd = tankSeedAt >= 0 ? store.indexOf('\n    add(', tankSeedAt + 5) : -1;
const tankSeed = tankSeedAt >= 0 ? store.slice(tankSeedAt, tankSeedEnd >= 0 ? tankSeedEnd : tankSeedAt + 700) : '';
check(/manifest\("colosseum\.well\.tankoyomi", "Tankoyomi",\s*"[^"]+"/s.test(tankSeed),
  'Tankoyomi source row has real extension copy');
check(tankSeed.includes('{}, true'),
  'Tankoyomi advertises the in-app configuration surface');
check(packageManifest.behaviorHints?.configurable === true,
  'Tankoyomi package manifest advertises the in-app configuration surface');
check(!store.includes('add("colosseum.well.weebcentral.pages"'),
  'WeebCentral is no longer a standalone seeded source');
check(store.includes('"colosseum.well.weebcentral.pages"'),
  'legacy WeebCentral id remains referenced for migration/retirement');
check(catalog.includes('"colosseum.well.tankoyomi":'),
  'Tankoyomi has a house job label');
check(!catalog.includes('"colosseum.well.weebcentral.pages": "chapter pages"'),
  'WeebCentral no longer owns the chapter-pages job');

check(store.includes('legacyWeebCentralAt'),
  'migration detects an existing standalone WeebCentral row');
check(store.includes('m_items[legacyWeebCentralAt].insert(QStringLiteral("id"), QStringLiteral("colosseum.well.tankoyomi"))'),
  'migration renames WeebCentral in place to preserve enabled state and ordering');
if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi owns the Tankoban chapter-source row');
