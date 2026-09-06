import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
MODULE_PATH = ROOT / "tools" / "server_lab" / "adapters" / "colosseum01.py"
SPEC = importlib.util.spec_from_file_location("colosseum01", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise ImportError(f"cannot load adapter from {MODULE_PATH}")
colosseum01 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(colosseum01)


class Colosseum01AdapterTests(unittest.TestCase):
    def test_required_closure_refuses_reduced_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            result = colosseum01.check_required_inputs(
                Path(directory), ["native/required.cpp", "tests/fixtures/tiny.mp4"]
            )

        self.assertFalse(result["complete"])
        self.assertEqual(result["missing"], ["native/required.cpp", "tests/fixtures/tiny.mp4"])

    def test_ctest_listing_does_not_call_omitted_playback_a_pass(self):
        result = colosseum01.classify_ctest_listing(
            "Test project C:/tmp/build\n  Test #1: runtime_lifecycle_test\n"
        )

        self.assertFalse(result["runtime_mpv_playback_test_configured"])
        self.assertFalse(result["playback_qualified"])
        self.assertEqual(result["status"], "BASELINE_UNAVAILABLE")

    def test_source_receipt_keeps_exact_revision_and_object_identity(self):
        receipt = colosseum01.source_receipt(
            "a3fcaa96ec2650014e1dd94f603d76b2b1e48387",
            [("native/example.cpp", "blob-id", 3, "sha256")],
        )

        self.assertEqual(
            receipt["source_revision"],
            "a3fcaa96ec2650014e1dd94f603d76b2b1e48387",
        )
        self.assertEqual(receipt["files"][0]["git_object"], "blob-id")
        self.assertEqual(receipt["files"][0]["sha256"], "sha256")

    def test_missing_tool_is_preserved_as_bounded_command_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "missing-tool.txt"
            exit_code = colosseum01._run(["__colosseum01_missing_tool__"], Path(directory), output)

            self.assertEqual(exit_code, 127)
            self.assertIn("__colosseum01_missing_tool__", output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
