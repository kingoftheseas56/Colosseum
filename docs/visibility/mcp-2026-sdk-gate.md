# MCP 2026-07-28 official Python SDK capability gate (Slice N1-SDK-Gate)

> **What this is.** A standalone research gate, run early/in parallel per Agent 0's 2026-08-13
> ordering note (it needs neither the app, the build dir, nor the locked binary). It proves —
> against the actually **installed** official MCP Python SDK, never against the release docs'
> word alone — whether that SDK can implement the current stateless 2026-07-28 core plus the
> formal **Tasks** extension, before N1-Protocol touches `native/tools/lanista-mcp/server.py`.
> **`server.py` was not edited to produce this document; Night Watch was not touched.**

**Baseline — record this first.**

| Fact | Value | Pin |
|---|---|---|
| SDK package | `mcp` | `pip show mcp` (isolated venv) |
| SDK version pinned | `2.0.0` | `pip show mcp` (isolated venv); [PyPI history](https://pypi.org/project/mcp/#history) lists it released 2026-07-28, the same day as the protocol revision |
| Companion types package | `mcp_types` | `2.0.0` |
| Install method | `python -m venv %TEMP%\mcp-sdk-gate-venv` then `pip install mcp` — **isolated**, outside the repo tree, no repo requirements/lock file touched | this doc's own probe run |
| Protocol release notes read fresh | <https://blog.modelcontextprotocol.io/posts/2026-07-28/> | WebFetch, 2026-08-13 |
| Tasks extension overview read fresh | <https://modelcontextprotocol.io/extensions/tasks/overview> | WebFetch, 2026-08-13 |
| SDK's own release notes read fresh | <https://github.com/modelcontextprotocol/python-sdk/releases/tag/v2.0.0> | WebFetch, 2026-08-13 |
| v0 facade transcript before/after | identical (12/12 `tests/test_lanista_mcp_forensics.py`, only wall-clock differs) | `artifacts/visibility-phase2/n1-sdk-gate/v0-transcript-{before,after}.log` |
| `native/tools/lanista-mcp/server.py` touched | **No** | `git status --porcelain` clean for that path throughout this slice |

---

## 1. Verdict

**Plan contradicted for N1's Tasks route.** The pinned SDK (`mcp==2.0.0`) genuinely implements
the stateless 2026-07-28 core, `server/discover`, and stdio transport — those three are real and
ground-truthed below. But it ships **no working route for the `io.modelcontextprotocol/tasks`
extension** at protocol 2026-07-28: `tasks/get`, `tasks/cancel` exist only as unreachable
"types-only" classes (the SDK's own source comment says so explicitly), and `tasks/update` does
not exist in the SDK **at all**, at any protocol version. A live stdio JSON-RPC round trip against
a real `MCPServer` confirms this behaviorally, not just by static reading: all three `tasks/*`
methods return `-32601 Method Not Found` even with a fully valid stateless envelope attached.

Per program ruling 5 and this slice's own instructions: **N1-Protocol, N1-Tasks, N1-Register, and
N1-First-Run do not proceed on the Tasks route this SDK version offers.** N0 (standalone Night
Watch, no MCP Tasks) remains the path; nothing about N0-Runner or N0-Battery is affected by this
finding, since neither depends on this slice.

This is exactly the "legitimately either way" outcome the plan names as valuable — it is not
papered over. Four of the eight named capability checks are correctly, honestly red; see §5 for
why the harness treats that as the correct terminal signal rather than something to route around.

## 2. What genuinely works (ground-truthed, not assumed)

| Capability | Result | How it was proven |
|---|---|---|
| Stateless per-request core (no `initialize`/`initialized` handshake) | **Supported** | Live probe: a real `tools/call` succeeded over real stdio with `session.initialize_result` staying `None` for the whole process — `initialize` was never sent. Static: `("initialize", "2026-07-28")` is **absent** from `mcp_types.methods.CLIENT_REQUESTS`; the handshake is registered only for `2024-11-05` through `2025-11-25`. |
| `server/discover` | **Supported** | Live probe: `server/discover` returned `supportedVersions: ["2026-07-28"]` and the session negotiated `protocol_version == "2026-07-28"`. Static: `("server/discover", "2026-07-28")` is registered in `CLIENT_REQUESTS`/`SERVER_RESULTS`, and `mcp/server/lowlevel/server.py:448` wires a real handler (`self._handle_discover`), not just a type. |
| stdio transport | **Supported** | The entire probe (discover + tool call + three raw `tasks/*` probes) ran over a **real spawned subprocess's stdin/stdout pipes** (`mcp.client.stdio.stdio_client` talking to `mcp.server.mcpserver.MCPServer.run("stdio")` in a child `python.exe`), not an in-process shortcut. |
| Normative status enum values | **Match** | `mcp_types.TaskStatus = Literal["working", "input_required", "completed", "failed", "cancelled"]` is value-for-value identical to the ratified 5-state Tasks lifecycle table. (The type is defined but unreachable via live 2026-07-28 dispatch — see §3 — so this is a values-only match, not proof the enum is exercised on the wire.) |

## 3. What genuinely does not work

| Capability | Result | How it was proven |
|---|---|---|
| Extension capability negotiation for Tasks specifically | **Not supported** | Live probe: `server/discover`'s `capabilities.extensions` is `null` and `capabilities.tasks` is `null` — nothing is advertised. Static: `mcp.server.apps.Apps` (`io.modelcontextprotocol/ui`) is the **only** `Extension` subclass shipped anywhere in the installed `mcp` package (grep-verified zero other `identifier =` class attributes under `mcp/`). The SDK's own v2.0.0 release notes state under "Known gaps": *"The tasks extension (SEP-2663) is not part of this release."* |
| Task-augmented tool calls | **Not supported** | The entire `mcp_types/_v2026_07_28/__init__.py` module (generated straight from `schema/2026-07-28.json`) contains **zero** occurrences of `Task` or `tasks/` — no `ToolExecution.task_support` field, no `CreateTaskResult`/`resultType: "task"` shape exists at this protocol version at all. |
| `tasks/get`, `tasks/update`, `tasks/cancel` | **Not supported** | Live probe (the decisive evidence): all three raw JSON-RPC calls, sent **with** a fully valid stateless envelope (`io.modelcontextprotocol/protocolVersion` + `io.modelcontextprotocol/clientCapabilities` in `_meta`, so the failure can't be blamed on envelope validation), returned `{"code": -32601, "message": "Method not found"}`. Static: `mcp_types/methods.py` marks every protocol-version section with `# 2025-11-25 (tasks/* deliberately absent)` (repeated at 4 separate method-surface sections) — `CLIENT_REQUESTS` carries **zero** `("tasks/*", <any version>)` entries, for any version the SDK knows, not just 2026-07-28. `tasks/update` doesn't exist as a type or method anywhere in the installed distribution — only `tasks/get`, `tasks/cancel`, and the legacy `tasks/result` exist, all types-only. |
| `tasks/get` terminal result/error carriage | **Not supported** | Follows directly from the above: there is no live route to reach a terminal task state at all. The typed `GetTaskResult(Result, Task)` class exists in `mcp_types/_types.py`, but the module's own comment states outright: *"Tasks: introduced in 2025-11-25, removed from the core spec in 2026-07-28 (continuing as an extension). Defined here types-only; their methods are not in the request/notification unions below, so they are never dispatched."* |

## 4. A deliberate SDK escape hatch that stays out of scope here

The SDK ships a real, general-purpose extension-plugin framework
(`mcp.server.extension.Extension`, `MethodBinding`, `ToolBinding`) that a project could use to
**hand-build** a Tasks extension on top of it — `MethodBinding`'s own docstring literally uses
`tasks/get` as its illustrative example (`mcp/server/extension.py:59`), and the framework's
`ServerCapabilities.extensions[identifier]` advertisement path is exactly what real capability
negotiation needs. This is real, working infrastructure, not vaporware.

It is out of scope for N1 anyway: program ruling 5 requires the official SDK to *already* support
the ratified Tasks surface — *"it does not hand-roll a second task protocol."* Building our own
Tasks extension on top of the SDK's generic plugin hook would be exactly that hand-rolling, just
one layer down from the wire. This gate's job is to check whether the SDK arrives with Tasks
already proven, not to build the missing piece ourselves.

## 5. Why the failing tests are correct, not a bug to fix

`tests/test_mcp_2026_sdk_gate.py`'s 8 named cases each assert their capability **is** supported.
Four fail red against the real, unmutated contract — deliberately. The plan's own honest-outcome
rule is explicit: *"If it does NOT [support Tasks] → report Plan contradicted plainly, with the
evidence... do NOT paper over a missing feature to force a green."* Weakening those 4 assertions
to tolerate `false` would silently convert a real gap into a fake pass. If a future SDK release
ships real Tasks support, re-probing (rerun `artifacts/visibility-phase2/n1-sdk-gate/probe/
probe_client.py` against the new version, update `tests/contracts/mcp-2026-sdk-gate.json`) is the
correct way to flip those cases green — not editing the test file's assertions.

## 6. Negative control

In a temporary copy of `tests/contracts/mcp-2026-sdk-gate.json`, `statelessCore` was flipped
`true → false`. Scoped to the 8 plan-named cases (`McpSdkGateNamedCaseTests`): exactly
`test_stateless_core_supported` newly went red (5 failures vs. the real contract's 4); the other 7
cases' pass/fail status — including the 4 already-red Tasks cases — were unchanged. Restoring the
mutation brought the suite back to exactly the real contract's baseline (4 failures, the same 4
Tasks-related ones), confirming the harness is sensitive to `statelessCore` specifically rather
than either always-green or always-red.

One honest side effect, reported rather than hidden: running the **full** test file (not scoped to
the named-case class) against the mutated copy also fails `test_missing_features_list_matches_
false_capabilities` — a bonus shape-consistency check this slice added beyond the plan's 8 named
cases, which correctly notices that the mutated copy's `missingFeatures` list no longer matches
its (now 5, not 4) false capabilities. That collateral failure is expected given a
partial/synthetic mutation, not a defect in the control; see
`artifacts/visibility-phase2/n1-sdk-gate/negative-control-direction-a-red.log` (full suite, 6
failures) vs. `...-named-cases-only.log` (8 named cases, 5 failures — the mandated control) vs.
`...-direction-b-restored.log` (8 named cases restored, back to 4 failures).

## 7. Evidence index

- `tests/contracts/mcp-2026-sdk-gate.json` — the machine-readable contract this doc summarizes, with one citation per claim.
- `tests/test_mcp_2026_sdk_gate.py` — the 8 named cases plus contract-shape tests.
- `artifacts/visibility-phase2/n1-sdk-gate/probe/probe_server.py` — the throwaway one-tool `MCPServer` fixture the probe drives.
- `artifacts/visibility-phase2/n1-sdk-gate/probe/probe_client.py` — the real stdio JSON-RPC probe (discover, tool call, three raw `tasks/*` calls with a valid envelope).
- `artifacts/visibility-phase2/n1-sdk-gate/probe.json` — the probe's full raw output (this doc's §2/§3 tables are a summary of this file).
- `artifacts/visibility-phase2/n1-sdk-gate/pip-freeze.txt` — the isolated venv's exact dependency pins.
- `artifacts/visibility-phase2/n1-sdk-gate/test.log` — full `test_mcp_2026_sdk_gate.py` run against the real contract (12 pass, 4 fail).
- `artifacts/visibility-phase2/n1-sdk-gate/negative-control-direction-a-red.log`, `...-named-cases-only.log`, `...-direction-b-restored.log` — the negative control, both directions (§6).
- `artifacts/visibility-phase2/n1-sdk-gate/v0-transcript-before.log`, `v0-transcript-after.log` — `tests/test_lanista_mcp_forensics.py` (v0/F1-Bridge facade's own Python-layer tests) rerun unchanged before and after the isolated SDK installation, proving zero contamination of the repo's own Python.
