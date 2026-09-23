"use strict";

// K12-A source/native differential.
//
// The real M172 (EngineFS), M662 (Counter), M663 (GuessFileIdx), M304
// (parse-video-name), M80 (safeStatelessRegex) and M100 (router) factories run
// against controlled engines, a virtual clock and controlled route requests.
// Every case in cases/K12.json is observed on the source and, through
// `--emit`, on the native build; the two outputs are compared as exact JSON
// text per case. Nothing on either side is a hand-written expectation.
//
// Stubbed: M612 PeerSearch (attaches the scenario's stats to swarm.peerSearch,
// as the real constructor attaches itself), M664 spoofedPeerId (fixed id), the
// os module (fixed loadavg/cpus/tmpdir) and EngineFS.getCachePath (fixed
// prefix, as the M564 entrypoint also replaces it). The three-line response
// close guard of the stream route (M172 L18255-18258) is mirrored in openStream
// below; H01 owns that route.
//
// Usage: node run_oracle.js [--source-only] [--native <exe>] [--out <dir>]

const crypto = require("crypto");
const childProcess = require("child_process");
const events = require("events");
const fs = require("fs");
const path = require("path");
const util = require("util");
const vm = require("vm");

const args = process.argv.slice(2);
const option = (name, fallback) => {
    const index = args.indexOf(name);
    return index >= 0 ? args[index + 1] : fallback;
};
const sourceOnly = args.includes("--source-only");
const outDir = path.resolve(option("--out", path.join(__dirname, "raw")));
const casesPath = path.resolve(option("--cases", path.join(__dirname, "cases", "K12.json")));
const nativeExe = option("--native", process.env.K12_NATIVE_EXE || "");

const oracle = process.env.SERVER1_ORACLE_BUNDLE
    || "C:/b/Colosseum-Server-1.0-Planning-Pack/oracle/stremio-service-v4.21.1-server-bundle/server.js";
const expectedBundle = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f";
const expectedFactories = {
    M80: "de77a63d6333c32ae8911f2acc43595ae76a87137d31857d6bd41bb5f33089d8",
    M100: "f8b0b1ae8bd44fb66d27bb0572420c8e60f3454e849346404c5bf27df4cd7806",
    M172: "bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486",
    M304: "83b3a52ffac8663b219db8e4e85de77182fbb8cf7412bfe27978fd0968793c0a",
    M662: "fb3f3872c39d5587bf694679581f4a7d0a497a86b0fc21ad3d02c884de94ad26",
    M663: "4953bf2e5b17300fc3e5d51f2c874b1e24ba18e3076a36f2cbfdcba9dd90288d",
};

// ---- virtual clock (the context's setTimeout/clearTimeout) ----
const clock = {now: 0, seq: 0, timers: new Map()};
const vSetTimeout = (callback, milliseconds) => {
    let delay = Number(milliseconds);
    if (!(delay >= 1 && delay <= 2147483647)) delay = 1;
    const id = ++clock.seq;
    clock.timers.set(id, {at: clock.now + Math.trunc(delay), callback});
    return {id};
};
const vClearTimeout = (handle) => { if (handle) clock.timers.delete(handle.id); };
const resetClock = () => { clock.now = 0; clock.seq = 0; clock.timers.clear(); };
const settle = async (turns = 40) => {
    for (let i = 0; i < turns; i++) await new Promise((resolve) => setImmediate(resolve));
};
const advance = async (milliseconds) => {
    const target = clock.now + milliseconds;
    for (;;) {
        let next = null;
        for (const [id, timer] of clock.timers)
            if (timer.at <= target && (!next || timer.at < next[1].at
                                       || (timer.at === next[1].at && id < next[0])))
                next = [id, timer];
        if (!next) break;
        clock.timers.delete(next[0]);
        clock.now = next[1].at;
        next[1].callback();
        await settle();
    }
    clock.now = target;
};

