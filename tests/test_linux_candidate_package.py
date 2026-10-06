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

    @staticmethod
    def elf_header(kind, bits=64, byteorder="little"):
        import struct
        endian = "<" if byteorder == "little" else ">"
        ident = b"\x7fELF" + bytes([2 if bits == 64 else 1,
                                    1 if byteorder == "little" else 2, 1, 0]) + bytes(8)
        if bits == 64:
            return ident + struct.pack(endian + "HHIQQQIHHHHHH",
                kind, 62, 1, 0, 0, 0, 0, 64, 56, 0, 64, 0, 0)
        return ident + struct.pack(endian + "HHIIIIIHHHHHH",
            kind, 3, 1, 0, 0, 0, 0, 52, 32, 0, 40, 0, 0)

    def test_runtime_elf_distinguishes_objects_executables_and_shared_libraries(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "no-extension"
            for bits in (32, 64):
                for order in ("little", "big"):
                    for kind, expected in ((1, False), (2, True), (3, True), (4, False)):
                        with self.subTest(bits=bits, order=order, kind=kind):
                            path.write_bytes(self.elf_header(kind, bits, order))
                            self.assertEqual(self.module.is_runtime_elf(path), expected)
            path.write_bytes(b"ordinary QML or resource")
            self.assertFalse(self.module.is_runtime_elf(path))
            self.assertFalse(self.module.is_runtime_elf(Path(temp)))

    def test_malformed_elf_headers_fail_closed(self):
        good = self.elf_header(3)
        malformed = [good[:4], good[:15], good[:-1]]
        for offset, value in ((4, 0), (5, 0), (6, 0), (20, 0), (52, 0)):
            bad = bytearray(good)
            bad[offset] = value
            malformed.append(bytes(bad))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "broken.so"
            for data in malformed:
                with self.subTest(header=data.hex()):
                    path.write_bytes(data)
                    with self.assertRaisesRegex(RuntimeError, "Malformed ELF"):
                        self.module.is_runtime_elf(path)

    def test_sdk_copy_excludes_object_outputs_but_keeps_runtime_resources(self):
        import shutil
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "sdk"
            target = Path(temp) / "runtime"
            keep = ["QtWebEngine/qmldir", "QtWebEngine/libplugin.so",
                    "qtwebengine_resources.pak", "icudtl.dat", "en-US.pak"]
            exclude = ["qrc_init.cpp.o", "object.obj", "libstatic.a",
                       "objects-RelWithDebInfo/component/.qt/rcc/qrc.cpp.o"]
            for name in keep + exclude:
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            shutil.copytree(source, target, ignore=self.module.sdk_runtime_ignores)
            for name in keep:
                self.assertTrue((target / name).is_file(), name)
            for name in exclude:
                self.assertFalse((target / name).exists(), name)
            self.assertFalse((target / "objects-RelWithDebInfo").exists())

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
