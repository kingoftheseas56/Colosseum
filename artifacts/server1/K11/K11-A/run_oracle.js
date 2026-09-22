"use strict";

// K11-A source/native differential.
//
// Both sides are observed, never asserted: the real M172 and M814 webpack
// factories run against controlled stubs and record raw events; the native
// candidate runs its K11 scenarios with --trace and records raw events from the
// running registry. One set of normalization functions turns each raw log into
// a projection, and the projections are compared field by field. The only
// accepted difference is the disclosed source precommit visibility, carried as
// an explicit divergence record on the source side.

const crypto = require("crypto");
const childProcess = require("child_process");
const events = require("events");
const fs = require("fs");
const os = require("os");
const path = require("path");
const util = require("util");
const vm = require("vm");

const oracle = process.env.SERVER1_ORACLE_BUNDLE
    || "C:/b/Colosseum-Server-1.0-Planning-Pack/oracle/stremio-service-v4.21.1-server-bundle/server.js";
const expectedBundle = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f";
const expectedFactories = {
    M172: "bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486",
    M814: "05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e",
};
const DISCLOSED_SOURCE_DIVERGENCES = [{kind: "source-precommit-visible", piece: 1}];

const bytes = fs.readFileSync(oracle);
const bundleHash = crypto.createHash("sha256").update(bytes).digest("hex");
if (bundleHash !== expectedBundle) throw new Error(`bundle hash ${bundleHash}`);

const entry = "__webpack_require__(__webpack_require__.s = 564)";
let source = bytes.toString("utf8");
if (source.split(entry).length !== 2) throw new Error("webpack entry identity drifted");
source = source.replace(entry,
    "(globalThis.__oracle_modules__ = modules, globalThis.__oracle_require__ = __webpack_require__)");

// Controlled interval: records start/cancel and delivers a tick only while the
// interval is active, so post-cancel delivery attempts are observable.
const timerTrace = [];
const intervals = new Map();
let intervalId = 0;
const controlledSetInterval = (callback, milliseconds) => {
    const id = ++intervalId;
    intervals.set(id, {callback, active: true});
    timerTrace.push(["start", id, milliseconds]);
    return id;
};
const controlledClearInterval = (id) => {
    const interval = intervals.get(id);
    if (interval) interval.active = false;
    timerTrace.push(["cancel", id]);
};
const deliverInterval = (id) => {
    const interval = intervals.get(id);
    if (!interval || !interval.active) {
        timerTrace.push(["tick-refused", id]);
        return;
    }
    timerTrace.push(["tick", id]);
    interval.callback();
};
const context = {require, Buffer, console, process, setTimeout, clearTimeout,
                 setInterval: controlledSetInterval,
                 clearInterval: controlledClearInterval};
vm.runInNewContext(source, context, {filename: "server.js"});
const modules = context.__oracle_modules__;
if (!Array.isArray(modules) || modules.length <= 814) throw new Error("module table missing");

const factoryHash = (id) => crypto.createHash("sha256")
    .update(modules[id].toString().replace(/\r\n/g, "\n"), "utf8").digest("hex");
const observedFactoryHashes = {M172: factoryHash(172), M814: factoryHash(814)};
if (JSON.stringify(observedFactoryHashes) !== JSON.stringify(expectedFactories))
    throw new Error(`factory identity drift ${JSON.stringify(observedFactoryHashes)}`);

const settle = async (turns = 80) => {
    for (let i = 0; i < turns; i++) await new Promise((resolve) => setImmediate(resolve));
};
const cacheRoot = os.tmpdir();
const scrub = (value) => JSON.parse(JSON.stringify(value).split(JSON.stringify(cacheRoot).slice(1, -1))
    .join("<cache-root>"));

