# K12-A source trace

Producer: `[Agent (Claude), K12-A producer]`. Parent packet: K12 (Translate EngineFS counters, statistics and removal). Barrier: B-W3F.

## Frozen source

- **Oracle bundle.** `stremio-service-v4.21.1-server-bundle/server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- **Hash convention.** Each module hash is SHA-256 over its exact byte range from `MODULE-INDEX.json` of the v2.1 plan pack, and equals `sha256(modules[id].toString())` in `run_oracle.js`.

| Module | Lines | SHA-256 | K12 use |
|---|---|---|---|
| M172 EngineFS | 18110-18500 | `bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486` | counters, stream-open cache handler, getStatistics, stats/create/remove/removeAll routes, keepConcurrency |
| M662 Counter | 62017-62032 | `fb3f3872c39d5587bf694679581f4a7d0a497a86b0fc21ad3d02c884de94ad26` | reference counts and one-shot timers |
| M663 GuessFileIdx | 62032-62053 | `4953bf2e5b17300fc3e5d51f2c874b1e24ba18e3076a36f2cbfdcba9dd90288d` | guessed-file selection |
| M664 spoofedPeerId | 62053-62065 | `e770c8eec9cac4a2509806bc6c26288c7188c5fcbcaeee1327199d5eddb46458` | options.id; stubbed to a fixed id in the oracle |
| M304 parse-video-name | 27805-27908 | `83b3a52ffac8663b219db8e4e85de77182fbb8cf7412bfe27978fd0968793c0a` | dependency of M663, not listed in the K12 entry |
| M80 safeStatelessRegex | 10600-10617 | `de77a63d6333c32ae8911f2acc43595ae76a87137d31857d6bd41bb5f33089d8` | fileMustInclude evaluation |
| M564 entrypoint | 46578-46979 | `d02004f3597ab40da72799e55f852428db0946210ee93dba9533800d1b0b0569` | L46884 overrides; read, not executed |
| M100 router | 11691-11866 | `f8b0b1ae8bd44fb66d27bb0572420c8e60f3454e849346404c5bf27df4cd7806` | executed in the oracle to drive M172 routes |

M304 and M80 are dependencies of the listed modules. They are translated because M663's series match and M172's fileMustInclude cannot be faithful without them.

## Values after entrypoint overrides

- **M172 L18112.** `STREAM_TIMEOUT = 3e4`, `ENGINE_TIMEOUT = 6e4`.
- **M564 L46884.** Sets `STREAM_TIMEOUT = 2e4` and `ENGINE_TIMEOUT = 12e4`, and wires two handlers:
  - `engine-idle` calls `settings(hash, {swarm: "PAUSE"})`;
  - `engine-inactive` calls `remove(hash)`.
- **Native values.** `kEntrypointStreamTimeoutMs = 20000` and `kEntrypointEngineTimeoutMs = 120000` are the `EngineLifecycleConfig` defaults. The source defaults are kept as `kSource*`.
- **When the value is read.** M662 calls `timeout()` when a count reaches zero, so a later assignment takes effect for the next zero. Native counters read the current value at that moment too. Case L11 proves this, and mutant `stream-timeout-read-at-construction` shows the test detects the difference.
- **Entrypoint handlers.** The M564 handler bodies (swarm pause, remove) are composition work for C00/H01. K12 emits the events and implements `remove`. The swarm-pause surface is part of WIRING-REQUEST.json.

## Behaviour map

**M662 Counter → `EngineCounter`.**

- Unknown id: `onPositive` fires and the count is incremented.
- Any increment clears a pending timer.
- A decrement to exactly 0 arms `setTimeout(timeout())`. When it fires, `onZero` runs, then the id is deleted.
- A decrement of an unknown id stores NaN, and the id never fires again.
- Counts can go negative, and a pending timer survives a later decrement.

**M172 counters.**

- **Stream counter**: stream-open/close by `hash:idx`. Emits `stream-active`; after the stream timeout, `stream-inactive` if the engine still exists.
- **Engine counter**: stream-open/close by `hash`. Emits `engine-active`; after the engine timeout, `engine-inactive` if the engine still exists.
- **Idle counter**: stream-created/cached by `hash`, with no positive hook. After the stream timeout, `engine-idle` if the engine still exists.
- **Emit.** `Emit(args)` emits the name, then the colon-joined name.

**M172 stream-open listener (L18312-18337).**

- Runs `e.ready(cb)`, then, once per torrent file:
  - `stream-created` with the file object, which already carries `__cacheEvents: true`;
  - a `verify` listener that emits `stream-progress:h:i`, whose second argument is always undefined, and `stream-cached:h:i` plus `stream-cached` with `store.getDest(i)`;
  - `select(start*ratio, (end+1)*ratio, false)` unless `e.buffer`.
- **Registration order.** The listener is registered before the counters, and M814 `ready` defers with `process.nextTick`. So `stream-active` and `engine-active` precede `stream-created`.

**getStatistics (L18185-18229) → `serializeEngineStatistics`.** Key order and quirks are listed in the ledger.

**removeEngine / removeAll / keepConcurrency (L18261-18266, L18308-18310, L18375-18387) → `EngineLifecycle::remove`, `removeAll`, `keepConcurrency`.**

**`/:infoHash/create` selection (L18247-18273) → `createRouteSelection`, `matchFileMustInclude`, `JsRegExp`.**

**M663 → `guessFileIdx`.** **M304 → `parseVideoName`.** The M304 regular expressions are hand-written matchers: the source uses a lookbehind, and `std::regex` has none.

## Compatibility ledger: source quirks kept, and recorded rather than corrected

1. **uploadSpeed.** `uploadSpeed` is `e.swarm.downloadSpeed()` (L18217). Covered by S01 and mutant `upload-speed-corrected`.
2. **Per-file key order.** Per-file statistics keys come out as `streamProgress, streamName, streamLen`, because `util._extend` copies keys last-first (S01, mutant `file-stats-natural-key-order`). The same reverse copy orders `createEngine` options: new body keys are appended in reverse (S04 opts).
3. **Null or absent keys.**
   - Without a torrent, `name` and `files` are `null`, not absent.
   - `queued`, `sources` and `peerSearchRunning` are absent when their source is undefined.
   - `wires` is `null` whenever an index is given, including a non-numeric one.
4. **Index lookup.** The file index goes through `isNaN(idx)` and then a property lookup. `"01"` and `"-0"` pass `isNaN` but name no array element, so there are no file statistics (S01 `/A/01`, `/A/-0`; mutant `lenient-file-index`).
5. **Cross-boundary progress.** Progress uses `ceil(length/pieceLength)` pieces but counts every piece the file touches, so it can be negative or above 1 (L09 progress -1, 0, 1). A zero-length file gives `0/0`, which serializes as `null` (S03).
6. **`__cacheEvents`.** After the first stream-open, `__cacheEvents: true` appears on that torrent file in statistics (S01).
7. **Case-exact stats lookup.** `/:infoHash/stats.json` looks up `engines[req.params.infoHash]` case-exactly, so an uppercase hash returns `null`. `exists`, `remove` and `getSelections` lowercase the key (S01 `/Aupper`).
8. **Counter NaN wedge and negative counts** (M662 cases).
9. **Double removal.** `removeEngine` checks existence before the asynchronous destroy completes, so a remove racing `removeAll` destroys the same engine twice and emits `engine-destroyed` twice (L15). `EngineRegistry::remove` erases at once, so through `RegistryEngineCatalog` the second removal is a no-op. This is a K11-level divergence and is recorded here.
10. **keepConcurrency.**
    - `slice(0, enginesCount - concurrency)` uses JavaScript slice coercion: NaN becomes 0 and fractions truncate (K06 `"x"`, `1.5`).
    - The promise resolves when the last-indexed removal completes, even if earlier ones are still pending (K05).
    - Engines with active selections and the requesting hash (compared case-insensitively) are never removed (K03, K04).
11. **fileMustInclude.**
    - A falsy alternative (`""`, `null`, `0`) matches, but `find` returns that element, so the outer search continues and the last matching file wins (S04 `[""]`, index 4; mutant `falsy-alternative-stops-search`).
    - `null` becomes the pattern `/null/`.
    - `"/x/q"` has invalid flags, so it falls back to the literal string pattern `"/x/q"`.
    - A non-string truthy alternative throws `TypeError`; an invalid string pattern throws `SyntaxError`. The route never responds in either case, so native reports the error.
12. **GuessFileIdx.**
    - Media extensions use an unescaped `.`, so `xmkv` and `cats` count as media (G06).
    - Season and episode compare with `===`, so a string season never matches (G03).
    - A falsy season disables series matching (S04 `{season:0}`).
    - Ties keep the first file (G09).
13. **M304 parse-video-name.**
    - The excluded-word table is a plain object, so `constructor` is excluded through `Object.prototype` and the name becomes `""`.
    - `Number()` semantics accept `1e10` and `0x1f` as numeric stamps.
    - `firstNameSplit.reverse()` mutates the array in place before `indexOf`.
    - A deleted key re-inserted later moves to the end.
    - The full corpus is in `cases/K12.json`: 86 names, plus 8 option variants.

## Native-seam divergences (disclosed)

- **Regular expressions.** A user `fileMustInclude` pattern is evaluated with `std::regex` ECMAScript.
  - Constructs JavaScript accepts but `std::regex` rejects (lookbehind, named groups, `\p{}`) are treated as invalid.
  - The `u`/`v` flags change no semantics.
  - `m` is accepted as a no-op, because MSVC `std::regex` has no multiline flag.
  - `s` is emulated by rewriting `.`.
  - `y` anchors at index 0, as M80's fresh `match` does.
  - A regex-engine complexity error counts as "no match", as M80 treats a 500 ms timeout. The thresholds differ.
- **M304 characters.** M304 matching uses ASCII classes on UTF-8 bytes. Word boundaries and `\W` treat every non-ASCII byte as a non-word character, which matches JavaScript for BMP text, where every non-ASCII code unit is a non-word character.
- **Unknown engine on stream-open.** The source throws from the stream-open handler before the counters run, and the route stops. Native emits `stream-open` and nothing else, and the returned stream is already closed (native check `K12-01 UNKNOWN-ENGINE`).
- **Oracle Node version.** The oracle runs under Node v24.14.0. `util._extend` order and V8 regular-expression semantics match the source runtime. Flag validity (`d`, `v`) follows Node 24.

## Oracle normalization and stubs

- **Normalization.** `engine-create*` and `engine-ready*` events come from `createEngine`, K11's accepted contract, and are dropped from source lifecycle logs. No other output is normalized.
- **Stubs.**
  - M612 PeerSearch attaches the scenario's stats to `swarm.peerSearch`, as the real constructor attaches itself.
  - M664 returns a fixed id.
  - `os` returns a fixed tmpdir, loadavg and cpus.
  - `getCachePath` returns `cache/<ih>`, and the M564 entrypoint replaces it in the product as well.
- **Stream route.** The stream route's three-line close guard (`closed` flag, `finish`/`close`) is mirrored in the oracle's `open` step, because H01 owns that route.
