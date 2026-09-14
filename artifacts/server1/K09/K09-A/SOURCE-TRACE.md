# K09-A source trace

- Oracle: `server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- M612 lines 58255-58309 (`dac17bb43e2d6f2615ba81713bb1f504dfe26bb85f99ad9d0f0a9e0e0ca9d69b`): typed source parsing, coordinator-wide peer uniqueness, per-source counts, run/pause/close/stats, strict min/max queued hysteresis, paused-swarm handling, 30-second rerun, and close cleanup.
- M613 lines 58309-58330 (`166a8bfc8dcda3b2e7d3311acd179d388f293a0a95f9fdf414dbbc4442dc3843`): DHT defers lookup 1500 ms, counts lookup start, cancels waiting work or schedules abort after pause, and destroys on close.
- M624 lines 59001-59019 (`3a1e1620ad371c099676605abf74fb7e4e183ed1ae4df5143655aeefaa97056f`): tracker counts each run; pause is a no-op.
- M626 lines 59022-59107 (`82e99353539ff19db4729f0dbf5022f0cbea18ea20c1927eb30c2327784a3363`) and M290 lines 26267-26757 (`3900d32bb67bb1795561f42eb2a222de5e01e7136f1855aed31319819d02ce65`): tracker/DHT client lifecycle authority.
- M172 lines 18110-18500 (`bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486`): torrent announces replace configured sources, are prefixed with `tracker:`, and append the infohash DHT source. Internal engine DHT/tracker remain disabled.
- M804 lines 71090-71092 (`77e375644fdae102dec312e7d8b99918194b25f5bd7a447d936e938f4ca133bd`): frozen default tracker corpus inspected; no larger list is introduced.
