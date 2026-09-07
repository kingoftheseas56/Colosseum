import crypto from "node:crypto";
import { EventEmitter } from "node:events";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const scenarioDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scenarioDirectory, "../../../../..");
// Set K13_B_ORACLE_PATH to the exact oracle mirror when this worktree has no
// adjacent ../oracle directory. The adjacent path remains a convenience for
// the review mirror and is still checked by the same SHA-256 guard below.
const configuredOraclePath = process.env.K13_B_ORACLE_PATH?.trim();
const adjacentOraclePath = path.resolve(repositoryRoot, "..", "oracle", "server.js");
const oraclePath = configuredOraclePath
  ? path.resolve(configuredOraclePath)
  : adjacentOraclePath;
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
  if (source.indexOf(entrypoint) === -1) {
    throw new Error("oracle webpack entrypoint was not found");
  }

  const hostSetTimeout = globalThis.setTimeout;
  const hostClearTimeout = globalThis.clearTimeout;
  const oracleProcess = {
    platform: "win32",
    env: Object.create(null),
  };
  const context = vm.createContext({
    Buffer,
    console: {
      error() {},
      log() {},
      warn() {},
    },
    process: oracleProcess,
    setTimeout(callback, delay, ...args) {
      // Keep the oracle's ten-second debounce semantics while making this
      // controlled trace bounded. The candidate uses the same latest-value
      // input with a short injected delay in its native test.
      return hostSetTimeout(callback, delay >= 10000 ? 20 : delay, ...args);
    },
    clearTimeout: hostClearTimeout,
    setInterval: globalThis.setInterval,
    clearInterval: globalThis.clearInterval,
  });

  const instrumented = source.replace(
    entrypoint,
    '__webpack_require__.p = "", globalThis.__K13_B_ORACLE__ = { modules, require: __webpack_require__ };',
  );
  vm.runInContext(instrumented, context, { filename: oraclePath });
  const oracle = context.__K13_B_ORACLE__;
  if (!oracle || !Array.isArray(oracle.modules)) {
    throw new Error("oracle module table was not exposed");
  }
  return { context, oracle, oracleProcess };
}

function invokeOracleModule(oracle, moduleId, overrides = {}) {
  const module = { exports: {} };
  const dependencyRequire = dependencyId => {
    if (Object.prototype.hasOwnProperty.call(overrides, dependencyId)) {
      return overrides[dependencyId];
    }
    return oracle.require(dependencyId);
  };
  oracle.modules[moduleId].call(
    module.exports,
    module,
    module.exports,
    dependencyRequire,
  );
  return module.exports;
}

function extractAssignedFunction(moduleSource, assignment, terminator) {
  const start = moduleSource.indexOf(assignment);
  if (start === -1) {
    throw new Error(`oracle assignment not found: ${assignment}`);
  }
  const expressionStart = start + assignment.length;
  const end = moduleSource.indexOf(terminator, expressionStart);
  if (end === -1) {
    throw new Error(`oracle assignment terminator not found: ${assignment}`);
  }
  return moduleSource.slice(expressionStart, end).trim();
}

function makeSettingsFs() {
  const settingsFs = Object.create(fs);
  settingsFs.writeFile = (filePath, contents, callback) => {
    fs.writeFileSync(filePath, contents);
    if (callback) callback(null);
  };
  return settingsFs;
}

function loadOracleSettings(oracle, oracleProcess, root, disableCaching) {
  const settingsPath = path.join(root, disableCaching ? "no-cache" : "fresh");
  fs.mkdirSync(settingsPath, { recursive: true });
  fs.writeFileSync(path.join(settingsPath, "server-settings.json"), "{}");

  const previousEnvironment = oracleProcess.env;
  oracleProcess.env = {
    SETTINGS_PATH: settingsPath,
    ...(disableCaching ? { DISABLE_CACHING: "1" } : {}),
  };
  try {
    const isPositiveInteger = invokeOracleModule(oracle, 400);
    return invokeOracleModule(oracle, 106, {
      0: {
        _extend: Object.assign,
      },
      1: makeSettingsFs(),
      5: path,
      194: settingsPath,
      400: isPositiveInteger,
      413: { version: "4.21.0" },
    });
  } finally {
    oracleProcess.env = previousEnvironment;
  }
}

function manifestEntry(root, name, size, atime, activeEngine = false) {
  return {
    root: path.join(root, activeEngine ? "active-engine" : "inactive-engine"),
    file: {
      name,
      size,
      atime: new Date(atime),
      mtime: new Date(atime),
    },
  };
}

function makeOracleCacheApi(oracle, traceState) {
  const sourceFs = Object.create(fs);
  sourceFs.unlink = (filePath, callback) => {
    traceState.deletedNames.push(path.basename(filePath));
    if (callback) callback(null);
  };

  const enginefs = {
    list: () => traceState.activeEngineIds,
    getCachePath: () => traceState.delayedCachePath,
  };
  const walker = {
    walk() {
      const events = new EventEmitter();
      const manifest = traceState.manifest;
      queueMicrotask(() => {
        for (const item of manifest) {
          events.emit("file", item.root, item.file, () => {});
        }
        events.emit("end");
      });
      return events;
    },
  };
  const child = {
    exec(_command, callback) {
      const response = traceState.diskResponses.shift() ?? null;
      callback(null, response ?? "Caption FreeSpace Size\r\n", "");
    },
  };
  const once = callback => {
    let called = false;
    return (...args) => {
      if (called) return;
      called = true;
      callback(...args);
    };
  };

  return invokeOracleModule(oracle, 414, {
    1: sourceFs,
    5: path,
    22: os,
    32: child,
    35: once,
    172: enginefs,
    806: walker,
  });
}

