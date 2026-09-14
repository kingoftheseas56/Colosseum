# K04-A source trace

- Oracle: `server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- M814 lines 72439-72764, SHA-256 `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`.
- K04-01: request depth is `round(45 * (1-clamp((unchoked-1)/29,0,1))^4 + 5)`, yielding 50,50,44,8,5,5 for 0,1,2,15,30,100.
- K04-02: requester below one 16 KiB block/s cannot hotswap. A victim must be active, below 48 KiB/s, no faster than half the requester, and no faster than the current minimum. Equal minima replace the earlier candidate, matching the source's later-wins loop. Canceled reservations are excluded.
- K04-03: every update first emits update; flood reached plus speed strictly above pulse goes through a 500 ms trailing debounce. Request errors cancel their reservation, replaced-piece callbacks do not terminalize twice, and a corrupt verification group resets its exact half-open native range.
- M851 lines 74856-74884, SHA-256 `2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041`, was rechecked for 16 KiB block reservation/cancellation semantics.

The request ledger expresses actions and terminal outcomes. It does not add connection-recovery retries or execute transport operations.

Fix Round 1 (2026-09-15): the K04-owned public header carries generation, peer, selection, piece, block, offset, and length through normal then hotswap decisions. Hotswap emits cancel then request, and the ledger admits one terminal outcome.
