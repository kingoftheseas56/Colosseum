from pathlib import Path
import tempfile
import unittest

from scripts.clang_tidy_quality_gate import (
    Diagnostic,
    load_allowlist,
    parse_diagnostics,
    select_application_entries,
)


class ClangTidyQualityGateTest(unittest.TestCase):
    def test_selects_only_first_party_colosseum_translation_units(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo"
            entries = [
                {
                    "file": str(root / "native" / "main.cpp"),
                    "output": "CMakeFiles/colosseum.dir/main.cpp.o",
                    "command": f"c++ -c {root / 'native' / 'main.cpp'}",
                    "directory": str(Path(tmp) / "build"),
                },
                {
                    "file": str(root / "native" / "torrent" / "TorrentEngine.cpp"),
                    "output": "CMakeFiles/some_harness.dir/TorrentEngine.cpp.o",
                    "command": f"c++ -c {root / 'native' / 'torrent' / 'TorrentEngine.cpp'}",
                    "directory": str(Path(tmp) / "build"),
                },
                {
                    "file": str(root / "native" / "third_party" / "vendor.cpp"),
                    "output": "CMakeFiles/colosseum.dir/third_party/vendor.cpp.o",
                    "command": f"c++ -c {root / 'native' / 'third_party' / 'vendor.cpp'}",
                    "directory": str(Path(tmp) / "build"),
                },
                {
                    "file": str(Path(tmp) / "build" / "colosseum_autogen" / "mocs_compilation.cpp"),
                    "output": "CMakeFiles/colosseum.dir/colosseum_autogen/mocs_compilation.cpp.o",
                    "command": f"c++ -c {Path(tmp) / 'build' / 'colosseum_autogen' / 'mocs_compilation.cpp'}",
                    "directory": str(Path(tmp) / "build"),
                },
            ]

            selected = select_application_entries(entries, root / "native")

            self.assertEqual([Path(entry["file"]).name for entry in selected], ["main.cpp"])

    def test_diagnostics_are_deduplicated_at_exact_site(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo" / "native"
            source = root / "engine" / "ComicDownloader.cpp"
            external = Path(tmp) / "Qt" / "include" / "QObject"
            output = "\n".join(
                [
                    f"{source}:1427:1: warning: Potential leak of memory pointed to by 'watcher' [clang-analyzer-cplusplus.NewDeleteLeaks]",
                    f"{source}:1427:1: warning: Potential leak of memory pointed to by 'watcher' [clang-analyzer-cplusplus.NewDeleteLeaks]",
                    f"{external}:5:1: warning: external [clang-analyzer-core.CallAndMessage]",
                ]
            )
            findings = parse_diagnostics(output, root)

            self.assertEqual(
                findings,
                {
                    Diagnostic(
                        "native/engine/ComicDownloader.cpp",
                        1427,
                        "clang-analyzer-cplusplus.NewDeleteLeaks",
                        "Potential leak of memory pointed to by 'watcher'",
                    )
                },
            )

    def test_allowlist_uses_exact_path_check_and_message(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "allowlist.txt"
            path.write_text(
                "# Proven framework false positive\n"
                "native/main.cpp|873|clang-analyzer-cplusplus.NewDeleteLeaks|Potential memory leak\n",
                encoding="utf-8",
            )
            self.assertEqual(
                load_allowlist(path),
                {Diagnostic("native/main.cpp", 873, "clang-analyzer-cplusplus.NewDeleteLeaks", "Potential memory leak")},
            )


if __name__ == "__main__":
    unittest.main()
