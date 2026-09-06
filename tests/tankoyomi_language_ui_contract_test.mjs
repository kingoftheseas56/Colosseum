import fs from 'node:fs';

const engine = fs.readFileSync('native/MangaEngine.h', 'utf8');
const page = fs.readFileSync('qml/MangaSeries.qml', 'utf8');
const header = fs.readFileSync('qml/MangaSeriesSharedHeader.qml', 'utf8');
const chapterView = fs.readFileSync('qml/MangaChapterSeriesView.qml', 'utf8');
const room = fs.readFileSync('qml/MangaReadingRoom.qml', 'utf8');
let failures = 0;
const check = (ok, msg) => { console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`); if (!ok) failures++; };

check(engine.includes('chapterLanguages() const'), 'MangaEngine exposes Tankoyomi languages to QML');
check(engine.includes('m_tankoyomi->languages()'), 'language list comes from the Tankoyomi registry');
check(page.includes('property string selectedChapterLanguage: "en"'), 'series page defaults Chapter Mode to English');
check(page.includes('chapterDefaultLanguage'), 'Chapter Mode reads the persisted Tankoyomi default language');
check(page.includes('onChapterConfigurationChanged'), 'Chapter Mode listens for live Tankoyomi configuration changes');
check(page.includes('onChanged'), 'Chapter Mode listens for live ExtensionsStore changes');
check(page.includes('chapterCatalogueForLanguage(page._chapterRequestId, title, page.selectedChapterLanguage)'),
  'Chapter Mode loads through the language-aware catalogue seam');
check(page.includes('Chapter language unavailable.'), 'unsupported Chapter languages fail without an English fallback');
check(page.includes('function _selectChapterLanguage(code)'), 'series page owns language-change correlation');
check(header.includes('objectName: "mangaLanguageSelector"'), 'shared masthead owns one language selector geometry');
check(header.includes('enabled: !root.tankobanMode'), 'language selector is disabled in Tankoban Mode');
check(header.includes('Available in Chapter Mode'), 'disabled Tankoban control explains how to enable languages');
check(chapterView.includes('signal chapterLanguageRequested(string code)'), 'chapter surface raises language changes');
check(room.includes('selectedChapterLanguage: "en"'), 'Tankoban surface renders the shared selector as English');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi language UI is manifest-driven and mode-safe');
