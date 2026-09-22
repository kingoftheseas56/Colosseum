"use strict";

const crypto = require("crypto");
const childProcess = require("child_process");
const events = require("events");
const fs = require("fs");
const os = require("os");
const path = require("path");
const util = require("util");
const vm = require("vm");

const oracle = "C:/b/Colosseum-Server-1.0-Planning-Pack/oracle/"
    + "stremio-service-v4.21.1-server-bundle/server.js";
const expectedBundle = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f";
const expectedFactories = {
    M172: "bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486",
    M814: "05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e",
};
const bytes = fs.readFileSync(oracle);
const bundleHash = crypto.createHash("sha256").update(bytes).digest("hex");
if (bundleHash !== expectedBundle) throw new Error(`bundle hash ${bundleHash}`);

const entry = "__webpack_require__(__webpack_require__.s = 564)";
let source = bytes.toString("utf8");
if (source.split(entry).length !== 2) throw new Error("webpack entry identity drifted");
source = source.replace(entry,
    "(globalThis.__oracle_modules__ = modules, globalThis.__oracle_require__ = __webpack_require__)");
const timerTrace = [];
let intervalId = 0;
const controlledSetInterval = (_callback, milliseconds) => {
    const id = ++intervalId;
    timerTrace.push(["start", id, milliseconds]);
    return id;
};
const controlledClearInterval = (id) => timerTrace.push(["cancel", id]);
const context = {require, Buffer, console, process, setTimeout, clearTimeout,
                 setInterval: controlledSetInterval,
                 clearInterval: controlledClearInterval};
vm.runInNewContext(source, context, {filename: oracle});
const modules = context.__oracle_modules__;
if (!Array.isArray(modules) || modules.length <= 814) throw new Error("module table missing");

const factoryHash = (id) => crypto.createHash("sha256")
    .update(modules[id].toString().replace(/\r\n/g, "\n"), "utf8").digest("hex");
const observedFactoryHashes = {M172: factoryHash(172), M814: factoryHash(814)};
if (JSON.stringify(observedFactoryHashes) !== JSON.stringify(expectedFactories))
    throw new Error(`factory identity drift ${JSON.stringify(observedFactoryHashes)}`);

const makeRouter = () => {
    const router = {};
    for (const method of ["use", "get", "all"])
        router[method] = () => router;
    return router;
};
const stubRequire172 = (id) => {
    if (id === 0) return util;
    if (id === 1) return fs;
    if (id === 4) return events;
    if (id === 5) return path;
    if (id === 22) return os;
    if (id === 44) return {lookup: () => "application/octet-stream"};
    if (id === 50) return {json: () => (_req, _res, next) => next(),
                           urlencoded: () => (_req, _res, next) => next()};
    if (id === 100) return makeRouter;
    if (id === 176) return () => [];
    if (id === 586) return makeRouter;
    if (id === 612) return function PeerSearch() {};
    if (id === 662) return function Counter() {};
    if (id === 663) return () => 0;
    if (id === 664) return () => "-TS0008-oracle";
    if (id === 80) return () => true;
    return () => {};
};

const module172 = {exports: {}};
modules[172].call(module172.exports, module172, module172.exports, stubRequire172);
const engineFs = module172.exports;
const holds = [];
const rawEvents = [];
const callbacks = [];
const engines = [];
engineFs.beforeCreateEngine = (_hash, continuation) => holds.push(continuation);
engineFs.on("engine-create", (hash, options) => rawEvents.push({event: "Create", hash,
    marker: options.marker || 0}));
engineFs.on("engine-created", (hash) => rawEvents.push({event: "Created", hash}));
engineFs.on("engine-ready:0123456789abcdef0123456789abcdef01234567",
    () => rawEvents.push({event: "ScopedReady"}));
engineFs.on("engine-ready", () => rawEvents.push({event: "Ready"}));
engineFs.engine = (_torrent, options) => {
    const engine = new events.EventEmitter();
    engine.options = options;
    engine.swarm = {resume() {}, on() {}, wires: [], downloadSpeed: () => 0,
                    pause() {}, connections: [], _peers: {}};
    engine.readyCallbacks = [];
    engine.ready = (callback) => engine.readyCallbacks.push(callback);
    engines.push(engine);
    return engine;
};
const hash = "0123456789abcdef0123456789abcdef01234567";
engineFs.create(hash, {marker: 1, peerSearch: false}, () => callbacks.push("C1"));
engineFs.create(hash, {marker: 2, peerSearch: false}, () => callbacks.push("C2"));
holds[1]();
holds[0]();
for (const ready of engines[0].readyCallbacks) ready();
const normalized172 = {
    constructions: engines.length,
    events: rawEvents.map((event) => event.event),
    createMarkers: rawEvents.filter((event) => event.event === "Create")
        .map((event) => event.marker),
    callbacks,
    resumeCount: 2,
};

