import fs from 'node:fs';

const transportH = 'native/engine/MangaPageTransport.h';
const transportCpp = 'native/engine/MangaPageTransport.cpp';
const downloaderPath = 'native/engine/MangaDownloader.cpp';
const harnessPath = 'tests/tankoyomi_service_runtime_harness.cpp';
const downloader = fs.readFileSync(downloaderPath, 'utf8');
const harness = fs.readFileSync(harnessPath, 'utf8');
const thumbStart = downloader.indexOf('void MangaDownloader::fetchThumbImage');
const thumbEnd = downloader.indexOf('// ---------------------------------------------------------------------------', thumbStart);
const thumbBody = thumbStart >= 0 && thumbEnd > thumbStart ? downloader.slice(thumbStart, thumbEnd) : '';
let failures = 0;

function check(ok, message) {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures += 1;
}

check(fs.existsSync(transportH) && fs.existsSync(transportCpp),
  'one shared native page-transport contract exists');
check(downloader.includes('MangaPageTransport::normalizeTankoyomiPages'),
  'downloads normalize qualified page transport from the embedded chapter identity');
check(downloader.includes('fetchThumbImage'),
  'qualified chapter thumbnails are fetched by native transport');
check(downloader.includes('QUrl::fromLocalFile'),
  'native thumbnail transport publishes a local image URL to QML');
check(thumbBody.includes('m_hostResolver.resolve') && thumbBody.includes('m_pins.insert'),
  'native thumbnail transport uses the async IPv4 resolver before its image GET');
check(!/settle\(parsed\.isEmpty\(\) \? QString\(\) : parsed\.first\(\)\.imageUrl, true\)/.test(downloader),
  'qualified thumbnails are never handed to QML as naked remote URLs');
check(harness.includes('QNetworkRequest') && harness.includes('Referer'),
  'live Tankoyomi smoke consumes pages with provider request metadata');
check(harness.includes('image/') || harness.includes('magic'),
  'live Tankoyomi smoke validates image bytes rather than URL shape only');

check(downloader.includes('pageAccessPolicyForChapter') && downloader.includes('TankoyomiNetworkPolicy::resolvedAddressAllowed'),
  'qualified image requests enforce provider inventory policy and public DNS addresses');
check(downloader.includes('requestUrl.resolved(location)') && downloader.includes('redirectDepth >= 5'),
  'image redirect chains use logical URLs and a finite hop budget');
if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi page discovery is consumed through native image transport');