// ---- load the frozen bundle ----
function loadModules(bundlePath) {
    const bytes = fs.readFileSync(bundlePath);
    const bundleHash = crypto.createHash("sha256").update(bytes).digest("hex");
    if (bundleHash !== expectedBundle) throw new Error(`bundle hash ${bundleHash}`);
    const entry = "__webpack_require__(__webpack_require__.s = 564)";
    let source = bytes.toString("utf8");
    if (source.split(entry).length !== 2) throw new Error("webpack entry identity drifted");
    source = source.replace(entry,
        "(globalThis.__oracle_modules__ = modules, globalThis.__oracle_require__ = __webpack_require__)");
    const context = {require, Buffer, console: {log() {}, warn() {}, error() {}}, process,
                     setTimeout: vSetTimeout, clearTimeout: vClearTimeout,
                     setInterval: () => ({}), clearInterval: () => {}, setImmediate,
                     URL, TextDecoder, TextEncoder};
    context.global = context;
    vm.runInNewContext(source, context, {filename: "server.js"});
    const modules = context.__oracle_modules__;
    if (!Array.isArray(modules) || modules.length <= 664) throw new Error("module table missing");
    const hashes = {};
    for (const name of Object.keys(expectedFactories)) {
        hashes[name] = crypto.createHash("sha256")
            .update(modules[Number(name.slice(1))].toString().replace(/\r\n/g, "\n"), "utf8").digest("hex");
    }
    return {bundleHash, modules, require: context.__oracle_require__, hashes};
}

const loaded = loadModules(oracle);
if (JSON.stringify(loaded.hashes) !== JSON.stringify(expectedFactories))
    throw new Error(`factory identity drift ${JSON.stringify(loaded.hashes)}`);
const cases = JSON.parse(fs.readFileSync(casesPath, "utf8"));
const hashOf = (key) => {
    if (key === "Aupper") return cases.hashes.A.toUpperCase();
    return cases.hashes[key] || key;
};
const resolvePath = (url) => url.replace(/^\/(A|B|C|Aupper)(?=\/|$)/, (_m, key) => "/" + hashOf(key));
const fileObjects = (list) => (list === "createFiles" ? cases.createFiles : list)
    .map(([filePath, name, length, offset]) => ({path: filePath, name, length, offset}));
const jsonArgs = (values) => JSON.parse(JSON.stringify(values.map((value) =>
    value === undefined ? null : value)));

// ---- a fresh M172 instance over controlled stubs ----
function freshEngineFs(log) {
    const osStub = {
        tmpdir: () => "tmp",
        loadavg: () => cases.sys.loadavg,
        cpus: () => cases.sys.cpus,
    };
    const peerSearchSpecs = new Map();
    const stubRequire = (id) => {
        if (id === 22) return osStub;
        if (id === 612) return function PeerSearch(_sources, swarm) {
            const spec = peerSearchSpecs.get(swarm);
            if (!spec) return;
            swarm.peerSearch = {stats: () => spec.stats, isRunning: () => spec.running};
        };
        if (id === 664) return () => cases.spoofedPeerId;
        return loaded.require(id);
    };
    const module172 = {exports: {}};
    loaded.modules[172].call(module172.exports, module172, module172.exports, stubRequire);
    const engineFs = module172.exports;
    engineFs.STREAM_TIMEOUT = 2e4;   // M564 entrypoint
    engineFs.ENGINE_TIMEOUT = 12e4;
    engineFs.getCachePath = (ih) => cases.cachePathPrefix + ih;
    const originalEmit = engineFs.emit.bind(engineFs);
    engineFs.emit = (name, ...values) => {
        if (log.enabled) log.entries.push([clock.now, name, ...jsonArgs(values)]);
        return originalEmit(name, ...values);
    };
    return {engineFs, peerSearchSpecs};
}

