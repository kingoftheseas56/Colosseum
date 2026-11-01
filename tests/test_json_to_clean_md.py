"""Regression tests for the dependency-free session transcript converter."""
import json
import sys
import tempfile
import unittest
import hashlib
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import json_to_clean_md as converter  # noqa: E402

REPO = Path(__file__).resolve().parents[1]


def save_jsonl(path, rows):
    path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


def sample(cwd):
    return [
        {"type": "session_meta", "payload": {"cwd": str(cwd), "id": "fixture-01"}},
        {"type": "response_item", "payload": {"type": "message", "role": "user",
         "content": [{"type": "input_text", "text": "Build the usable transcript exporter"}]}},
        {"type": "event_msg", "payload": {"type": "user_message",
         "message": "Build the usable transcript exporter"}},
        {"type": "response_item", "payload": {"type": "message", "role": "system",
         "content": [{"type": "text", "text": "SECRET SYSTEM CONTENT"}]}},
        {"type": "response_item", "payload": {"type": "message", "role": "user",
         "content": [{"type": "input_text", "text": "<environment_context>SECRET WRAPPER</environment_context>"}]}},
        {"type": "response_item", "payload": {"type": "message", "role": "assistant",
         "channel": "analysis", "content": [{"type": "output_text", "text": "SECRET THINKING"}]}},
        {"type": "response_item", "payload": {"type": "function_call",
         "name": "exec_command", "arguments": "SECRET TOOL CALL"}},
        {"type": "response_item", "payload": {"type": "function_call_output",
         "output": "SECRET TOOL RESULT"}},
        {"type": "response_item", "payload": {"type": "message", "role": "assistant",
         "channel": "final", "content": [{"type": "output_text", "text": "The export works."}]}},
        {"type": "event_msg", "payload": {"type": "agent_message", "message": "The export works."}},
    ]


