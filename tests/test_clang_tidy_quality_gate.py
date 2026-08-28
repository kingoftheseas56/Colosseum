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
        root = Path(r"C:\repo")
        entries = [
            {
                "file": r"C:\repo\native\main.cpp",
                "output": r"CMakeFiles\colosseum.dir\main.cpp.obj",
                "command": "cl /c C:\\repo\\native\\main.cpp",
                "directory": r"C:\build",
            },
            {
                "file": r"C:\repo\native\torrent\TorrentEngine.cpp",
                "output": r"CMakeFiles\some_harness.dir\TorrentEngine.cpp.obj",
                "command": "cl /c C:\\repo\\native\\torrent\\TorrentEngine.cpp",
                "directory": r"C:\build",
            },            {
                "file": r"C:\repo\native\third_party\vendor.cpp",
                "output": r"CMakeFiles\colosseum.dir\third_party\vendor.cpp.obj",
                "command": "cl /c C:\\repo\\native\\third_party\\vendor.cpp",
                "directory": r"C:\build",
            },
            {
                "file": r"C:\build\colosseum_autogen\mocs_compilation.cpp",
                "output": r"CMakeFiles\colosseum.dir\colosseum_autogen\mocs_compilation.cpp.obj",
                "command": "cl /c C:\\build\\colosseum_autogen\\mocs_compilation.cpp",
                "directory": r"C:\build",
            },
        ]

        selected = select_application_entries(entries, root / "native")

        self.assertEqual([Path(entry["file"]).name for entry in selected], ["main.cpp"])

    def test_diagnostics_are_deduplicated_at_exact_site(self):
        root = Path(r"C:\repo") / "native"
        output = "\n".join(
            [
                r"C:\repo\native\engine\ComicDownloader.cpp:1427:1: warning: Potential leak of memory pointed to by 'watcher' [clang-analyzer-cplusplus.NewDeleteLeaks]",
                r"C:\repo\native\engine\ComicDownloader.cpp:1427:1: warning: Potential leak of memory pointed to by 'watcher' [clang-analyzer-cplusplus.NewDeleteLeaks]",
                r"C:\Qt\include\QObject:5:1: warning: external [clang-analyzer-core.CallAndMessage]",
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