function runOracleClear(cacheApi, traceState, manifest, toSize, requiredSize, diskResponse) {
  traceState.manifest = manifest;
  traceState.deletedNames = [];
  traceState.diskResponses = [diskResponse];
  return new Promise((resolve, reject) => {
    try {
      cacheApi.clearCache(
        manifest[0]?.root ?? traceState.delayedCachePath,
        toSize,
        requiredSize,
        result => resolve(result),
      );
    } catch (error) {
      reject(error);
    }
  });
}

async function main() {
  const { context, oracle, oracleProcess } = requireOracleMirror();
  const traceRoot = fs.mkdtempSync(path.join(os.tmpdir(), "server1-k13-b-oracle-"));
  const traceState = {
    activeEngineIds: ["active-engine"],
    delayedCachePath: path.join(traceRoot, "delayed-cache"),
    deletedNames: [],
    diskResponses: [],
    manifest: [],
  };

  try {
    const freshSettings = loadOracleSettings(oracle, oracleProcess, traceRoot, false);
    console.log(`K13-02 default.cacheSize=${freshSettings.cacheSize}`);
    console.log(`K13-02 default.btMaxConnections=${freshSettings.btMaxConnections}`);

    const noCacheSettings = loadOracleSettings(oracle, oracleProcess, traceRoot, true);
    const module564Source = oracle.modules[564].toString();
    const isPositiveInteger = invokeOracleModule(oracle, 400);
    const getDefaultsSource = extractAssignedFunction(
      module564Source,
      "enginefs.getDefaults = ",
      ";\n        const rarHttp",
    );
    const getDefaults = Function(
      "isPositiveInteger",
      "settings",
      "argv",
      "defaultTrackers",
      `return (${getDefaultsSource});`,
    )(isPositiveInteger, noCacheSettings, { noCache: false }, []);
    const noCacheDefaults = getDefaults("TRACE_ENGINE");
    console.log(`K13-02 noCache.buffer=${noCacheDefaults.buffer}`);
    console.log(
      `K13-02 noCache.circularBuffer.size=${noCacheDefaults.circularBuffer.size}`,
    );

    const cacheApi = makeOracleCacheApi(oracle, traceState);
    const equalRoot = path.join(traceRoot, "equal-atime");
    const equalResult = await runOracleClear(
      cacheApi,
      traceState,
      [
        manifestEntry(equalRoot, "active.bin", 4, 300, true),
        manifestEntry(equalRoot, "newest", 4, 200),
        manifestEntry(equalRoot, "equal-atime-a", 4, 100),
        manifestEntry(equalRoot, "equal-atime-b", 4, 100),
      ],
      4,
      0,
      null,
    );
    if (equalResult.deleted !== 3) {
      throw new Error(`unexpected oracle deletion count: ${equalResult.deleted}`);
    }
    if (traceState.deletedNames.includes("active.bin")) {
      throw new Error("oracle selected the active engine file");
    }
    console.log(`K13-03 equal-atime-order=${traceState.deletedNames.join(",")}`);
    console.log(`K13-03 active-omitted=${traceState.deletedNames.includes("active.bin") ? 0 : 1}`);

    const diskRoot = path.join(traceRoot, "disk-adjustment");
    const diskResult = await runOracleClear(
      cacheApi,
      traceState,
      [
        manifestEntry(diskRoot, "active.bin", 4, 300, true),
        manifestEntry(diskRoot, "old.bin", 4, 200),
        manifestEntry(diskRoot, "new.bin", 4, 100),
      ],
      10,
      4,
      "Caption FreeSpace Size\r\nC:\t1\t100\r\n",
    );
    console.log(`K13-03 disk-target=${diskResult.to}`);

    traceState.manifest = [];
    traceState.deletedNames = [];
    traceState.diskResponses = [null];
    const delayedLatest = await new Promise((resolve, reject) => {
      try {
        cacheApi.setOptionValues({ cacheSize: 2147483648 });
        cacheApi.setOptionValues({ cacheSize: 5368709120 }, result => resolve(result.to));
      } catch (error) {
        reject(error);
      }
    });
    console.log(`K13-03 delayed-latest=${delayedLatest}`);

    const getCachePathSource = extractAssignedFunction(
      module564Source,
      "enginefs.getCachePath = ",
      ", enginefs.getDefaults =",
    );
    let mkdirCalls = 0;
    const getCachePath = Function(
      "path",
      "settings",
      "mkdirp",
      "os",
      `return (${getCachePathSource});`,
    )(
      path,
      { cacheRoot: path.join(traceRoot, "unwritable-cache-root") },
      {
        sync() {
          mkdirCalls += 1;
          if (mkdirCalls === 1) throw new Error("controlled cache-root failure");
        },
      },
      os,
    );
    const fallbackPath = getCachePath("fallback-engine");
    const fallbackUsesTemp = fallbackPath.startsWith(os.tmpdir());
    if (!fallbackUsesTemp) {
      throw new Error(`oracle fallback path escaped os.tmpdir(): ${fallbackPath}`);
    }
    console.log(`K13-03 fallback-temp=${Number(fallbackUsesTemp)}`);
    void context;
  } finally {
    fs.rmSync(traceRoot, { recursive: true, force: true });
  }
}

await main();
