// Execute the authenticated M181 bencode decoder and M303 torrent parser.
// The probe is evidence-only and is not a production dependency.

import fs from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const oraclePath = process.argv[2];
if (!oraclePath) {
  console.error("usage: node oracle_k01_probe.mjs <authenticated-oracle-server.js>");
  process.exit(2);
}

const bundle = fs.readFileSync(oraclePath, "utf8");
const instrumentedBundle = bundle.replace(
  "!(function(modules) {",
  "!(function(modules) { globalThis.__oracle_require = __webpack_require__;",
);

const savedConsole = {
  log: console.log,
  warn: console.warn,
  error: console.error,
};
console.log = () => {};
console.warn = () => {};
console.error = () => {};
try {
  new Function("require", instrumentedBundle)(require);
} finally {
  console.log = savedConsole.log;
  console.warn = savedConsole.warn;
  console.error = savedConsole.error;
}

const oracleRequire = globalThis.__oracle_require;
const bencode = oracleRequire(181);
const m303ParseTorrent = oracleRequire(303);

const bstring = value => {
  const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value, "utf8");
  return Buffer.concat([Buffer.from(String(bytes.length) + ":"), bytes]);
};
const bbytes = value => bstring(value);
const bintText = value => Buffer.from("i" + value + "e");
const bint = value => bintText(String(value));
const blist = values => Buffer.concat([Buffer.from("l"), ...values, Buffer.from("e")]);
const bdict = entries => {
  const ordered = [...entries].sort((left, right) =>
    Buffer.compare(Buffer.from(left[0]), Buffer.from(right[0])));
  return Buffer.concat([
    Buffer.from("d"),
    ...ordered.flatMap(([key, value]) => [bstring(key), value]),
    Buffer.from("e"),
  ]);
};
const repeatedBytes = (count, value) => Buffer.alloc(count, value);
const pieces = value => bbytes(repeatedBytes(20, value));

const oneFileTorrent = (length, pieceLength, name = "single") => bdict([
  ["info", bdict([
    ["length", bint(length)],
    ["name", bstring(name)],
    ["piece length", bint(pieceLength)],
    ["pieces", bbytes(repeatedBytes(60, 0x11))],
  ])],
]);

const multiFileTorrent = () => bdict([
  ["info", bdict([
    ["files", blist([
      bdict([
        ["length", bint(0)],
        ["path", blist([bstring("empty.txt")])],
      ]),
      bdict([
        ["length", bint(600000)],
        ["path", blist([bstring("disc"), bstring("track.mkv")])],
      ]),
      bdict([
        ["length", bint(900000)],
        ["path", blist([bstring("fallback.mkv")])],
        ["path.utf-8", blist([bstring("日本.mkv")])],
      ]),
    ])],
    ["name", bstring("album")],
    ["piece length", bint(1048576)],
    ["pieces", bbytes(repeatedBytes(60, 0x22))],
  ])],
]);

const differentialTorrent = () => bdict([
  ["announce", bstring("https://tracker.example/fallback")],
  ["announce-list", blist([
    blist([bstring("https://tracker.example/a"), bstring("https://tracker.example/a")]),
    blist([bstring("https://tracker.example/b")]),
  ])],
  ["info", bdict([
    ["files", blist([
      bdict([
        ["length", bint(3)],
        ["path", blist([bstring("clip%20one.mkv")])],
      ]),
    ])],
    ["name", bstring("fallback")],
    ["name.utf-8", bstring("日本")],
    ["piece length", bint(524288)],
    ["pieces", pieces(0x33)],
  ])],
  ["private", bint(1)],
  ["url-list", blist([bstring("https://seed.example/file"), bstring("https://seed.example/file")])],
]);

const privateTorrent = value => bdict([
  ["info", bdict([
    ["length", bint(1)],
    ["name", bstring("private")],
    ["piece length", bint(16384)],
    ["pieces", pieces(0x66)],
    ["private", bint(value)],
  ])],
]);

const parserCounterexampleTorrent = (length, pieceLength, suffix = "") => {
  const torrent = bdict([
    ["info", bdict([
      ["length", bintText(length)],
      ["name", bstring("single")],
      ["piece length", bintText(pieceLength)],
      ["pieces", pieces(0x33)],
    ])],
  ]);
  return Buffer.concat([torrent, Buffer.from(suffix)]);
};

const slashPath = value => String(value).replaceAll("\\", "/");

function parseTorrent(bytes) {
  // Execute M181 explicitly on the exact bytes before M303 receives them.
  bencode.decode(bytes);
  return m303ParseTorrent(bytes);
}

