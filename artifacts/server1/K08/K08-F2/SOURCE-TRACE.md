# K08-F2 source trace

This additive tranche exposes one engine-facing terminal-failure entry point on `FileReader`. It changes the public declaration and its lifecycle contract only. `FileReader.cpp` is deliberately unchanged until independent contract approval.

The contract makes failure first-wins, requires a non-empty reason, releases live reader work and the scheduler selection, preserves bytes already queued for the consumer, rejects late source completions, and exposes the reason once through `takeError()`.

Merge endpoint: fast-forward into `feature/colosseum-server-1.0` only after independent Sol contract/code approval, Agent 4 acceptance, and all K08 regression, mutation, and blob-seal gates pass.
