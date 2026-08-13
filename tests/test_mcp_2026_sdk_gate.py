#!/usr/bin/env python3
"""test_mcp_2026_sdk_gate.py — validates tests/contracts/mcp-2026-sdk-gate.json

Slice N1-SDK-Gate (Agent Visibility Phase 2, run early/parallel per Agent 0's
2026-08-13 ordering note — standalone SDK research, touches neither the app
nor native/tools/lanista-mcp/server.py). Ground-truthed against the actually
installed official MCP Python SDK (package `mcp`, pinned 2.0.0, isolated venv
outside the repo) via a real stdio JSON-RPC round trip
(artifacts/visibility-phase2/n1-sdk-gate/probe/probe_client.py ->
artifacts/visibility-phase2/n1-sdk-gate/probe.json) plus static source
inspection of the installed distribution — never accepted on the release
docs' or a model's word alone.

HONEST-OUTCOME RULE (do not weaken these assertions to force a green suite):
each of the 8 named cases below asserts that its capability IS supported.
Four of them are expected to fail RED against the real, unmutated contract,
because the installed SDK genuinely does not implement the Tasks extension
for protocol 2026-07-28 (see verdictReason in the contract and
docs/visibility/mcp-2026-sdk-gate.md). That red is the correct, valuable
signal this gate exists to produce — Plan contradicted for N1; N0 (standalone
Night Watch) remains the path. A future SDK release that actually ships
Tasks support would flip the corresponding case(s) green with no code change
here, only a contract update after re-probing.

Usage:
    python tests/test_mcp_2026_sdk_gate.py

Negative control:
    Point MCP_SDK_GATE_CONTRACT_PATH at a TEMPORARY copy of the real contract
    with exactly top-level "statelessCore" flipped true -> false. Only
    test_stateless_core_supported may go red in the mutated copy; every other
    case's pass/fail must be UNCHANGED from the real contract (in particular,
    the four Tasks-related cases stay red in both the real and mutated copy —
    this control proves the harness is sensitive to statelessCore, not that
    the suite goes fully green). Never edit the committed contract to run
    this control — copy it, mutate the copy, point the env var at the copy,
    then unset it (or don't set it) to rerun against the real contract.

        set MCP_SDK_GATE_CONTRACT_PATH=<path to mutated temp copy>   (cmd)
        $env:MCP_SDK_GATE_CONTRACT_PATH = "<path>"                   (PowerShell)
        MCP_SDK_GATE_CONTRACT_PATH=<path> python tests/test_mcp_2026_sdk_gate.py  (sh)
"""

import json
import os
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONTRACT_PATH = REPO_ROOT / "tests" / "contracts" / "mcp-2026-sdk-gate.json"

# capability field name -> the exact test-case name the plan specifies
NAMED_CASES = {
    "statelessCore": "stateless_core_supported",
    "serverDiscover": "server_discover_supported",
    "tasksExtensionNegotiates": "tasks_extension_negotiates",
    "taskAugmentedToolCalls": "task_augmented_tool_supported",
    "tasksGetUpdateCancel": "tasks_get_update_cancel_supported",
    "tasksGetTerminalOutput": "tasks_get_carries_terminal_output",
    "normativeStatusEnumMatches": "normative_status_enum_matches",
    "stdioTransport": "stdio_transport_supported",
}

REQUIRED_TOP_LEVEL_FIELDS = [
    "sdkPackage", "sdkVersion", "mcpTypesVersion", "installIsolation",
    "sourceDocs", "verdict", "missingFeatures", "citations",
    *NAMED_CASES.keys(),
]

EXTERNAL_FILE_PREFIXES = ("COMMAND:", "URL:", "SDK:")


def _contract_path() -> Path:
    override = os.environ.get("MCP_SDK_GATE_CONTRACT_PATH")
    if override:
        return Path(override)
    return DEFAULT_CONTRACT_PATH


def _load_contract() -> dict:
    path = _contract_path()
    if not path.is_file():
        raise FileNotFoundError(f"contract not found: {path}")
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