class TranscriptTests(unittest.TestCase):
    def test_workspace_after_long_metadata_preamble(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "late.jsonl"
            rows = [{"type": "custom-title", "sessionId": "late"} for _ in range(70)]
            rows.append({"type": "user", "cwd": str(REPO), "sessionId": "late",
                         "message": {"role": "user", "content": "Keep this conversation"}})
            save_jsonl(file, rows)
            self.assertEqual(converter.scope_metadata(file)["cwd"], str(REPO))

    def test_claude_directory_metadata_dates_and_source_hash(self):
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "raw"
            source.mkdir()
            file = source / "claude.jsonl"
            save_jsonl(file, [
                {"type": "user", "cwd": str(REPO), "sessionId": "claude-fixture",
                 "timestamp": "2026-10-13T01:02:03Z",
                 "message": {"role": "user", "content": "Check catalogue loading"}},
                {"type": "assistant", "cwd": str(REPO), "sessionId": "claude-fixture",
                 "timestamp": "2026-10-13T01:02:04Z",
                 "message": {"role": "assistant", "content": [{"type": "text", "text": "The cached catalogue loaded."}]}},
            ])
            original = file.read_bytes()
            output = Path(name) / "out"
            self.assertEqual(converter.main([str(source), "-o", str(output)]), 0)
            text = (output / "claude-jsonl.md").read_text(encoding="utf-8")
            self.assertIn("2026-10-13T01:02:04Z", text)
            self.assertIn("**Raw reference:** L2", text)
            self.assertIn(hashlib.sha256(original).hexdigest(), text)
            self.assertEqual(file.read_bytes(), original)

    def test_generated_summary_is_labeled_and_repeats_preserved(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "summary.jsonl"
            save_jsonl(file, [
                {"type": "user", "cwd": str(REPO), "isCompactSummary": True,
                 "message": {"role": "user", "content": "Generated account setup recap."}},
                {"type": "user", "message": {"role": "user", "content": "Check again"}},
                {"type": "user", "message": {"role": "user", "content": "Check again"}},
            ])
            metadata, messages = converter.parse_session(file)
            self.assertEqual(len(messages), 3)
            self.assertIn("Generated Context Summary", converter.render(file, metadata, messages))

    def test_redaction_does_not_remove_hashes_or_account_names(self):
        secret = "ghp_" + "A" * 30
        text, count = converter.redact_secrets(
            f"Account kingoftheseas56; commit {'a' * 40}; token {secret}; "
            "postgresql://user:password@host/database; api_key=secretvalue123")
        self.assertEqual(count, 3)
        self.assertNotIn(secret, text)
        self.assertIn("kingoftheseas56", text)
        self.assertIn("a" * 40, text)
        repeated, count = converter.redact_secrets("api_key=" + secret)
        self.assertEqual(count, 1)
        self.assertEqual(repeated, "api_key=[REDACTED]")

    def test_unrelated_workspace_is_not_fully_parsed(self):
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "raw"
            source.mkdir()
            save_jsonl(source / "other.jsonl", sample(Path(name) / "unrelated"))
            with patch.object(converter, "parse_session", side_effect=AssertionError("must not parse")):
                self.assertEqual(converter.main([str(source), "-o", str(Path(name) / "out")]), 0)

    def test_changed_source_is_accounted_without_export(self):
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "raw"
            source.mkdir()
            save_jsonl(source / "one.jsonl", sample(REPO))
            output = Path(name) / "out"
            with patch.object(converter, "file_hash", side_effect=["a" * 64, "b" * 64]):
                self.assertEqual(converter.main([str(source), "-o", str(output)]), 0)
            report = json.loads((output / "REPORT.json").read_text())
            self.assertEqual(report["skipped_files"][0]["reason"], "changed_during_read")
            self.assertEqual(report["exported"], 0)

    def test_worktree_scope_and_restricted_paths(self):
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "raw"
            source.mkdir()
            alias = Path(name) / "worktree"
            save_jsonl(source / "alias.jsonl", sample(alias))
            save_jsonl(source / "restricted.jsonl", sample(
                Path(name) / "Preflight-Architect" / "operating"))
            output = Path(name) / "out"
            self.assertEqual(converter.main([str(source), "-o", str(output),
                                            "--include-workspace", str(alias)]), 0)
            report = json.loads((output / "REPORT.json").read_text())
            self.assertEqual(report["exported"], 1)
            self.assertEqual(report["skipped_files"][0]["reason"], "restricted")

    def test_codex_visible_only_and_deduplicated(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "sample.jsonl"
            save_jsonl(file, sample(REPO))
            meta, messages = converter.parse_session(file)
            self.assertEqual(meta["id"], "fixture-01")
            self.assertEqual(messages, [
                ("user", "Build the usable transcript exporter"),
                ("assistant", "The export works."),
            ])
            result = converter.render(file, meta, messages)
            self.assertNotIn("SECRET", result)
            self.assertEqual(result.count("## Assistant"), 1)

    def test_claude_and_json_message_array(self):
        with tempfile.TemporaryDirectory() as name:
            file = Path(name) / "claude.json"
            file.write_text(json.dumps({"messages": [
                {"type": "user", "message": {"role": "user", "content": "Explain the feature"}},
                {"type": "assistant", "message": {"role": "assistant", "content": [
                    {"type": "thinking", "thinking": "SECRET"},
                    {"type": "tool_use", "name": "read_file", "input": {"path": "PRIVATE"}},
                    {"type": "text", "text": "It is complete."},
                ]}},
            ]}), encoding="utf-8")
            _, messages = converter.parse_session(file)
            self.assertEqual(messages, [("user", "Explain the feature"),
                                         ("assistant", "It is complete.")])

    def test_workspace_scope_and_manifest(self):
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "raw"
            output = Path(name) / "clean"
            source.mkdir()
            own = source / "own.jsonl"
            other = source / "other.jsonl"
            save_jsonl(own, sample(REPO))
            outsider = REPO.parent if REPO.name.lower() == "colosseum" else REPO / "Colosseum"
            save_jsonl(other, sample(outsider))
            self.assertEqual(converter.main([str(source), "-o", str(output)]), 0)
            report = json.loads((output / "REPORT.json").read_text(encoding="utf-8"))
            self.assertEqual(report["exported"], 1)
            self.assertEqual(report["skipped"], 1)
            self.assertTrue((output / "own-jsonl.md").exists())
            self.assertFalse((output / "other-jsonl.md").exists())
            self.assertEqual(len((output / "MANIFEST.csv").read_text().splitlines()), 2)


if __name__ == "__main__":
    unittest.main()
