import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const scenarioDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scenarioDirectory, "../../../../..");
const configuredOraclePath = process.env.K13_A_ORACLE_PATH?.trim();
const adjacentOraclePath = path.resolve(repositoryRoot, "..", "oracle", "server.js");
const planningPackOraclePath = path.resolve(
  repositoryRoot,
  "..",
  "..",
  "Colosseum-Server-1.0-Planning-Pack",
  "oracle",
  "stremio-service-v4.21.1-server-bundle",
  "server.js",
);
const oraclePath = configuredOraclePath
  ? path.resolve(configuredOraclePath)
  : fs.existsSync(adjacentOraclePath)
    ? adjacentOraclePath
    : planningPackOraclePath;
const expectedOracleSha256 =
  "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f";

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function requireOracleMirror() {
  const oracleBytes = fs.readFileSync(oraclePath);
  const actualOracleSha256 = sha256(oracleBytes);
  if (actualOracleSha256 !== expectedOracleSha256) {
    throw new Error(
      `oracle SHA-256 mismatch: ${actualOracleSha256} != ${expectedOracleSha256}`,
    );
  }

  const source = oracleBytes.toString("utf8");
  const entrypoint =
    '__webpack_require__.p = "", __webpack_require__(__webpack_require__.s = 564);';
  if (!source.includes(entrypoint)) {
    throw new Error("oracle webpack entrypoint was not found");
  }

  const oracleProcess = { platform: "win32", env: Object.create(null) };
  const context = vm.createContext({
    Buffer,
    console: { error() {}, log() {}, warn() {} },
    process: oracleProcess,
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  });

  const instrumented = source.replace(
    entrypoint,
    '__webpack_require__.p = "", globalThis.__K13_A_ORACLE__ = { modules, require: __webpack_require__ };',
  );
  vm.runInContext(instrumented, context, { filename: oraclePath });
  const oracle = context.__K13_A_ORACLE__;
  if (!oracle || !Array.isArray(oracle.modules)) {
    throw new Error("oracle module table was not exposed");
  }
  return { oracle, oracleProcess, actualOracleSha256 };
}

function invokeOracleModule(oracle, moduleId, overrides = {}) {
  const module = { exports: {} };
  const dependencyRequire = dependencyId =>
    Object.prototype.hasOwnProperty.call(overrides, dependencyId)
      ? overrides[dependencyId]
      : oracle.require(dependencyId);
  oracle.modules[moduleId].call(
    module.exports,
    module,
    module.exports,
    dependencyRequire,
  );
  return module.exports;
}

function makeSettingsFs() {
  const settingsFs = Object.create(fs);
  settingsFs.writeFile = (filePath, contents, callback) => {
    fs.writeFileSync(filePath, contents);
    if (callback) callback(null);
  };
  return settingsFs;
}

function loadOracleSettings(oracle, oracleProcess, settingsPath, disableCaching) {
  fs.mkdirSync(settingsPath, { recursive: true });
  const settingsFile = path.join(settingsPath, "server-settings.json");
  if (!fs.existsSync(settingsFile)) fs.writeFileSync(settingsFile, "{}");

  const previousEnvironment = oracleProcess.env;
  oracleProcess.env = {
    SETTINGS_PATH: settingsPath,
    ...(disableCaching ? { DISABLE_CACHING: "1" } : {}),
  };
  try {
    const isPositiveInteger = invokeOracleModule(oracle, 400);
    return invokeOracleModule(oracle, 106, {
      0: { _extend: Object.assign },
      1: makeSettingsFs(),
      5: path,
      194: settingsPath,
      400: isPositiveInteger,
    });
  } finally {
    oracleProcess.env = previousEnvironment;
  }
}

function requireEqual(actual, expected, message) {
  if (!Object.is(actual, expected)) {
    throw new Error(`${message}: ${String(actual)} != ${String(expected)}`);
  }
}
const { oracle, oracleProcess, actualOracleSha256 } = requireOracleMirror();
const traceRoot = fs.mkdtempSync(path.join(os.tmpdir(), "server1-k13-a-oracle-"));
try {
  const defaultsDir = path.join(traceRoot, "defaults");
  fs.mkdirSync(defaultsDir, { recursive: true });
  fs.writeFileSync(path.join(defaultsDir, "server-settings.json"), "{}");
  const defaults = loadOracleSettings(oracle, oracleProcess, defaultsDir, false);
  requireEqual(defaults.serverVersion, "4.21.0", "embedded serverVersion");
  requireEqual(defaults.cacheSize, 2147483648, "default cacheSize");
  requireEqual(defaults.btMaxConnections, 55, "default btMaxConnections");
  requireEqual(defaults.remoteHttps, "", "default remoteHttps");
  requireEqual(defaults.transcodeProfile, null, "default transcodeProfile");

  const noCacheDir = path.join(traceRoot, "no-cache");
  fs.mkdirSync(noCacheDir, { recursive: true });
  fs.writeFileSync(path.join(noCacheDir, "server-settings.json"), "{}");
  const noCache = loadOracleSettings(oracle, oracleProcess, noCacheDir, true);
  requireEqual(noCache.cacheSize, 0, "DISABLE_CACHING cacheSize");

  console.log(`K13-01 default.cacheSize=${defaults.cacheSize}`);
  console.log(`K13-01 default.btMaxConnections=${defaults.btMaxConnections}`);
  console.log(`K13-01 default.remoteHttps=${defaults.remoteHttps}`);
  console.log(`K13-01 default.transcodeProfile=${JSON.stringify(defaults.transcodeProfile)}`);

  const loadedDir = path.join(traceRoot, "loaded");
  fs.mkdirSync(loadedDir, { recursive: true });
  fs.writeFileSync(
    path.join(loadedDir, "server-settings.json"),
    JSON.stringify({
      serverVersion: "9.9.9",
      cacheSize: 0,
      btMaxConnections: 99,
      unknownKey: "retained",
    }),
  );
  const loaded = loadOracleSettings(oracle, oracleProcess, loadedDir, false);
  requireEqual(loaded.serverVersion, "4.21.0", "readonly loaded serverVersion");
  console.log(`K13-01 load.cacheSize=${loaded.cacheSize}`);
  console.log(`K13-01 load.btMaxConnections=${loaded.btMaxConnections}`);
  console.log(`K13-01 load.unknownKey=${loaded.unknownKey}`);
  console.log(`K13-01 load.serverVersion=${loaded.serverVersion}`);

  loaded.extend({ unknownKey: "overridden", newUnknown: true });
  loaded.save(() => {});
  const persistedPath = path.join(loadedDir, "server-settings.json");
  const persisted = JSON.parse(fs.readFileSync(persistedPath, "utf8"));
  console.log(`K13-03 persisted.unknownKey=${persisted.unknownKey}`);
  console.log(`K13-03 persisted.newUnknown=${persisted.newUnknown}`);

  loaded.cacheSize = Infinity;
  loaded.save(() => {});
  const nonfinite = JSON.parse(fs.readFileSync(persistedPath, "utf8"));
  console.log(`K13-03 infinite-null=${Number(nonfinite.cacheSize === null)}`);

  if (actualOracleSha256 !== expectedOracleSha256) {
    throw new Error("unreachable oracle SHA mismatch");
  }
} finally {
  fs.rmSync(traceRoot, { recursive: true, force: true });
}
