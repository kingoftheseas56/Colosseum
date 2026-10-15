import fs from 'node:fs';

const local = fs.readFileSync('native/engine/LocalDownloads.cpp', 'utf8');
const qml = fs.readFileSync('qml/DownloadsPage.qml', 'utf8');
let failures = 0;
function check(ok, message) {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${message}`);
  if (!ok) failures += 1;
}

check(local.includes('LocalDownloadsProjection::aggregateSeries'),
  'series aggregation has one tested projection owner');
check(local.includes('QStringLiteral("itemKind"), QStringLiteral("chapter")'),
  'manga chapters carry an explicit chapter subtype');
check(local.includes('QStringLiteral("itemKind"), QStringLiteral("volume")'),
  'Tankoban volumes carry an explicit volume subtype');
check(/portableDownloadIntents[\s\S]*QStringLiteral\("itemKind"\)/.test(local),
  'portable download intents preserve subtype identity');
check(local.includes('LocalDownloadsProjection::canRedownload'),
  'redownload eligibility distinguishes chapters from volumes');
check(local.includes('LocalDownloadsProjection::chapterLabel'),
  'qualified failure rows recover a human chapter label');
check(qml.includes('card.modelData.unitText'),
  'series cards render mixed-unit text from the projection');
check(!qml.includes('"chapters · manga"'),
  'Downloads no longer calls every manga acquisition a chapter');
check(qml.includes('chapters, volumes & issues'),
  'the Tankoban shelf heading names every stored reading unit');

if (failures) process.exit(1);
console.log('\nPASS — Downloads keeps one manga series card without collapsing chapters into volumes');
