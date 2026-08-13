#!/usr/bin/env python3
"""test_lanista_mcp_forensics.py — Slice F1-Bridge (Agent Visibility Phase 2).

Contract tests for the `vault_forensics` facade tool added to
native/tools/lanista-mcp/server.py. These are PYTHON-LAYER tests: they prove the
adapter plumbs scope/key/limit/timeoutMs into the "vault-forensics" CLI call
correctly, bounds the bridge deadline (v0's floor+slack pattern), and hands the
C++ bridge's reply back to the caller UNCHANGED. They do NOT spawn a real
Colosseum process or touch a named pipe — `server.run_lanista` is monkeypatched
so this file runs with no native build and no live app. The OTHER half of the
"unchanged" proof — that the C++ bridge itself passes VaultForensics::query()'s
map through unmodified — is tests/lanista_harness.cpp's
`vault_forensics_passes_response_unchanged` case; this file proves the Python
wrapper on top of that bridge reply doesn't reshape it either. The full,
genuinely-live round trip is the isolated-session runtime replay recorded in
this slice's report.

Usage:
    python tests/test_lanista_mcp_forensics.py
"""
from __future__ import annotations

import json
import os
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "native" / "tools" / "lanista-mcp"))
import server  # noqa: E402  (path must be extended first)

LEGACY_NAMES = [
    "session_start", "session_stop", "act", "get", "snapshot", "wait_for",
    "grab", "warnings", "lanista_call", "lanista_grab", "lanista_snapshot",
]


class ToolSchemaIsExactTests(unittest.TestCase):
    def test_tool_schema_is_exact(self):
        by_name = {t["name"]: t for t in server.TOOLS}
        self.assertIn("vault_forensics", by_name)
        tool = by_name["vault_forensics"]

        schema = tool["inputSchema"]
        self.assertEqual(schema["type"], "object")
        self.assertEqual(schema["required"], ["scope"])

        props = schema["properties"]
        self.assertEqual(set(props.keys()), {"scope", "key", "limit", "timeoutMs"})
        self.assertEqual(props["scope"]["type"], "string")
        self.assertEqual(props["scope"]["enum"], ["summary", "root", "node", "identity"])
        self.assertEqual(props["key"]["type"], "string")
        self.assertEqual(props["limit"]["type"], "integer")
        self.assertEqual(props["timeoutMs"]["type"], "integer")

        self.assertIn("vault_forensics", server.TOOL_IMPLS)
        self.assertIs(server.TOOL_IMPLS["vault_forensics"], server.tool_vault_forensics)


class _FakeSessionMixin(unittest.TestCase):
    """Simulates an active session without spawning anything. SESSION is the
    adapter's own module-level dict; _require_active_session()'s only other
    check is proc.poll(), skipped by leaving "proc" as None (falsy shortcut:
    `proc is not None and proc.poll() ...` never reaches poll())."""

    def setUp(self):
        self._orig_session = dict(server.SESSION)
        server.SESSION = {"active": True, "pipe": "ColosseumLanistaTest-fake", "proc": None}
        self._orig_run_lanista = server.run_lanista
        self.calls = []

    def tearDown(self):
        server.SESSION = self._orig_session
        server.run_lanista = self._orig_run_lanista

    def _stub_run_lanista(self, canned_reply):
        def _stub(cmd, pipe, extra_args=None, timeout_ms=server.DEFAULT_CMD_TIMEOUT_MS,
                   grab_target=None):
            self.calls.append({"cmd": cmd, "pipe": pipe,
                                "extra_args": list(extra_args or []),
                                "timeout_ms": timeout_ms})
            return 0, canned_reply, json.dumps(canned_reply), ""
        server.run_lanista = _stub


class SummaryRoundTripTests(_FakeSessionMixin):
    def test_summary_round_trip(self):
        canned = {
            "type": "reply", "seq": 1, "schema": "colosseum.vault.forensics.v1",
            "scope": "summary", "revision": 3, "truncated": False, "errors": [],
            "roots": {"count": 1, "rows": [{"path": "C:/root", "itemCount": 2}]},
            "browseCount": 2, "itemCount": 2,
            "recent": {"count": 2, "rows": []},
        }
        self._stub_run_lanista(canned)

        result = server.tool_vault_forensics({"scope": "summary", "limit": 10})

        self.assertFalse(result.get("isError"))
        returned = json.loads(result["content"][0]["text"])
        self.assertEqual(returned, canned,
                          "the tool's reply is F1-Core's map, byte-for-byte")

        self.assertEqual(len(self.calls), 1)
        call = self.calls[0]
        self.assertEqual(call["cmd"], "vault-forensics")
        self.assertEqual(call["pipe"], "ColosseumLanistaTest-fake")
        self.assertIn("scope=summary", call["extra_args"])
        self.assertIn("limit=10", call["extra_args"])
        self.assertTrue(any(a.startswith("timeoutMs=") for a in call["extra_args"]),
                         "the bridge deadline is always forwarded")

    def test_missing_scope_is_rejected_before_any_call(self):
        self._stub_run_lanista({"type": "reply"})
        result = server.tool_vault_forensics({})
        self.assertTrue(result.get("isError"))
        self.assertEqual(self.calls, [], "no subprocess is shelled without a scope")

    def test_no_session_fails_clean(self):
        self._stub_run_lanista({"type": "reply"})
        server.SESSION = {"active": False}
        result = server.tool_vault_forensics({"scope": "summary"})
        self.assertTrue(result.get("isError"))
        self.assertEqual(self.calls, [], "no subprocess is shelled without an active session")


