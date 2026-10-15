// addon_direct_stream_contract_test.mjs — standard Stremio direct streams stay HTTP,
// and only origin request headers cross the AddonClient → player boundary.
//
// STAMP: uncompiled / untested / unexecuted / unadopted / unverified
//
// Loads the real qml/AddonClient.js. This is a deterministic contract test: no network,
// debrid provider, or running Colosseum instance is required.
import fs from 'fs';

let src = fs.readFileSync('qml/AddonClient.js', 'utf8').replace(/^\.pragma library\s*$/m, '');
const mod = {};
new Function('module', src +
  '\nmodule.parseStream=parseStream;')(mod);

let failures = 0;
function check(cond, msg) {
  if (cond) { console.log("  ok   " + msg); }
  else { console.log("  FAIL " + msg); failures++; }
}

function emptyObjectMap(value) {
  return value && typeof value === 'object'
      && !Array.isArray(value)
      && Object.keys(value).length === 0;
}

console.log("direct stream normalization — plain URL");

const plainUrl = "https://media.example/video.mkv";
let row = mod.parseStream({
  name: "1080p",
  url: plainUrl
}, "Direct Well", 0);

check(row !== null, "plain url stream is carried");
check(row.streamKind === "Direct", "plain url stream is Direct");
check(row.url === plainUrl, "plain url is preserved");
check(row.infoHash === "url:" + plainUrl,
      "plain url keeps the existing url: routing convention");
check(emptyObjectMap(row.headers), "plain url carries an empty request-header map");

console.log("direct stream normalization — standard nested request headers");

const headerUrl = "https://media.example/headered.mkv";
row = mod.parseStream({
  name: "1080p",
  url: headerUrl,
  behaviorHints: {
    notWebReady: true,
    proxyHeaders: {
      request: {
        "Referer": "https://origin.example/",
        "Origin": "https://origin.example",
        "User-Agent": "Colosseum-Direct-Test"
      },
      response: {
        "X-Do-Not-Forward": "response-only"
      }
    }
  }
}, "Header Well", 1);

check(row !== null, "header-bearing url stream is carried");
check(row.streamKind === "Direct", "header-bearing url stream is Direct");
check(row.headers.Referer === "https://origin.example/",
      "standard proxyHeaders.request Referer is flattened");
check(row.headers.Origin === "https://origin.example",
      "standard proxyHeaders.request Origin is flattened");
check(row.headers["User-Agent"] === "Colosseum-Direct-Test",
      "standard proxyHeaders.request User-Agent is flattened");
check(row.headers.response === undefined,
      "proxyHeaders.response wrapper never enters the request map");
check(row.headers["X-Do-Not-Forward"] === undefined,
      "response-only header never enters the request map");

console.log("direct stream normalization — response-only standard wrapper");

row = mod.parseStream({
  url: "https://media.example/response-only.mkv",
  behaviorHints: {
    notWebReady: true,
    proxyHeaders: {
      response: {
        "X-Response-Only": "never-request"
      }
    }
  }
}, "Response Well", 2);

check(emptyObjectMap(row.headers),
      "response-only standard wrapper produces an empty request-header map");

console.log("direct stream normalization — historical flat compatibility");

row = mod.parseStream({
  url: "https://media.example/legacy.mkv",
  behaviorHints: {
    proxyHeaders: {
      "Referer": "https://legacy.example/",
      "Origin": "https://legacy.example"
    }
  }
}, "Legacy Well", 3);

check(row.headers.Referer === "https://legacy.example/",
      "legacy flat proxyHeaders Referer remains supported");
check(row.headers.Origin === "https://legacy.example",
      "legacy flat proxyHeaders Origin remains supported");

console.log("direct stream normalization — malformed array header shapes");

row = mod.parseStream({
  url: "https://media.example/array-wrapper.mkv",
  behaviorHints: {
    proxyHeaders: []
  }
}, "Malformed Wrapper", 4);

check(emptyObjectMap(row.headers),
      "array-shaped proxyHeaders is rejected as an empty object map");

row = mod.parseStream({
  url: "https://media.example/array-request.mkv",
  behaviorHints: {
    proxyHeaders: {
      request: []
    }
  }
}, "Malformed Request", 5);

check(emptyObjectMap(row.headers),
      "array-shaped proxyHeaders.request is rejected as an empty object map");

console.log("direct stream normalization — torrent precedence and isolation");

row = mod.parseStream({
  infoHash: "0123456789abcdef0123456789abcdef01234567",
  fileIdx: 4,
  url: "https://media.example/must-not-win.mkv",
  behaviorHints: {
    proxyHeaders: {
      request: {
        "Referer": "https://must-not-reach-torrent.example/"
      }
    }
  }
}, "Torrent Well", 6);

check(row !== null, "torrent stream is carried");
check(row.streamKind === "Torrent", "infoHash keeps Torrent precedence");
check(row.infoHash === "0123456789abcdef0123456789abcdef01234567",
      "torrent infoHash is preserved");
check(row.url === "", "torrent row does not also carry a direct url");
check(emptyObjectMap(row.headers), "torrent row carries an empty request-header map");

console.log("direct stream normalization — unsupported pointer");

row = mod.parseStream({
  externalUrl: "https://example.invalid/watch"
}, "External Only", 7);

check(row === null, "externalUrl-only stream remains outside the player contract");

if (failures) {
  console.log("\nFAIL — " + failures + " check(s) failed");
  process.exit(1);
}
console.log("\nPASS — Stremio Direct/Torrent stream contract holds");