function makeEngine(spec, hash, log, peerSearchSpecs) {
    const engine = new events.EventEmitter();
    const have = new Set(spec.have || []);
    engine.infoHash = hash;
    engine.buffer = spec.buffer || undefined;
    engine.selection = Array.isArray(spec.selections) ? spec.selections
        : Array.from({length: spec.selections || 0}, () => ({from: 0, to: 0}));
    const torrentSpec = typeof spec.torrent === "boolean"
        ? {name: "t", pieceLength: spec.pieceLength, files: spec.files,
           verificationLen: spec.verificationLen}
        : spec.torrent;
    const buildTorrent = () => {
        if (!torrentSpec) return null;
        const torrent = {name: torrentSpec.name, pieceLength: torrentSpec.pieceLength,
                         files: fileObjects(torrentSpec.files)};
        if (torrentSpec.verificationLen !== undefined) torrent.verificationLen = torrentSpec.verificationLen;
        return torrent;
    };
    engine.torrent = spec.torrent === false ? null : buildTorrent();
    engine.metadata = () => { engine.torrent = buildTorrent(); engine.emit("ready"); };
    engine.bitfield = {get: (index) => have.has(index), set: (index) => have.add(index)};
    engine.have = have;
    engine.store = spec.dest !== undefined ? {getDest: () => spec.dest} : {};
    engine.ready = (callback) => engine.torrent ? process.nextTick(callback)
                                                : engine.once("ready", callback);
    engine.select = (from, to, priority) => {
        if (log.enabled) log.entries.push([clock.now, "select", hash, from, to, priority]);
    };
    engine.held = null;
    engine.destroy = (callback) => {
        if (log.enabled) log.entries.push([clock.now, "destroy", hash]);
        if (engine.holdDestroy) { (engine.held = engine.held || []).push(callback); return; }
        process.nextTick(callback);
    };
    const wires = (spec.wires || []).map((wire) => {
        const record = {peerChoking: wire.peerChoking, requests: new Array(wire.requests).fill(0),
                        amInterested: wire.amInterested, isSeeder: wire.isSeeder,
                        downloadSpeed: () => wire.downSpeed, uploadSpeed: () => wire.upSpeed};
        if ("address" in wire) record.peerAddress = wire.address;
        return record;
    });
    const peers = {};
    for (let i = 0; i < (spec.unique || 0); i++) peers["p" + i] = {};
    engine.swarm = {
        resume() {}, pause() {}, on() {},
        wires, _peers: peers, tries: spec.tries, paused: !!spec.paused,
        connections: new Array(spec.connections || 0).fill(0), size: spec.size,
        downloaded: spec.downloaded, uploaded: spec.uploaded,
        downloadSpeed: () => spec.downloadSpeed, uploadSpeed: () => spec.uploadSpeed,
    };
    if ("queued" in spec) engine.swarm.queued = spec.queued;
    if (spec.peerSearch) peerSearchSpecs.set(engine.swarm, spec.peerSearch);
    return engine;
}

async function createEngines(instance, specs, log) {
    const engines = {};
    instance.engineFs.engine = (torrent) => {
        const hash = /btih:([0-9a-f]{40})/i.exec(String(torrent))[1].toLowerCase();
        const spec = instance.pendingSpecs.get(hash);
        const engine = makeEngine(spec, hash, log, instance.peerSearchSpecs);
        engines[hash] = engine;
        return engine;
    };
    instance.pendingSpecs = instance.pendingSpecs || new Map();
    for (const spec of specs) {
        const hash = hashOf(spec.key);
        instance.pendingSpecs.set(hash, spec);
        instance.engineFs.create(hash, {}, () => {});
        await settle();
    }
    return engines;
}

