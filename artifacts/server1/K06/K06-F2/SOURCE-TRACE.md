# K06-F2 restored-commit source trace

Producer: `[Agent 4 (Codex/Sol subagent), K06-F2 producer]`

Base: `2dc47a3ed6b5650b6b971d972f19f425b1b057e8`

Branch: `sol/server1-w3-store-restored-commit-20260915`

Merge endpoint: fast-forward into `feature/colosseum-server-1.0` only after independent Sol review, Agent 4 acceptance, focused K06 regression gates, meaningful behavioral mutation/negative controls, and the required aggregate gates pass.

Production scope: `native/colosseum_server_v1/src/storage/PersistentPieceStore.cpp` and the bounded review repair in `native/colosseum_server_v1/src/storage/VerificationBitmap.cpp`. No public contract change.

The inherited reopen audit requires persisted true bits to restore both verified and committed state only after validating complete logical file-map coverage and complete physical destination coverage. Invalid persisted bits are cleared and persisted during construction. Persisted false bits are never inferred from disk bytes. Restored pieces remain unassembled, and staged reads retain memory-first behavior.

Review repair: bitmap persistence now fails loudly when its destination cannot open, accept the complete write, or flush. If constructor invalidation cannot durably clear a stale bit, construction throws and no `PersistentPieceStore` is exposed. Destination overrides remain process-local; this packet does not claim that a `setDestination()` override survives restart.
