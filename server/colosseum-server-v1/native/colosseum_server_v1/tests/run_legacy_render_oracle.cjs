'use strict';

// Execute only the pinned legacy middleware factories with controlled process,
// preparation and response boundaries. Never boot the bundled server.
const fs = require('node:fs');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');
const { extractModules } = require('../../../tools/server_lab/oracle/module_loader.cjs');
const oracle = extractModules(process.argv[2]);
const expectedHash = '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f';
if (oracle.sha256 !== expectedHash) throw new Error('Pinned legacy oracle hash mismatch');

let instance, capture;
const cache = new Map();
const subtitles = {};
const paths = { ffmpeg: 'ffmpeg', ffsplit: 'ffsplit' };
const prepareQueue = { push(task, callback) { callback(null, instance); } };
const convertQueue = { push(task) { task(() => {}); } };
function load(id) {
    if (cache.has(id)) return cache.get(id).exports;
    const overrides = {
        6: require('node:url'),
        32: { spawn(program, args) { capture.program = program; capture.args = args; return {}; } },
        35: fn => { let called = false; return (...args) => { if (!called) { called = true; return fn(...args); } }; },
        180: () => {},
        305: { probeVideo(source, callback) { callback(null, instance); } },
    };
    if (Object.hasOwn(overrides, id)) return overrides[id];
    const module = { exports: {} };
    cache.set(id, module);
    vm.runInNewContext('(' + oracle.modules[id].source + ')', {
        process: { env: {} }, console: { error() {}, log() {} }, Buffer,
        setTimeout: callback => callback(),
    }, { filename: `pinned-server.js#${id}` })(module, module.exports, load);
    return module.exports;
}
const common = load(81);
common.pipeProcToResp = () => {};
load(668).init(prepareQueue, subtitles);
load(673).init(prepareQueue);
load(674).init(prepareQueue, subtitles, { emit() {} });
load(675).init(paths, prepareQueue, convertQueue);
load(729).init(paths, convertQueue);
const middleware = {
    master: [668, 'masterPlaylistMiddleware'],
    multi: [668, 'masterMultiPlaylistMiddleware'],
    playlist: [673, 'streamPlaylistMiddleware'],
    segment: [675, 'segmentMiddleware'],
    transcode: [675, 'segmentTranscodeMiddleware'],
    thumb: [729, 'thumbMiddleware'],
};
const base = {
    duration: 12500, bitrate: 123456, container: 'mp4',
    streams: [
        { codec_type: 'video', codec_name: 'h264', stream: 0, size: [1280, 720], fps: 25 },
        { codec_type: 'audio', codec_name: 'aac', stream: 1, lang: 'eng', default: true },
    ],
    qualities: [320, 480, 720],
    segmentsUniform: [{ at: 0, duration: 6000 }, { at: 6000, duration: 6000 }, { at: 12000, duration: 500 }],
    segmentsByFrame: [{ at: 0, duration: 2000 }, { at: 2000, duration: 10500 }],
};
base.videoStream = base.streams[0];
const rows = [];
function record(name, action, params = {}, query = {}, options = {}) {
    instance = structuredClone(base);
    if (options.lang !== undefined) instance.streams[1].lang = options.lang;
    if (options.noLanguage) delete instance.streams[1].lang;
    if (options.noFrames) delete instance.segmentsByFrame;
    if (options.codec) instance.streams[0].codec_name = options.codec;
    paths.ffsplit = options.hasSplitter === false ? null : 'ffsplit';
    capture = { status: 200, headers: {}, body: '' };
    const request = {
        url: '/file/movie/master.m3u8?x=1', search: '?x=1',
        headers: { host: 'localhost:11470' },
        params: { from: 'fixture.mp4', ...params }, query,
    };
    const input = structuredClone(request);
    const response = {
        setHeader(key, value) { capture.headers[key.toLowerCase()] = value; },
        writeHead(status) { capture.status = status; },
        write(value) { capture.body += value; },
        end(value) { if (value) capture.body += value; },
        set statusCode(value) { capture.status = value; },
    };
    const [id, method] = middleware[action];
    load(id)[method](request, response, () => {});
    if (capture.args) capture.args = capture.args.map(String);
    rows.push({ name, action, instance, input, hasSplitter: !!paths.ffsplit, expected: capture });
}

for (const stream of ['0', '1', '01', '+1', ' 1 ', '1.0', '1e0', '-0', '-1', '2', '4294967295', '', null, 0, 1, 1.5]) {
    for (const action of ['segment', 'transcode']) {
        record(`stream-key ${JSON.stringify(stream)} ${action}`, action,
            { stream, quality: action === 'segment' ? 'o' : '480', seg: '0' });
    }
}
for (const quality of ['0x140', '0X1e0', '0b10', '0B10', '0o10', '0O10', '0x', '-0x140', '+0x140', '0x1p2', 'inf', 'Infinity', '+Infinity', '-Infinity', '1e309', '-1e309', '1e-999', ' 720 ', '720tail', '', null, 0, false, true]) {
    record(`quality-number ${JSON.stringify(quality)}`, 'segment', { quality, seg: '0' });
    record(`thumb-number ${JSON.stringify(quality)}`, 'thumb', {}, { at: quality });
}
for (const lang of Object.keys(load(669).languageNames).concat(['fra', 'unknown'])) {
    record(`language ${lang}`, 'multi', {}, {}, { lang });
}
record('language absent', 'multi', {}, {}, { noLanguage: true });
for (const hasSplitter of [true, false]) {
    for (const adaptiveBitrate of [false, true]) {
        for (const noFrames of [false, true]) {
            for (const codec of ['h264', 'vp9']) {
                record(`master splitter=${hasSplitter} adaptive=${adaptiveBitrate} frames=${!noFrames} codec=${codec}`,
                    'master', { adaptiveBitrate }, {}, { hasSplitter, noFrames, codec });
            }
        }
    }
    record(`multi splitter=${hasSplitter}`, 'multi', {}, {}, { hasSplitter });
}
fs.writeFileSync(process.argv[3], JSON.stringify(rows, null, 2) + '\n');
console.log(JSON.stringify({ oracle: oracle.sha256, cases: rows.length, output: process.argv[3] }));
// Optional native executable makes regeneration plus differential comparison a
// single test invocation; it never launches the original server entry point.
if (process.argv[4]) {
    const result = spawnSync(process.argv[4], [process.argv[3]], { stdio: 'inherit' });
    if (result.error) throw result.error;
    process.exitCode = result.status === 0 ? 0 : 1;
}
