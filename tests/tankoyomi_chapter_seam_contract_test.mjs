import fs from 'node:fs';

const enginePath = 'native/MangaEngine.h';
const serviceH = 'native/engine/TankoyomiChapterService.h';
const serviceCpp = 'native/engine/TankoyomiChapterService.cpp';
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(fs.existsSync(serviceH), 'Tankoyomi chapter service header exists');
check(fs.existsSync(serviceCpp), 'Tankoyomi chapter service implementation exists');
const engine = fs.readFileSync(enginePath, 'utf8');
const h = fs.existsSync(serviceH) ? fs.readFileSync(serviceH, 'utf8') : '';
const cpp = fs.existsSync(serviceCpp) ? fs.readFileSync(serviceCpp, 'utf8') : '';

check(engine.includes('#include "engine/TankoyomiChapterService.h"'),
  'MangaEngine depends on Tankoyomi chapter service');
check(engine.includes('TankoyomiChapterService *m_tankoyomi'),
  'MangaEngine owns one Tankoyomi chapter service');
check(engine.includes('m_tankoyomi->fetchCatalogue(requestId, title, QStringLiteral("en"))'),
  'legacy two-argument Chapter Mode routes through Tankoyomi English');
check(engine.includes('chapterCatalogueForLanguage('),
  'MangaEngine exposes a language-aware Chapter Mode entry point');
check(engine.includes('m_tankoyomi->fetchCatalogue(requestId, title, language)'),
  'language-aware Chapter Mode delegates to Tankoyomi');

const methodStart = engine.indexOf('Q_INVOKABLE void chapterCatalogue(');
const methodEnd = engine.indexOf('// Reader:', methodStart);
const chapterBlock = methodStart >= 0 && methodEnd > methodStart
  ? engine.slice(methodStart, methodEnd) : '';
check(!chapterBlock.includes('new WeebCentralScraper'),
  'MangaEngine no longer constructs WeebCentral inside chapterCatalogue');

check(h.includes('void fetchCatalogue(const QString &requestId, const QString &title, const QString &language);'),
  'Tankoyomi service accepts an explicit language');
check(cpp.includes('TankoyomiProviderRegistry::fromResource'),
  'Tankoyomi loads every chapter source through the manifest registry');
check(!cpp.includes('WeebCentralScraper'),
  'English no longer needs provider-specific C++ inside Tankoyomi');
check(cpp.includes('row.insert(QStringLiteral("source"), descriptor.id)'),
  'all chapter rows retain manifest provider provenance');
check(cpp.includes('row.insert(QStringLiteral("language"), language)'),
  'all chapter rows retain their selected language');

if (failures) process.exit(1);
console.log('\nPASS — Chapter Mode reaches WeebCentral through Tankoyomi');
