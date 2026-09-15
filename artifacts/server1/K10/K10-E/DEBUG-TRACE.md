# K10-E systematic debugging trace

The first combined native-source run passed five cases and timed out once in the real
magnet-before-metadata case. The factory emitted no failure, and an isolated rerun passed.
This localized the defect to fixture readiness rather than source parsing or alert handling:
the seeder exposed a non-zero listen port before its listen socket acceptance path was ready.

The deterministic native fixture now consumes real libtorrent alerts and waits for
`listen_succeeded_alert` before adding the seeded torrent or publishing its port. Repeated
magnet and close runs passed after that barrier, followed by the clean 59/59 aggregate.
Production transport timing was not weakened and no sleep was added to production code.

Negative control: generation validation was temporarily mutated from `generation == 0` to
`generation == UINT64_MAX`. The K10-E generation test failed, proving the case detects removal
of the required validation. The production condition was restored before final verification.
