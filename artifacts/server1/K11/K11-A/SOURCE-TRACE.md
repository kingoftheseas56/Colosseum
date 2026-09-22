# K11-A repair source and behavioral trace (K11-v4)

Producer: `[Agent (Claude), K11-A repair producer]`. This is producer evidence, not acceptance.

## Authorities

- Oracle bundle SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f` (rehashed by `run_oracle.js` on every run).
- M172 factory SHA-256 `bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486` (EngineFS, LF lines 18110-18500).
- M814 factory SHA-256 `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e` (torrent engine, LF lines 72439-72764).
- M612 PeerSearch (LF lines 58255-58309, SHA-256 `dac17bb43e2d6f2615ba81713bb1f504dfe26bb85f99ad9d0f0a9e0e0ca9d69b`) is read for the discovery translation; it is the accepted K09 contract's source and is not re-executed here.
- Frozen packet: `IMPLEMENTATION-PLAN.md` §45 K11, `PARALLEL-WORK-ITEMS.json` K11-A. Accepted contracts consumed unchanged: K02 PieceBuffer, K03/K04 Scheduler and SchedulerActionContract, K05 SwarmPolicy, K06 PersistentPieceStore, K07 CircularPieceStore, K08 FileReader, K09 PeerSearch/SwarmCaps, P08-T/K10 TorrentTransport.

## Translation map

| Source behavior | Source site | Native site |
|---|---|---|
| `options.stream` aliases `options.torrent` before `beforeCreateEngine` | M172 `createEngine` first statement | `normalizeInputOptions` in `EngineRegistry::create` |
| Continuation subscribes `once("engine-ready:"+hash, cb)` | M172 `createEngine` | `Entry::waiters`; delivered at the first scoped emission |
| `util._extend(getDefaults(hash), options)` (shallow); `path = path \|\| getCachePath(hash)` | M172 | `applyCreate`: `shallowExtend(defaultsFor)`, `effectivePath` |
| `engine-create` emitted before `options.id = spoofedPeerId()` | M172 | `applyCreate`: `emitted` snapshot, then fresh `-TS0008-native-<token>` id |
| One engine per hash; `isNew` constructs | M172 `engines[infoHash]` | `entries` + `constructionCount`; one strand per key |
| `e.swarm.resume(); e.options = options` on every create | M172 | `TorrentEngine::resume` → `setSwarmPaused(false)`, effective options |
| `isNew && options.peerSearch` → `new PeerSearch(announce ? trackers+dht : options.peerSearch.sources)` | M172 | `Impl::start` from creation options; metainfo announce parsed from `MetainfoSource` |
| `isNew && options.swarmCap` → updater on `wire`, `wire-disconnect`, `download`; bound to creation `swarmCap` | M172 | `swarmCap` from creation options; `updateSwarmCap` on a new wire and after a commit |
| `engine-created` once for a new engine | M172 | `Created` event |
| `e.ready(cb)`: nextTick when torrent known, else on `ready`; emits `engine-ready:hash` then `engine-ready` | M814 `engine.ready`, M172 | `emitReadyPairs`: one pair per create, callbacks inside the scoped emission |
| `removeEngine`: `destroy(cb)` → `engine-destroyed` → delete → `cb()` | M172 | `remove`: close → work-lane transport/store close → `Destroyed` → `Removed` completions → callback |
| `ontorrent`: verification bitfield restore, virtual geometry, store open | M814 | work-lane `metadata-install`/`restore`, then app-lane `install` |
| `setInterval(rechoke, 1e4)` in `ontorrent`; `clearInterval` in `destroy` | M814 | `kRechokeIntervalMs = 10000` started in `install`, cancelled once in `close` |
| Rechoke ranking and slots (`uploads === false \|\| 0 ? 0 : +uploads \|\| 5`) | M814 | `rechokeTick` over accepted K05 `SwarmPolicy::rechoke`, `uploadSlots` |
| `checkseeder` compares wire bitfield length to engine piece count | M814 | `isSeeder` (verification count must equal virtual count) |
| `onupdatewire`: a wire with `downloaded == 0` requests one block from the selection end; afterwards `select` fills the per-wire budget | M814 | `pumpRequests`: per-peer downloaded bytes and outstanding count into `Scheduler::choosePiece` and `SchedulerActionContract::decide` |
| Virtual index/offset → verification wire coordinates | M814 `onrequest` | `wireBlock` |
| `store.write` → `verify` → `commit` → `have` | M814 | work-lane `stage`/`verify-*`/`commit`, app-lane `AdvertisePieceAction` once per verification piece |
| Upload `wire.on("request")` → `store.read` → slice | M814 | `acceptUpload`: committed persistent only, work-lane `uploadRead` |
| `PeerSearch` source `peer` event → `swarm.add(addr)`; `update` on `wire`/`wire-disconnect`/`resume`/`pause` | M612 | `discoverPeer` → `drainPeerAdds` → generation-owned `ConnectAction` on the work lane; `peerSearchUpdate` on new wire and pause changes |

## Real-piece completeness (round 2)

P08-T5/K10-H: a real verification piece is advertised (HAVE) or uploaded only when every virtual component is durably committed. K06 restores virtual pieces individually, so a truncated backing file can keep one component and drop another. K11 records the whole restored set before any advertisement decision (`install`), advertises through `realPieceCommitted`, checks the same condition before admitting an upload and again on the work lane (`uploadRead` over the group range). A surviving component stays individually readable because K06 reports it committed.

K06 verifies only fully staged groups, and restore leaves survivors committed but unstaged. When a re-downloaded component leaves only restored survivors missing, `PersistentBackend::restageRestored` stages their durable bytes so the whole real piece is re-hashed and re-committed as one group. A corrupt survivor fails that hash; K06 resets the group and K11 drops it from the committed mirror and re-demands it.

Committed-only visibility makes a reader depend on its whole real piece, but its scheduler selection may start inside the group (M846 selects virtual pieces; source M814 reads a written piece before its group commits). K11 adds one engine-owned scheduler selection for missing group members outside every selection and releases it when the real piece commits. No K06 production code changed.

## Lanes

The app lane runs registry state, events, callbacks, scheduler decisions, FileReader interaction and mailbox `poll`/`submit`. The per-engine serialized work lane runs every blocking peer or disk operation: transport open (libtorrent session creation), `ConnectAction` (a synchronous native event-loop barrier), transport close, metadata install with the persistent restore scan, stage/verify/commit, cache and upload reads, and store close. An injected `workExecutor` is used only when it defers work off the app thread; inline or app-thread execution falls back to a registry-owned worker.

## Differential derivation

`run_oracle.js` executes the real M172 and M814 factories with controlled stubs and records raw events. The native binary's `--trace` mode runs the same two scenarios and records raw events from observed transport actions, `EngineTrace` work-lane records, events, callbacks, reader deliveries and timer calls. Both raw logs pass through the same `normalize172`/`events814`/`normalize814` functions. Rules are recorded in `raw/comparison.json` under `normalizationRules`.

## Divergences

1. Disclosed (safety): source M814 emits `download(1)` when virtual piece 1 is staged, before its verification group commits. Native readers and uploads see only committed persistent pieces. The source projection carries `{"kind":"source-precommit-visible","piece":1}`; the native projection must carry none. Removing it from the source or adding one to native fails the comparison.
2. Approved lifecycle hardening kept from K11-v3: generations isolate remove/recreate; a source error is terminal and never fakes Ready; the hook continuation is single-use; destruction completes unfinished creates as Cancelled.
3. Native erases the registry entry at `remove`; source deletes it in the destroy callback. Generation isolation makes the difference unobservable to a recreated engine.
4. Native rejects malformed discovery results before the transport; source `swarm.add` accepts any string.
5. Rechoke uses the accepted K05 `SwarmPolicy` with peer-handle salt instead of `Math.random()` and without optimistic-unchoke persistence; the controlled single-peer rechoke matches the source.