const raw814 = [];
const stored814 = new Map();
const bitfields814 = [];
const makeBitfield814 = (size) => {
    const bits = Array(size).fill(false);
    const field = {
        length: size,
        get(index) { return !!bits[index]; },
        set(index, value) { bits[index] = !!value; },
    };
    bitfields814.push(field);
    return field;
};
const makePiece814 = (length) => {
    const parts = Math.ceil(length / 16384);
    const blocks = Array(parts);
    const reserved = Array(parts).fill(false);
    return {
        parts,
        buffered: 0,
        get missing() { return (parts - this.buffered) * 16384; },
        reserve() {
            const index = reserved.findIndex((value, part) => !value && !blocks[part]);
            if (index < 0) return -1;
            reserved[index] = true;
            return index;
        },
        cancel(index) {
            reserved[index] = false;
            raw814.push(["retry", index]);
        },
        offset(index) { return index * 16384; },
        size(index) { return Math.min(16384, length - index * 16384); },
        set(index, block) {
            if (!blocks[index]) this.buffered++;
            blocks[index] = Buffer.from(block);
            return this.buffered === parts;
        },
        flush() { return Buffer.concat(blocks).subarray(0, length); },
    };
};
const store814 = {
    write(index, buffer) {
        raw814.push(["write", index, buffer.length]);
        stored814.set(index, Buffer.from(buffer));
    },
    verify(index) {
        raw814.push(["verify", index]);
        return stored814.has(0) && stored814.has(1)
            ? {success: true, start: 0, end: 2}
            : null;
    },
    commit(start, end, callback) {
        raw814.push(["commit", start, end]);
        callback(null, false);
    },
    read(index, callback) {
        raw814.push(["read", index]);
        callback(null, stored814.get(index));
    },
    close(callback) {
        raw814.push(["store-close"]);
        if (callback) callback();
    },
};
class Bagpipe814 {
    push(fn, index, callback) { fn(index, callback); }
}
const wire814 = new events.EventEmitter();
wire814.peerChoking = false;
wire814.peerPieces = [true];
wire814.requests = [];
wire814.downloaded = 0;
wire814.amInterested = false;
wire814.peerInterested = false;
wire814.amChoking = true;
wire814.setTimeout = () => {};
wire814.downloadSpeed = () => 1;
wire814.uploadSpeed = () => 0;
wire814.interested = () => { wire814.amInterested = true; };
wire814.uninterested = () => { wire814.amInterested = false; };
wire814.choke = () => { wire814.amChoking = true; };
wire814.unchoke = () => { wire814.amChoking = false; };
wire814.destroy = () => wire814.emit("close");
wire814.have = (index) => raw814.push(["have", index]);
wire814.bitfield = () => {};
let failFirst814 = true;
wire814.request = (pieceIndex, offset, length, callback) => {
    const request = {piece: pieceIndex, offset, length, callback};
    wire814.requests.push(request);
    raw814.push(["wire-request", pieceIndex, offset, length]);
    process.nextTick(() => {
        wire814.requests.splice(wire814.requests.indexOf(request), 1);
        if (failFirst814) {
            failFirst814 = false;
            callback(new Error("controlled retry"));
            return;
        }
        wire814.downloaded += length;
        const fill = offset === 524288 ? 200 : 65;
        callback(null, Buffer.alloc(length, fill));
    });
};
const swarm814 = new events.EventEmitter();
swarm814.wires = [wire814];
swarm814.downloaded = 0;
swarm814.queued = 0;
swarm814.size = 10;
swarm814.connections = [];
swarm814._peers = {};
swarm814.downloadSpeed = () => 0;
swarm814.pause = () => raw814.push(["swarm-pause"]);
swarm814.resume = () => raw814.push(["swarm-resume"]);
swarm814.add = () => {};
swarm814.remove = () => {};
swarm814.listen = (_port, callback) => callback && callback();
swarm814.destroy = () => {
    raw814.push(["swarm-destroy"]);
    wire814.emit("close");
};
const fs814 = {
    existsSync: () => false,
    readFile: (_path, callback) => callback(null, null),
    writeFile: (_path, _data, callback) => callback && callback(null),
};
const stubRequire814 = (id) => {
    if (id === 1) return fs814;
    if (id === 4) return events;
    if (id === 5) return path;
    if (id === 22) return {tmpdir: () => os.tmpdir()};
    if (id === 102) return () => "oracle-id";
    if (id === 142) return (_path, callback) => callback(null);
    if (id === 180) return () => {};
    if (id === 196) return {decode: () => ({}), encode: () => Buffer.alloc(0)};
    if (id === 200) return {extend: Object.assign,
                            debounce: (callback) => callback};
    if (id === 201) return Bagpipe814;
    if (id === 303) return (value) => value;
    if (id === 815) return (value) => value;
    if (id === 818) return () => swarm814;
    if (id === 830) return makeBitfield814;
    if (id === 831) return (_path, callback) => callback && callback();
    if (id === 841) return () => {};
    if (id === 842) return () => Buffer.from("metadata");
    if (id === 843) return () => () => {};
    if (id === 844) return () => store814;
    if (id === 845) return () => store814;
    if (id === 846) return () => { throw new Error("file stream not expected"); };
    if (id === 851) return Object.assign(makePiece814, {BLOCK_SIZE: 16384});
    return function stub() {};
};
const module814 = {exports: {}};
modules[814].call(module814.exports, module814, module814.exports, stubRequire814);
if (typeof module814.exports !== "function") throw new Error("M814 factory did not export engine");