// ---- M172: EngineFS create/reuse/remove over the real factory ----
async function runSource172() {
    const makeRouter = () => {
        const router = {};
        for (const method of ["use", "get", "all"]) router[method] = () => router;
        return router;
    };
    let peerId = 0;
    const peerSearches = [];
    const stubRequire = (id) => {
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
        if (id === 612) return function PeerSearch(sources, _swarm, options) {
            peerSearches.push({sources, options: JSON.parse(JSON.stringify(options))});
        };
        if (id === 662) return function Counter() {};
        if (id === 663) return () => 0;
        if (id === 664) return () => `-TS0008-oracle-${++peerId}`;
        if (id === 80) return () => true;
        return () => {};
    };
    const module172 = {exports: {}};
    modules[172].call(module172.exports, module172, module172.exports, stubRequire);
    const engineFs = module172.exports;
    const hash = "0123456789abcdef0123456789abcdef01234567";
    const stream = "magnet:?xt=urn:btih:" + hash.toUpperCase();
    const holds = [];
    const timeline = [];
    const createOptions = [];
    const ids = [];
    const engines = [];
    engineFs.beforeCreateEngine = (_hash, continuation) => holds.push(continuation);
    engineFs.on("engine-create", (_hash, options) => {
        timeline.push(["Create", options.marker || 0]);
        createOptions.push(JSON.parse(JSON.stringify(options)));
    });
    engineFs.on("engine-created", () => timeline.push(["Created"]));
    engineFs.on("engine-ready:" + hash, () => timeline.push(["ScopedReady"]));
    engineFs.on("engine-ready", () => timeline.push(["Ready"]));
    engineFs.on("engine-destroyed", () => timeline.push(["Destroyed"]));
    engineFs.engine = (_torrent, options) => {
        const engine = new events.EventEmitter();
        engine.options = options;
        engine.resumeCount = 0;
        engine.swarm = {resume() { engine.resumeCount++; }, on() {}, wires: [],
                        downloadSpeed: () => 0, pause() {}, connections: [], _peers: {}};
        engine.torrent = null;
        engine.ready = (callback) => engine.torrent ? process.nextTick(callback)
                                                    : engine.once("ready", callback);
        engine.destroy = (callback) => process.nextTick(callback);
        engines.push(engine);
        return engine;
    };
    const callback = (name) => () => timeline.push(["callback", name]);
    engineFs.create(hash, {stream, marker: 1, path: false,
                           peerSearch: {sources: ["dht:custom"]}}, callback("C1"));
    engineFs.create(hash, {marker: 2}, callback("C2"));
    holds[1]();
    ids.push(engines[0].options.id);
    holds[0]();
    ids.push(engines[0].options.id);
    engines[0].torrent = {files: []};
    engines[0].emit("ready");
    await settle();
    engineFs.create(hash, {marker: 3, path: ""}, callback("C3"));
    holds[2]();
    await settle();
    ids.push(engines[0].options.id);
    const resumeCount = engines[0].resumeCount;
    const finalOptions = JSON.parse(JSON.stringify(engines[0].options));
    await new Promise((resolve) => engineFs.remove(hash, () => {
        timeline.push(["remove-callback"]);
        resolve();
    }));
    engineFs.create(hash, {marker: 4}, callback("C4"));
    holds[3]();
    await settle();
    ids.push(engines[1].options.id);
    return scrub({hash, stream, cacheRoot, constructions: engines.length, resumeCount, timeline,
                  createOptions, ids, finalOptions,
                  peerSearchSources: peerSearches.length ? peerSearches[0].sources : []});
}

