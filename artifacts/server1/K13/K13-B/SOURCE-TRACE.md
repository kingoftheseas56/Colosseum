# K13-B source trace

Worker: `K13-B`
Packet: `K13`
Oracle: `C:\Users\PublicUser\Desktop\Brotherhood\.codex\server1-review\oracle\server.js`
Oracle SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`
Oracle input: `K13_B_ORACLE_PATH` overrides the path; when unset, the probe uses the adjacent
`../oracle/server.js` review-mirror fallback. The recorded run used:
`K13_B_ORACLE_PATH='C:\Users\PublicUser\Desktop\Brotherhood\.codex\server1-review\oracle\server.js'`.

Hash convention: the oracle hash is SHA-256 over the exact raw bytes of the local mirror with no
newline normalization. The module/range hashes above retain the planning-pack source-byte
convention for the identified inclusive line ranges; artifact hashes in `HASHES.json` are SHA-256
over each captured file's raw bytes. The differential compares the captured source and candidate
stdout line sequences without normalization or generated claims.

The probe reads and verifies the local oracle bytes, disables only the webpack entrypoint so the
module table can be exercised in a controlled VM, and executes the oracle's module 106 settings
initializer, module 414 cache API, and module 564 `getCachePath`/`getDefaults` function bodies.
The walker, disk probe, engine list, and filesystem unlink surface are controlled adapters that
feed the same values as the native trace; cache policy decisions and emitted values come from the
oracle functions, not probe literals.

The source ranges used for this slice are:

| Module | Lines | SHA-256 | K13-B use |
| --- | --- | --- | --- |
| M106 | 12792-12833 | `12b723abb6dd833967bb40ad214679a4584bbe3c8b6d2b97cafbc19e3fc52179` | fresh desktop settings defaults, including 55 connections and 2 GiB cache |
| M194 | 19675-19682 | `b55829ef3824da2c446073bfd42c764b6871effd46136f64fd67d2a6c1c69843` | settings/option convergence context |
| M400 | 34767-34775 | `868936ff7407bd2a05aecd8205ae465b097b09656c09b3c3265b8188949c5fde` | cache-entry and filesystem context |
| M414 | 35959-36170 | `16b1c32e135ce079b4d718a627c0a6405f34bb0878a6804f1dbecd51a5b5770d` | cache enumeration, active-engine omission, atime order, disk-space clamp, delayed cleaning, and Windows cache locations |
| M564 | 46578-46979 | `d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569` | no-cache 15 MiB/45 MiB entrypoint defaults and temporary-directory fallback |

Implementation mapping:

- `CachePolicy.cpp` consumes K13-A's production `server1/settings/SettingsStore.h` through `CachePolicy.h`; no probe-only redeclarations remain.
- Enumeration records regular-file size, real access time, and active-engine state from the immediate engine directory.
- Eviction uses stable descending atime order, counts active files in the size sum, and never selects active-engine files.
- Disk-space adjustment applies `min(toSize, cacheSize + free - requiredSize || toSize)` semantics.
- Option changes are coalesced and applied after the ten-second production delay; tests use a short injected delay.
- Cache creation falls back to the system temporary directory when `stremio-cache` cannot be created.
- Windows keeps `GetFileAttributesExW` access-time retrieval. Supported POSIX targets use `stat` atime fields; failures return unknown atime `0` and never substitute mtime.

The executable nine-line source trace and candidate trace match byte-for-byte with the configured
oracle path and SHA-256 above. The focused atime
test separately crosses atime and mtime and passes on Windows; it will report `NOT_APPLICABLE`
only on unsupported platforms.
