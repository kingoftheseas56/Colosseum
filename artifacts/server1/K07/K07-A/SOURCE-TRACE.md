# K07-A source trace

- Oracle: `server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- M845 lines 74645-74730, SHA-256 `6aa06288e0ad7a91abc168127b87c05eb071a13a2ec72e2219b500437ecf482a`: memory/fs modes, `floor(size/pieceLength)` slots, access timestamps, committed-only eviction, selection/lock exclusions, exact full-buffer error, reset on eviction, commit no-notify-have behavior, and verification-group reads.
- M846 lines 74730-74772, SHA-256 `fdff76e6c1b5a55a544a9de1d52f7882a63aef8a8d64a3425cd9f0218391c727`: selected window is `readFrom..selectTo`; stream reads lock piece indices and update the bounded window.
- M814 lines 72439-72764, SHA-256 `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`: circular store is selected only for circular-buffer mode and reset flows through the scheduler.

Native fs completion tokens carry slot generations. This makes the source callback lifecycle explicit: read-before-spill sees memory, and a stale/canceled/closed completion cannot replace a newer slot occupant.

Fix Round 1 (2026-09-15): K07 now exports its public store contract. Commit exposes the verification result, rejects failed verification, and returns `noNotifyHave=true` without claiming later scheduler integration.

Fix Round 2 (2026-09-15): commit hashes the complete required in-memory or spilled byte range using SHA-1. Missing pieces, missing spill files, absent expected hashes, and corrupt bytes fail before commit; only verified success returns `noNotifyHave=true`.

Fix Round 3 (2026-09-15): while a filesystem spill is pending, a repeated successful commit for the same piece and slot generation returns the existing token instead of creating another. Completing either reference first writes the original bytes once; the duplicate completion is rejected.
