# Phase 0 · Slice 0 — Specimen pin and isolated lab

- **Executed:** 2026-08-07 · **Plan:** `docs/superpowers/plans/2026-08-07-tankorent-2-phase0-rosetta-dig.md` @ `55304c5`
- **Status:** Runtime-validated (see the layer matrix at the end)
- **Lab location:** `native/build-msvc/_t2lab/` — gitignored by the existing `native/build*/` rule.
  Reproducible tooling is committed under `docs/research/tankorent2-phase0/labscripts/`.

## Baseline (recorded BEFORE anything was copied or started)

| Fact | Value |
|---|---|
| Port 11470 | nothing listening; `curl` returned HTTP 000 |
| Stremio processes | none running (`stremio-runtime`, `stremio-service`, `stremio`) |
| `server.js` | `567a397bb11b788571bf1750fd05dd78927f97bec0c9ddeaa6d9cc1eccee3922` · 6,631,104 bytes · 2026-04-14 17:54:56 |
| `stremio-runtime.exe` | `8ad810919df76741a153dbf28180a84f7ab395ea3da2534374a10f0e6dca7e3b` · 65,451,520 bytes · 2026-04-14 17:54:56 |

Install inventory: 17 files. Binaries dated 2026-04-14 (server + runtime), codecs 2026-01-09,
uninstaller 2026-07-05, LICENSE.md 2024-03-11.

## The specimen, pinned

Copied (never moved, never symlinked) into `_t2lab/specimen/` — 13 files:

```
567a397bb11b788571bf1750fd05dd78927f97bec0c9ddeaa6d9cc1eccee3922  server.js
8ad810919df76741a153dbf28180a84f7ab395ea3da2534374a10f0e6dca7e3b  stremio-runtime.exe
d45c7f3deffeabfaa9082d4df35f804aafbb13e7c42b4840b2886fddff423553  ffmpeg.exe
b6d63e805aaf9a07008b7ef88cfeee3e910410cbb1af216d965096d90181ce5f  ffprobe.exe
be1443a4e7adfee366612175bf24f37c82694da058bdd3926cec3b07f5772076  avcodec-58.dll
7a5a43715acb6272df237a6212fd92dec66469c9afda1fd120315d28e690e52a  avdevice-58.dll
28f08919868802f792d2ba7e35b352fcd2d835142820a8b73fe6ca870ec32116  avfilter-7.dll
55d3d2e5e7d47adb799fd05f9732124475a56f4281fcb6b594b80eb555a1ec2c  avformat-58.dll
ed5204555abfbf6a46360037da9cd8b8900bad595267047094dc683eb383efe9  avutil-56.dll
82c64ba1678d261eea3689ecdcf3889bd3875401a0d850167858bfc4e2771ee9  postproc-55.dll
d826aff5851ec89fa3a8cfee2fa2fbe016c44663cc8ff7288d6c1bf20fd2af70  swresample-3.dll
1af16094c5b2249bdade328e38fa8b4437eef2dfd13525765d5d222cd822ca6f  swscale-5.dll
aef8b4222b79d0dbf6bf17cfff71c90a6a6bb8917a4162abe417b469ed22da2e  LICENSE.md
```

**Server version, read from the running specimen: `4.20.17`.**

`_t2lab/specimen/` is the PRISTINE reference — byte-identical to the install, never edited.
Slice 1's static analysis reads this copy.

## PLAN CONTRADICTED → amended: there is no port knob, and the plan's fallback was unsafe

**What the plan assumed.** Slice 0's guidance said to "determine the port knob from the
bundle/env and record it; if no port knob exists … the launcher instead asserts 11470 is free
before starting, refusing to run if it is not."

**What is actually true.** The port is a hardcoded literal. The EngineFS listen block reads:

```js
var app = enginefs.app(),
    server = (http = __webpack_require__(11), enginefs._server = http.createServer(app)),
    port = 11470;
server.listen(port);
server.on("error", function (err) {
  port++ < 11474 ? (console.warn(err), server.listen(port)) : console.error(err);
});
```

