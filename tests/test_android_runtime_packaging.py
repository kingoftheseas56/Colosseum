"""Android runtime packaging correspondence check.

Regression guard for the task011 defect: an APK shipped a stale
assets/colosseum-runtime/qml/Main.qml (no onFixturePlayRequested) while the
checkout and the generated android-package overlay already carried the fixed
file, because Gradle staging was copied from a stale android-build tree.

The check proves, without changing product behavior:
  1. the generated overlay's qml/Main.qml is byte-equal to the source qml/Main.qml;
  2. the overlay's qml-build.manifest / runtime-files.manifest are internally
     consistent (recomputed tree/bundle hashes match the packaged values);
  3. when a packaged candidate is supplied (APK file or Gradle staging dir),
     every assets/colosseum-runtime member matches the overlay hash-for-hash,
     with no missing or extra members.

Skips when the generated overlay does not exist (machine without the Android
build tree). Stdlib only; runs under python -I.
"""
import hashlib
import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_MAIN_QML = os.path.join(REPO_ROOT, "qml", "Main.qml")
ANDROID_BUILD_DIR = os.environ.get(
    "NUVIO_ANDROID_BUILD_DIR", os.path.join(REPO_ROOT, "build", "colosseum-on")
)
OVERLAY_RUNTIME = os.path.join(
    ANDROID_BUILD_DIR, "android-package", "assets", "colosseum-runtime"
)
DEFAULT_CANDIDATE = os.environ.get("NUVIO_ANDROID_PACKAGING_CANDIDATE", "")


def _sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def _tree_hashes(root, exclude_names=()):
    """Recompute the cmake write_qml_build_manifest tree material for a dir."""
    rels = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames)
        for fn in filenames:
            if fn in exclude_names:
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), root).replace("\\", "/")
            rels.append(rel)
    rels.sort()
    material = ""
    entries = []
    for rel in rels:
        file_hash = _sha256_file(os.path.join(root, rel.replace("/", os.sep)))
        material += f"{rel}\n{file_hash}\n"
        entries.append((rel, file_hash))
    return _sha256_bytes(material.encode("utf-8")), entries


@unittest.skipUnless(
    os.path.isfile(SOURCE_MAIN_QML), f"source Main.qml missing: {SOURCE_MAIN_QML}"
)
@unittest.skipUnless(
    os.path.isfile(os.path.join(OVERLAY_RUNTIME, "qml", "Main.qml")),
    f"generated Android overlay missing (no Android build tree): {OVERLAY_RUNTIME}",
)
class AndroidRuntimePackaging(unittest.TestCase):
    def test_overlay_main_qml_equals_source(self):
        self.assertEqual(
            _sha256_file(os.path.join(OVERLAY_RUNTIME, "qml", "Main.qml")),
            _sha256_file(SOURCE_MAIN_QML),
            "generated overlay Main.qml differs from source qml/Main.qml; "
            "re-run the CMake Android overlay generation before packaging",
        )

    def test_overlay_qml_build_manifest_consistent(self):
        manifest = os.path.join(OVERLAY_RUNTIME, "qml-build.manifest")
        with open(manifest, "r", encoding="utf-8") as f:
            content = f.read()
        packaged = None
        for line in content.splitlines():
            if line.startswith("qmlTreeSha256="):
                packaged = line.split("=", 1)[1].strip()
        tree, _ = _tree_hashes(os.path.join(OVERLAY_RUNTIME, "qml"))
        self.assertEqual(
            tree,
            packaged,
            "qml-build.manifest tree hash does not match the overlay qml tree",
        )

    def test_overlay_runtime_files_manifest_consistent(self):
        manifest = os.path.join(OVERLAY_RUNTIME, "runtime-files.manifest")
        with open(manifest, "r", encoding="utf-8") as f:
            content = f.read()
        packaged = None
        for line in content.splitlines():
            if line.startswith("bundleSha256="):
                packaged = line.split("=", 1)[1].strip()
        bundle, entries = _tree_hashes(OVERLAY_RUNTIME, exclude_names=("runtime-files.manifest",))
        self.assertEqual(
            bundle,
            packaged,
            "runtime-files.manifest bundle hash does not match the overlay runtime tree",
        )
        listed = {}
        for line in content.splitlines():
            if line.startswith("file="):
                rel, sha = line[5:].split("\t")
                listed[rel] = sha
        actual = dict(entries)
        self.assertEqual(sorted(listed), sorted(actual), "manifest file set differs")
        for rel in actual:
            self.assertEqual(listed.get(rel), actual[rel], f"manifest hash drift: {rel}")

    def test_candidate_matches_overlay(self):
        candidate = DEFAULT_CANDIDATE
        if not candidate:
            self.skipTest("no NUVIO_ANDROID_PACKAGING_CANDIDATE supplied")
        overlay = {}
        for dirpath, dirnames, filenames in os.walk(OVERLAY_RUNTIME):
            for fn in filenames:
                p = os.path.join(dirpath, fn)
                rel = "assets/colosseum-runtime/" + os.path.relpath(p, OVERLAY_RUNTIME).replace("\\", "/")
                overlay[rel] = _sha256_file(p)
        members = {}
        if os.path.isfile(candidate):
            import zipfile

            with zipfile.ZipFile(candidate) as z:
                for name in z.namelist():
                    if name.startswith("assets/colosseum-runtime/") and not name.endswith("/"):
                        members[name] = _sha256_bytes(z.read(name))
        elif os.path.isdir(candidate):
            runtime_dir = os.path.join(candidate, "assets", "colosseum-runtime")
            if not os.path.isdir(runtime_dir):
                self.fail(f"candidate staging has no assets/colosseum-runtime: {candidate}")
            for dirpath, dirnames, filenames in os.walk(runtime_dir):
                for fn in filenames:
                    p = os.path.join(dirpath, fn)
                    rel = "assets/colosseum-runtime/" + os.path.relpath(p, runtime_dir).replace("\\", "/")
                    members[rel] = _sha256_file(p)
        else:
            self.fail(f"candidate not found: {candidate}")
        self.assertEqual(
            sorted(members), sorted(overlay), "packaged runtime member set differs from overlay"
        )
        for rel in sorted(overlay):
            self.assertEqual(
                members.get(rel), overlay[rel], f"packaged runtime member drift: {rel}"
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
