# K08-F2 source trace

This additive tranche exposes one engine-facing terminal-failure entry point on `FileReader`. It changes the public declaration and its lifecycle contract only. `FileReader.cpp` is deliberately unchanged until independent contract approval.

The contract makes the first active failure call win and normalizes empty input to the exact fallback `file reader failed`. It requires state/ownership detachment before external cancellation and deselection, exact-once cancellation of detached active reads, disposal of waiting and out-of-order work, preservation of the already-ready queue, reentrant/late-completion rejection, one-shot error observation, and strict isolation from sibling readers.

Merge endpoint: fast-forward into `feature/colosseum-server-1.0` only after independent Sol contract/code approval, Agent 4 acceptance, and all K08 regression, mutation, and blob-seal gates pass.
