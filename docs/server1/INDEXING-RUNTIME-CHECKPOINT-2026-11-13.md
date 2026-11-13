# Native indexing and pinned-runtime checkpoint

This checkpoint extends the native server with explicit local indexing and
persistence, uncached torrent metadata lookup, device information and hardware
profiling, and the pinned portable Node22 runtime compatibility profile.
Production remains native C/C++; Node is used only by oracle tests.

## Qualification

A clean independent Release rebuild passed **206/206 registered tests**, with
zero failures or skips, in 183.06 seconds. The oracle was official Node22.16.0
with libuv1.49.2. Frozen input manifest SHA256:
`10826cd236b5ab0776d7a29fdf97ed8190e7827c06a8e323b28b84988c4ba4de`.
CTest log SHA256: `59bb773e3e1ea1a8fce5d1c02caaf1be060299c26f81b7cd07cd55a32ccaa93f`.

The regex runner now checks 24,365 exact matching cases and eight nested file
selection cases, including Unicode16 data, parser restrictions, class-set
algebra, seeded grammar cases and repetition-register memory safety. The prior
implementation serves as a negative control rather than a changed expectation.
The statistics backend matches the pinned libuv version; wrong-runtime tests
continue to reject incompatible profiles.

Production smoke tests cover FTP and NNTP/archive ranges, passive endpoints,
explicit synthetic-torrent indexing, database-only reload, metadata and stream
projection, and graceful pending-request shutdown. A preloaded synthetic torrent
also exercises uncached local-addon metadata through the real engine registry,
with no roots, database or peer discovery; the catalog remains empty.

The actual application was rebuilt and run offscreen in a disposable profile.
First local-media HLS playback and independent FFmpeg decode succeed before the
smoke test requests device information. Heartbeat, settings persistence and
passive addon/casting routes also pass. Application binary SHA256:
`bb020904ac619d2dc15959c72db1a2c46fc9e8bae5bb6a0699e960dd8f374078`.
Browser sandboxing and certificate validation were not disabled.

## Reproduced startup interaction and explicit repair

The first integrated snapshot passed 203/205 tests. Two real HLS tests failed
because startup hardware probes could evict the first playback converter from
the shared, default one-source HLS registry. Both failures reproduced three
consecutive times. Disabling profiling in otherwise unchanged controls made both
pass three times; those controls did not replace the production assertions.

The pinned source starts profiling at listener startup and shares this ownership.
Native profiling deliberately uses fresh private conversion ownership for each
attempt, configured from current settings. Playback's existing source LRU and
public routes remain unchanged. Repeated IDs, overlapping profiler calls,
timeouts and cancellation do not reuse destroyed converters or cancel playback.
Sample input URLs still use the existing loopback listener.

The deterministic regression preserves the original shared-registry failure and
checks the isolated behavior. The helper/test passed ASan+UBSan; their existing
linked libraries were not all rebuilt with sanitizers. LeakSanitizer could not
run under the execution environment's ptrace restrictions. These results do not
constitute a whole-server sanitizer or real-GPU qualification.

One aggregate run was interrupted by execution-environment recovery. Its partial
log was retained separately, and the complete 206-test run restarted against the
same unchanged snapshot. The interrupted run is not counted as a pass.

## Boundaries

- Local discovery requires explicit roots/database; online title recognition is
  opt-in. No user media or public recognition provider was used in tests.
- Sample payloads are external-only in this checkpoint. Missing samples return
  404, and device information never fabricates verified acceleration. Original
  upstream media bytes are not bundled.
- New secure FTP/NNTP transport and active casting-provider prototypes are not
  integrated or published here. The existing listener remains loopback-only.
- Public-peer transfer, current YouTube success, real casting/GPU devices, the
  unavailable ffsplit runtime, Windows and full Stremio-runtime qualification
  remain unproven or incomplete. Earlier documented source-bug and defensive
  bounds differences remain explicit.

This is a tested development checkpoint, not a release or a claim of literal,
complete server.js parity. See [local runtime integration](LOCAL-RUNTIME-INTEGRATION.md),
[pinned runtime](PINNED-NODE22-RUNTIME.md),
[regex class sets](SAFE-REGEX-CLASS-SETS-2026-11-13.md), and
[protocol scope](PROTOCOL-CHECKPOINT-2026-11-13.md).
