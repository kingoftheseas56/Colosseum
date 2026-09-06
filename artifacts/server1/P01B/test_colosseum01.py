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

    def test_installed_windows_toolchain_resolves_from_frozen_paths(self):
        toolchain = colosseum01.resolve_toolchain()

        self.assertTrue(toolchain["complete"], toolchain["missing"])
        self.assertEqual(
            Path(toolchain["cmake"]),
            Path("C:/Qt/Tools/CMake_64/bin/cmake.exe"),
        )
        self.assertEqual(
            Path(toolchain["ninja"]),
            Path("C:/Qt/Tools/Ninja/ninja.exe"),
        )
        self.assertEqual(
            Path(toolchain["qt_prefix"]),
            Path("C:/Qt/6.11.1/msvc2022_64"),
        )
        self.assertEqual(
            Path(toolchain["vcvars64"]),
            Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"),
        )

    def test_vcvars_capture_produces_x64_msvc_environment(self):
        toolchain = colosseum01.resolve_toolchain()
        environment = colosseum01._msvc_environment(Path(toolchain["vcvars64"]))

        self.assertEqual(environment["VSCMD_ARG_TGT_ARCH"], "x64")
        self.assertIn("VCTOOLSINSTALLDIR", environment)

    def test_installed_input_receipt_records_path_size_and_sha256(self):
        with tempfile.TemporaryDirectory() as directory:
            installed_input = Path(directory) / "input.lib"
            installed_input.write_bytes(b"frozen-input")

            receipt = colosseum01.installed_input_receipt({"example": installed_input})

        self.assertEqual(receipt["example"]["path"], str(installed_input))
        self.assertEqual(receipt["example"]["bytes"], 12)
        self.assertEqual(
            receipt["example"]["sha256"],
            "23b57499cad5e8dd735d7260115bacbd89e8e0fcdb5d548c966f098c707e9040",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
