# Colosseum Verification Tooling Map — the external-tools survey, ruled

> The research arc the original Lanista guide deferred ("compare external desktop,
> accessibility, browser, network-replay, video, performance, and CI orchestration tactics
> against the in-process Lanista model") — surveyed 2026-08-06, ruled by Hemanth with
> Agent 0. **This is a map, not a work order.** Nothing below gets built without a task
> demanding it; the ledgers stay the authority on what exists.

## The standing verdict

Lanista is not replaced by any of these; it is the semantic center. The layered model we
already run (Qt Test → Qt Quick Test → Lanista sessions → pixels → Hemanth's eyes) is the
survey's own recommended architecture, minus the layers that haven't earned a customer.

## Item verdicts

| Tool / idea | Verdict | Why |
|---|---|---|
| Qt Test + Qt Quick Test | **DONE** | The 2026-08-06 arc: CTest seam, pilots, ledger, skills. |
| Lanista semantic commands (`open-series`, …) | **QUEUED (Planned arc 2)** | The semantic-contract arc; waits for its first real customer, per demand-driven doctrine. |
| Structured snapshots / event logs | **QUEUED (Planned arc 3)** | Deferred on demand; the two vacuous-pass traps from the pilot are its evidence file. |
| Accessibility / stable identifiers | **PARTIALLY LAW** | World-namespaced automation names are a binding convention (Lanista ledger, field-learned traps). Full accessible-role metadata waits for the OS-automation layer to demand it. |
| **FlaUI / winapp ui (outside-the-process Windows automation)** | **THE NAMED CANDIDATE for the queued window-restore demand** | The one new capability class here: it acts where Lanista structurally cannot (taskbar, real focus, window restore — our standing Bridge blocked item), and it verifies what Windows shows the user, not what the app claims. When minimize/restore gets its customer, the build decision is: in-process `window-set-state` vs. a FlaUI-MCP prototype. Prerequisite either way: accessible names on the shell chrome. |
| GammaRay-style deep reflection | **REJECTED as a bridge surface** | Unrestricted QObject/property dumping is a standing stop-condition; the invoke-read allowlist discipline exists precisely to not become this. GammaRay itself remains fine as a human debugging tool. |
| Squish | **REJECTED** | Commercial; overlaps capabilities we now own in-repo. |
| Appium | **REJECTED** | Cross-platform machinery for a Windows-only app; FlaUI is the sharper tool if the OS layer is ever built. |
| Robot Framework | **REJECTED** | A readability layer over scenarios; our scenario JSON + workflow skills already carry that role without a new runtime. |
| UFO² | **REFERENCE ONLY** | Its lesson is adopted as doctrine: an agent chooses between control surfaces (semantic command > accessibility > pixels); Lanista routes, it doesn't grow one giant command list. |
| OSWorld / Windows Agent Arena | **FAR-FUTURE REFERENCE** | A "Colosseum Arena" for scoring agents is a fun idea with no current customer. |
| DeepEval / Promptfoo (LLM-judged agent eval) | **REJECTED as a judge** | Our pressure tests are human-scored by doctrine; an LLM asserting the reader "probably restored correctly" is the evidence-theater this whole system exists to kill. Permissible someday strictly ON TOP of deterministic evidence, never instead of it. |

## The one sentence to carry

Every tool above offers a viewpoint; the ledgers say which viewpoints exist today, and a
viewpoint gets built when a slice is blocked without it — never because the map shows it.
