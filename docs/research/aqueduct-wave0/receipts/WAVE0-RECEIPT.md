# Arc 44 Wave 0 Receipt

**Wave:** W00–W02 · Freeze oracle before C++
**Branch:** `arc/aqueduct-native-stream-server`
**Base at receipt:** `56f2ae4428eb9b13ef8d319b04755181f06f6862` (`origin/master`)

## W00 — upstream authority

- Exact server: Stremio Server `4.20.17`.
- `server.js`: 6,631,104 bytes, SHA-256 `567A397BB11B788571BF1750FD05DD78927F97BEC0C9DDEAA6D9CC1ECCEE3922`.
- `stremio-runtime.exe`: SHA-256 `8AD810919DF76741A153DBF28180A84F7AB395EA3DA2534374A10F0E6DCA7E3B`.
- Bundle split: 1,310 Webpack modules, byte-exact round trip.
- Source-port matrix: 90 explicit route registrations + 18 internal behavioral rows.
- Every matrix row is assigned `PORT` or `PORT VIA PROVEN EQUIVALENT`; no unresolved row remains.

## W01 — oracle harness

Harness files:
- `oracle/capture_oracle.py`
- `oracle/fixture_server.py`
- `oracle/golden/control.json`
- `oracle/golden/offline.json`
- `oracle/golden/live.json`
- `compare_candidate.py`
- `verify_wave0.py`

Executed oracle suites:
- control: 8 records.
- offline: 13 records covering HLS, probe, subtitles, proxy and ZIP/TAR/TGZ behavior.
- live: 11 records on fixed legal Sintel torrent `08ada5a7a6183aae1e09d831df6748d566095a10`, file index 5.

Live observations frozen as oracle behavior:
- create: HTTP 200.
- immediate metadata-race range: HTTP 206.
- HEAD: HTTP 200.
- open-ended, bounded and deep-seek ranges: HTTP 206.
- huge invalid range: Stremio returns HTTP 200/full-file semantics rather than 416; capture is bounded to 64 KiB.
- remove: HTTP 200.

The deterministic control/offline suites were independently rerun after pinning the fixture origin to port 11580; `compare_documents` reported `GREEN_DETERMINISTIC_RERUN`.

The oracle shutdown originally exposed a real harness bug: Stremio's HLS hardware probe could leave a lab `ffmpeg.exe` child holding copied FFmpeg DLLs. Shutdown now terminates the spawned Windows process tree and retries transient payload deletion. Fresh runs leave no process whose executable path is under `_aqueduct-wave0/oracle/payload`.

## W02 — Tankoban 2 regression corpus

`TANKOBAN2-REGRESSION-CORPUS.md` defines R01–R10:
1. catastrophic cold-open;
2. bandwidth without head-piece progress;
3. premature EOF/reconnect loops;
4. scheduler/session-tuning sensitivity;
5. seek-class and tail-metadata preservation;
6. time-critical queue overload;
7. metadata/readiness chicken-and-egg;
8. competing HTTP readers/audio starvation;
9. low-connected-peer tail;
10. lifecycle cleanup.

Historical exact cases that depend on obsolete swarms remain explicitly historical; deterministic test shapes and future packet owners are recorded rather than pretending those swarms are reproducible forever.

## RED/green state

- Stremio oracle: GREEN.
- Wave-0 artifact verifier: GREEN.
- deterministic oracle rerun: GREEN.
- Colosseum Server candidate parity: intentionally RED because no C++ candidate captures exist yet.
- Production streaming code modified by Wave 0: **no**.
