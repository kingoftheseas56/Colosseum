// Deterministic parser, capability mapping and source-order regression.
// Runs the real AddonClient.js with a fake Android capability snapshot and fake addon replies.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const sourcePath = process.argv[2] || 'qml/AddonClient.js';
const source = fs.readFileSync(sourcePath, 'utf8').replace(/^\.pragma library\s*$/m, '');

function makeAddon(platformRuntime, responseForUrl) {
    function FakeXMLHttpRequest() {
        this.readyState = 0;
        this.status = 0;
        this.responseText = '';
    }
    FakeXMLHttpRequest.DONE = 4;
    FakeXMLHttpRequest.prototype.open = function(method, url) { this.url = url; };
    FakeXMLHttpRequest.prototype.send = function() {
        this.readyState = FakeXMLHttpRequest.DONE;
        this.status = 200;
        this.responseText = JSON.stringify(responseForUrl ? responseForUrl(this.url) : { streams: [] });
        this.onreadystatechange();
    };
    FakeXMLHttpRequest.prototype.abort = function() {};

    const mod = {};
    new Function('module', 'XMLHttpRequest', source +
        '\nmodule.parseStream = parseStream; module.loadStreams = loadStreams;')(
            mod, FakeXMLHttpRequest);
    mod.runtime = platformRuntime;
    return mod;
}

function runtime(snapshot, android = true) { return { android, videoDecodeCapabilities: snapshot }; }
function snapshot(decoders, available = true, complete = true) {
    return { available, complete, decoders };
}
function decoder(mime, profiles, profilesComplete = true) {
    return { name: 'fixture-decoder', mime, profiles, profilesComplete };
}
function profile(profileName, level = 0) { return { profile: profileName, level }; }
function parse(addon, label, index = 0, extra = {}) {
    const stream = Object.assign({
        name: label,
        url: `https://media.example/${index}.mkv`
    }, extra);
    return addon.parseStream(stream, 'Fixture', 0, 'fixture-addon', addon.runtime);
}
function state(row) { return row && row.videoCompatibility && row.videoCompatibility.state; }

const hevcOnly = snapshot([decoder('video/hevc', [profile('main', 153)])]);
let addon = makeAddon(runtime(hevcOnly));

let row = parse(addon, '2160p HEVC Main');
assert.equal(row.videoCodec.mime, 'video/hevc');
assert.equal(row.videoCodec.profile, 'main');
assert.equal(row.videoCompatibility.state, 'unknown', 'missing source level is never condemned');

row = parse(addon, '2160p x265 Main10 HDR10+');
assert.equal(row.videoCodec.mime, 'video/hevc');
assert.equal(row.videoCodec.profile, 'main10');
assert.equal(state(row), 'unsupported', 'complete Main-only decoder inventory rejects explicit Main10');
assert.match(row.videoCompatibility.reason, /Main10/i);
assert.ok(row.tags.includes('HDR10+'));

row = parse(addon, '1080p x265 10-bit');
assert.equal(row.videoCodec.profile, 'main10', 'explicit 10-bit HEVC evidence maps to Main10');
assert.equal(state(row), 'unsupported');

// Android MediaCodec may report an exact profile inventory while omitting its
// level values (level 0). The T20-shaped inventory also reports Main Still,
// which is a known profile but is not a Main10 decoder.
addon = makeAddon(runtime(snapshot([decoder('video/hevc', [
    profile('main', 0), profile('main-still', 0)
], true)])));
row = parse(addon, '2160p x265 Main10 HDR10+');
assert.equal(state(row), 'unsupported',
    'complete Main/Main-Still inventory excludes Main10 even when decoder levels are unknown');
row = parse(addon, '1080p HEVC Main');
assert.equal(state(row), 'unknown',
    'matching Main with missing source and decoder levels remains unknown');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [profile('main-still', 0)], true)])));
row = parse(addon, '1080p HEVC Main');
assert.equal(state(row), 'unsupported', 'Main Still alone does not cover HEVC Main');
row = parse(addon, '2160p x265 Main10 HDR10+');
assert.equal(state(row), 'unsupported', 'Main Still alone does not cover HEVC Main10');

addon = makeAddon(runtime(snapshot([decoder('video/av01', [profile('main', 0)], true)])));
row = parse(addon, '2160p AV1 Main10 HDR10');
assert.equal(state(row), 'unsupported',
    'complete level-unknown AV1 Main inventory excludes Main10');
