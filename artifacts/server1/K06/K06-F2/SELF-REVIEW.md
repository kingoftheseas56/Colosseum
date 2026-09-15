# K06-F2 producer self-review

- Production change is confined to `PersistentPieceStore.cpp`; no public header or contract changed.
- Persisted false bits are still authoritative and are never inferred from physical bytes.
- Persisted true bits restore verified and committed only after complete logical coverage and required physical file lengths are proven.
- Missing, non-file, short, and logically incomplete destinations clear the affected bitmap bit. All invalid bits are persisted once after the constructor scan.
- Restored pieces remain unassembled. Memory-first staged reads and verify-before-commit semantics are unchanged.
- Cross-file and final-tail geometry uses the exact overlapping range required from each destination.
- Seven live production mutations proved the new assertions reject realistic wrong behavior.
- Focused, legacy, combined K06, and full aggregate gates are green on the restored candidate.
- This branch remains a candidate pending independent Sol review and Agent 4 acceptance. It is not integrated.