function request(engineFs, method, url, body) {
    return new Promise((resolve) => {
        const res = {statusCode: 200, headers: {}, body: undefined,
                     setHeader(name, value) { this.headers[name] = value; },
                     writeHead(code) { this.statusCode = code; },
                     end(text) { this.body = text === undefined ? "" : String(text); resolve(this); }};
        const req = {method, url, headers: {}, body, connection: {setTimeout() {}}};
        let thrown = null;
        const onError = (error) => { thrown = error; resolve({thrown}); };
        process.once("uncaughtException", onError);
        try {
            engineFs.getRootRouter()(req, res, (error) => resolve({next: error ? String(error) : "next"}));
        } catch (error) {
            resolve({thrown: error});
        }
        settle().then(() => {
            process.removeListener("uncaughtException", onError);
            if (res.body === undefined && !thrown) resolve({pending: true});
        });
    }).then((result) => {
        if (result.thrown) return {throws: result.thrown.name};
        if (result.pending) return {pending: true};
        if (result.next) return {next: result.next};
        return {status: result.statusCode, body: result.body};
    });
}

// ---- source scenarios ----
async function sourceCounter(scenario) {
    resetClock();
    const Counter = loaded.require(662);
    const emitter = new events.EventEmitter();
    const entries = [];
    new Counter(emitter, "inc", "dec", (hash, idx) => hash + ":" + idx,
                (hash, idx) => entries.push([clock.now, "positive", hash, idx]),
                (hash, idx) => entries.push([clock.now, "zero", hash, idx]),
                () => 1000);
    for (const op of scenario.ops) {
        if (op[0] === "advance") await advance(op[1]);
        else emitter.emit(op[0], op[1], op[2]);
    }
    return entries;
}

// engine-create* and engine-ready* come from createEngine (K11's accepted
// create/ready contract), not from the K12 lifecycle; they are dropped from
// lifecycle logs on the source side only.
const lifecycleOwned = (entry) => !/^engine-(create|ready)/.test(entry[1]);

async function sourceLifecycle(scenario) {
    resetClock();
    const log = {enabled: false, entries: []};
    const instance = freshEngineFs(log);
    const engines = await createEngines(instance, scenario.engines, log);
    log.enabled = true;
    const streams = {};
    const engineFs = instance.engineFs;
    for (const step of scenario.steps) {
        const [op] = step;
        if (op === "open") {
            const hash = hashOf(step[1]);
            const idx = step[2];
            engineFs.emit("stream-open", hash, idx);
            let closed = false;
            streams[step[3]] = () => {
                if (!closed) { closed = true; engineFs.emit("stream-close", hash, idx); }
            };
        } else if (op === "finish" || op === "close") {
            streams[step[1]]();
        } else if (op === "advance") {
            await advance(step[1]);
        } else if (op === "verify") {
            const engine = engines[hashOf(step[1])];
            engine.have.add(step[2]);
            engine.emit("verify", step[2]);
        } else if (op === "remove") {
            const hash = hashOf(step[1]);
            engineFs.remove(hash, () => log.entries.push([clock.now, "remove-done", hash]));
        } else if (op === "removeHeld") {
            const hash = hashOf(step[1]);
            engines[hash].holdDestroy = true;
            engineFs.remove(hash, () => log.entries.push([clock.now, "remove-done", hash]));
        } else if (op === "removeAll") {
            const response = await request(engineFs, "GET", "/removeAll");
            log.entries.push([clock.now, "response", response.status, response.body]);
        } else if (op === "release") {
            for (const engine of Object.values(engines)) {
                const held = engine.held || [];
                engine.held = null;
                engine.holdDestroy = false;
                for (const callback of held) callback();
            }
        } else if (op === "metadata") {
            engines[hashOf(step[1])].metadata();
        } else if (op === "setStreamTimeout") {
            engineFs.STREAM_TIMEOUT = step[1];
        } else {
            throw new Error("unknown lifecycle step " + op);
        }
        await settle();
    }
    return log.entries.filter(lifecycleOwned);
}

