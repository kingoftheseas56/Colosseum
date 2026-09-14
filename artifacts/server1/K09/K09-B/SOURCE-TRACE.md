# K09-B source trace

- Oracle: `server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- M172 lines 18110-18500, SHA-256 `bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486`: count unchoked peers; apply strict `>` for maxSpeed, maxBuffer and minPeers; a maxBuffer option overwrites the earlier speed primary condition; average only selections whose `readFrom` and `selectTo` are both nonzero; pause only when primary condition and min-peer condition both hold.
- M612/M613/M624/M626/M290/M804 were rechecked against the K09-A coordinator/source files during convergence. Torrent announces replace configured sources and append DHT; internal engine DHT/tracker defaults remain false, preventing duplicate discovery ownership.
- K09-01 covers strict queued min/max hysteresis and coordinator-wide duplicate accounting.
- K09-02 covers source selection, no sources, tracker failure visibility, exact DHT delay, and teardown before delayed work.
- K09-03 covers strict minPeers/speed/buffer boundaries, maxBuffer precedence, and zero-window exclusion.

Fix Round 1 (2026-09-15): public linked composition replaces test-private cpp inclusion, and K09/P08 share an actionable external-control-only autonomy policy.

Fix Round 2 (2026-09-15): convergence now includes the K09-A globally unique actionable peer-add queue; duplicate addresses across sources are not surfaced twice.
