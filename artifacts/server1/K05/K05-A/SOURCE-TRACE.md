# K05-A source trace

Oracle: `server.js`, 6,676,503 bytes, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Frozen ranges inspected:

- M664 62053-62065 / `e770c8eec9cac4a2509806bc6c26288c7188c5fcbcaeee1327199d5eddb46458`: per-engine spoofed peer identity shape.
- M814 72439-72764 / `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`: swarm construction, 5 s choke check, 10 s rechoke, seeded-peer handling, stable sort keys, optimistic slot, and per-engine teardown.
- M818 72812-72975 / `eab958a614c693923c1114bae213dadd73d977a87fd640874402418bbcfa493e`: queued/connected lifecycle, handshake/connect timeouts, pause/resume, and close cleanup.
- M822 73188-73361 / `0ff8f245d5904808629f49a79bb25edb3e2c887ab82dee63a13a9c5ecf039fc5`: request lifecycle, choke/unchoke state, and request timeout behavior.
- M843 74473-74529 / `520011da2b4a68a3c71f967416cda41badb047ade587672ef031b1edce9efa30`: 16 KiB metadata chunks, negative/reject handling, 4 MiB cap, out-of-order assembly, and SHA-1 validation.

K05-01 covers M843 chunk and hash behavior. K05-02 covers M814/M818/M822 choke and rechoke policy. K05-03 covers M814/M818 per-engine cleanup and isolation. The packet models higher-level source policy only; native wire control remains behind the already accepted P08 seam.

Fix Round 1 (2026-09-15): M664 peer identity and M818/M822 queued, handshake, request, generation, and timeout behavior are now public exported K05 symbols. Transport effects are typed actions; no socket or K10 adapter was added.

Fix Round 2 (2026-09-15): M814/M818 per-engine teardown now purges queued connect, request, cancel, and disconnect actions before restart generation replacement or stop, so no old-generation transport effect can escape later.
