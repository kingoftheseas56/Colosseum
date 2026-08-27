from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "code-quality.yml"
DESKTOP_WORKFLOW = ROOT / ".github" / "workflows" / "desktop-ci.yml"
CONFIG = ROOT / ".github" / "codeql" / "codeql-config.yml"
QML_GATE = ROOT / "scripts" / "qml_quality_gate.py"


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
