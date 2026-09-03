import fs from 'node:fs';

const runtimePath = 'native/engine/TankoyomiScriptProvider.cpp';
const mangaNightPath = 'extensions/tankoyomi/languages/pt/manga-night.js';
const runtime = fs.readFileSync(runtimePath, 'utf8');
const mangaNight = fs.readFileSync(mangaNightPath, 'utf8');
let failures = 0;
const check = (ok, msg) => {
  console.log(`${ok ? '  ok  ' : '  FAIL'} ${msg}`);
  if (!ok) failures++;
};

check(runtime.includes('const int defaultTimeoutMs = 15000'),
  'provider requests keep the 15s default');
check(runtime.includes('qBound(1000, requestedTimeoutMs, 45000)'),
  'provider-requested timeouts are bounded to 45s');
check(runtime.includes('options.value(QStringLiteral("timeoutMs"))'),
  'runtime reads timeout only from scoped request options');
check(mangaNight.includes('{ timeoutMs: 45000 }'),
  'Manga Night explicitly opts its heavy HTML requests into the longer budget');

if (failures) process.exit(1);
console.log('\nPASS — Tankoyomi request timeout override stays bounded and provider-scoped');
