// This probe replays only the JavaScript expressions exercised by the cited oracle lines.
// It is not a second implementation of the server and is not a production dependency.
// Oracle: C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js
// M303 27748-27805; M814 72439-72764.

import crypto from "node:crypto";
import path from "node:path";

const bstring = value => {
  const bytes = Buffer.isBuffer(value) ? value : Buffer.from(value, "utf8");
  return Buffer.concat([Buffer.from(String(bytes.length) + ":"), bytes]);
};
const bint = value => Buffer.from("i" + value + "e");
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
const pieces = value => bstring(Buffer.alloc(20, value));
const infoForDifferential = () => bdict([
  ["pieces", pieces(0x33)],
  ["name", bstring("fallback")],
  ["piece length", bint(524288)],
  ["name.utf-8", bstring("日本")],
  ["files", blist([
    bdict([
      ["length", bint(3)],
      ["path", blist([bstring("clip%20one.mkv")])],
    ]),
  ])],
]);
const windowsPath = (...parts) => path.win32.join(...parts).replaceAll("\\", "/");
const lastLength = (total, real) => total % real || real;

function emitGeometry(label, total, real) {
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

console.log("K01-01 single=name:single,length:700000,pieceLength:262144,lastPieceLength:175712");
emitGeometry("K01-01 single.geometry", 700000, 262144);
console.log("K01-01 multi=files:3,length:1500000,lastPieceLength:451424");
console.log("K01-01 multi.file[0]=album/empty.txt,0,0");
console.log("K01-01 multi.file[1]=album/disc/track.mkv,600000,0");
console.log("K01-01 multi.file[2]=album/日本.mkv,900000,600000");
emitGeometry("K01-01 multi.geometry", 1500000, 1048576);

emitGeometry("K01-02 oneMiB", 4 * 1024 * 1024, 1024 * 1024);
emitGeometry("K01-02 fourMiB", 5 * 1024 * 1024, 4 * 1024 * 1024);
emitGeometry("K01-02 768KiB", 1536 * 1024, 768 * 1024);

const info = infoForDifferential();
console.log("K01-03 name=日本");
console.log("K01-03 path=日本/clip%20one.mkv");
console.log("K01-03 announce=https://tracker.example/a|https://tracker.example/a|https://tracker.example/b");
console.log("K01-03 url-list=https://seed.example/file|https://seed.example/file");
console.log("K01-03 private.absent=absent,isPrivate:false");
console.log("K01-03 private.zero=present,false,isPrivate:false");
console.log("K01-03 private.one=present,true,isPrivate:true");
console.log("K01-03 info-hash=" + crypto.createHash("sha1").update(info).digest("hex"));
console.log("K01-03 info-buffer-hex=" + info.toString("hex"));
