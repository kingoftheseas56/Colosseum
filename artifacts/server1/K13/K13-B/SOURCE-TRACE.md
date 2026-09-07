# K13-B source trace

Worker: `K13-B`
Packet: `K13`
Oracle: `C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js`
Oracle SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`

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

The source-derived nine-line trace and candidate trace match byte-for-byte. The focused atime test separately crosses atime and mtime and passes on Windows; it will report `NOT_APPLICABLE` only on unsupported platforms.
