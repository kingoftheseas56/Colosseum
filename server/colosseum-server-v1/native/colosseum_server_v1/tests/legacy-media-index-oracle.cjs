'use strict';
// Execute only the pinned legacy factories and their dependencies, never the
// bundle's global entry point. All generated media and HTTP traffic are local.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const http = require('node:http');
const { execFileSync, execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { extractModules } = require('../../../tools/server_lab/oracle/module_loader.cjs');
const [bundle, candidate, output] = process.argv.slice(2);
if (!output) throw Error('usage: node legacy-media-index-oracle.cjs SERVER_JS CANDIDATE OUTPUT_DIR');
const extracted = extractModules(bundle);
if (extracted.sha256 !== '405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f') throw Error('oracle hash mismatch');
const cache = new Map();
const fileReads = [];
let activeSource;
const observedFs = { ...fs, createReadStream(file, options) {
    if (file === activeSource) fileReads.push(options?.start || 0);
    return fs.createReadStream(file, options);
} };
const context = vm.createContext({ require, Buffer, process, console, setTimeout, clearTimeout,
    setInterval, clearInterval, setImmediate, clearImmediate });
context.global = context;
function req(id) {
    if (typeof id !== 'number') return require(id);
    if (id === 1) return observedFs;
    if (cache.has(id)) return cache.get(id).exports;
    const module = { exports: {} }; cache.set(id, module);
    vm.runInContext('(' + extracted.modules[id].source + ')', context)(module, module.exports, req);
    return module.exports;
}
req.o = (o, p) => Object.prototype.hasOwnProperty.call(o, p);
req.d = (o, n, get) => { if (!req.o(o, n)) Object.defineProperty(o, n, { enumerable: true, get }); };
req.r = o => Object.defineProperty(o, '__esModule', { value: true });
req.n = m => { const get = m && m.__esModule ? () => m.default : () => m; req.d(get, 'a', get); return get; };
const concat = (...values) => Buffer.concat(values);
function number(value, width) {
    value = BigInt(value); const bytes = Buffer.alloc(width);
    for (let i = width - 1; i >= 0; --i) { bytes[i] = Number(value & 255n); value >>= 8n; }
    return bytes;
}
function vint(value) {
    for (let width = 1; width <= 8; ++width)
        if (BigInt(value) < (1n << BigInt(7 * width)) - 1n)
            return number((1n << BigInt(7 * width)) | BigInt(value), width);
    throw Error('vint overflow');
}
const tag = (id, payload) => concat(Buffer.from(id, 'hex'), vint(payload.length), payload);
function uint(id, value) {
    let width = 1; while (BigInt(value) >= 1n << BigInt(8 * width)) ++width;
    return tag(id, number(value, width));
}
const track = (num, type) => tag('ae', concat(uint('d7', num), uint('83', type)));
const cue = (num, time) => tag('bb', concat(uint('b3', time), tag('b7', concat(uint('f7', num), uint('f1', 0)))));
function mkv(entries, points, { cues = true, info = false, cuesFirst = false } = {}) {
    const parts = [];
    if (info) parts.push(['1549a966', tag('1549a966', uint('2ad7b1', 2000000))]);
    const tracks = ['1654ae6b', tag('1654ae6b', entries)];
    const cuePart = ['1c53bb6b', tag('1c53bb6b', points)];
    if (cues && cuesFirst) parts.push(cuePart);
    parts.push(tracks);
    if (cues && !cuesFirst) parts.push(cuePart);
    function head(start) {
        return tag('114d9b74', concat(...parts.map(([id, bytes]) => {
            const seek = tag('4dbb', concat(tag('53ab', Buffer.from(id, 'hex')), tag('53ac', number(start, 8))));
            start += bytes.length; return seek;
        })));
    }
    const ebml = tag('1a45dfa3', concat(uint('4286', 1), uint('42f7', 1), uint('42f2', 4),
        uint('42f3', 8), tag('4282', Buffer.from('matroska')), uint('4287', 4), uint('4285', 2)));
    return concat(ebml, tag('18538067', concat(head(head(0).length), ...parts.map(p => p[1]))));
}
fs.mkdirSync(output, { recursive: true });
const fixtureDir = path.join(output, 'fixtures'); fs.mkdirSync(fixtureDir, { recursive: true });
const cases = [];
function fixture(name, mode, bytes) {
    const file = path.join(fixtureDir, name + '.' + mode); fs.writeFileSync(file, bytes);
    cases.push({ name, mode, file });
}
const video = track(7, 1), points = concat(cue(7, 2000), cue(7, 0), cue(7, 1000));
fixture('mkv-no-info', 'mkv', mkv(video, points));
fixture('mkv-nondefault-scale', 'mkv', mkv(video, points, { info: true }));
fixture('mkv-cues-first', 'mkv', mkv(video, points, { cuesFirst: true }));
fixture('mkv-no-video', 'mkv', mkv(track(1, 2), cue(1, 100)));
fixture('mkv-no-cues', 'mkv', mkv(video, Buffer.alloc(0), { cues: false }));
fixture('mkv-empty-cues', 'mkv', mkv(video, Buffer.alloc(0)));
fixture('mkv-void-only-cues', 'mkv', mkv(video, tag('ec', Buffer.from('x'))));
// Non-CuePoint children still distinguish a populated Cues element from empty.
fixture('mkv-no-cuepoints', 'mkv', mkv(video, tag('bf', Buffer.alloc(4))));
fixture('mkv-no-matching-cues', 'mkv', mkv(video, cue(8, 100)));
const entries = concat(...Array.from({ length: 11 }, (_, i) => track(i + 1, i >= 9 ? 1 : 2)));
fixture('mkv-last-video-9-vs-10', 'mkv', mkv(entries, concat(cue(10, 90), cue(11, 100))));
const realMp4 = path.join(fixtureDir, 'mp4-tail.mp4');
execFileSync(process.env.FFMPEG || 'ffmpeg', ['-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
    'testsrc2=size=32x32:rate=5', '-t', '2', '-c:v', 'libx264', '-g', '5', '-bf', '0', '-y', realMp4]);
cases.push({ name: 'mp4-tail', mode: 'mp4', file: realMp4 });
const fastMp4 = path.join(fixtureDir, 'mp4-fast.mp4');
execFileSync(process.env.FFMPEG || 'ffmpeg', ['-hide_banner', '-loglevel', 'error', '-i', realMp4,
    '-c', 'copy', '-movflags', '+faststart', '-y', fastMp4]);
cases.push({ name: 'mp4-fast', mode: 'mp4', file: fastMp4 });
const fastBytes = fs.readFileSync(fastMp4);
let moov, ftyp;
for (let offset = 0; offset + 8 <= fastBytes.length;) {
    const size = fastBytes.readUInt32BE(offset);
    const type = fastBytes.toString('ascii', offset + 4, offset + 8);
    if (type === 'ftyp') ftyp = fastBytes.subarray(offset, offset + size);
    if (type === 'moov') moov = fastBytes.subarray(offset, offset + size);
    offset += size;
}
fixture('mp4-extended-moov', 'mp4', concat(ftyp, number(1, 4), Buffer.from('moov'), number(moov.length + 8, 8), moov.subarray(8)));
fixture('mp4-large-mdat', 'mp4', concat(ftyp, number(100008, 4), Buffer.from('mdat'), Buffer.alloc(100000), moov));
fixture('mp4-post-mdat-free', 'mp4', concat(ftyp, number(108, 4), Buffer.from('mdat'), Buffer.alloc(100),
    number(70008, 4), Buffer.from('free'), Buffer.alloc(70000), moov));
const records = [];
const byName = new Map(cases.map(c => [c.name, fs.readFileSync(c.file)]));
const server = http.createServer((request, response) => {
    const [, label, name] = request.url.split('/');
    const bytes = byName.get(name);
    if (!bytes) { response.writeHead(404); response.end(); return; }
    const offset = Number((request.headers.range || 'bytes=0-').match(/bytes=(\d+)-/)[1]);
    records.push({ label, name, offset });
    const body = bytes.subarray(offset);
    response.writeHead(206, { 'Content-Length': body.length, 'Content-Range': `bytes ${offset}-${bytes.length - 1}/${bytes.length}` });
    response.flushHeaders();
    let position = 0;
    // Force both 8- and 16-byte atom/EBML headers to cross data events.
    const timer = setInterval(() => {
        if (response.destroyed) { clearInterval(timer); return; }
        if (position < Math.min(20, body.length)) response.write(body.subarray(position, ++position));
        else { clearInterval(timer); response.end(body.subarray(position)); }
    }, 2);
    response.on('close', () => clearInterval(timer));
});
function oracle(mode, source) {
    activeSource = source;
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(Error('oracle timed out: ' + source)), 5000);
        req(mode === 'mp4' ? 716 : 678)(source, (error, frames) => {
            clearTimeout(timer);
            resolve(error ? { available: false, error: error.message } : { available: true,
                frames: Array.from(frames, frame => ({ pts: frame.pts, dts: frame.dts })) });
        });
    });
}
async function native(mode, source) {
    const { stdout } = await promisify(execFile)(candidate, ['--probe', mode, source], { timeout: 6000 });
    const result = JSON.parse(stdout);
    return { result: result.available ? { available: true,
        frames: Object.values(result.tracks).flatMap(track => track.samples.map(frame => ({ pts: frame.pts, dts: frame.dts }))) }
        : { available: false, error: result.error }, stats: result };
}
(async () => {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = 'http://127.0.0.1:' + server.address().port;
    const rows = [];
    try {
        for (const c of cases) for (const transport of ['local-sync', 'local-async', 'fragmented-http']) {
            const remote = transport === 'fragmented-http';
            const readStart = fileReads.length;
            const expected = await oracle(c.mode, remote ? `${base}/oracle/${c.name}` : c.file);
            const actual = await native((transport === 'local-sync' ? '' : 'async-') + c.mode,
                remote ? `${base}/candidate/${c.name}` : c.file);
            const sourceRanges = remote ? records.filter(r => r.name === c.name && r.label === 'oracle').map(r => r.offset)
                : fileReads.slice(readStart);
            const candidateRanges = remote ? records.filter(r => r.name === c.name && r.label === 'candidate').map(r => r.offset)
                : actual.stats.sourceOffsets;
            const passed = JSON.stringify(expected) === JSON.stringify(actual.result)
                && (transport === 'local-sync' || JSON.stringify(sourceRanges) === JSON.stringify(candidateRanges));
            rows.push({ name: c.name, transport, passed, oracle: expected, candidate: actual,
                ...(transport !== 'local-sync' ? { sourceRanges, candidateRanges } : {}) });
            console.log(`${passed ? 'PASS' : 'FAIL'} ${c.name} ${transport}`);
        }
    } finally {
        server.closeAllConnections(); server.close();
        fs.writeFileSync(path.join(output, 'comparison.json'), JSON.stringify({ oracle: extracted.sha256, rows }, null, 2));
        fs.writeFileSync(path.join(output, 'requests.json'), JSON.stringify(records, null, 2));
    }
    console.log(`${rows.filter(r => r.passed).length}/${rows.length} pinned legacy factory comparisons passed`);
    process.exitCode = rows.every(r => r.passed) ? 0 : 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