No environment variable influences it. Verified by enumerating **every** `process.env` read in
the bundle — the complete set is HLS_DEBUG, DEBUG, NODE_DEBUG, FFMPEG_DEBUG, TV_ENV, IOS_APP,
READABLE_STREAM, NO_NETWORK_INTERFACES, HOME, HLS_V2_DISABLED, CASTING_DISABLED, WEBOS_ENV,
UNITY_ENV, TIZEN_ENV, NO_CORS, HLS_DEBUG_DIR, DISABLE_CACHING, http_proxy, TMP, TEMP,
SUDO_USER, PATHEXT, PATH, OSTYPE, NO_HTTPS_SERVER. None is a port.

**Why the plan's fallback was actively unsafe.** It has the danger inverted. The hazard is not
"11470 is occupied" — it is "11470 is **free**", because then the lab server binds it, and
Colosseum's adopt-first probe (`streamserver.cpp:79`) would adopt the lab engine on the next
press of Play. The plan's fallback would have permitted exactly the capture it was written to
prevent. At baseline 11470 *was* free, so this would have fired on the first run.

**The amended mechanism (implemented).** A second copy, `_t2lab/specimen-lab/`, carries one
change: every literal `11470` → `11480`. Six occurrences, one byte each (`7`→`8`), so byte
offsets never shift and the two copies stay diffable.

| # | Site | Role | Why it must move |
|---|---|---|---|
| 1 | `http.createServer(app)), port = 11470` | **the listener** | keeps the lab off the port Colosseum adopts |
| 2 | `let ip = "127.0.0.1", port = 11470` (usenet module) | outbound self-reference | left at 11470 it would reach the REAL service |
| 3 | `serverPort = serverPort \|\| 11470` (hls-converter) | outbound self-reference | same |
| 4 | `fetch("http://127.0.0.1:11470/subtitles.srt…")` | outbound self-reference | same |
| 5 | `let engineUrl = "http://127.0.0.1:11470"` (local addon) | outbound self-reference | same |
| 6 | CORS origin regex `(127.0.0.1\|localhost):11470$` | inbound check | consistency only |

Sites 2–5 are an **isolation** matter, not tidiness: left at 11470 the lab would phone the
production engine whenever it happened to be running.

**Retry band deliberately left at 11474.** The listener's fallback is `port++ < 11474`; starting
at 11480 makes that predicate false immediately, so a busy lab port fails **loudly** instead of
walking back toward the production range. Confirmed present and unmodified (`11474` count = 1).

Patch verification: `cmp -l` reports exactly 6 differing bytes, all octal `67`→`70`, at offsets
2629375 · 2643954 · 4084131 · 4778643 · 5250283 · 5454067. Lab copy sha256
`af4b3b7af9e7fca2c48651084e04886c2bdea1d874daf3eec785042ed95593fb`; pristine copy still
`567a397b…` and still equal to the install.

## FINDING — the stock caps in Colosseum's code comment are stale

`streamserver.cpp:256-264` documents the runtime's stock caps as **35 connections, 1.6 / 2.5
MB/s** soft/hard. The plan carried that forward as ground truth. Read live off the specimen's
`/settings`, the actual defaults for server 4.20.17 are:

| Setting | Comment claims | **Actual** |
|---|---|---|
| `btMaxConnections` | 35 | **55** |
| `btDownloadSpeedSoftLimit` | 1.6 MB/s | **2,621,440 (2.5 MB/s)** |
| `btDownloadSpeedHardLimit` | 2.5 MB/s | **3,670,016 (3.5 MB/s)** |

The comment describes an older server. It is not load-bearing for behavior — the app overrides
all three anyway (200 conns, 20/40 MB/s) — but every Phase-0 number must come from the specimen,
not from a comment. **A correction to `streamserver.cpp`'s comment is owed to A4's lane; this
plan ships no app code, so it is recorded here rather than fixed.**