row = parse(addon, '1080p AV1 Main');
assert.equal(state(row), 'unknown',
    'matching AV1 Main with missing source and decoder levels remains unknown');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [
    profile('main', 0), profile('main-still', 0)
], false)])));
row = parse(addon, '2160p x265 Main10 HDR10+');
assert.equal(state(row), 'unknown', 'an explicitly incomplete profile inventory cannot exclude Main10');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [
    profile('main', 0), profile('vendor-future-profile', 0)
], true)])));
row = parse(addon, '2160p x265 Main10 HDR10+');
assert.equal(state(row), 'unknown', 'an unrecognized profile keeps absence conclusions conservative');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [profile('main10', 153)])])));
row = parse(addon, '1080p HEVC Main');
assert.equal(state(row), 'unknown', 'missing source level prevents a positive support claim');
assert.notEqual(state(row), 'unsupported', 'HEVC Main10 decoder covers Main sources');

addon = makeAddon(runtime(snapshot([decoder('video/av01', [profile('main', 8)])])));
row = parse(addon, '2160p AV1 Main 8');
assert.equal(row.videoCodec.mime, 'video/av01');
assert.equal(row.videoCodec.profile, 'main');
assert.equal(state(row), 'unknown', 'profile match without a source level stays uncondemned');
row = parse(addon, '2160p AV1 10-bit HDR10');
assert.equal(row.videoCodec.profile, 'main10');
assert.equal(state(row), 'unsupported', 'complete Main-only AV1 inventory rejects explicit Main10');
row = parse(addon, '2160p AV01 Main 8');
assert.equal(row.videoCodec.mime, 'video/av01', 'AV01 fourcc spelling maps to canonical AV1 MIME');
assert.ok(row.tags.includes('AV1'));

addon = makeAddon(runtime(snapshot([decoder('video/dolby-vision', [])])));
row = parse(addon, '1080p HEVC Dolby Vision');
assert.equal(row.videoCodec.mime, 'video/dolby-vision');
assert.equal(row.videoCodec.profile, '');
assert.equal(state(row), 'supported', 'Dolby Vision is evaluated as a family-only capability');
addon = makeAddon(runtime(snapshot([decoder('video/hevc', [profile('main10', 153)])])));
row = parse(addon, '1080p HEVC Dolby Vision');
assert.equal(state(row), 'unknown', 'HEVC base-layer fallback depends on the Dolby Vision profile');
addon = makeAddon(runtime(snapshot([decoder('video/avc', [profile('high', 5.2)])])));
row = parse(addon, '1080p H.264 Dolby Vision');
assert.equal(state(row), 'unknown', 'AVC base-layer fallback depends on the Dolby Vision profile');
addon = makeAddon(runtime(snapshot([
    decoder('video/hevc', [profile('main10', 153)]),
    decoder('video/avc', [profile('high', 5.2)])
])));
row = parse(addon, '1080p Dolby Vision');
assert.equal(state(row), 'unknown', 'unreported base-layer metadata cannot rule out profile-specific fallback');
addon = makeAddon(runtime(snapshot([])));
row = parse(addon, '1080p Dolby Vision');
assert.equal(state(row), 'unsupported');
row = parse(addon, '1080p HEVC Main');
assert.equal(state(row), 'unsupported', 'explicit family with no decoder is unsupported');

addon = makeAddon(runtime(snapshot([decoder('video/avc', [profile('high', 4.2)])])));
row = parse(addon, '1080p AVC Constrained High @L5.2');
assert.equal(row.videoCodec.mime, 'video/avc');
assert.equal(row.videoCodec.profile, 'high');
assert.equal(row.videoCodec.level, 5.2);
assert.equal(state(row), 'unsupported', 'explicit level above every matching AVC level is rejected');
row = parse(addon, '1080p H.264 Constrained Baseline @L4.1');
assert.equal(row.videoCodec.profile, 'baseline');
assert.equal(state(row), 'supported', 'AVC High decoder includes lower 8-bit profiles at a supported level');
addon = makeAddon(runtime(snapshot([decoder('video/avc', [profile('high422', 5.2), profile('high444', 5.2)])])));
row = parse(addon, '1080p H.264 High 4:2:2 @L5.2');
assert.equal(row.videoCodec.profile, 'high422', 'AVC High 4:2:2 is not confused with High');
assert.equal(row.videoCodec.ambiguous, false);
assert.equal(state(row), 'supported');
row = parse(addon, '1080p H.264 High 4:4:4 @L5.2');
assert.equal(row.videoCodec.profile, 'high444');
assert.equal(state(row), 'supported');

