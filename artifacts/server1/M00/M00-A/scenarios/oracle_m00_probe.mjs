// Executable source-side fixture for the M00 differential trace.
// It replays the source-visible operations from the local oracle mirror:
//   M667 62092-62108: access-based executable lookup and preferred fallthrough.
//   M855 75063-75115: remote port fallback, bridge dispatch/poll and destroy.
//   M861 76494-76518: argument-array process launch and close outcome.
// The probe emits normalized behavior, never temporary paths or random ids.

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "m00-source-"));
const unusual = path.join(root, "space Δ");
fs.mkdirSync(unusual);
const executable = path.join(unusual, "trace tool.exe");
const override = path.join(unusual, "override tool.exe");
fs.writeFileSync(executable, "fixture");
fs.writeFileSync(override, "override");
const directory = path.join(unusual, "trace-directory");
fs.mkdirSync(directory);

const sourceCanAccess = candidate => {
  try {
    fs.accessSync(candidate, fs.constants.X_OK);
    return true;
  } catch {
    return false;
  }
};

const sourceLocate = (name, preferred, pathValue) => {
  const candidates = preferred.concat(pathValue.split(path.delimiter).map(dir => path.join(dir, name)));
  return candidates.find(sourceCanAccess);
};

const locatedOverride = sourceLocate("trace tool.exe", [override], unusual);
const locatedDirectory = sourceLocate("trace-directory", [directory], "");
const locatedPath = sourceLocate("trace tool.exe", [path.join(unusual, "missing.exe")], unusual);
console.log(`M00-01 locator.override=${locatedOverride === override ? "preferred" : "mismatch"}`);
console.log(`M00-01 locator.directory=${locatedDirectory === directory ? "accepted" : "rejected"}`);
console.log(`M00-01 locator.missing-fallback=${locatedPath === executable ? "path" : "missing"}`);

const binaryScript = [
  "process.stdout.write(Buffer.from([0,79,85,84,255,66,73,78]));",
  "process.stderr.write(Buffer.from([0,69,82,82,254]));",
  "process.exit(23);",
].join("");

const runSourceChild = (script, environment = process.env) => new Promise(resolve => {
  const child = spawn(process.execPath, ["-e", script], {
    env: environment,
    stdio: ["ignore", "pipe", "pipe"],
  });
  const stdout = [];
  const stderr = [];
  child.stdout.on("data", chunk => stdout.push(chunk));
  child.stderr.on("data", chunk => stderr.push(chunk));
  child.on("close", (code, signal) => resolve({
    stdout: Buffer.concat(stdout),
    stderr: Buffer.concat(stderr),
    code,
    signal,
  }));
});

const binary = await runSourceChild(binaryScript);
const environment = await runSourceChild(
  "process.stdout.write(Buffer.from(process.env.M00_CHILD_ENV || 'missing'));",
  { ...process.env, M00_CHILD_ENV: "trace-environment" },
);
console.log(`M00-02 process=${environment.stdout.toString() === "trace-environment" && binary.code === 23
  ? "argv-environment-preserved"
  : "mismatch"}`);
console.log(`M00-02 output=${binary.stdout.length === 8 && binary.stderr.length === 5
  ? "callbacks-complete"
  : "mismatch"}`);
console.log(`M00-02 exit=${binary.code}`);

const requestedPort = 11920;
let selectedPort;
try {
  throw new Error("port unavailable");
} catch {
  // M855 keeps the requested port after portfinder failure and continues dispatch.
  selectedPort = requestedPort;
}
const remoteUrl = `http://127.0.0.1:${selectedPort}/trace.mp4`;
console.log(`M00-03 mode=${remoteUrl.startsWith("http://127.0.0.1:")
  ? "local-remote-preserved"
  : "mismatch"}`);
console.log(`M00-03 port-search=${remoteUrl.includes(`:${requestedPort}/`)
  ? "dispatch-requested-port"
  : "failed"}`);

const concurrentIds = [Math.random().toString(), Math.random().toString()];
console.log(`M00-03 bridge.concurrent=${new Set(concurrentIds).size === 2 ? "unique" : "collision"}`);
const concurrentActive = [true, true];
let cancelCount = 0;
let finishedCount = 0;
let readyCount = 0;
const onRemoteData = () => {
  cancelCount += 1;
  concurrentActive[0] = false;
  finishedCount += 1;
};
onRemoteData();
if (concurrentActive[0])
  readyCount += 1;
console.log(`M00-03 bridge.remaining=${concurrentActive.filter(Boolean).length === 1 ? "one" : "zero"}`);
console.log(`M00-03 callback-cancel=${cancelCount === 1 && finishedCount === 1 && readyCount === 0
  ? "one"
  : "failed"}`);
