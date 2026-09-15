# K06-F2 producer self-review

- Production change is confined to `PersistentPieceStore.cpp` plus the bounded `VerificationBitmap.cpp` review repair; no public header or contract changed.
- Persisted false bits are still authoritative and are never inferred from physical bytes.
- Persisted true bits restore verified and committed only after complete logical coverage and required physical file lengths are proven.
- Missing, short, and logically incomplete destinations clear the affected bitmap bit. All invalid bits are persisted once after the constructor scan.
- Bitmap persistence checks open, complete write, and flush state. A failed invalidation persist throws out of the store constructor, so no restored committed state escapes while disk still records stale truth.
- Restored pieces remain unassembled. Memory-first staged reads and verify-before-commit semantics are unchanged.
- Cross-file and final-tail geometry uses the exact overlapping range required from each destination.
- Eight tracked production mutations and an isolated deterministic runner prove the assertions reject each recorded wrong behavior; the raw outputs and exact patches are reproducible.
- Focused K03, focused/repeated K06, combined K06, and full aggregate gates are green on the repaired candidate.
- `setDestination()` remains a process-local override; no restart-persistence claim is made.
- This branch remains a candidate pending independent Sol review and Agent 4 acceptance. It is not integrated.
