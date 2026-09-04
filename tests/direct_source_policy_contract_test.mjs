import fs from 'fs';

const helperPath = 'qml/DirectSourcePolicy.js';
const playerPath = 'qml/PlayerPage.qml';
let failures = 0;
function check(cond, msg) {
  if (cond) console.log('  ok   ' + msg);
  else { console.log('  FAIL ' + msg); failures++; }
}

console.log('provider Direct source admission');
check(fs.existsSync(helperPath), 'shared Direct source policy helper exists');
let policy = {};
if (fs.existsSync(helperPath)) {
  const src = fs.readFileSync(helperPath, 'utf8').replace(/^\.pragma library\s*$/m, '');
  new Function('module', src + '\nmodule.admitProviderUrl=admitProviderUrl; module.copyHeaders=copyHeaders;')(policy);
}

const accepted = [
  'https://media.example/video.mkv',
  'HTTPS://cdn.example:8443/live.m3u8',
  'https://8.8.8.8/video.mp4',
  'https://[2606:4700:4700::1111]/video.mp4'
];
const rejected = [
  'http://media.example/video.mkv',
  '//media.example/video.mkv',
  'media.example/video.mkv',
  'https://localhost/video.mkv',
  'https://localhost./video.mkv',
  'https://user@127.0.0.1/video.mkv',
  'https://2130706433/video.mkv',
  'https://0177.0.0.1/video.mkv',
  'https://[::ffff:127.0.0.1]/video.mkv',
  'https://player.localhost/video.mkv',
  'https://127.0.0.1/video.mkv',
  'https://10.0.0.8/video.mkv',
  'https://172.31.4.5/video.mkv',
  'https://192.168.1.20/video.mkv',
  'https://169.254.1.2/video.mkv',
  'https://0.0.0.0/video.mkv',
  'https://nas.local/video.mkv',
  'https://[::1]/video.mkv',
  'https://[::]/video.mkv',
  'https://[fc00::1]/video.mkv',
  'https://[fd12::1]/video.mkv',
  'https://[fe80::1]/video.mkv',
  'file:///tmp/video.mkv',
  'content://media/external/video/1',
  'ftp://media.example/video.mkv',
  'data:video/mp4;base64,AAAA',
  'javascript:alert(1)',
  'rtmp://media.example/live',
  'udp://239.1.1.1:1234',
  'https:///missing-host'
];
if (typeof policy.admitProviderUrl === 'function') {
  for (const url of accepted)
    check(policy.admitProviderUrl(url) === url, 'accept unchanged: ' + url);
  for (const url of rejected)
    check(policy.admitProviderUrl(url) === '', 'reject: ' + url);
} else {
  check(false, 'admitProviderUrl is executable');
}

console.log('generation-scoped header copying');
if (typeof policy.copyHeaders === 'function') {
  const original = { Referer: 'https://origin.example/', Cookie: 'sid=1' };
  const copied = policy.copyHeaders(original);
  original.Referer = 'https://mutated.example/';
  check(copied.Referer === 'https://origin.example/', 'active headers do not alias provider objects');
  check(copied.Cookie === 'sid=1', 'header values are preserved');
  check(Object.keys(policy.copyHeaders({})).length === 0, 'empty replacement stays empty');
  check(Object.keys(policy.copyHeaders(null)).length === 0, 'missing headers become empty');
} else {
  check(false, 'copyHeaders is executable');
}

const player = fs.readFileSync(playerPath, 'utf8');
check(player.includes('import "DirectSourcePolicy.js" as DirectSourcePolicy'),
      'PlayerPage imports the shared admission helper');
check(/property\s+var\s+activeDirectHeaders\s*:\s*\(\{\}\)/.test(player),
      'PlayerPage owns generation-scoped Direct headers');
check(/function\s+loadDirectStreamUrl[\s\S]*?DirectSourcePolicy\.admitProviderUrl/.test(player),
      'last-mile Direct loader enforces provider admission');
check(/function\s+loadDirectStreamUrl[\s\S]*?activeDirectHeaders\s*=\s*requestHeaders[\s\S]*?mpv\.loadSource\(directUrl,\s*requestHeaders\)/.test(player),
      'Direct replacement overwrites header state and sends an explicit empty map when needed');
check(/function\s+reloadActiveDirectSource[\s\S]*?activeDirectHeaders/.test(player),
      'same-source reconnect reloads the active header generation');
check(/function\s+tickWakeReconnect[\s\S]*?reloadActiveDirectSource\(\)/.test(player),
      'wake reconnect reuses the active Direct header generation');
check(/function\s+handlePlaybackFailure[\s\S]*?streamRetryCount\s*<\s*1[\s\S]*?reloadActiveDirectSource\(\)/.test(player),
      'Arriving no-candidate failure retry reuses active Direct headers');
check(/function\s+retryCurrentStream[\s\S]*?loadDirectStreamUrl\(directUrl,\s*c\.headers\)/.test(player),
      'ordinary candidate retry remains header-aware');
check(/function\s+normalizeStreamCandidates[\s\S]*?DirectSourcePolicy\.admitProviderUrl/.test(player),
      'candidate admission removes rejected Direct rows before playback');
check(/function\s+playLocalFile[\s\S]*?clearActiveDirectHeaders\(\)/.test(player),
      'local/content replacement clears active Direct headers');
check(/function\s+playLocalFile[\s\S]*?arrivingStreamHeaders\s*=\s*arrivingUrl\.length\s*\?\s*DirectSourcePolicy\.copyHeaders/.test(player),
      'disk-first Arriving keeps an immutable header copy for frontier handoff');
check(/function\s+stop\(\)[\s\S]*?clearActiveDirectHeaders\(\)/.test(player),
      'stop clears active Direct headers');

if (failures) {
  console.log('\nFAIL - ' + failures + ' check(s) failed');
  process.exit(1);
}
console.log('\nPASS - provider source admission and Direct header-state contract holds');
