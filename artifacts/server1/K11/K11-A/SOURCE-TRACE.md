# K11-v3 source and behavioral trace

Producer: `[Agent 4 (Codex/Sol subagent), K11-v3 producer]`

The exact oracle bundle SHA-256 is `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`. Executed factory identities are M172 `bf9f64c9a27d9121f4360f2bd759206af4e7692c9592d44d164514d8d4230486` and M814 `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`. They are the authenticated identities associated with M172 LF lines 18110-18500 and M814 LF lines 72439-72764. Raw `sed` slicing uses different byte boundaries and is not substituted for the frozen identities.

`run_oracle.js` rehashes the bundle, exposes the unchanged factory table in memory, executes M172, and executes actual M814 behavior with controlled swarm, wire, storage, filesystem, random-independent clock path, and timers. Corrupted whole-bundle and factory negatives are rejected.

M172 parity covers stream alias before every hook, reordered continuations sharing one construction, shallow options, falsy path fallback, fresh ids, create/created and scoped/global ready ordering, resume snapshots, and callback order.

M814 parity covers 1 MiB verification to 512 KiB virtual geometry, virtual piece 1 to wire `[0,524288,1]`, first-request retry, 34 group requests, partial write/incomplete verify, group write/success verify/commit, HAVE 0, committed visibility/notify, mapped upload byte 200, and teardown timer cancellation. Native `--trace` matches the normalized projection exactly.

Explained divergence: source M814 internally emits `download(virtual 1)` before group commit and notifies selection twice. K11 preserves the frozen safety contract: readers/uploads see only committed persistent pieces, corruption resets the group, and one verification-piece HAVE follows commit. Raw source retains the early event; the shared projection records committed visibility. There are zero unexplained differences.

Approved lifecycle divergence: generations isolate remove/recreate; G1 completes once as Removed and cannot affect G2. Destruction completes unfinished creates once as Cancelled. Source errors are terminal, emit scoped/global error before SourceError, and never fake Ready. Deferred work/callback lanes, weak-core tasks, and cancellable repeat timers protect lifetime.
