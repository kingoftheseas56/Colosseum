# K12 source speedometer utility (partial)

Parent: b3b77ec40ae2bdc298c78a5aa9bacdee23a63b20.

EngineSpeedometer ports the locked M416 cumulative ring algorithm, with an
owner-supplied shared modulo-65536 quarter-second clock. It introduces no timer,
thread or transport observer. Calls are serialized by its existing owner.
The typed window accepts integer seconds 0..16383 (0 means the source default 5).
Dynamic JavaScript arguments outside that typed contract are not qualified.

Fourteen controlled source/native differential cases cover startup, same-tick
bursts, idle decay, ring overwrite, zero/default window, tick wrap/full-wrap
alias, independent meters sharing a clock, negative deltas, NaN and infinities.
Non-finite diagnostic strings distinguish NaN from both infinities; they do not
specify HTTP serialization. Three native ownership checks cover absent clock,
invalid window before clock invocation and a throwing clock preserving state.

Oracle whole-file SHA256:
405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f
M416 byte range [1985144,1985973), SHA256:
63410c90938208354e1c4c0db52f340008d227794ed7c16367db8e09f81b60cf
Both identities are enforced before execution. The source factory runs with a
controlled 250ms interval callback. No oracle source is patched.

## Reproduction

Configure the existing native project with BUILD_TESTING=ON and
SERVER1_K12_ORACLE pointing to the exact locked server.js. Node must be installed.

    cmake --build build/server1 -j2
    ctest --test-dir build/server1 -R K12-speedometer --output-on-failure

The same differential runner can be invoked directly:

    node tools/server_lab/oracle/k12_speedometer.cjs server.js tools/server_lab/cases/K12-speedometer.json build/server1/p03-tests/server1_k12_speedometer_test

Ordinary complete native suite: 103/103 PASS, zero skipped, 69.21 seconds.
Focused matching RelWithDebInfo AddressSanitizer: 2/2 PASS (detect_leaks=0).
Differential also passes on official Node 22.16.0; full CTest used Node 24.19.0.
Details and binary identity are in evidence/dots-2026-10-18/speedometer-tests.txt.

## Retained failures and limits

Initial target invocation preceded CMake reconfiguration and failed as an unknown
target. Reconfiguration repaired setup. The deliberately incomplete zero-return
implementation failed differential comparison; its original output is retained.
This is test sensitivity evidence, not a pre-existing production defect claim.

Live shared-clock scheduling, transport feed integration and M172 HTTP statistics
serialization remain unfinished. This utility is not end-to-end throughput proof.
Full locked server startup remains blocked by uv_interface_addresses errno 1 in
this environment. Windows and LeakSanitizer are not qualified. Overall Server 1.0
parity and HTTP-to-player streaming remain open.
