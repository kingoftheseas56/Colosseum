"""P05-B trace comparator mutation tests and qualified Node ordering probe."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


CASE_PATH = Path(__file__).resolve().parents[1] / "cases" / "P05-A.json"
CONTRACT_PATH = Path(__file__).resolve().parents[3] / "docs" / "server1" / "TRACE-CONTRACT.json"


def load_case() -> dict:
    return json.loads(CASE_PATH.read_text(encoding="utf-8"))


def load_contract() -> dict:
    return json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))


def rejection(identity: str, path: str, rule: str) -> dict:
    return {"accepted": False, "rejection": {"identity": identity, "path": path, "rule": rule}}


class TraceComparator:
    """Raw trace comparator; this deliberately does not normalize either input."""

    def compare(self, expected: list[dict], actual: list[dict], *, trace_context: dict) -> dict:
        if not isinstance(expected, list) or not isinstance(actual, list):
            raise TypeError("trace must be a list")
        contract = load_contract()
        required_context = {
            "oracle_sha256": contract["oracle"]["sha256"],
            "module_ids": [module["id"] for module in contract["modules"]],
            "injected_inputs": contract["injected_inputs"],
            "observation_level": contract["observation_level"],
            "event_schema": contract["event_schema"]["required_fields"],
        }
        if trace_context != required_context:
            return rejection("trace-context", "trace_context", "trace-context-identity")
        if expected == actual:
            return {"accepted": True, "rejection": None}

        expected_priorities = [
            event for event in expected if event.get("kind") == "selection.priority"
        ]
        actual_priorities = [
            event for event in actual if event.get("kind") == "selection.priority"
        ]
        if expected_priorities and [event["identity"] for event in expected_priorities] != [
            event["identity"] for event in actual_priorities
        ]:
            for index, (expected_event, actual_event) in enumerate(
                zip(expected_priorities, actual_priorities)
            ):
                if expected_event["identity"] != actual_event["identity"]:
                    return rejection(
                        expected_event["identity"],
                        f"trace[{index}].identity",
                        "priority-decision-order",
                    )

        for index, actual_event in enumerate(actual):
            if actual_event.get("kind") == "request.cancelled":
                expected_event = expected[index] if index < len(expected) else None
                if expected_event and (
                    actual_event.get("generation") != expected_event.get("generation")
                    or actual_event.get("payload", {}).get("target_generation")
                    != expected_event.get("payload", {}).get("target_generation")
                ):
                    return rejection(
                        expected_event["identity"],
                        f"trace[{index}].generation",
                        "cancellation-targets-active-generation",
                    )

        for index, (expected_event, actual_event) in enumerate(zip(expected, actual)):
            expected_payload = expected_event.get("payload", {})
            actual_payload = actual_event.get("payload", {})
            for key, expected_value in expected_payload.items():
                if key not in actual_payload:
                    return rejection(
                        expected_event["identity"],
                        f"trace[{index}].payload.{key}",
                        "required-key-present",
                    )
                if actual_payload[key] != expected_value:
                    return rejection(
                        expected_event["identity"],
                        f"trace[{index}].payload.{key}",
                        "raw-payload-value-equality",
                    )
            for key, actual_value in actual_payload.items():
                if key not in expected_payload and actual_value is None:
                    return rejection(
                        actual_event["identity"],
                        f"trace[{index}].payload.{key}",
                        "omitted-key-is-not-null",
                    )

        terminal_events = [
            (index, event)
            for index, event in enumerate(actual)
            if event.get("kind") == "stream.terminal"
        ]
        seen_terminal: dict[str, int] = {}
        for index, event in terminal_events:
            identity = event["identity"]
            if identity in seen_terminal:
                return rejection(identity, f"trace[{index}]", "terminal-event-cardinality-one")
            seen_terminal[identity] = index

        expected_callbacks = {
            event["identity"]: index
            for index, event in enumerate(expected)
            if event.get("kind") == "callback.completed"
        }
        actual_callbacks = {
            event["identity"] for event in actual if event.get("kind") == "callback.completed"
        }
        for identity, index in expected_callbacks.items():
            if identity not in actual_callbacks:
                return rejection(identity, f"trace[{index}]", "required-terminal-callback-present")

        if len(actual) != len(expected):
            identity = expected[min(len(actual), len(expected) - 1)]["identity"]
            return rejection(identity, f"trace[{len(actual)}]", "trace-cardinality-equality")
        for index, (expected_event, actual_event) in enumerate(zip(expected, actual)):
            if expected_event != actual_event:
                return rejection(
                    expected_event.get("identity", actual_event.get("identity", "")),
                    f"trace[{index}]",
                    "raw-event-equality",
                )
        return rejection("trace", "trace", "raw-event-divergence")


def run_qualified_node_order_probe() -> dict:
    contract = load_contract()
    expected_version = contract["qualified_node_runtime"]["version"]
    node = os.environ.get("P05_QUALIFIED_NODE")
    if not node:
        raise AssertionError("P05_QUALIFIED_NODE must name the qualified Node executable")
    probe = """