addon = makeAddon(runtime(snapshot([decoder('video/avc', [profile('high', 0)])])));
row = parse(addon, '1080p H.264 High Level 5.2');
assert.equal(state(row), 'unknown', 'zero or unknown decoder level never proves rejection');
row = parse(addon, '1080p H.264 @L6.2');
assert.equal(row.videoCodec.profile, '');
assert.equal(state(row), 'unknown', 'a level without explicit profile cannot establish an unsupported match');

addon = makeAddon(runtime(snapshot([decoder('video/avc', [profile('main', 5.2)], false)])));
row = parse(addon, '1080p H.264 High @L5.2');
assert.equal(state(row), 'unknown', 'incomplete decoder profile map never proves rejection');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [profile('main', 153)])], false, true)));
row = parse(addon, '1080p HEVC Main10');
assert.equal(state(row), 'unknown', 'unavailable probe is unknown');
addon = makeAddon(runtime(snapshot([], true, false)));
row = parse(addon, '1080p HEVC Main10');
assert.equal(state(row), 'unknown', 'partial decoder inventory is unknown');

addon = makeAddon(null);
row = parse(addon, '1080p HEVC Main10');
assert.equal(state(row), 'unknown', 'a missing optional runtime never condemns a source');

addon = makeAddon(runtime(snapshot([]), false));
row = parse(addon, '1080p HEVC Main10');
assert.equal(state(row), 'unknown', 'desktop receives no Android codec policy');

addon = makeAddon(runtime(snapshot([decoder('video/hevc', [profile('main10', 153)])])));
row = parse(addon, '2160p x265');
assert.equal(row.videoCodec.profile, '');
assert.equal(state(row), 'unknown', 'known family without explicit profile never assumes Main10');
row = parse(addon, '1080p x265', 0, { behaviorHints: { filename: 'movie.mkv' } });
assert.equal(row.videoCodec.profile, '', 'filename without profile evidence remains unknown');
row = parse(addon, '1080p', 0, { behaviorHints: { filename: 'movie x265 Main10.mkv' } });
assert.equal(row.videoCodec.profile, 'main10', 'explicit filename profile evidence is considered');
row = parse(addon, '1080p AV1 / H.264 Main10');
assert.equal(row.videoCodec.ambiguous, true);
assert.equal(state(row), 'unknown', 'conflicting codec families are never condemned');
row = parse(addon, '1080p 10-bit Level 5.2');
assert.equal(row.videoCodec.mime, '');
assert.equal(state(row), 'unknown', 'bare bit depth and level do not invent a codec');
assert.ok(row, 'metadata uncertainty retains a routable row');

const sortSnapshot = snapshot([
    decoder('video/hevc', [profile('main', 153)]),
    decoder('video/avc', [profile('high', 5.2)])
]);
const replies = {
    early: { streams: [{ name: '2160p HEVC Main10 HDR10+', infoHash: 'unsupported-high' }] },
    normal: { streams: [
        { name: '1080p H.264 High @L4.1', infoHash: 'supported-normal' },
        { name: '720p Mystery release', infoHash: 'unknown-normal' }
    ] },
    late: { streams: [{ name: '4K Mystery release', infoHash: 'unknown-late' }] }
};
addon = makeAddon(runtime(sortSnapshot), url => {
    if (url.indexOf('early.example') >= 0) return replies.early;
    if (url.indexOf('normal.example') >= 0) return replies.normal;
    return replies.late;
});
const extensions = [
    { id: 'early', transportUrl: 'https://early.example/manifest.json', enabled: true, manifest: { name: 'Early' } },
    { id: 'normal', transportUrl: 'https://normal.example/manifest.json', enabled: true, manifest: { name: 'Normal' } },
    { id: 'late', transportUrl: 'https://late.example/manifest.json', enabled: true, manifest: { name: 'Late' } }
];
let sortedRows = null;
addon.loadStreams(extensions, 'movie', 'tt1234567', () => {}, rows => { sortedRows = rows; }, addon.runtime);
assert.ok(sortedRows, 'loadStreams returns rows from all addon replies');
assert.deepEqual(sortedRows.map(item => item.infoHash), [
    'supported-normal', 'unknown-normal', 'unknown-late', 'unsupported-high'
], 'known unsupported rows sort after supported/unknown rows; others retain legacy priority/quality order');
assert.deepEqual(sortedRows.map(item => state(item)), [
    'supported', 'unknown', 'unknown', 'unsupported'
]);
assert.equal(sortedRows.length, 4, 'compatibility ordering never removes routable source identities');

console.log('ADDON_VIDEO_COMPATIBILITY PASS explicit families/profiles/levels, unknown preservation and unsupported-last ranking');