class NodeRoundTripTests(_FakeSessionMixin):
    def test_node_round_trip(self):
        canned = {
            "type": "reply", "seq": 2, "schema": "colosseum.vault.forensics.v1",
            "scope": "node", "revision": 3, "truncated": True, "errors": [],
            "node": {"key": "C:/root/Shows"},
            "browse": {"count": 105, "rows": [{"key": "c0", "nodeType": "Film"}]},
        }
        self._stub_run_lanista(canned)

        result = server.tool_vault_forensics(
            {"scope": "node", "key": "C:/root/Shows", "limit": 101})

        returned = json.loads(result["content"][0]["text"])
        self.assertEqual(returned, canned,
                          "node-scope reply is also passed through byte-for-byte")

        call = self.calls[0]
        self.assertIn("scope=node", call["extra_args"])
        self.assertIn("key=C:/root/Shows", call["extra_args"])
        self.assertIn("limit=101", call["extra_args"])

    def test_key_omitted_when_not_supplied(self):
        self._stub_run_lanista({"type": "reply", "scope": "summary"})
        server.tool_vault_forensics({"scope": "summary"})
        call = self.calls[0]
        self.assertFalse(any(a.startswith("key=") for a in call["extra_args"]),
                          "no spurious key= for a scope that doesn't need one")


class DeadlineIsBoundedTests(_FakeSessionMixin):
    def test_deadline_is_clamped_to_the_ceiling(self):
        self._stub_run_lanista({"type": "reply"})
        # Absurdly large -- must clamp to the ceiling, never pass through raw.
        server.tool_vault_forensics({"scope": "summary", "timeoutMs": 999999999})
        call = self.calls[-1]
        self.assertIn("timeoutMs={}".format(server.VAULT_FORENSICS_MAX_TIMEOUT_MS),
                       call["extra_args"])
        # The CLI's OWN client deadline must OUTLIVE the bridge deadline (v0's
        # floor+slack rule, same as wait_for()): a hung owner-thread wait must
        # never let Python give up before the bridge's own bounded wait would
        # have returned a coded error.
        self.assertEqual(call["timeout_ms"],
                          max(10000, server.VAULT_FORENSICS_MAX_TIMEOUT_MS + 5000))

    def test_deadline_is_clamped_to_the_floor(self):
        self._stub_run_lanista({"type": "reply"})
        # Absurdly small -- must clamp UP so the owner-thread degrade path is
        # never starved of a sane deadline.
        server.tool_vault_forensics({"scope": "summary", "timeoutMs": 1})
        call = self.calls[-1]
        self.assertIn("timeoutMs={}".format(server.VAULT_FORENSICS_MIN_TIMEOUT_MS),
                       call["extra_args"])

    def test_default_deadline_when_unspecified(self):
        self._stub_run_lanista({"type": "reply"})
        server.tool_vault_forensics({"scope": "summary"})
        call = self.calls[-1]
        self.assertIn("timeoutMs={}".format(server.DEFAULT_CMD_TIMEOUT_MS),
                       call["extra_args"])


class LegacyToolsUnchangedTests(unittest.TestCase):
    """Purely additive: F1-Bridge must not rename, remove, reorder, or reshape
    any of the 11 facade v0 tools -- only append a 12th at the end."""

    def test_legacy_tools_unchanged(self):
        names = [t["name"] for t in server.TOOLS]
        self.assertEqual(len(names), 12, "11 legacy tools + vault_forensics")
        self.assertEqual(names[:11], LEGACY_NAMES,
                          "the 11 legacy tools keep their exact names and order")
        self.assertEqual(names[11], "vault_forensics",
                          "the new tool is appended, never inserted")

        for name in LEGACY_NAMES:
            self.assertIn(name, server.TOOL_IMPLS, "{} still dispatches".format(name))

    def test_legacy_three_tools_target_resolution_is_untouched(self):
        # Unchanged behavior (F1-Bridge's own "Behavior to preserve" clause): the
        # 3 legacy tools still resolve to whatever COLOSSEUM_LANISTA_PIPE names
        # at call time, default the daily pipe -- never a session-owned one.
        self.assertEqual(server.LEGACY_PIPE,
                          os.environ.get("COLOSSEUM_LANISTA_PIPE", "ColosseumLanista"))

    def test_legacy_schemas_are_byte_identical(self):
        # A hand-pinned snapshot of the 11 legacy schemas as they existed before
        # this slice -- proves F1-Bridge didn't quietly widen/narrow one of them
        # while adding vault_forensics.
        by_name = {t["name"]: t for t in server.TOOLS}
        expected_required = {
            "session_start": None, "session_stop": None, "act": ["action"],
            "get": ["target", "props"], "snapshot": None, "wait_for": ["target", "prop", "value"],
            "grab": ["target"], "warnings": None, "lanista_call": ["cmd"],
            "lanista_grab": ["target"], "lanista_snapshot": None,
        }
        for name, required in expected_required.items():
            schema = by_name[name]["inputSchema"]
            self.assertEqual(schema.get("required"), required,
                              "{} required-fields unchanged".format(name))


if __name__ == "__main__":
    unittest.main()
