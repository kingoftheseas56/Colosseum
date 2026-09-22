# K11-A repair behavioral mutations (K11-v4)

Producer: `[Agent (Claude), K11-A repair producer]`.

`run_mutations.py` applies one edit, rebuilds `server1_k11_engine_registry_test` in `mutant-build/`, runs the listed checks, and restores both production files with a SHA-256 check. `oracle` is `run_oracle.js` in dry-run mode against the mutant binary. Final run: 19/19 killed, sources restored (`raw/repair/MUTATION-RESULTS.json`, `raw/repair/mutation-run-final.stdout.txt`, per-check logs in `raw/repair/mutants/`).

| Mutant | Finding | Killing checks |
|---|---|---|
| strand-runs-inline-on-app-lane | 1 app lane | R1 (slow open stalled app lane), K11-02 (work trace off lane) |
| destruction-delivers-on-destroying-thread | 2 lifetime | R2 (callback on destroying thread) |
| destruction-rewrites-decided-removal | 2 lifetime | R2 (fenced finalize lost decided Removed) |
| timer-interval-500 | 3 timer | R3, oracle |
| timer-never-cancelled | 3 timer | R3, K11-02, oracle |
| timer-ticks-after-close | 3 timer | oracle (ticks after cancel) |
| timer-tick-without-rechoke | 3 timer | R3, oracle |
| peer-adds-never-drained | 4 peer search | R4 |
| malformed-port-accepted | 4 peer search | R4 |
| closed-generation-still-connects | 4 peer search | R4 |
| wire-event-skips-peer-search | 4 peer search | R4 (no-swarm-cap hysteresis) |
| staged-bytes-uploadable | committed-only upload | R5, K11-02 |
| staged-piece-marked-visible | committed-only visibility | R5, K11-02 |
| scheduler-decision-discarded | scheduler owns requests | K11-02 |
| stale-generation-block-accepted | generation ownership | K11-02 |
| callbacks-after-global-ready | 5 M172 order | oracle |
| create-event-carries-id | 5 M172 options | oracle |
| reuse-rebinds-swarm-cap | 5 M172 isNew binding | K11-01 |
| create-does-not-resume-swarm | 5 M172 resume | K11-01, R4 |

The first full run (`raw/repair/mutation-run-1-with-survivors.stdout.txt`) killed 16/19. Three survivors exposed test gaps, not engine defects: R2's close could finish before destruction (fixed by fencing the work lane), R4's swarm-cap updater also triggers the M612 update so the wire-listener mutant was equivalent there (fixed with a no-swarm-cap hysteresis case), and the work-lane `isCommitted` guard masked the staged-visibility mutant (fixed with a reader starting inside the staged piece). All three are killed in the final run.

## Round 2 (partial restore)

`K11_MUTATION_OUT=repair2 python run_mutations.py`: 24/24 killed on the first run, sources restored (`raw/repair2/MUTATION-RESULTS.json`, `raw/repair2/mutation-run.stdout.txt`, `raw/repair2/mutants/`). New mutants, each killed by R6 (`regressionPartialRestore`):

| Mutant | Killing checks |
|---|---|
| advertise-without-group-completeness | R6, K11-02 |
| upload-without-group-completeness | R6 |
| restored-survivor-not-restaged | R6 |
| group-completion-selection-disabled | R6 |
| failed-group-stays-visible | R6 |

`staged-bytes-uploadable` now removes the app-lane real-piece guard and both work-lane guards.

Differential-level controls (`raw/comparison.json` `controls`): 24 single-fact alterations of the native or source raw log, each detected by the comparison.
