// Execute unchanged module factories from the authenticated Stremio bundle.
// Only filesystem/process/network/timer boundaries are injected. No policy replay.
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { spawn } from "node:child_process";
import { EventEmitter, once } from "node:events";
import { PassThrough, Readable } from "node:stream";

const expectedSha = "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f";
const oraclePath = process.env.M00_ORACLE_PATH;
assert(oraclePath, "Set M00_ORACLE_PATH to the authenticated server.js");
const bytes = fs.readFileSync(oraclePath);
const sha256 = value => crypto.createHash("sha256").update(value).digest("hex");
assert.equal(sha256(bytes), expectedSha, "oracle SHA-256 mismatch");
const source = bytes.toString("utf8");
const entry = '__webpack_require__.p = "", __webpack_require__(__webpack_require__.s = 564);';
assert.equal(source.split(entry).length, 2, "unique webpack entrypoint required");
const timers = new Map();
let timerId = 0;
const oracleProcess = { platform: process.platform, env: {} };
const context = vm.createContext({
  Buffer, URL, process: oracleProcess,
  console: { log() {}, error() {}, warn() {} },
  setTimeout(callback, delay, ...args) {
    const id = ++timerId;
    timers.set(id, { callback, delay, args });
    return id;
  },
  clearTimeout(id) { timers.delete(id); },
});
vm.runInContext(source.replace(entry,
  '__webpack_require__.p = "", globalThis.__M00__ = { modules, require: __webpack_require__ };'),
  context, { filename: oraclePath, timeout: 5000 });
