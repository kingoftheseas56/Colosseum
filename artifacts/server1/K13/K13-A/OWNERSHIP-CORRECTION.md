# K13-A ownership correction

Date: 2026-09-07
Worker: `K13-A`
Packet: `K13`

## Original assumption

The work item listed only `native/colosseum_server_v1/src/settings/SettingsStore.cpp` as K13-A
production ownership. The implementation therefore first included
`server1/settings/SettingsStore.h` through a packet-local temporary declaration under
`artifacts/server1/K13/K13-A/probe-src/`.

## Live evidence

At base `e5660e27a320845ab993201918443e609e1a34d5`, the include directory had no
`native/colosseum_server_v1/include/server1/settings/SettingsStore.h`, and the K13-A/K13-B worker
entries assigned no production header. The first RED compile exited 2 with MSVC C1083 for the
missing `SettingsStore.cpp`; after the source was authored, the same missing declaration surface
would have blocked any normal consumer compile. A cpp-only implementation cannot expose the
`EffectiveSettingsContract` to K13-B or the packet manifest.

## Authorized correction

Sol authorized the smallest ownership correction: K13-A now owns the exact declaration previously
staged only for the probe at
`native/colosseum_server_v1/include/server1/settings/SettingsStore.h`. No other production source,
test, manifest, or shared file changed. The probe compile include path now resolves the production
header; the duplicate packet-local header was removed.

Affected workers: `K13-A`, `K13-B`. Affected barrier: `B-W2B`. Integration owner: `INT-W2`.
Topology: unchanged; K13-A still produces `EffectiveSettingsContract`, K13-B still consumes it,
and B-W2B remains the merge endpoint.
