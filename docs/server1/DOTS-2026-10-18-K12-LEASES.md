# K12 registry response leases (partial integration)

Parent source: e12bdaaed202dfc0b6aa569f49f5ec8443c5346d.
The existing EngineRegistry owns the counters and response obligations. This is
not full K12 acceptance or an HTTP streaming qualification.

## Implemented

- Noncopyable, shared EngineStreamLease: explicit close and destruction retire
  once, posting through the existing app lane. Registry destruction makes late
  leases harmless through weak ownership.
- Ready, valid files acquire independent file and engine counters. A response
  lease need not create a FileReader (the eventual HEAD path); reader EOF is not
  used as response completion. Actual HTTP attachment remains a subsequent gate.
- Generic/scoped active and inactive event order, reopen grace cancellation,
  multiple responses, module-default 30/60 second delays, and invalid-file refusal.
  The entrypoint's 20/120 second overrides still need host integration.
- Partial acquisition records only successfully acquired obligations. A rejected
  file timer cannot skip engine retirement. Queued dispatch drains the current
  batch before rethrowing the first callback error, preserving sibling cleanup.
- Active-event generation is diagnostic attribution to the acquiring engine.
  Counter identity remains M172/M662 hash/index. Generic/scoped pairing survives
  reentrant removal, while operational engine access remains revoked. Inactive
  events use the source missing-engine guard. This does not grant a retired
  engine reader, transport or storage capability.
- Shared timing types moved to EngineTiming.h to remove the Registry/Counter
  header cycle. The counter test now includes its own required thread header.

## Focused regression evidence

R18: response lease without reader; multiple readers; duplicate close; grace
reopen; RAII close; timer expiry; invalid index. Initial stub failed, fixed passes.
R19: real Qt timers, foreign registry destruction, lease outliving registry,
reentrant owner destruction; QT_FATAL_WARNINGS=1. Pass.
R20: file-timer rejection, file-active callback failure, engine-active callback
failure; precise partial retirement and later balanced recovery. Failed before
obligation tracking, passes after.
R21: active callback removes engine; source event pair order and original
attribution; no subsequent operational access or missing-engine inactive events.
R22: two different-file responses close in the same dispatch batch; the first
scheduler fails. The second cleanup is retained and later acquisitions balance.
Failed before dispatch hardening, passes after.

Run from configured build:

    cmake --build build/server1 -j2
    ctest --test-dir build/server1 -R 'R(18|19|20|21|22)-engine-registry' --output-on-failure
    ctest --test-dir build/server1 --output-on-failure --timeout 90

Linux setup and fixed-interface test qualification remain in LINUX-BUILD.md and
DOTS-2026-10-18-LINUX-QUALIFICATION.md. Detailed run totals and hashes accompany
this checkpoint in evidence/dots-2026-10-18/k12-leases-tests.txt.

## Still open

Cache/progress/idle observation, truthful statistics, source-facing retirement
visibility, file guessing, concurrency retention, HTTP lease attachment and
would-block/resume integration. The host remains streaming-ready=NO. No full
Stremio parity, audible playback, public swarm or Windows claim.

Full unmodified reference-process execution remains separately blocked by this
cloud's uv_interface_addresses error. Hash-locked module tests are not a
substitute for that final process-level differential gate. The external parallel
work-item authority file is still unavailable, as recorded in LAB-LINUX.md.
