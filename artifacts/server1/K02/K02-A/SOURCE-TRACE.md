# K02-A source trace

Oracle: `server.js`, 6,676,503 bytes, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

Authority: M851 lines 74856-74884, frozen SHA-256 `2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041`.

- K02-01 follows lines 74859, 74863-74868: 16 KiB parts, exact tail remainder, monotonic reservations, and `-1` exhaustion.
- K02-02 follows lines 74869-74875: cancellation stack pop order and first-set-only counters.
- K02-03 follows lines 74876-74882 plus M814 lines 72564-72566: incomplete flush returns null, complete flush is terminal, and the caller's stable piece/generation identity rejects a completion targeting a replacement.

The native contract adds an explicit generation argument at the completion boundary. It does not add network, disk, scheduling, or retry policy to `PieceBuffer`.

Fix Round 1 (2026-09-15): source reservation/cancellation behavior is now represented by an explicit per-block ownership state. Cancel-before-reserve, repeated cancel, and cancel-after-delivery are rejected without widening K02 into transport policy.