async function sourceKeep(scenario) {
    resetClock();
    const log = {enabled: false, entries: []};
    const instance = freshEngineFs(log);
    const engines = await createEngines(instance, scenario.engines.map((engine) =>
        ({key: engine.key, torrent: true, pieceLength: 64, files: [["f", "f", 1, 0]],
          have: [], selections: engine.selections})), log);
    log.enabled = true;
    const order = (scenario.destroyOrder || []).map(hashOf);
    if (order.length) for (const engine of Object.values(engines)) engine.holdDestroy = true;
    for (const [hash, concurrency] of scenario.calls) {
        const key = cases.hashes[hash] ? cases.hashes[hash] : hash;
        instance.engineFs.keepConcurrency(key, concurrency)
            .then(() => log.entries.push([clock.now, "resolved", key, concurrency]));
        await settle();
    }
    for (const hash of order) {
        const engine = engines[hash];
        const held = engine.held || [];
        engine.held = null;
        engine.holdDestroy = false;
        for (const callback of held) callback();
        await settle();
    }
    log.entries.push([clock.now, "remaining", ...instance.engineFs.list()]);
    return log.entries;
}

async function sourceStats(scenario) {
    resetClock();
    const log = {enabled: false, entries: []};
    const instance = freshEngineFs(log);
    const specs = scenario.engines.map((engine) =>
        Object.assign({key: engine.key}, cases.statStates[engine.state]));
    if (scenario.requests.some((request) => request[0] === "POST"))
        specs.push(Object.assign({key: "A", deferred: true}, cases.statStates.create));
    const engines = await createEngines(instance, specs.filter((spec) => !spec.deferred), log);
    for (const spec of specs.filter((spec) => spec.deferred)) instance.pendingSpecs.set(hashOf(spec.key), spec);
    for (const [key, idx] of scenario.open || []) {
        instance.engineFs.emit("stream-open", hashOf(key), idx);
        await settle();
    }
    void engines;
    const responses = [];
    for (const [method, url, body] of scenario.requests) {
        const response = await request(instance.engineFs, method, resolvePath(url),
                                       body === undefined ? undefined : JSON.parse(JSON.stringify(body)));
        responses.push([method, url, response]);
        await settle();
    }
    return responses;
}

function sourceGuess(scenario) {
    const guess = loaded.require(663);
    return guess(fileObjects(scenario.files), scenario.seriesInfo);
}

function sourceVideoName(filePath, options) {
    const parse = loaded.require(304);
    try {
        return {meta: parse(filePath, options)};
    } catch (error) {
        return {throws: error.name};
    }
}

async function runSource() {
    const outputs = {};
    for (const scenario of cases.counter) outputs[scenario.id] = await sourceCounter(scenario);
    for (const scenario of cases.lifecycle) outputs[scenario.id] = await sourceLifecycle(scenario);
    for (const scenario of cases.keep) outputs[scenario.id] = await sourceKeep(scenario);
    for (const scenario of cases.stats) outputs[scenario.id] = await sourceStats(scenario);
    for (const scenario of cases.guess) outputs[scenario.id] = sourceGuess(scenario);
    outputs["N-numbers"] = [cases.numbers, cases.numbers.map(String)];
    outputs["V-names"] = cases.videoNames.map((name) => [name, sourceVideoName(name)]);
    outputs["V-options"] = cases.videoNameOptions.map(([name, options]) =>
        [name, options, sourceVideoName(name, options)]);
    return outputs;
}

function perCase(outputs) {
    const result = {};
    for (const [id, value] of Object.entries(outputs)) result[id] = JSON.stringify(value);
    return result;
}

// One observed value changed: the last leaf of the output, or an extra
// element when the output is empty.
function drift(value) {
    if (Array.isArray(value)) {
        if (!value.length) return ["drift"];
        const copy = value.slice();
        copy[copy.length - 1] = drift(copy[copy.length - 1]);
        return copy;
    }
    if (value && typeof value === "object") {
        const keys = Object.keys(value);
        if (!keys.length) return {drift: true};
        return Object.assign({}, value, {[keys[keys.length - 1]]: drift(value[keys[keys.length - 1]])});
    }
    if (typeof value === "number") return value + 1;
    if (typeof value === "string") {
        try { return JSON.stringify(drift(JSON.parse(value))); } catch (_) { return value + "x"; }
    }
    if (typeof value === "boolean") return !value;
    return "drift";
}

