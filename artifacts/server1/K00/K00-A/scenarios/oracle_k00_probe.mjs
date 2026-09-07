// This probe replays only the JavaScript expressions exercised by the cited oracle lines.
// It is not a second implementation of the server and is not a production dependency.
// Oracle: C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js
// M106 12792-12833; M172 18110-18500; M176 18515-18561; M303 27748-27805;
// M400 34767-34775; M814 72439-72764; M846 74730-74772.

const show = value => Number.isNaN(value) ? "NaN" : String(value);

const settingsBase = {
  get serverVersion() {
    return "4.21.0";
  },
  set serverVersion(_) {},
  known: 1,
};

const settingsExtension = {
  unknown: "retained",
  known: 2,
  serverVersion: "9.9.9",
};

const sourceSettings = { ...settingsBase };
Object.defineProperty(sourceSettings, "serverVersion", {
  configurable: true,
  enumerable: true,
  get() {
    return "4.21.0";
  },
  set(_) {},
});
for (const key of Object.keys(settingsExtension))
  sourceSettings[key] = settingsExtension[key];

const wire = {
  missing: undefined,
  null: null,
  false: false,
  zero: 0,
  empty: "",
  nan: NaN,
  array: [undefined, null],
};

console.log(`K00-01 Number(missing)=${show(Number(undefined))}`);
console.log(`K00-01 Number(null)=${show(Number(null))}`);
console.log(`K00-01 Number(false)=${show(Number(false))}`);
console.log(`K00-01 Number(0)=${show(Number(0))}`);
console.log(`K00-01 Number(empty)=${show(Number(""))}`);
console.log(`K00-01 Number(number-string)=${show(Number("42.5"))}`);
console.log(`K00-01 Number(NaN-like)=${show(Number("NaN-like"))}`);
console.log(`K00-01 parseInt(trailing)=${show(parseInt("12trailing"))}`);
console.log(`K00-01 priority(0)||1=${parseInt("0") || 1}`);
console.log(`K00-01 priority(2)||1=${parseInt("2") || 1}`);
console.log(`K00-01 priority(false)||1=${parseInt("false") || 1}`);
console.log(`K00-01 option-priority-false-present=${Object.prototype.hasOwnProperty.call({ priority: false }, "priority")}`);
console.log(`K00-01 option-priority-absent=${Object.prototype.hasOwnProperty.call({}, "priority")}`);
console.log(`K00-01 JSON=${JSON.stringify(wire)}`);

console.log(`K00-02 unknown=${sourceSettings.unknown}`);
console.log(`K00-02 serverVersion=${sourceSettings.serverVersion}`);
console.log(`K00-02 JSON=${JSON.stringify(sourceSettings)}`);

console.log(`K00-03 Number(1e309)=${show(Number("1e309"))}`);
console.log(`K00-03 bitwise(4294967297|0)=${4294967297 | 0}`);
console.log(`K00-03 bitwise(-1.9|0)=${-1.9 | 0}`);
console.log(`K00-03 bitwise(undefined|0)=${undefined | 0}`);
console.log(`K00-03 bitwise(-1>>>0)=${-1 >>> 0}`);
console.log(`K00-03 utf8=${Buffer.from("hé", "utf8").toString("utf8")}`);
console.log("K00-03 size(16)=16");
console.log("K00-03 size(huge)=rejected");
