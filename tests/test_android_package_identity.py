#!/usr/bin/env python3
"""PF-10: native/cmake/AndroidPackageIdentity.cmake accepts the current lanes (default, the
authorized world.getcolosseum.app.beta, the permanent id, Qt development ids) and rejects
malformed/out-of-lane package ids, non-decimal or out-of-range versionCodes and unsafe
labels/versionNames at configure time. Runs CMake in script mode; no Android toolchain."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "native/cmake/AndroidPackageIdentity.cmake"


def run(app_id="", name="Colosseum", version_name="1.2.0", version_code=""):
    with tempfile.TemporaryDirectory() as tmp:
        script = Path(tmp) / "check.cmake"
        def q(v):
            return v.replace("\\", "\\\\").replace('"', '\\"')
        script.write_text(
            f'include("{MODULE.as_posix()}")\n'
            f'colosseum_validate_android_identity("{q(app_id)}" "{q(name)}" "{q(version_name)}" "{q(version_code)}")\n'
            'message(STATUS "IDENTITY_OK")\n', encoding="utf-8")
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
        return result.returncode == 0 and "IDENTITY_OK" in (result.stdout + result.stderr), result.stderr


class AndroidPackageIdentityTest(unittest.TestCase):
    def accept(self, **kw):
        ok, err = run(**kw)
        self.assertTrue(ok, f"{kw} should be accepted: {err}")

    def reject(self, **kw):
        ok, _ = run(**kw)
        self.assertFalse(ok, f"{kw} should be rejected")

    def test_current_lanes_accepted(self):
        self.accept()
        self.accept(app_id="world.getcolosseum.app.beta", version_code="1")
        self.accept(app_id="world.getcolosseum.app", version_name="1.2.0-beta", version_code="2100000000")
        self.accept(app_id="world.getcolosseum.app.alpha")
        self.accept(app_id="org.qtproject.example.colosseum")
        self.accept(app_id="org.qtproject.example.colosseum.preview2")
        self.accept(name="Colosseum Beta", version_name="1.2.0 beta 3")

    def test_package_ids_rejected(self):
        for bad in ["World.getcolosseum.app", "world.getcolosseum.app.", "colosseum", "world..app",
                    "world.getcolosseum.app.beta.extra", "com.example.other", "world.getcolosseum.app.1beta",
                    "world.getcolosseum.app.be-ta"]:
            self.reject(app_id=bad)

    def test_version_codes_rejected(self):
        for bad in ["0", "-1", "01", "1.5", "12a", "2100000001", "99999999999", " 5"]:
            self.reject(version_code=bad)

    def test_labels_rejected(self):
        self.reject(name="")
        self.reject(name="Colosseum\nBeta")
        self.reject(name='Colo"sseum')
        self.reject(version_name="")
        self.reject(version_name="-1.0")
        self.reject(version_name="1.0<beta>")


if __name__ == "__main__":
    sys.exit(unittest.main())