function compare(source, native) {
    const differences = [];
    for (const id of Object.keys(source)) {
        if (!(id in native)) differences.push({id, kind: "missing-native"});
        else if (source[id] !== native[id]) differences.push({id, kind: "mismatch",
            source: source[id], native: native[id]});
    }
    for (const id of Object.keys(native))
        if (!(id in source)) differences.push({id, kind: "extra-native"});
    return differences;
}

(async () => {
    fs.mkdirSync(outDir, {recursive: true});
    const source = perCase(await runSource());
    fs.writeFileSync(path.join(outDir, "source-outputs.json"), JSON.stringify(source, null, 1) + "\n");
    if (sourceOnly) {
        process.stdout.write(`source cases=${Object.keys(source).length}\n`);
        return;
    }
    if (!nativeExe) throw new Error("--native <exe> is required unless --source-only");
    const nativePath = path.join(outDir, "native-outputs.json");
    const run = childProcess.spawnSync(nativeExe, ["--emit", casesPath, nativePath],
                                       {encoding: "utf8"});
    fs.writeFileSync(path.join(outDir, "native-emit.stdout.txt"), run.stdout || "");
    fs.writeFileSync(path.join(outDir, "native-emit.stderr.txt"), run.stderr || "");
    if (run.status !== 0) throw new Error(`native emit exit ${run.status}`);
    const native = JSON.parse(fs.readFileSync(nativePath, "utf8"));
    const differences = compare(source, native);

    // Negative controls: one altered observation per category must be caught,
    // and a corrupted bundle or factory must be refused.
    const controls = [];
    for (const id of Object.keys(source)) {
        const altered = Object.assign({}, native);
        altered[id] = JSON.stringify(drift(JSON.parse(native[id])));
        controls.push({id, detected: compare(source, altered).some((d) => d.id === id)});
    }
    const corruptPath = path.join(outDir, "corrupt-bundle.js");
    fs.writeFileSync(corruptPath, Buffer.concat([fs.readFileSync(oracle), Buffer.from("\n//x")]));
    let corruptBundleRejected = false;
    try { loadModules(corruptPath); } catch (error) { corruptBundleRejected = /bundle hash/.test(error.message); }
    fs.unlinkSync(corruptPath);
    const tampered = Object.assign({}, loaded.hashes, {M662: "0".repeat(64)});
    const corruptFactoryRejected = JSON.stringify(tampered) !== JSON.stringify(expectedFactories);

    const comparison = {
        schema: "colosseum-server1-k12-differential/v1",
        bundleHash: loaded.bundleHash,
        factoryHashes: loaded.hashes,
        cases: Object.keys(source).length,
        match: differences.length === 0,
        differences,
        controlsDetected: `${controls.filter((c) => c.detected).length}/${controls.length}`,
        controls,
        corruptBundleRejected,
        corruptFactoryRejected,
    };
    fs.writeFileSync(path.join(outDir, "comparison.json"), JSON.stringify(comparison, null, 1) + "\n");
    process.stdout.write(`cases=${comparison.cases} match=${comparison.match} differences=${differences.length} ` +
        `controls=${comparison.controlsDetected} corruptBundle=${corruptBundleRejected} ` +
        `corruptFactory=${corruptFactoryRejected}\n`);
    if (!comparison.match || controls.some((c) => !c.detected) || !corruptBundleRejected
        || !corruptFactoryRejected) process.exitCode = 1;
})().catch((error) => {
    process.stderr.write(String(error && error.stack || error) + "\n");
    process.exitCode = 2;
});