class McpSdkGateContractShapeTests(unittest.TestCase):
    """Validates tests/contracts/mcp-2026-sdk-gate.json's shape (Slice N1-SDK-Gate)."""

    @classmethod
    def setUpClass(cls):
        cls.contract = _load_contract()

    def test_schema_and_required_fields_present(self):
        for field in REQUIRED_TOP_LEVEL_FIELDS:
            self.assertIn(field, self.contract, f"contract missing required field '{field}'")

    def test_every_capability_field_is_boolean(self):
        for key in NAMED_CASES:
            self.assertIsInstance(
                self.contract.get(key), bool,
                f"contract['{key}'] must be a JSON boolean, got {type(self.contract.get(key))}",
            )

    def test_citations_present(self):
        citations = self.contract.get("citations")
        self.assertTrue(citations, "contract carries no 'citations' array — every claim must be pinned")

    def test_citations_have_required_keys(self):
        for i, c in enumerate(self.contract.get("citations", [])):
            for key in ("claim", "file", "line"):
                self.assertIn(key, c, f"citations[{i}] missing '{key}': {c}")
            self.assertTrue(str(c["claim"]).strip(), f"citations[{i}] has an empty claim")
            self.assertTrue(str(c["file"]).strip(), f"citations[{i}] has an empty file")

    def test_citations_reference_existing_repo_files(self):
        for i, c in enumerate(self.contract.get("citations", [])):
            file_field = str(c["file"])
            if file_field.startswith(EXTERNAL_FILE_PREFIXES):
                continue  # a command/URL/installed-SDK provenance note, not a repo file
            target = REPO_ROOT / file_field
            self.assertTrue(
                target.is_file(),
                f"citations[{i}] cites a file that does not exist in the repo: {file_field}",
            )

    def test_every_named_capability_has_citation(self):
        claims = {str(c.get("claim", "")) for c in self.contract.get("citations", [])}
        for key in NAMED_CASES:
            has_citation = any(claim == key or claim.startswith(key + ".") for claim in claims)
            self.assertTrue(
                has_citation,
                f"capability '{key}' has no citation in the contract's 'citations' array "
                f"(missing citation is a reject, per this slice's contract test requirement)",
            )

    def test_missing_features_list_matches_false_capabilities(self):
        false_caps = {k for k in NAMED_CASES if self.contract.get(k) is False}
        self.assertEqual(
            set(self.contract.get("missingFeatures", [])), false_caps,
            "missingFeatures must list exactly the capabilities marked false — no silent omission, "
            "no stale leftover entry for a capability that is actually true",
        )

    def test_verdict_matches_capability_booleans(self):
        all_true = all(self.contract.get(k) is True for k in NAMED_CASES)
        expected_verdict = "TEST_REPORTED_APPROVED" if all_true else "PLAN_CONTRADICTED"
        self.assertEqual(
            self.contract.get("verdict"), expected_verdict,
            "the recorded verdict must follow mechanically from the 8 capability booleans — "
            "never hand-picked independent of them",
        )


class McpSdkGateNamedCaseTests(unittest.TestCase):
    """The 8 exact-named cases the plan requires. Each asserts its capability IS
    supported — several are EXPECTED to fail red against the real contract; see
    the module docstring's Honest-outcome rule. Do not weaken these to force green."""

    @classmethod
    def setUpClass(cls):
        cls.contract = _load_contract()

    def _assert_capability(self, field):
        self.assertIs(
            self.contract.get(field), True,
            f"contract['{field}'] is not true — capability not supported by the pinned SDK "
            f"(sdkPackage={self.contract.get('sdkPackage')!r} "
            f"sdkVersion={self.contract.get('sdkVersion')!r}); "
            f"see contract['verdictReason'] and docs/visibility/mcp-2026-sdk-gate.md",
        )

    def test_stateless_core_supported(self):
        self._assert_capability("statelessCore")

    def test_server_discover_supported(self):
        self._assert_capability("serverDiscover")

    def test_tasks_extension_negotiates(self):
        self._assert_capability("tasksExtensionNegotiates")

    def test_task_augmented_tool_supported(self):
        self._assert_capability("taskAugmentedToolCalls")

    def test_tasks_get_update_cancel_supported(self):
        self._assert_capability("tasksGetUpdateCancel")

    def test_tasks_get_carries_terminal_output(self):
        self._assert_capability("tasksGetTerminalOutput")

    def test_normative_status_enum_matches(self):
        self._assert_capability("normativeStatusEnumMatches")

    def test_stdio_transport_supported(self):
        self._assert_capability("stdioTransport")


if __name__ == "__main__":
    # Print the resolved contract path so a reader can tell at a glance which file (real or a
    # negative-control temp copy) this run validated.
    print(f"[test_mcp_2026_sdk_gate] contract: {_contract_path()}")
    unittest.main(verbosity=2)
