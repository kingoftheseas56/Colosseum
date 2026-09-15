# K05-A2 implementation source trace

- `native/colosseum_server_v1/src/policy/SwarmPolicy.cpp`: generation-bound optional query
  and exact one-state lifecycle counting under the registry's existing ownership style.
- `native/colosseum_server_v1/tests/test_swarm_metadata.cpp`: full lifecycle, replacement,
  stop, absence, stale ownership, current-empty, and info-hash isolation matrix.
- `native/colosseum_server_v1/tests/cmake/K05.cmake`: registered K05-A2 aggregate test.
- `artifacts/server1/K05/K05-A2/run_behavioral_mutations.ps1`: four compiled behavioral
  mutations with exact failure signals.

The query is read-only and follows the registry's existing single-owner access model; it does
not introduce a separate mutex or alter peer transitions.
