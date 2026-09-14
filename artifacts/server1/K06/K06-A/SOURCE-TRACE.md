# K06-A source trace

Oracle SHA-256: `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

- M814 72439-72764 / `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`: virtual-to-real verification groups, assembled bitfield, corrupt-group reset, and commit-before-verify notifications.
- M830 73605-73630 / `fe603735d0586d911a767f0cc8996681c3951b1f4a38622eeef630ca55b22a29`: persisted verification bitmap load/get/set semantics.
- M844 74529-74645 / `71a56c9f02ee5ed434085228d1e585949cd79c2f7081f270ffe783b4cd27a730`: numeric destinations, destination replacement, memory-first reads, cross-file segments, serialized writes, SHA-1 verification, queued close, and callback multiplicity.

The M844 successful commit callback is invoked once per committed virtual piece and once by the terminal `async.each` callback. `CommitResult.callbackCount` preserves that measured N+1 behavior instead of normalizing it to once-only.

Fix Round 1 (2026-09-15): geometry is rejected before division; restage clears memory and bitmap verification; commit requires verified staged bytes plus complete destination coverage; queued write errors precede close in the causal ledger.
