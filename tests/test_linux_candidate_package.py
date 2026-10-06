"""Catch missing WebEngine payloads, mismatched QML and incomplete ELF resolution."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


class CandidatePackageTests(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).resolve().parents[1] / "scripts/linux_candidate_package.py"
        self.assertTrue(path.is_file(), "Linux candidate packager is missing")
        spec = importlib.util.spec_from_file_location("candidate_package", path)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def test_unresolved_dependency_fails_closed(self):
        with self.assertRaisesRegex(RuntimeError, "libMpvQt.so.1"):
            self.module.parse_ldd("libMpvQt.so.1 => not found\n")

    def test_ldd_keeps_resolved_dependencies(self):
        output = ("linux-vdso.so.1 (0x0001)\n"
                  "libQt6Core.so.6 => /opt/qt/lib/libQt6Core.so.6 (0x0002)\n"
                  "/lib64/ld-linux-x86-64.so.2 (0x0003)\n")
        self.assertEqual(self.module.parse_ldd(output),
                         [Path("/opt/qt/lib/libQt6Core.so.6"),
                          Path("/lib64/ld-linux-x86-64.so.2")])

    def test_matching_qml_is_accepted_and_modified_qml_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "Main.qml").write_bytes(b"import QtQuick\n")
            # sha256('Main.qml\n' + sha256(file) + '\n'), independently generated.
            import hashlib
            material = b"Main.qml\n" + hashlib.sha256(b"import QtQuick\n").hexdigest().encode() + b"\n"
            manifest = root / "../" / (root.name + "-manifest")
            try:
                manifest.write_text("schema=1\nqmlTreeSha256=" + hashlib.sha256(material).hexdigest() + "\n")
                self.module.verify_qml(root, manifest)
                (root / "Main.qml").write_bytes(b"import QtQuick\nItem {}\n")
                with self.assertRaisesRegex(RuntimeError, "QML build mismatch"):
                    self.module.verify_qml(root, manifest)
            finally:
                manifest.unlink(missing_ok=True)

    def test_webengine_runtime_requires_helper_resources_locales_and_plugin(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            required = ["usr/bin/colosseum", "usr/libexec/QtWebEngineProcess",
                        "usr/resources/qtwebengine_resources.pak",
                        "usr/resources/qtwebengine_resources_100p.pak",
                        "usr/resources/qtwebengine_resources_200p.pak",
                        "usr/resources/icudtl.dat", "usr/resources/v8_context_snapshot.bin",
                        "usr/translations/qtwebengine_locales/en-US.pak",
                        "usr/plugins/platforms/libqxcb.so",
                        "usr/qml/QtWebEngine/qmldir", "usr/bin/qml-build.manifest", "qml/Main.qml"]
            for name in required:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"fixture")
            self.module.verify_payload(root)
            (root / "usr/resources/qtwebengine_resources.pak").unlink()
            with self.assertRaisesRegex(RuntimeError, "qtwebengine_resources.pak"):
                self.module.verify_payload(root)


if __name__ == "__main__":
    unittest.main()
