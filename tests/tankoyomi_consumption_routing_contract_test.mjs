import fs from 'node:fs';

const engine = fs.readFileSync('native/MangaEngine.h', 'utf8');
const downloaderH = fs.readFileSync('native/engine/MangaDownloader.h', 'utf8');
const downloader = fs.readFileSync('native/engine/MangaDownloader.cpp', 'utf8');
const result = fs.readFileSync('native/engine/MangaResult.h', 'utf8');
let failures = 0;
function check(ok, message) {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures += 1;
}

check(engine.includes('TankoyomiIdentity'), 'MangaEngine knows the qualified chapter identity seam');
check(/pages\([^)]*chapterId[^)]*\)[\s\S]{0,500}isQualifiedChapter/.test(engine),
  'reader pages distinguish qualified Tankoyomi ids');
check(engine.includes('m_tankoyomi->fetchPages'), 'qualified reader pages route through Tankoyomi');
check(engine.includes('m_wc->fetchPages(chapterId)'), 'legacy raw reader pages retain WeebCentral path');
check(downloaderH.includes('TankoyomiChapterService'), 'downloader owns the Tankoyomi page resolver seam');
check(downloader.includes('TankoyomiIdentity::isQualifiedChapter'),
  'downloader distinguishes qualified ids from legacy raw ids');
check(downloader.includes('m_tankoyomi->fetchPages'),
  'qualified downloader page discovery routes through Tankoyomi');
check(downloader.includes('WeebCentralScraper'),
  'legacy raw downloader path remains available');
check(/fetchThumb[\s\S]{0,5000}TankoyomiIdentity::isQualifiedChapter/.test(downloader),
  'qualified chapter thumbnails resolve through the provider router');
check(result.includes('QString referer;'), 'page model can carry provider-specific image referer');
check(downloader.includes('job->pages[pageIndex].referer'),
  'page image requests use provider-specific referer metadata');
check(downloader.includes('QCryptographicHash::hash(chapterId.toUtf8()'),
  'filesystem identity still hashes the full qualified chapter id');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi reader/download consumption routing contract');
