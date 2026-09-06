# Colosseum Server 1.0 decisions

## Immutable execution identity

Server 1.0 execution uses branch `feature/colosseum-server-1.0` in this isolated worktree from immutable base `81e750b65f3788af7be99392f16b86a9212ff2bd`.

The merge endpoint is the complete feature branch into `master` only after Q04 passes parity, platform, embedded-player, exception, rollback, and independent-review gates. If that endpoint is abandoned or fails, record retirement and do not merge.

## Worktree provisioning ruling

The execution ledger at `.superpowers/sdd/PARALLEL-EXECUTION-PLAN/progress.md` records the controlling ruling: the isolated worktree was provisioned before W0 writes under Hemanth's current execution instruction. This does not advance P03. P03's Server 1.0 skeleton/build work remains blocked by P00, P01A, and P08A, with P08A required before P03 implementation.

## Source authority

Authority remains, in order:

1. The exact authenticated Stremio `server.js` bundle locked by bytes and SHA-256.
2. Exact dependency revisions encoded by that bundle.
3. Upstream source at those exact revisions, used only to explain bundled behavior.
4. Forensic notes derived from the same specimen.
5. Secondary context, which cannot override the oracle.

The locked identity pair is CDN selector `v4.21.1` and embedded `stremio-server` version `4.21.0`. The authoritative file is 6,676,503 bytes with SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.

## Server 0.1 boundary

Server 0.1 specimen commit `a3fcaa96ec2650014e1dd94f603d76b2b1e48387` is distinct from historical application master `2deed52cb8a930219affd3791f2c6486ec8c37be`. Server 0.1 is comparison evidence only and is not a donor or correctness authority by default.

P01B is nonblocking. Failure to reconstruct or benchmark Server 0.1 must produce `BASELINE_UNAVAILABLE`; it cannot block Server 1.0 source-led correctness, G-NATIVE, G-TORRENT, composition, or release qualification.

## Frozen substrate and scope

Server 1.0 parity work remains on C++17 and frozen libtorrent 2.0. Libtorrent 2.1, WebTorrent, synchronization features, policy modernization, and deliberate performance deviations remain deferred until after the 1.0 parity baseline unless an explicit accepted deviation changes scope.

The input `SPEC.md` is preserved byte-for-byte. Clarifications and execution deviations belong in this decisions ledger and must never rewrite the source specification or alter the 64-packet graph.