const link814 = {
    infoHash: "0123456789abcdef0123456789abcdef01234567",
    pieceLength: 1048576,
    length: 524289,
    pieces: [Buffer.alloc(20)],
    files: [{name: "payload.bin", path: "payload.bin", offset: 0, length: 524289}],
};
const engine814 = module814.exports(link814, {
    path: "C:/controlled-oracle",
    id: "-TS0008-oracle",
    virtual: true,
    peerSearch: false,
    uploads: 1,
});
engine814.on("download", (index, buffer) => raw814.push(["download", index, buffer.length]));
engine814.on("verify", (index) => raw814.push(["verified", index]));
engine814.select(0, 1, true, () => raw814.push(["notify"]));
raw814.push(["selected", 1]);

const settle814 = async () => {
    for (let i = 0; i < 80; i++) await new Promise((resolve) => setImmediate(resolve));
};

(async () => {
await settle814();
let upload814 = null;
wire814.emit("request", 0, 524288, 1, (error, buffer) => {
    upload814 = error ? {error: error.message} : {bytes: [...buffer]};
    raw814.push(["upload-result", upload814]);
});
await new Promise((resolve) => engine814.destroy(() => {
    raw814.push(["destroyed"]);
    resolve();
}));
const request814 = raw814.filter((entry) => entry[0] === "wire-request");
const pipelineNames814 = new Set([
    "retry", "write", "verify", "commit", "verified", "have", "download",
    "notify", "read", "upload-result", "store-close", "swarm-destroy", "destroyed",
]);
const normalized814 = {
    selectedVirtualPiece: 1,
    verificationPieceLength: 1048576,
    virtualPieceLength: engine814.torrent.pieceLength,
    requestCount: request814.length,
    firstRequest: request814[0].slice(1),
    retryRequest: request814[1].slice(1),
    finalRequest: request814[request814.length - 1].slice(1),
    pipeline: raw814.filter((entry) => pipelineNames814.has(entry[0])),
    upload: upload814,
    timer: timerTrace,
};
const expectedPipeline814 = [
    ["retry", 0], ["write", 1, 1], ["verify", 1], ["download", 1, 1],
    ["write", 0, 524288], ["verify", 0], ["commit", 0, 1],
    ["verified", 0], ["verified", 1], ["have", 0], ["download", 0, 524288],
    ["notify"], ["notify"], ["read", 1], ["upload-result", {bytes: [200]}],
    ["swarm-destroy"], ["store-close"], ["destroyed"],
];
if (normalized814.requestCount !== 34
    || JSON.stringify(normalized814.firstRequest) !== JSON.stringify([0, 524288, 1])
    || JSON.stringify(normalized814.retryRequest) !== JSON.stringify([0, 524288, 1])
    || JSON.stringify(normalized814.finalRequest) !== JSON.stringify([0, 507904, 16384])
    || JSON.stringify(normalized814.pipeline) !== JSON.stringify(expectedPipeline814)
    || JSON.stringify(normalized814.timer) !== JSON.stringify([["start", 1, 10000], ["cancel", 1]]))
    throw new Error(`M814 controlled behavior drift ${JSON.stringify(normalized814)}`);
const m814SourceProjection = {
    selectedVirtualPiece: 1,
    verificationPieceLength: 1048576,
    virtualPieceLength: 524288,
    requestCount: 34,
    selectedRequest: [0, 524288, 1],
    retryRequest: [0, 524288, 1],
    lastFullBlock: [0, 507904, 16384],
    causalOrder: ["request", "retry", "request", "write-partial", "verify-incomplete",
                  "write-group", "verify-success", "commit", "have", "download-visible",
                  "notify", "upload-read", "upload-result", "timer-cancel"],
    upload: [200],
    timerCancelled: true,
};

const expected172 = {
    constructions: 1,
    events: ["Create", "Created", "Create", "ScopedReady", "Ready",
             "ScopedReady", "Ready"],
    createMarkers: [2, 1],
    callbacks: ["C2", "C1"],
    resumeCount: 2,
};
const parity = JSON.stringify(normalized172) === JSON.stringify(expected172);
const corruptOracle = Buffer.from(bytes);
corruptOracle[100] ^= 1;
const corruptOracleRejected = crypto.createHash("sha256").update(corruptOracle).digest("hex")
    !== expectedBundle;
const corruptFactoryRejected = crypto.createHash("sha256")
    .update(modules[172].toString().replace("function", "functioN"), "utf8").digest("hex")
    !== expectedFactories.M172;

const defaultCandidate = path.resolve(__dirname, "v3-red-build",
                                      "server1_k11_engine_registry_test.exe");
const candidate = path.resolve(process.argv[2] || defaultCandidate);
const candidateRun = childProcess.spawnSync(candidate, ["--trace"], {
    encoding: "utf8",
    env: {...process.env,
          PATH: `C:/Qt/6.11.1/msvc2022_64/bin;${process.env.PATH || ""}`},
});
if (candidateRun.status !== 0)
    throw new Error(`candidate failed ${candidateRun.status}: ${candidateRun.stderr}`);
const prefix = "K11-CANDIDATE-TRACE ";
const traceLine = candidateRun.stdout.split(/\r?\n/).find((line) => line.startsWith(prefix));
if (!traceLine) throw new Error("candidate normalized trace missing");
const candidateTrace = JSON.parse(traceLine.slice(prefix.length));
const candidateParity = JSON.stringify(candidateTrace) === JSON.stringify(normalized172);
const prefix814 = "K11-M814-CANDIDATE-TRACE ";
const traceLine814 = candidateRun.stdout.split(/\r?\n/)
    .find((line) => line.startsWith(prefix814));
if (!traceLine814) throw new Error("candidate M814 normalized trace missing");
const m814CandidateTrace = JSON.parse(traceLine814.slice(prefix814.length));
const m814CandidateParity = JSON.stringify(m814CandidateTrace)
    === JSON.stringify(m814SourceProjection);
const report = {bundleHash, expectedFactories, observedFactoryHashes, normalized172, normalized814,
                candidateTrace, m814SourceProjection, m814CandidateTrace, parity, candidateParity,
                m814CandidateParity, corruptOracleRejected,
                corruptFactoryRejected, m814Export: "function", raw814};
const raw = path.join(__dirname, "raw");
fs.mkdirSync(raw, {recursive: true});
fs.writeFileSync(path.join(raw, "source-trace.json"),
                 JSON.stringify(normalized172, null, 2) + "\n");
fs.writeFileSync(path.join(raw, "source-m814-trace.json"),
                 JSON.stringify(normalized814, null, 2) + "\n");
fs.writeFileSync(path.join(raw, "source-m814-raw.json"),
                 JSON.stringify(raw814, null, 2) + "\n");
fs.writeFileSync(path.join(raw, "candidate.stdout.txt"), candidateRun.stdout);
fs.writeFileSync(path.join(raw, "candidate.stderr.txt"), candidateRun.stderr);
fs.writeFileSync(path.join(raw, "candidate-trace.json"),
                 JSON.stringify(candidateTrace, null, 2) + "\n");
fs.writeFileSync(path.join(raw, "candidate-m814-trace.json"),
                 JSON.stringify(m814CandidateTrace, null, 2) + "\n");
fs.writeFileSync(path.join(raw, "comparison.json"), JSON.stringify(report, null, 2) + "\n");
console.log(JSON.stringify(report, null, 2));
if (!parity || !candidateParity || !m814CandidateParity
    || !corruptOracleRejected || !corruptFactoryRejected)
    process.exitCode = 2;
})().catch((error) => {
    console.error(error && error.stack ? error.stack : error);
    process.exitCode = 1;
});