const oracle = context.__M00__;
assert(Array.isArray(oracle.modules), "webpack module table was not exposed");
function invoke(id, dependencies = {}) {
  const module = { exports: {} };
  const dependencyRequire = key => {
    assert(Object.hasOwn(dependencies, key), `M${id}: unprovided dependency ${key}`);
    return dependencies[key];
  };
  oracle.modules[id].call(module.exports, module, module.exports, dependencyRequire);
  return module.exports;
}
function sourceMap() {
  return [433, 434, 436, 564, 667, 855, 861].map(id => {
    const text = oracle.modules[id].toString();
    const start = source.indexOf(text);
    assert(start >= 0 && source.indexOf(text, start + 1) === -1,
      `M${id}: factory must be an exact, unique slice of the verified bundle`);
    return {
      module: `M${id}`, factory_sha256: sha256(Buffer.from(text)),
      byte_start: Buffer.byteLength(source.slice(0, start)),
      byte_end_exclusive: Buffer.byteLength(source.slice(0, start + text.length)),
      first_lf_line: source.slice(0, start).split("\n").length,
      last_lf_line: source.slice(0, start + text.length).split("\n").length,
    };
  });
}
const moduleMap = sourceMap();
const trace = [];
const details = { moduleMap, injected_boundaries: [], source_only: {} };
function emit(key, value) { trace.push(`${key}=${value}`); }
function advanceTimer(id) {
  const timer = timers.get(id);
  assert(timer, `missing scheduled timer ${id}`);
  assert.equal(timer.delay, 500, "source polling/close delay must remain 500ms");
  timers.delete(id);
  timer.callback(...timer.args);
}
const tick = () => new Promise(resolve => setImmediate(resolve));
const root = fs.mkdtempSync(path.join(os.tmpdir(), "server1-m00-oracle-"));
const children = new Set();
const openFiles = [];
const streams = new Set();
const watchdog = setTimeout(() => {
  for (const child of children) child.kill();
  console.error("M00 oracle fixture exceeded its bounded deadline");
  process.exitCode = 1;
}, 15000);
watchdog.unref();
try {
  const unusual = path.join(root, "space \u0394");
  fs.mkdirSync(unusual);
  const preferred = path.join(unusual, "override tool.exe");
  const inPath = path.join(unusual, "trace tool.exe");
  const directory = path.join(unusual, "trace-directory");
  fs.writeFileSync(preferred, "fixture");
  fs.writeFileSync(inPath, "fixture");
  fs.chmodSync(preferred, 0o755);
  fs.chmodSync(inPath, 0o755);
  fs.mkdirSync(directory);
  oracleProcess.env.PATH = unusual;
  const locator = invoke(667, { 1: fs, 5: path });
  assert.equal(locator.locateExecutable("trace tool.exe", [preferred]), preferred);
  emit("M00-01 locator.override", "preferred");
  assert.equal(locator.locateExecutable("trace-directory", [directory]), directory);
  emit("M00-01 locator.directory", "accepted");
  assert.equal(locator.locateExecutable("trace tool.exe", [path.join(unusual, "missing.exe")]), inPath);
  emit("M00-01 locator.missing-fallback", "path");
  const retained = { ffmpeg: preferred };
  locator.init(retained);
  assert.equal(locator.locateAllExecutables({ ffmpeg: ["missing-tool"] }).ffmpeg, preferred);
  details.source_only.locateAll_retains_previous = true;

  const sourceFs = Object.create(fs);
  sourceFs.openSync = (...args) => {
    const fd = fs.openSync(...args); openFiles.push(fd); return fd;
  };
  const spawnCalls = [];
  const observedSpawn = (program, args, options) => {
    spawnCalls.push({ program, args: Array.from(args), detached: options.detached,
      stdout_mode: options.stdio[1], stderr_mode: typeof options.stdio[2] === "number" ? "debug-file" : options.stdio[2] });
    const child = spawn(program, Array.from(args), options);
    children.add(child);
    child.once("close", () => children.delete(child));
    return child;
  };
  oracleProcess.env.HLS_DEBUG = "1";
  oracleProcess.env.HLS_DEBUG_DIR = root;
  delete oracleProcess.env.HLSV2_REMOTE;
  const localMode = invoke(436);
  assert.equal(localMode, "local");
  const dependencies = {
    1: sourceFs, 3: { Readable }, 4: EventEmitter, 5: path,
    11: {}, 32: { spawn: observedSpawn },
    434: () => { throw new Error("unexpected local destroy in a completed-child case"); },
    436: localMode, 856: {}, 195: new EventEmitter(),
  };
  const LocalFfmpeg = invoke(855, dependencies);
  const binaryScript = 'process.stdout.write(Buffer.from([0,79,85,84,255,66,73,78]));process.stderr.write(Buffer.from([0,69,82,82,254]));process.exitCode=23;';
  const binary = new LocalFfmpeg({ ffmpeg: process.execPath,
    args: ["-e", binaryScript], track: "binary", mediaURL: "http://fixture/binary", flowingMode: true });
  const exited = once(binary.events, "exit");
  await binary.create();
  const output = [];
  binary.stream.on("data", chunk => output.push(chunk));
  binary.stream.resume();
  const closed = once(binary.convertProcess, "close");
  await exited;
  await closed;
  assert.equal(binary.exitCode, 23);
  assert.equal(binary.signalCode, null);
  assert.deepEqual(Buffer.concat(output), Buffer.from([0,79,85,84,255,66,73,78]));
  const stderrPath = path.join(root, "ffmpeg_binary_%2Fbinary.log");
  assert.deepEqual(fs.readFileSync(stderrPath), Buffer.from([0,69,82,82,254]));
  assert.equal(spawnCalls[0].args.at(-1), "pipe:1");
  assert.equal(spawnCalls[0].detached, true);
  const savedEnvironment = process.env.M00_CHILD_ENV;
  process.env.M00_CHILD_ENV = "trace-environment";
  try {
    const environment = new LocalFfmpeg({ ffmpeg: process.execPath,
      args: ["-e", 'process.stdout.write(process.env.M00_CHILD_ENV || "missing");'],
      track: "environment", mediaURL: "http://fixture/environment", flowingMode: true });
    await environment.create();
    const envOutput = [];
    environment.stream.on("data", chunk => envOutput.push(chunk));
    environment.stream.resume();
    await once(environment.convertProcess, "close");
    assert.equal(Buffer.concat(envOutput).toString(), "trace-environment");
  } finally {
    if (savedEnvironment === undefined) delete process.env.M00_CHILD_ENV;
    else process.env.M00_CHILD_ENV = savedEnvironment;
  }
  emit("M00-02 process", "argv-environment-preserved");
  emit("M00-02 output", "callbacks-complete");
  emit("M00-02 exit", binary.exitCode);
  details.source_only.local_stderr = "M855 debug-file sink; native trace uses callbacks. Equal bytes, not equal callback APIs.";
  details.source_only.local_spawn = spawnCalls.map(({ program, ...call }) => ({ ...call,
    args: call.args.map(arg => arg.includes("process.") ? "<scripted-child>" : arg), program: "<node-fixture>" }));

  // Exercise the actual remote driver over deterministic transport ports.
  // The virtual clock preserves and asserts the source's 500ms delays.
  oracleProcess.env.HLSV2_REMOTE = "1";
  const remoteMode = invoke(436);
  assert.equal(remoteMode, "remote");
  const dispatches = [];
  const cancellations = [];
  const active = new Set();
  const bridge = new EventEmitter();
  bridge.dispatch = (event, data) => {
    if (event === "ffmpeg") {
      dispatches.push({ ...data, args: Array.from(data.args) });
      active.add(data.id);
    } else if (event === "ffmpeg:cancel") {
      cancellations.push(data.id);
      active.delete(data.id);
    } else throw new Error(`unexpected bridge dispatch ${event}`);
  };
  const requests = [];
  let failNextPoll = false;
  const httpPort = {
    request(url, onResponse) {
      const req = new EventEmitter();
      req.url = url;
      req.destroyed = false;
      req.destroy = () => { req.destroyed = true; };
      req.end = () => queueMicrotask(() => {
        if (failNextPoll) {
          failNextPoll = false;
          req.emit("error", new Error("controlled pending conversion"));
          return;
        }
        const response = new PassThrough();
        streams.add(response);
        req.response = response;
        onResponse(response);
      });
      requests.push(req);
      return req;
    },
  };
  let reserveSelection = null;
  const reserves = [];
  const RemoteFfmpeg = invoke(855, {
    ...dependencies, 436: remoteMode, 195: bridge, 11: httpPort,
    856: { async getPortPromise({ port }) {
      reserves.push(port);
      if (reserveSelection === null) throw new Error("controlled port-search failure");
      return reserveSelection;
    } },
  });
  const options = () => ({ ffmpeg: "ffmpeg", args: ["-i", "trace media.mp4"],
    track: "remote", mediaURL: "http://fixture/remote", flowingMode: true });
  async function createRemote(instance, retry = false) {
    const created = instance.create();
    await tick();
    const originalTimer = instance.polling;
    advanceTimer(originalTimer);
    await tick();
    if (retry) {
      assert.notEqual(instance.polling, originalTimer);
      advanceTimer(instance.polling);
      await tick();
    }
    return created;
  }
  const fallback = new RemoteFfmpeg(options());
  failNextPoll = true;
  await createRemote(fallback, true);
  assert.equal(fallback.mode, "remote");
  assert.equal(fallback.port, reserves[0]);
  assert.deepEqual(dispatches[0].args.slice(0, 4), ["-i", "trace media.mp4", "-listen", "1"]);
  assert(dispatches[0].args[4].startsWith(`http://127.0.0.1:${reserves[0]}/`));
  assert.equal(requests.length, 2, "failed poll must schedule one retry");
  assert.equal(requests[1].url, dispatches[0].args[4]);
  let exitEvents = 0;
  fallback.events.on("exit", () => { exitEvents++; });
  bridge.emit("ffmpeg:result", { id: "unrelated", error: null });
  assert.equal(exitEvents, 0);
  bridge.emit("ffmpeg:result", { id: fallback.id, error: null });
  assert.equal(exitEvents, 1);
  assert.equal(fallback.exitCode, 0);
  emit("M00-03 mode", "local-remote-preserved");
  emit("M00-03 port-search", "dispatch-requested-port");
  fallback.destroy();
  assert.equal(fallback.request.destroyed, true);
  assert.equal(fallback.stream.destroyed, true);

  reserveSelection = 40301;
  const first = new RemoteFfmpeg(options());
  const second = new RemoteFfmpeg(options());
  await createRemote(first);
  reserveSelection++;
  await createRemote(second);
  assert.notEqual(first.id, second.id);
  const beforeCancel = cancellations.length;
  first.destroy();
  assert.equal(cancellations.length, beforeCancel + 1);
  assert.equal(cancellations.at(-1), first.id);
  assert(!active.has(first.id) && active.has(second.id));
  assert.equal(second.stream.destroyed, false);
  emit("M00-03 bridge.concurrent", "unique");
  emit("M00-03 bridge.remaining", "one");
  second.destroy();
  bridge.emit("ffmpeg:result", { id: first.id });
  bridge.emit("ffmpeg:result", { id: second.id });

  const callback = new RemoteFfmpeg(options());
  await createRemote(callback);
  const callbackBefore = cancellations.length;
  let outputCallbacks = 0;
  callback.stream.once("data", () => { outputCallbacks++; callback.destroy(); });
  callback.stream.resume();
  callback.stream.write(Buffer.from("trace-callback"));
  await tick();
  assert.equal(outputCallbacks, 1);
  assert.equal(cancellations.length, callbackBefore + 1);
  assert.equal(cancellations.at(-1), callback.id);
  assert.equal(callback.request.destroyed, true);
  assert.equal(callback.stream.destroyed, true);
  emit("M00-03 callback-cancel", "one");
  // Source destroy does not remove this listener. Complete it explicitly.
  details.source_only.remote_listener_after_destroy = bridge.listenerCount("ffmpeg:result");
  bridge.emit("ffmpeg:result", { id: callback.id });
  assert.equal(bridge.listenerCount("ffmpeg:result"), 0);
  assert.equal(active.size, 0);
  assert.equal(timers.size, 0);
  details.source_only.remote_polls = requests.length;
  details.source_only.callback_cancel_boundary = "Compares one bridge cancellation and closed streams only; native finish-once/no-ready checks remain native ownership assertions.";

  // M861 is a different wrapper. Its fixed argv, close event and remote data
  // path are source-observed separately, not falsely mapped onto M855.
  const probeCalls = [];
  const killCalls = [];
  const probeChild = new EventEmitter();
  probeChild.pid = 424242;
  probeChild.stdout = new PassThrough();
  streams.add(probeChild.stdout);
  const probeBridge = new EventEmitter();
  probeBridge.dispatch = (event, data) => probeCalls.push({ event, ...data });
  const Ffprobe = invoke(861, { ...dependencies, 436: remoteMode, 195: probeBridge,
    434: (pid, signal) => killCalls.push({ pid, signal }),
    32: { spawn(program, args, opts) {
      probeCalls.push({ program, args: Array.from(args), opts });
      return probeChild;
    } },
  });
  const localProbe = new Ffprobe({ ffprobe: "probe-fixture", mediaURL: "http://fixture/probe" }, "local");
  assert.equal(localProbe.mode, "local", "M861 forced mode precedes environment");
  localProbe.create();
  assert.equal(probeCalls[0].program, "probe-fixture");
  assert.equal(probeCalls[0].args.length, 5);
  assert.equal(probeCalls[0].args[0], "-show_entries");
  assert.equal(probeCalls[0].args[2], "-print_format");
  assert.equal(probeCalls[0].args[3], "json");
  assert.equal(probeCalls[0].args[4], "http://fixture/probe");
  const probeClose = once(localProbe.events, "close");
  probeChild.emit("close", 23, null);
  assert.deepEqual(await probeClose, [23, null]);
  localProbe.destroy();
  assert.deepEqual(killCalls, [{ pid: 424242, signal: "SIGKILL" }]);
  const remoteProbe = new Ffprobe({ ffprobe: "probe-fixture", mediaURL: "http://fixture/probe" });
  const remoteProbeCreated = remoteProbe.create();
  const probeDispatch = probeCalls.at(-1);
  assert.equal(probeDispatch.event, "ffprobe");
  const probeBytes = Buffer.from([0, 255, 80, 82, 79, 66, 69]);
  probeBridge.emit("ffprobe:result", { id: probeDispatch.id, data: probeBytes });
  await remoteProbeCreated;
  const remoteProbeOutput = [];
  for await (const chunk of remoteProbe.stream) remoteProbeOutput.push(chunk);
  assert.deepEqual(Buffer.concat(remoteProbeOutput), probeBytes);
  const remoteProbeClose = once(remoteProbe.events, "close");
  assert.equal(timers.size, 1);
  advanceTimer(timers.keys().next().value);
  assert.deepEqual(await remoteProbeClose, [0, null]);
  const callsBeforeDestroy = probeCalls.length;
  remoteProbe.destroy();
  assert.equal(probeCalls.length, callsBeforeDestroy, "M861 remote destroy is source no-op");
  assert.equal(new RemoteFfmpeg(options(), "local").mode, "remote",
    "M855 environment mode precedes forced mode");
  details.source_only.ffprobe = { argv: probeCalls[0].args, local_close: [23, null],
    remote_bytes_hex: probeBytes.toString("hex"), remote_close: [0, null],
    remote_destroy: "no-op", forced_mode_precedence: "forced mode first" };
  details.source_only.ffmpeg_forced_mode_precedence = "environment mode first";
  const exported = invoke(433, { 855: LocalFfmpeg, 861: Ffprobe });
  assert.equal(exported.ffmpeg, LocalFfmpeg);
  assert.equal(exported.ffprobe, Ffprobe);
  details.executed_modules = [433, 434, 436, 667, 855, 861];
  details.identity_only_modules = [564];
  const killCommands = [];
  const kill = invoke(434, { 32: { spawn() { throw new Error("unexpected non-Windows tree traversal"); },
    exec(command, callback) { killCommands.push(command); if (callback) callback(null); } } });
  if (process.platform === "win32") {
    kill(424242, "SIGKILL");
    assert.equal(killCommands.length, 1);
    assert(/taskkill/i.test(killCommands[0]) && /424242/.test(killCommands[0]));
    assert(/\/T/i.test(killCommands[0]) && /\/F/i.test(killCommands[0]));
  }
  details.source_only.tree_kill_command = killCommands;
  details.injected_boundaries = ["real temporary filesystem", "real Node children for M855 local spawn",
    "M855 bridge and HTTP port fixtures", "explicit virtual 500ms timers",
    "M861 observed spawn boundary", "M434 observed taskkill boundary; no arbitrary PID killed"];
  assert.equal(timers.size, 0);
  assert.equal(trace.length, 11);
  if (process.argv.includes("--json"))
    console.log(JSON.stringify({ oracle_sha256: expectedSha, trace_lines: trace, details }, null, 2));
  else console.log(trace.join("\n"));
} finally {
  clearTimeout(watchdog);
  for (const child of children) { child.kill(); await once(child, "close").catch(() => {}); }
  for (const stream of streams) stream.destroy();
  for (const fd of openFiles) fs.closeSync(fd);
  timers.clear();
  fs.rmSync(root, { recursive: true, force: true });
}