**Three knobs the comment never mentioned, newly surfaced:**

| Setting | Value | Why it may matter |
|---|---|---|
| `btMinPeersForStable` | **5** | a threshold expressed in *peers* — a direct candidate for the "200 seeders reads as single digits" thread. Trace it in Slice 2. |
| `btHandshakeTimeout` | 20000 ms | how long a peer has to complete handshake — a reachability lever |
| `btRequestTimeout` | 4000 ms | how long a block request may hang — a stall lever |

Other defaults: `cacheSize` 2 GB, `transcodeConcurrency` 1, `transcodeMaxWidth` 1920,
`localAddonEnabled` false, `proxyStreamsEnabled` false.

## Completion signal (met)

Lab startup emitted the exact line `streamserver.cpp:151` scrapes, on the lab port:

```
EngineFS server started at http://127.0.0.1:11480
```

`GET http://127.0.0.1:11480/settings` returned the settings JSON. The transcoder's own
self-generated URLs also read 11480 (`11480-qsv-win-video-hevc.mkv`,
`mediaURL=http%3A%2F%2F127.0.0.1%3A11480%2F…`) — independent confirmation that the
outbound-reference patches (sites 3–5) took effect.

Boot noise, benign and recorded: `Cannot update settings ENOENT … server-settings.json` (first
run, no settings file yet), hardware-transcode probing failing across qsv/nvenc/amf, and a
`Buffer()` deprecation warning. None affects BitTorrent behavior.

## Negative controls (all required by the plan; all performed)

| Control | Result |
|---|---|
| Install unmodified after a full run | **PASS** — both hashes byte-identical to baseline |
| Lab process is distinct and is the lab copy | **PASS** — one `stremio-runtime` pid 46620, path `_t2lab/specimen-lab/stremio-runtime.exe`, not the install |
| Production port never taken | **PASS** — `:11470` HTTP 000 while the lab ran; only `:11480` LISTENING |
| Launcher refuses a second run rather than colliding | **PASS** — exit 1, `REFUSING: port 11480 is already in use` |
| Ports return to baseline after stop | **PASS** — nothing on 11470 or 11480 |

**Incidental defect found and fixed during the controls:** `.labpid` recorded the *bash job* pid
(386), not the Windows pid (46620). Killing by that would have missed. `stop-specimen.sh` now
resolves the pid from `netstat` on the lab port and stops that — never by image name, per house
Rule 1 (`taskkill //IM` would also kill a brother's runtime).

## Evidence artifacts

| Artifact | Path |
|---|---|
| Startup transcript | `_t2lab/logs/specimen-run1.log` (local) |
| Pristine specimen | `_t2lab/specimen/` (local) |
| Patched lab copy | `_t2lab/specimen-lab/` (local) |
| Lab's own cache | `_t2lab/cache/` (local; `server-settings.json` + `stremio-cache`) |
| Port patcher | `docs/research/tankorent2-phase0/labscripts/patch_port.py` |
| Launcher / stopper | `docs/research/tankorent2-phase0/labscripts/run-specimen.sh`, `stop-specimen.sh` |

## Layer matrix

```
Qt Test:                not applicable — Phase 0 ships no app code
Qt Quick Test:          not applicable — no QML
Existing harnesses:     not applicable — this slice adds nothing to the build
Lanista:                not applicable — no app UI involved
Human aesthetic verdict: not applicable — no surface
Overall:                Runtime-validated
```

**One regression path remains open and is Hemanth's:** the plan's post-run sanity check is a
human-witnessed film — open Colosseum, play anything in Theatre, confirm it starts normally.
Baseline recorded no engine on 11470 and the lab never took that port, so the expectation is
"unchanged," but the plan ordered eyes and eyes are the gate. Recorded as **pending** until he
confirms.