function emitGeometry(label, torrent) {
  const total = torrent.length;
  const real = torrent.pieceLength;
  const virtualized = real > 524288 && real % 524288 === 0;
  const virtualLength = virtualized ? 524288 : real;
  console.log(label + ".meta=total:" + total + ",verificationLength:" + real
    + ",virtualLength:" + virtualLength + ",virtualized:" + virtualized);
  for (let index = 0, offset = 0; offset < total; ++index, offset += real) {
    console.log(label + ".verification[" + index + "]=" + offset + ","
      + Math.min(real, total - offset));
  }
  for (let index = 0, offset = 0; offset < total; ++index, offset += virtualLength) {
    const verificationIndex = Math.floor(offset / real);
    const verificationOffset = offset % real;
    console.log(label + ".virtual[" + index + "]=" + offset + ","
      + Math.min(virtualLength, total - offset) + ",verification:"
      + verificationIndex + "," + verificationOffset);
  }
  let wireCount = 0;
  let lastWire = null;
  for (let index = 0, offset = 0; offset < total; ++index, offset += virtualLength) {
    const length = Math.min(virtualLength, total - offset);
    for (let blockOffset = 0; blockOffset < length; blockOffset += 16384) {
      lastWire = {
        virtualPiece: index,
        offset: blockOffset,
        length: Math.min(16384, length - blockOffset),
      };
      ++wireCount;
    }
  }
  console.log(label + ".wire-count=" + wireCount);
  if (lastWire)
    console.log(label + ".wire-last=" + lastWire.virtualPiece + "," + lastWire.offset
      + "," + lastWire.length);
}

function emitMetadata(label, torrent) {
  console.log(label + "=name:" + torrent.name + ",length:" + torrent.length
    + ",pieceLength:" + torrent.pieceLength
    + ",lastPieceLength:" + torrent.lastPieceLength);
  emitGeometry(label + ".geometry", torrent);
}

const single = parseTorrent(oneFileTorrent(700000, 262144));
const multi = parseTorrent(multiFileTorrent());
emitMetadata("K01-01 single", single);
console.log("K01-01 multi=files:" + multi.files.length + ",length:" + multi.length
  + ",lastPieceLength:" + multi.lastPieceLength);
for (let index = 0; index < multi.files.length; ++index) {
  const file = multi.files[index];
  console.log("K01-01 multi.file[" + index + "]=" + slashPath(file.path) + ","
    + file.length + "," + file.offset);
}
emitGeometry("K01-01 multi.geometry", multi);

emitGeometry("K01-02 oneMiB", parseTorrent(oneFileTorrent(4 * 1024 * 1024, 1024 * 1024, "oneMiB")));
emitGeometry("K01-02 fourMiB", parseTorrent(oneFileTorrent(5 * 1024 * 1024, 4 * 1024 * 1024, "fourMiB")));
emitGeometry("K01-02 768KiB", parseTorrent(oneFileTorrent(1536 * 1024, 768 * 1024, "768KiB")));

const parsed = parseTorrent(differentialTorrent());
const privateZero = parseTorrent(privateTorrent(0));
const privateOne = parseTorrent(privateTorrent(1));
console.log("K01-03 name=" + parsed.name);
console.log("K01-03 path=" + slashPath(parsed.files[0].path));
console.log("K01-03 announce=" + parsed.announce.join("|"));
console.log("K01-03 url-list=" + parsed.urlList.join("|"));
console.log("K01-03 private.absent=absent,isPrivate:false");
console.log("K01-03 private.zero=" + (Object.prototype.hasOwnProperty.call(privateZero, "private")
  ? "present," + privateZero.private : "absent") + ",isPrivate:" + (!!privateZero.private));
console.log("K01-03 private.one=" + (Object.prototype.hasOwnProperty.call(privateOne, "private")
  ? "present," + privateOne.private : "absent") + ",isPrivate:" + (!!privateOne.private));
console.log("K01-03 info-hash=" + parsed.infoHash);
console.log("K01-03 info-buffer-hex=" + parsed.infoBuffer.toString("hex"));

const negativeLength = parseTorrent(parserCounterexampleTorrent("-1", "16384"));
const leadingZeroInteger = parseTorrent(parserCounterexampleTorrent("3", "016384"));
const trailingBytes = parseTorrent(parserCounterexampleTorrent("3", "16384", "x"));
console.log("K01-03 counterexample.negative-length=accepted,length=" + negativeLength.length);
console.log("K01-03 counterexample.leading-zero=accepted,pieceLength="
  + leadingZeroInteger.pieceLength);
console.log("K01-03 counterexample.trailing-bytes=accepted,length=" + trailingBytes.length);

let missingInfo;
try {
  parseTorrent(bdict([]));
  missingInfo = "accepted";
} catch (error) {
  missingInfo = "rejected," + error.message;
}
console.log("K01-03 invalid.missing-info=" + missingInfo);

// Some transitive oracle modules initialize server-side timers/sockets while
// M181/M303 are loaded; exit after capturing the source outputs.
process.exit(0);