// ---- M814: controlled full engine over the real factory ----
async function runSource814() {
    const raw = [];
    const stored = new Map();
    let metadataMarked = false;
    const makeBitfield = (size) => {
        if (!metadataMarked) {
            metadataMarked = true;
            raw.push(["metadata-install"]);
        }
        const bits = Array(size).fill(false);
        return {length: size, get(index) { return !!bits[index]; },
                set(index, value) { bits[index] = !!value; }};
    };
    const makePiece = (length) => {
        const parts = Math.ceil(length / 16384);
        const blocks = Array(parts);
        const reserved = Array(parts).fill(false);
        return {
            parts, buffered: 0,
            get missing() { return (parts - this.buffered) * 16384; },
            reserve() {
                const index = reserved.findIndex((value, part) => !value && !blocks[part]);
                if (index < 0) return -1;
                reserved[index] = true;
                return index;
            },
            cancel(index) { reserved[index] = false; raw.push(["retry", index]); },
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
    const store = {
        write(index, buffer) { raw.push(["write", index, buffer.length]); stored.set(index, Buffer.from(buffer)); },
        verify(index) {
            const complete = stored.has(0) && stored.has(1);
            raw.push(["verify", index, complete ? "success" : "incomplete", 0, 2]);
            return complete ? {success: true, start: 0, end: 2} : null;
        },
        commit(start, end, callback) { raw.push(["commit", start, end]); callback(null, false); },
        read(index, callback) { raw.push(["read", index]); callback(null, stored.get(index)); },
        close(callback) { raw.push(["store-close"]); if (callback) callback(); },
    };
    class Bagpipe { push(fn, index, callback) { fn(index, callback); } }
    const wire = new events.EventEmitter();
    Object.assign(wire, {peerChoking: false, peerPieces: [true], requests: [], downloaded: 0,
                         amInterested: false, peerInterested: false, amChoking: true});
    wire.setTimeout = () => {};
    wire.downloadSpeed = () => 1;
    wire.uploadSpeed = () => 0;
    wire.interested = () => { wire.amInterested = true; };
    wire.uninterested = () => { wire.amInterested = false; };
    wire.choke = () => { wire.amChoking = true; raw.push(["choke", 0]); };
    wire.unchoke = () => { wire.amChoking = false; raw.push(["unchoke", 0]); };
    wire.destroy = () => wire.emit("close");
    wire.have = (index) => raw.push(["have", index]);
    wire.bitfield = () => {};
    let failFirst = true;
    const payloadAt = (offset) => offset === 524288 ? 200 : 65;
    wire.request = (pieceIndex, offset, length, callback) => {
        const request = {piece: pieceIndex, offset, length, callback};
        wire.requests.push(request);
        raw.push(["wire-request", pieceIndex, offset, length]);
        process.nextTick(() => {
            wire.requests.splice(wire.requests.indexOf(request), 1);
            if (failFirst) {
                failFirst = false;
                raw.push(["response", "fail"]);
                callback(new Error("controlled retry"));
                return;
            }
            wire.downloaded += length;
            raw.push(["response", "block"]);
            callback(null, Buffer.alloc(length, payloadAt(offset)));
        });
    };
    const swarm = new events.EventEmitter();
    Object.assign(swarm, {wires: [wire], downloaded: 0, queued: 0, size: 10, connections: [],
                          _peers: {}});
    swarm.downloadSpeed = () => 0;
    swarm.pause = () => {};
    swarm.resume = () => {};
    swarm.add = () => {};
    swarm.remove = () => {};
    swarm.listen = (_port, callback) => callback && callback();
    swarm.destroy = () => { raw.push(["transport-close"]); wire.emit("close"); };
    const stubFs = {existsSync: () => false, readFile: (_p, callback) => callback(null, null),
                    writeFile: (_p, _d, callback) => callback && callback(null)};
    const stubRequire = (id) => {
        if (id === 1) return stubFs;
        if (id === 4) return events;
        if (id === 5) return path;
        if (id === 22) return {tmpdir: () => os.tmpdir()};
        if (id === 102) return () => "oracle-id";
        if (id === 142) return (_path, callback) => callback(null);
        if (id === 180) return () => {};
        if (id === 196) return {decode: () => ({}), encode: () => Buffer.alloc(0)};
        if (id === 200) return {extend: Object.assign, debounce: (callback) => callback};
        if (id === 201) return Bagpipe;
        if (id === 303) return (value) => value;
        if (id === 815) return (value) => value;
        if (id === 818) return () => swarm;
        if (id === 830) return makeBitfield;
        if (id === 831) return (_path, callback) => callback && callback();
        if (id === 841) return () => {};
        if (id === 842) return () => Buffer.from("metadata");
        if (id === 843) return () => () => {};
        if (id === 844) return () => store;
        if (id === 845) return () => store;
        if (id === 846) return () => { throw new Error("file stream not expected"); };
        if (id === 851) return Object.assign(makePiece, {BLOCK_SIZE: 16384});
        return function stub() {};
    };
    const module814 = {exports: {}};
    modules[814].call(module814.exports, module814, module814.exports, stubRequire);
    if (typeof module814.exports !== "function") throw new Error("M814 factory did not export engine");
    const link = {infoHash: "0123456789abcdef0123456789abcdef01234567", pieceLength: 1048576,
                  length: 524289, pieces: [Buffer.alloc(20)],
                  files: [{name: "payload.bin", path: "payload.bin", offset: 0, length: 524289}]};
    const engine = module814.exports(link, {path: "C:/controlled-oracle", id: "-TS0008-oracle",
                                            virtual: true, peerSearch: false, uploads: 1});
    const startTimer = timerTrace.find((item) => item[0] === "start");
    engine.on("download", (index, buffer) => raw.push(["download", index, buffer.length]));
    engine.select(0, 1, true, () => raw.push(["notify"]));
    await settle();
    deliverInterval(startTimer[1]);
    await settle(5);
    let upload = null;
    wire.emit("request", 0, 524288, 1, (error, buffer) => {
        upload = error ? {error: error.message} : [...buffer];
        raw.push(["upload", 0, 524288, 1, upload]);
    });
    await settle(5);
    await new Promise((resolve) => engine.destroy(() => { raw.push(["destroyed"]); resolve(); }));
    deliverInterval(startTimer[1]);
    deliverInterval(startTimer[1]);
    return {verificationPieceLength: 1048576, virtualPieceLength: engine.torrent.pieceLength,
            length: 524289, raw, timer: timerTrace.slice()};
}

// ---- shared normalization ----
const sep = (value) => typeof value === "string" ? value.replace(/\\/g, "/") : value;
function normalize172(raw) {
    const fallback = `<cache-root>/${raw.hash}`;
    const hashToken = (value) => JSON.parse(JSON.stringify(value).split(raw.hash).join("<hash>"));
    const createOptions = raw.createOptions.map((options) => ({
        marker: options.marker === undefined ? null : options.marker,
        stream: options.stream === undefined ? null
            : options.stream.toLowerCase() === raw.stream.toLowerCase() ? "<stream>" : "<other>",
        torrent: options.torrent === undefined ? null
            : typeof options.torrent === "string"
                && options.torrent.toLowerCase() === raw.stream.toLowerCase() ? "<stream>" : "<other>",
        path: sep(options.path) === fallback ? "<cache-path>" : sep(options.path),
        peerSearch: hashToken(options.peerSearch),
        dht: options.dht,
        tracker: options.tracker,
        idAtEmit: Object.prototype.hasOwnProperty.call(options, "id"),
    }));
    return {
        constructions: raw.constructions,
        resumeCount: raw.resumeCount,
        timeline: raw.timeline,
        createOptions,
        ids: {count: raw.ids.length, distinct: new Set(raw.ids).size,
              allStrings: raw.ids.every((id) => typeof id === "string" && id.startsWith("-TS0008-"))},
        finalOptions: {marker: raw.finalOptions.marker,
                       path: sep(raw.finalOptions.path) === fallback ? "<cache-path>" : "<other>",
                       peerSearch: hashToken(raw.finalOptions.peerSearch),
                       id: typeof raw.finalOptions.id === "string"},
        peerSearchSources: hashToken(raw.peerSearchSources),
    };
}

// Converts either side's raw M814 log to one ordered event vocabulary.
function events814(side, raw) {
    const out = [];
    if (side === "source") {
        for (const item of raw.raw) {
            const [kind] = item;
            if (kind === "wire-request") out.push(["request", item[1], item[2], item[3]]);
            else if (kind === "response") out.push(["response", item[1]]);
            else if (kind === "metadata-install") out.push(["metadata-install"]);
            else if (kind === "write") out.push(["stage", item[1], item[2]]);
            else if (kind === "verify") out.push(["verify", item[1], item[2], item[3], item[4]]);
            else if (kind === "commit") out.push(["commit", item[1], item[2] + 1]);
            else if (kind === "have") out.push(["have", item[1]]);
            else if (kind === "download") out.push(["visible", item[1], item[2]]);
            else if (kind === "notify") out.push(["notify"]);
            else if (kind === "read") out.push(["upload-read", item[1]]);
            else if (kind === "upload") out.push(["upload", item[1], item[2], item[3], item[4]]);
            else if (kind === "unchoke" || kind === "choke") out.push(["rechoke", kind, "<peer0>"]);
            else if (kind === "transport-close" || kind === "store-close" || kind === "destroyed")
                out.push(["teardown", kind]);
        }
        for (const item of raw.timer) {
            if (item[0] === "start") out.push(["timer-start", item[2]]);
            else if (item[0] === "cancel") out.push(["timer-cancel"]);
            else if (item[0] === "tick") out.push(["timer-tick"]);
            else if (item[0] === "tick-refused") out.push(["timer-refused"]);
        }
        return out;
    }
    const peers = new Map();
    const peerToken = (peer) => {
        if (!peers.has(peer)) peers.set(peer, `<peer${peers.size}>`);
        return peers.get(peer);
    };
    raw.requests.forEach((request, index) => {
        out.push(["request", request[0], request[1], request[2]]);
        if (raw.responses[index] !== undefined) out.push(["response", raw.responses[index]]);
    });
    for (const [kind, piece, start, end, length] of raw.trace) {
        if (kind === "metadata-install") out.push(["metadata-install"]);
        else if (kind === "stage") out.push(["stage", piece, length]);
        else if (kind === "verify-incomplete") out.push(["verify", piece, "incomplete", start, end]);
        else if (kind === "verify-success") out.push(["verify", piece, "success", start, end]);
        else if (kind === "verify-failure") out.push(["verify", piece, "failure", start, end]);
        else if (kind === "commit") out.push(["commit", start, end]);
        else if (kind === "read") out.push(["reader-read", piece, length]);
        else if (kind === "upload-read") out.push(["upload-read", piece]);
        else if (kind === "timer-start") out.push(["timer-start", length]);
        else if (kind === "timer-cancel") out.push(["timer-cancel"]);
        else if (kind === "timer-tick") out.push(["timer-tick"]);
        else if (kind === "timer-tick-ignored") out.push(["timer-refused"]);
        else if (kind === "transport-close" || kind === "store-close") out.push(["teardown", kind]);
    }
    for (const action of raw.actions) {
        if (action[0] === "have") out.push(["have", action[1]]);
        else if (action[0] === "unchoke" || action[0] === "choke")
            out.push(["rechoke", action[0], peerToken(action[1])]);
        else if (action[0] === "upload") out.push(["upload", action[1], action[2], action[3], action[4]]);
    }
    for (const event of raw.events) if (event === "Destroyed") out.push(["teardown", "destroyed"]);
    if (raw.readerBytes > 0) out.push(["notify"]);
    return out;
}

function normalize814(side, raw) {
    const list = events814(side, raw);
    const of = (kind) => list.filter((item) => item[0] === kind);
    const requests = of("request").map((item) => item.slice(1));
    const responses = of("response").map((item) => item[1]);
    const vlen = raw.virtualPieceLength;
    const toVirtual = ([piece, offset]) => Math.floor((piece * raw.verificationPieceLength + offset) / vlen);
    const commits = of("commit").map((item) => [item[1], item[2]]);
    const commitIndex = list.findIndex((item) => item[0] === "commit");
    const divergences = [];
    const visible = new Set();
    // Visibility: a source "download" event, or a native reader read (native
    // readers read only committed pieces). A source download that precedes its
    // group commit is recorded as a divergence and counted visible at commit.
    list.forEach((item, index) => {
        if (item[0] !== "visible" && item[0] !== "reader-read") return;
        const piece = item[1];
        const committedBefore = commitIndex >= 0 && index > commitIndex
            && commits.some(([start, end]) => piece >= start && piece < end);
        if (side === "source" && !committedBefore) divergences.push({kind: "source-precommit-visible", piece});
        if (side === "native" && !committedBefore) divergences.push({kind: "native-precommit-visible", piece});
        visible.add(piece);
    });
    const storage = list.filter((item) => ["stage", "verify", "commit"].includes(item[0]))
        .map((item) => item[0] === "verify" && item[2] === "incomplete" ? item.slice(0, 3) : item);
    const timerStartIndex = list.findIndex((item) => item[0] === "timer-start");
    const cancelIndex = list.findIndex((item) => item[0] === "timer-cancel");
    const ticks = list.map((item, index) => [item, index]).filter(([item]) => item[0] === "timer-tick");
    const upload = of("upload")[0];
    return {
        geometry: {verificationPieceLength: raw.verificationPieceLength, virtualPieceLength: vlen,
                   virtualPieces: Math.ceil(raw.length / vlen)},
        requests: {count: requests.length, sequence: requests,
                   firstVirtualPiece: requests.length ? toVirtual(requests[0]) : null,
                   failedThenRetried: responses[0] === "fail"
                       && JSON.stringify(requests[0]) === JSON.stringify(requests[1])},
        storage,
        have: of("have").map((item) => item[1]),
        visibleAfterCommit: [...visible].sort((a, b) => a - b),
        notified: of("notify").length > 0,
        rechoke: of("rechoke").map((item) => [item[1], item[2]]),
        uploadRead: of("upload-read").map((item) => item[1]),
        upload: upload ? {piece: upload[1], offset: upload[2], length: upload[3], bytes: upload[4]} : null,
        timer: {intervals: of("timer-start").map((item) => item[1]),
                cancels: of("timer-cancel").length,
                ticksWhileActive: ticks.filter(([, index]) => cancelIndex < 0 || index < cancelIndex).length,
                ticksAfterCancel: ticks.filter(([, index]) => cancelIndex >= 0 && index > cancelIndex).length,
                refusedAfterCancel: of("timer-refused").length,
                startedAfterMetadata: timerStartIndex > list.findIndex((item) => item[0] === "metadata-install")},
        teardown: of("teardown").map((item) => item[1]),
        divergences,
    };
}

function diff(left, right, at = "$") {
    if (JSON.stringify(left) === JSON.stringify(right)) return [];
    if (left && right && typeof left === "object" && typeof right === "object"
        && !Array.isArray(left) && !Array.isArray(right)) {
        const keys = new Set([...Object.keys(left), ...Object.keys(right)]);
        return [...keys].flatMap((key) => diff(left[key], right[key], `${at}.${key}`));
    }
    return [{path: at, source: left, native: right}];
}

function compare(source172, native172, source814, native814) {
    const s172 = normalize172(source172);
    const n172 = normalize172(native172);
    const s814 = normalize814("source", source814);
    const n814 = normalize814("native", native814);
    const strip = ({divergences, ...rest}) => rest;
    const differences = [
        ...diff(s172, n172, "$.m172"),
        ...diff(strip(s814), strip(n814), "$.m814"),
        ...diff(s814.divergences, DISCLOSED_SOURCE_DIVERGENCES, "$.m814.sourceDivergences"),
        ...diff(n814.divergences, [], "$.m814.nativeDivergences"),
    ];
    return {match: differences.length === 0, differences,
            projections: {source172: s172, native172: n172, source814: s814, native814: n814}};
}

const clone = (value) => JSON.parse(JSON.stringify(value));
// Each control alters one observed fact on one side; comparison must fail.
const driftControls = [
    ["native request offset", (s, n) => { n.m814.requests[2][1] += 16384; }],
    ["native request count", (s, n) => { n.m814.requests.pop(); n.m814.responses.pop(); }],
    ["native retry removed", (s, n) => { n.m814.responses[0] = "block"; }],
    ["native timer interval 500", (s, n) => {
        n.m814.trace = n.m814.trace.map((t) => t[0] === "timer-start" ? [t[0], t[1], t[2], t[3], 500] : t);
    }],
    ["native post-close tick", (s, n) => { n.m814.trace.push(["timer-tick", 0, 0, 0, 0]); }],
    ["native timer never cancelled", (s, n) => {
        n.m814.trace = n.m814.trace.filter((t) => t[0] !== "timer-cancel");
    }],
    ["native HAVE dropped", (s, n) => { n.m814.actions = n.m814.actions.filter((a) => a[0] !== "have"); }],
    ["native precommit visibility", (s, n) => {
        const at = n.m814.trace.findIndex((t) => t[0] === "commit");
        n.m814.trace.splice(at, 0, ["read", 1, 0, 0, 1]);
    }],
    ["native commit before verify", (s, n) => {
        const verify = n.m814.trace.findIndex((t) => t[0] === "verify-success");
        const commit = n.m814.trace.findIndex((t) => t[0] === "commit");
        [n.m814.trace[verify], n.m814.trace[commit]] = [n.m814.trace[commit], n.m814.trace[verify]];
    }],
    ["native upload byte", (s, n) => {
        n.m814.actions = n.m814.actions.map((a) => a[0] === "upload" ? [...a.slice(0, 4), [65]] : a);
    }],
    ["native rechoke missing", (s, n) => {
        n.m814.actions = n.m814.actions.filter((a) => a[0] !== "unchoke");
    }],
    ["native store close missing", (s, n) => {
        n.m814.trace = n.m814.trace.filter((t) => t[0] !== "store-close");
    }],
    ["native callback order", (s, n) => {
        const a = n.m172.timeline.findIndex((t) => t[1] === "C2");
        const b = n.m172.timeline.findIndex((t) => t[1] === "C1");
        [n.m172.timeline[a], n.m172.timeline[b]] = [n.m172.timeline[b], n.m172.timeline[a]];
    }],
    ["native resume count", (s, n) => { n.m172.resumeCount -= 1; }],
    ["native stream alias lost", (s, n) => { delete n.m172.createOptions[1].torrent; }],
    ["native falsy path kept", (s, n) => { n.m172.createOptions[1].path = false; }],
    ["native deep merge", (s, n) => { n.m172.createOptions[1].peerSearch.min = 40; }],
    ["native id before emit", (s, n) => { n.m172.createOptions[0].id = "-TS0008-x"; }],
    ["native reused id", (s, n) => { n.m172.ids[1] = n.m172.ids[0]; }],
    ["native extra construction", (s, n) => { n.m172.constructions += 1; }],
    ["native created missing", (s, n) => {
        n.m172.timeline = n.m172.timeline.filter((t) => t[0] !== "Created");
    }],
    ["source precommit visibility hidden", (s) => {
        s.m814.raw = s.m814.raw.filter((r) => !(r[0] === "download" && r[1] === 1));
    }],
    ["source request order", (s) => {
        const requests = s.m814.raw.filter((r) => r[0] === "wire-request");
        requests[2][2] += 16384;
    }],
    ["source resume count", (s) => { s.m172.resumeCount += 1; }],
];

(async () => {
    const source172 = await runSource172();
    const source814 = scrub(await runSource814());

    const defaultCandidate = path.resolve(__dirname, "repair-build", "server1_k11_engine_registry_test.exe");
    const candidate = path.resolve(process.argv[2] || defaultCandidate);
    const candidateRun = childProcess.spawnSync(candidate, ["--trace"], {
        encoding: "utf8",
        env: {...process.env, PATH: `C:/Qt/6.11.1/msvc2022_64/bin;${process.env.PATH || ""}`},
    });
    if (candidateRun.status !== 0)
        throw new Error(`candidate failed ${candidateRun.status}: ${candidateRun.stderr}`);
    const line = (prefix) => {
        const found = candidateRun.stdout.split(/\r?\n/).find((item) => item.startsWith(prefix));
        if (!found) throw new Error(`candidate trace missing ${prefix}`);
        return JSON.parse(found.slice(prefix.length));
    };
    const native172 = line("K11-M172-NATIVE-RAW ");
    const native814 = line("K11-M814-NATIVE-RAW ");

    const result = compare(source172, native172, source814, native814);
    const controls = driftControls.map(([name, mutate]) => {
        const s = {m172: clone(source172), m814: clone(source814)};
        const n = {m172: clone(native172), m814: clone(native814)};
        mutate(s, n);
        const mutated = compare(s.m172, n.m172, s.m814, n.m814);
        return {name, detected: !mutated.match, firstDifference: mutated.differences[0] || null};
    });
    const corruptOracle = Buffer.from(bytes);
    corruptOracle[100] ^= 1;
    const corruptOracleRejected = crypto.createHash("sha256").update(corruptOracle).digest("hex")
        !== expectedBundle;
    const corruptFactoryRejected = crypto.createHash("sha256")
        .update(modules[172].toString().replace("function", "functioN"), "utf8").digest("hex")
        !== expectedFactories.M172;
    const controlsDetected = controls.every((control) => control.detected);
    const report = {
        bundleHash, expectedFactories, observedFactoryHashes,
        disclosedSourceDivergences: DISCLOSED_SOURCE_DIVERGENCES,
        normalizationRules: [
            "M172 hash and cache root are tokenized; paths equal to <cache-root>/<hash> become <cache-path>.",
            "M172 ids compare by count, distinctness and -TS0008- prefix; values are generator-specific.",
            "M814 source 'verified' per-piece events are implied by commit and dropped; commit end becomes exclusive.",
            "M814 incomplete verification drops the group range on both sides.",
            "M814 visibility: source 'download' events and native committed reader reads; a visible event before its group commit is a divergence.",
            "M814 source selection notify callbacks and native reader delivery both project to notified=true.",
            "M814 peer handles are tokenized in first-seen order.",
            "M814 timer cancel is compared by count and position, not interleaved with teardown.",
        ],
        match: result.match,
        differences: result.differences,
        projections: result.projections,
        controls,
        controlsDetected,
        corruptOracleRejected,
        corruptFactoryRejected,
    };
    const rawDir = path.join(__dirname, "raw");
    // Mutation runs compare only; they never replace the recorded evidence.
    const dryRun = process.env.K11_ORACLE_DRY_RUN === "1";
    if (!dryRun) fs.mkdirSync(rawDir, {recursive: true});
    const write = (name, value) => dryRun || fs.writeFileSync(path.join(rawDir, name),
        (typeof value === "string" ? value : JSON.stringify(value, null, 2)) + "\n");
    write("source-m172-raw.json", source172);
    write("source-m814-raw.json", source814);
    write("native-m172-raw.json", native172);
    write("native-m814-raw.json", native814);
    write("source-m172-projection.json", result.projections.source172);
    write("native-m172-projection.json", result.projections.native172);
    write("source-m814-projection.json", result.projections.source814);
    write("native-m814-projection.json", result.projections.native814);
    write("candidate.stdout.txt", candidateRun.stdout.trimEnd());
    write("candidate.stderr.txt", candidateRun.stderr.trimEnd());
    write("comparison.json", report);
    console.log(JSON.stringify({match: result.match, differences: result.differences,
                                controls: controls.map((c) => [c.name, c.detected]),
                                corruptOracleRejected, corruptFactoryRejected}, null, 2));
    if (!result.match || !controlsDetected || !corruptOracleRejected || !corruptFactoryRejected)
        process.exitCode = 2;
})().catch((error) => {
    console.error(error && error.stack ? error.stack : error);
    process.exitCode = 1;
});
