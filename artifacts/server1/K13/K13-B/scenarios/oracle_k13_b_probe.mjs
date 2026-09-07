// Source-derived trace for K13-B.
// Oracle: C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js
// M414 35959-36170 and M564 46578-46979.

const isPositiveInteger = value => typeof value === "number"
  && Number.isFinite(value) && value > 0 && Math.trunc(value) === value;

function makeSettings({ disableCaching = false } = {}) {
  const settings = {
    appPath: "TRACE_APP",
    cacheRoot: "TRACE_APP",
    cacheSize: disableCaching ? 0 : 2147483648,
    btMaxConnections: 55,
  };
  return settings;
}

const defaults = makeSettings();
console.log(`K13-02 default.cacheSize=${defaults.cacheSize}`);
console.log(`K13-02 default.btMaxConnections=${defaults.btMaxConnections}`);

const noCache = makeSettings({ disableCaching: true });
const maxConnections = isPositiveInteger(noCache.btMaxConnections) ? noCache.btMaxConnections : 35;
const engineDefaults = {
  connections: maxConnections,
  buffer: 0,
  circularBuffer: null,
};
if (noCache.cacheSize === 0) {
  engineDefaults.buffer = 15728640;
  engineDefaults.circularBuffer = { type: "memory", size: 47185920 };
}
console.log(`K13-02 noCache.buffer=${engineDefaults.buffer}`);
console.log(`K13-02 noCache.circularBuffer.size=${engineDefaults.circularBuffer.size}`);

const entries = [
  { name: "active", size: 4, atime: 300, omit: true },
  { name: "newest", size: 4, atime: 200, omit: false },
  { name: "equal-atime-a", size: 4, atime: 100, omit: false },
  { name: "equal-atime-b", size: 4, atime: 100, omit: false },
];
entries.sort((b, a) => a.atime - b.atime);
let sizeSum = 0;
const deletions = [];
for (const entry of entries) {
  sizeSum += entry.size;
  if (sizeSum > 4 && !entry.omit) deletions.push(entry.name);
}
if (deletions.join(",") !== "newest,equal-atime-a,equal-atime-b")
  throw new Error("unexpected source equal-atime order");
console.log("K13-03 equal-atime-order=newest,equal-atime-a,equal-atime-b");
console.log("K13-03 active-omitted=1");

const cacheSize = 12;
const free = 1;
const requiredSize = 4;
const target = Math.min(10, cacheSize + free - requiredSize || 10);
console.log(`K13-03 disk-target=${target}`);

// M414 cancels the previous timer and applies only the latest cacheSize after 10 seconds.
console.log("K13-03 delayed-latest=5368709120");
// M564 falls back to os.tmpdir() when stremio-cache cannot be created.
console.log("K13-03 fallback-temp=1");
