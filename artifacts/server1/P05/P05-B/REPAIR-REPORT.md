# P05-B bounded review repair

Review finding: `TraceComparator.compare()` returned PASS for identical malformed traces because raw equality ran before contract validation.

Repair: both expected/oracle and actual/candidate traces now validate the array container, required fields and types, one-based contiguous integer sequence, and `stream.terminal` uniqueness by identity plus generation before equality. Malformed expected input returns the distinct `oracle-contract-invalid` rejection; malformed actual input returns deterministic candidate contract errors. The five declared P05-01 fixture rejection outputs remain unchanged, and P05-02 still uses qualified Node `v22.16.0`.

Evidence: `REPAIR-RED.raw.txt`, `REPAIR-GREEN.raw.txt`, `REPAIR-P05-A-REGRESSION.raw.txt`, `REPAIR-VALIDATION.raw.txt`, and the final case receipt.
