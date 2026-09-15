# K10-G source trace

- `native/colosseum_server_v1/src/transport/LibTorrent2Adapter.cpp`: PauseAction dispatch,
  current-generation/idempotent state, ConnectAction-only deferral, network-tick resume,
  deferred cleanup, and zero/stale RequestAction/CancelAction ownership rejection.
- `native/colosseum_server_v1/tests/test_native_transport.cpp`: current-generation legacy
  fixtures plus K10-G real-wire pause, payload, deferral, resume, and stale ownership proof.
- `native/colosseum_server_v1/tests/cmake/K10.cmake`: registered K10-G packet gate.
- `artifacts/server1/K10/K10-G/run_pause.ps1`: two controlled endpoints and wire-log oracle.
- `artifacts/server1/K10/K10-G/run_behavioral_mutations.ps1`: five compiled real behavior
  mutations with exact failure signals.

Pause does not call `torrent_handle::pause()`. Existing requests and peer callbacks retain
their normal dispatch path; only new outbound ConnectAction reaches `deferredConnects_`.