const events = [];
const record = (className, label) => events.push({ class: className, label });
record('immediate', 'immediate-1');
process.nextTick(() => record('nextTick', 'nextTick-1'));
Promise.resolve().then(() => record('promise', 'promise-1'));
setTimeout(() => {
  record('timer', 'timer-1');
  process.stdout.write(JSON.stringify({ version: process.version, events }));
}, 0);
"""
    with tempfile.NamedTemporaryFile("w", suffix=".cjs", encoding="utf-8", delete=False) as handle:
        handle.write(probe)
        probe_path = handle.name
    try:
        result = subprocess.run(
            [node, "--unhandled-rejections=strict", probe_path],
            check=False,
            capture_output=True,
            text=True,
        )
    finally:
        Path(probe_path).unlink(missing_ok=True)
    if result.returncode != 0:
        raise AssertionError(f"qualified Node probe failed: {result.stderr.strip()}")
    trace = json.loads(result.stdout)
    if trace["version"] != expected_version:
        raise AssertionError(
            f"qualified Node identity mismatch: expected {expected_version}, got {trace['version']}"
        )
    return {"runtime": {"version": trace["version"]}, "events": trace["events"]}


class TraceComparatorMutationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.definition = load_case()
        self.fixtures = {item["id"]: item for item in self.definition["mutation_fixtures"]}

    def assert_mutation_rejected(self, fixture_id: str) -> None:
        fixture = self.fixtures[fixture_id]
        result = TraceComparator().compare(
            fixture["baseline_trace"],
            fixture["mutated_trace"],
            trace_context=self.definition["trace_context"],
        )
        self.assertFalse(result["accepted"])
        self.assertEqual(result["rejection"], fixture["expected_rejection"])

    def test_baseline_trace_is_accepted_without_normalization(self) -> None:
        fixture = self.fixtures["P05-01-callback-removal"]
        result = TraceComparator().compare(
            fixture["baseline_trace"],
            fixture["baseline_trace"],
            trace_context=self.definition["trace_context"],
        )
        self.assertEqual(result, {"accepted": True, "rejection": None})

    def test_callback_removal_is_rejected_with_identity_path_and_rule(self) -> None:
        self.assert_mutation_rejected("P05-01-callback-removal")

    def test_duplicate_terminal_event_is_rejected_with_identity_path_and_rule(self) -> None:
        self.assert_mutation_rejected("P05-01-duplicate-terminal-event")

    def test_priority_reorder_is_rejected_with_identity_path_and_rule(self) -> None:
        self.assert_mutation_rejected("P05-01-priority-reorder")

    def test_null_for_omitted_key_is_rejected_with_identity_path_and_rule(self) -> None:
        self.assert_mutation_rejected("P05-01-null-replaces-omitted-key")

    def test_wrong_cancellation_generation_is_rejected_with_identity_path_and_rule(self) -> None:
        self.assert_mutation_rejected("P05-01-wrong-cancellation-generation")


class QualifiedNodeOrderingTests(unittest.TestCase):
    def test_immediate_nexttick_promise_and_timer_classes_are_distinct_and_ordered(self) -> None:
        trace = run_qualified_node_order_probe()
        self.assertEqual(trace["runtime"]["version"], "v22.16.0")
        self.assertEqual(
            [(event["class"], event["label"]) for event in trace["events"]],
            [
                ("immediate", "immediate-1"),
                ("nextTick", "nextTick-1"),
                ("promise", "promise-1"),
                ("timer", "timer-1"),
            ],
        )
        self.assertEqual(len({event["class"] for event in trace["events"]}), 4)


class TraceContractTests(unittest.TestCase):
    def test_contract_freezes_raw_trace_identity_and_ordering_boundaries(self) -> None:
        contract = load_contract()
        self.assertEqual(contract["oracle"]["sha256"], "405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f")
        self.assertEqual([module["id"] for module in contract["modules"]], [564, 730, 874])
        self.assertEqual(contract["observation_level"], "policy-trace")
        self.assertTrue(contract["event_schema"]["raw_trace_is_authoritative"])
        self.assertFalse(contract["event_schema"]["normalized_trace"]["allowed"])
        self.assertEqual(contract["event_schema"]["payload_key_presence"], "exact; omitted and null are distinct")
        self.assertEqual(contract["comparison_rules"]["cardinality"], "exact")
        self.assertEqual(contract["comparison_rules"]["terminal_event_uniqueness"], "one stream.terminal per identity per generation")
        self.assertEqual(contract["qualified_node_runtime"]["expected_order"], ["immediate", "nextTick", "promise", "timer"])


if __name__ == "__main__":
    unittest.main()
