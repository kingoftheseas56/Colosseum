// Generic Stremio extension matrix contract.
//
// This test exercises the real qml/AddonClient.js without network access or
// provider credentials. Extension names are fixture labels; no provider branch
// is allowed to exist in the implementation.
import fs from 'fs';

const addonSource = fs.readFileSync('qml/AddonClient.js', 'utf8')
  .replace(/^\.pragma library\s*$/m, '');
const manifests = JSON.parse(fs.readFileSync(
  'tests/fixtures/stremio/extension-manifests.json', 'utf8'));
const responses = JSON.parse(fs.readFileSync(
  'tests/fixtures/stremio/stream-responses.json', 'utf8'));

const mod = {};
new Function('module', addonSource
  + '\nmodule.streamEndpoint = streamEndpoint;'
  + '\nmodule.streamExtensions = streamExtensions;'
  + '\nmodule.loadStreams = loadStreams;')(mod);

let failures = 0;
function check(condition, message) {
  if (condition) console.log('  ok   ' + message);
  else {
    console.log('  FAIL ' + message);
    failures++;
  }
}

function ext(key, transportUrl, enabled = true) {
  return {
    id: manifests[key].id,
    enabled,
    transportUrl,
    manifest: manifests[key]
  };
}

console.log('configured endpoint resolution');
check(mod.streamEndpoint(
  'https://host.example/manifest.json', 'movie', 'tt123'
) === 'https://host.example/stream/movie/tt123.json',
'root manifest resolves to the standard stream endpoint');
check(mod.streamEndpoint(
  'https://host.example/user-state/manifest.json', 'movie', 'tt123'
) === 'https://host.example/user-state/stream/movie/tt123.json',
'configured path is retained');
check(mod.streamEndpoint(
  'https://host.example/user-state/manifest.json?token=fixture', 'movie', 'tt123'
) === 'https://host.example/user-state/stream/movie/tt123.json?token=fixture',
'configured query parameters are retained');
check(mod.streamEndpoint(
  'https://host.example/user-state', 'series', 'tt123:1:2'
) === 'https://host.example/user-state/stream/series/tt123:1:2.json',
'manifest suffix is added before the stream path when needed');
check(mod.streamEndpoint('', 'movie', 'tt123') === '',
'empty transport URL is rejected before a request');
check(mod.streamEndpoint('colosseum://house/manifest.json', 'movie', 'tt123') === '',
'in-app transport URL is rejected by the remote stream resolver');

console.log('enabled stream extension matching');
const installed = [
  ext('torrentio', 'https://torrentio.strem.fun/manifest.json'),
  ext('comet', 'https://comet.elfhosted.com/manifest.json'),
  ext('community', 'https://community.example/manifest.json'),
  ext('catalogueOnly', 'https://catalogue.example/manifest.json'),
  ext('peerflix', 'https://peerflix.example/manifest.json', false)
];
const asked = mod.streamExtensions(installed, 'movie', 'tt123');
check(asked.length === 3,
  'only enabled extensions with a matching stream resource are asked');
check(asked[2].id === manifests.community.id,
  'arbitrary community stream add-ons are included');

console.log('parallel fan-out and result isolation');
const requests = [];
class FakeXHR {
  static DONE = 4;
  open(method, url) {
    this.method = method;
    this.url = url;
  }
  send() {
    requests.push(this.url);
    this.status = responses[this.url] ? 200 : 503;
    this.responseText = JSON.stringify(responses[this.url] || {});
    this.readyState = FakeXHR.DONE;
    this.onreadystatechange();
  }
}
globalThis.XMLHttpRequest = FakeXHR;

let finalRows = [];
let finalNames = [];
mod.loadStreams([
  ext('torrentio', 'https://torrentio.strem.fun/manifest.json'),
  ext('comet', 'https://comet.elfhosted.com/manifest.json'),
  ext('aiostreams', 'https://configured.example/user-state/manifest.json?token=fixture'),
  ext('community', 'https://community.example/manifest.json'),
  ext('malformed', 'https://malformed.example/manifest.json'),
  ext('empty', 'https://empty.example/manifest.json'),
  ext('peerflix', 'https://disabled.example/manifest.json', false)
], 'movie', 'tt123', () => {}, (rows, names) => {
  finalRows = rows;
  finalNames = names;
});

check(requests.length === 6,
  'every enabled stream extension is queried exactly once');
check(requests.includes(
  'https://configured.example/user-state/stream/movie/tt123.json?token=fixture'
), 'configured extension receives its configured stream endpoint');
check(!requests.some(url => url.includes('disabled.example')),
  'disabled extension produces no request');
check(finalNames.length === 6,
  'all asked extension names are reported');
check(finalRows.some(row => row.streamKind === 'Torrent' && row.fileIdx === 2),
  'Torrent rows retain infoHash and fileIdx');
check(finalRows.some(row => row.streamKind === 'Direct'
  && row.headers.Referer === 'https://origin.example/'),
  'Direct rows retain nested origin request headers');
check(finalRows.some(row => row.addonName === 'Community Fixture Streams'),
  'one extension cannot suppress another extension result');
check(!finalRows.some(row => row.release === 'invalid'
  || String(row.url).includes('external.example')),
  'externalUrl-only and malformed rows are excluded');

if (failures) {
  console.error('\n' + failures + ' matrix contract check(s) failed');
  process.exit(1);
}
console.log('\nAll Stremio extension matrix checks passed');
