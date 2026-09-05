from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "code-quality.yml"
DESKTOP_WORKFLOW = ROOT / ".github" / "workflows" / "desktop-ci.yml"
CONFIG = ROOT / ".github" / "codeql" / "codeql-config.yml"
QML_GATE = ROOT / "scripts" / "qml_quality_gate.py"
CLANG_TIDY_GATE = ROOT / "scripts" / "clang_tidy_quality_gate.py"
CLANG_TIDY_POLICY = ROOT / ".clang-tidy"
CLANG_TIDY_ALLOWLIST = ROOT / ".github" / "clang-tidy-allowlist.txt"


class CodeQualityWorkflowContract(unittest.TestCase):
    def test_codeql_covers_product_languages(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        desktop = DESKTOP_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("name: code-quality", workflow)
        self.assertIn("security-events: write", workflow)
        self.assertIn("github/codeql-action/init@v4", workflow)
        self.assertIn("github/codeql-action/analyze@v4", workflow)
        self.assertIn("queries: security-and-quality", workflow)
        for language in ("go", "python", "javascript-typescript", "actions"):
            self.assertIn(language, workflow)
        self.assertNotIn("build-mode: none", workflow)
        self.assertNotIn("languages: c-cpp", workflow)
        self.assertIn("languages: c-cpp", desktop)
        self.assertIn("build-mode: manual", desktop)
        self.assertIn("security-events: write", desktop)
        self.assertIn("github/codeql-action/init@v4", desktop)
        self.assertIn("github/codeql-action/analyze@v4", desktop)
        self.assertLess(desktop.index("Initialize CodeQL C++"), desktop.index("Configure Colosseum"))
        self.assertLess(desktop.index("Configure Colosseum"), desktop.index("Analyze C++"))
        self.assertIn("go build ./...", workflow)

    def test_qml_gate_is_qt_611_and_high_signal(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        gate = QML_GATE.read_text(encoding="utf-8")
        self.assertIn('version: "6.11.1"', workflow)
        self.assertIn("qml_quality_gate.py", workflow)
        for category in ("alias-cycle", "assignment-in-condition", "duplicate-property-binding",
                         "signal-handler-parameters", "unreachable-code",
                         "var-used-before-declaration", "with"):
            self.assertIn(category, gate)

    def test_clang_tidy_gate_is_pinned_first_party_and_correctness_focused(self):
        desktop = DESKTOP_WORKFLOW.read_text(encoding="utf-8")
        gate = CLANG_TIDY_GATE.read_text(encoding="utf-8")
        policy = CLANG_TIDY_POLICY.read_text(encoding="utf-8")
        allowlist = CLANG_TIDY_ALLOWLIST.read_text(encoding="utf-8")
        self.assertIn('LLVM_VERSION: "22.1.8"', desktop)
        self.assertIn("LLVM_SHA256", desktop)
        self.assertIn("CMAKE_EXPORT_COMPILE_COMMANDS=ON", desktop)
        self.assertIn("clang_tidy_quality_gate.py", desktop)
        self.assertIn("--min-files 184", desktop)
        self.assertLess(desktop.index("Analyze C++"), desktop.index("Run clang-tidy correctness gate"))
        self.assertIn("cmakefiles/colosseum.dir/", gate)
        self.assertIn("third_party", gate)
        self.assertIn("build-", gate)
        for check in ("clang-analyzer-core.*", "clang-analyzer-cplusplus.NewDelete*",
                      "clang-analyzer-deadcode.DeadStores", "bugprone-empty-catch",
                      "bugprone-chained-comparison", "cert-err33-c"):
            self.assertIn(check, gate)
            self.assertIn(check, policy)
        self.assertNotIn("portability-avoid-pragma-once", policy)
        for ownership_false_positive in (
                "native/engine/BiblioCatalog.cpp|1186|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/ComicDownloader.cpp|1511|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/MangaDownloader.cpp|455|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/MangaDownloader.cpp|512|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/MangaDownloader.cpp|935|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/TankoyomiChapterService.cpp|196|clang-analyzer-cplusplus.NewDeleteLeaks",
                "native/engine/MangaVolumeArchiveIngestor.cpp|268|clang-analyzer-cplusplus.NewDeleteLeaks"):
            self.assertIn(ownership_false_positive, allowlist)

    def test_linux_ccache_persists_even_when_the_build_fails(self):
        desktop = DESKTOP_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("Restore Linux compiler cache", desktop)
        self.assertIn("uses: actions/cache/restore@v4", desktop)
        self.assertIn("path: ~/.cache/ccache", desktop)
        self.assertIn("key: linux-ccache-v1-${{ runner.os }}-${{ github.sha }}", desktop)
        self.assertIn("linux-ccache-v1-${{ runner.os }}-", desktop)
        self.assertIn("Save Linux compiler cache", desktop)
        self.assertIn("uses: actions/cache/save@v4", desktop)
        self.assertIn("if: always()", desktop)
        self.assertLess(desktop.index("Restore Linux compiler cache"),
                        desktop.index("Build Linux desktop and tests"))
        self.assertLess(desktop.index("Build Linux desktop and tests"),
                        desktop.index("Save Linux compiler cache"))

    def test_address_sanitizer_gate_builds_and_runs_high_risk_native_probes(self):
        desktop = DESKTOP_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("Configure AddressSanitizer probes", desktop)
        self.assertIn("Run AddressSanitizer probes", desktop)
        self.assertIn("/fsanitize=address", desktop)
        self.assertIn("_DISABLE_VECTOR_ANNOTATION", desktop)
        self.assertIn("_DISABLE_STRING_ANNOTATION", desktop)
        for target in ("comicreader_provider_harness", "comicreader_decode_harness",
                       "tst_vault_scanner", "tst_watchparty_lifecycle",
                       "cbz_archive_probe_harness"):
            self.assertIn(target, desktop)
        self.assertIn("ASAN_OPTIONS", desktop)
        self.assertIn("QT_QPA_PLATFORM", desktop)
        self.assertLess(desktop.index("Run clang-tidy correctness gate"),
                        desktop.index("Configure AddressSanitizer probes"))

    def test_codeql_ignores_noise_but_keeps_quality_suite(self):
        config = CONFIG.read_text(encoding="utf-8")
        for path in ("archive/**", "artifacts/**", "output/**", "tests/**",
                     "native/third_party/**", "native/prototypes/**", "qml/**",
                     "resources/reader2/vendor/**", "resources/reader2/qwebchannel.js",
                     "docs/research/**", "server/watchparty-relay/node_modules/**"):
            self.assertIn(path, config)
        for rule in ("cpp/short-global-name", "cpp/unused-static-variable",
                     "cpp/function-in-block", "cpp/ambiguously-signed-bit-field",
                     "cpp/local-variable-hides-global-variable",
                     "cpp/poorly-documented-function", "cpp/trivial-switch",
                     "cpp/long-switch", "cpp/complex-block"):
            self.assertIn(rule, config)


if __name__ == "__main__":
    unittest.main()
