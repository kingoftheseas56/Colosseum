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

  4. the overlay is exactly what the native/cmake/android.cmake rules produce
     from source (exclusions, +android discovery placeholders, the staged
     reader2/qmldir), so a stale but self-consistent overlay fails too.

Skips when the generated overlay does not exist (machine without the Android
build tree). COLOSSEUM_REQUIRE_PACKAGING_GATE=1 (set by
scripts/android/build_apk.sh) turns every skip into a failure, so an APK is
never qualified by skipped checks. Stdlib only; runs under python -I.
"""
import hashlib
import pathlib
import re
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
REQUIRE_GATE = os.environ.get("COLOSSEUM_REQUIRE_PACKAGING_GATE") == "1"

# Mirrors native/cmake/android.cmake; keep the two in step.
QML_EXCLUDES = (
    r"^reader2/Paper[.]qml$", r"^player2/", r"^player2host/", r"^feria/",
    r"(^|/)Vault[^/]*[.](qml|js)$", r"^(TrackerApprovalWebView|ExtensionsSetupSheet)[.]qml$",
    r"(^|/)[.][^/]+",
)
RUNTIME_EXCLUDES = (r"(^|/)[.][^/]+", r"^resources/reader2/")
SELECTOR_PLACEHOLDER = (b"// Android selector discovery placeholder; implementation is in +android.\n"
                        b"import QtQuick\nItem {}\n")
GENERATED_MANIFESTS = ("qml-build.manifest", "runtime-files.manifest")


def expected_overlay(repo_root):
    """Runtime overlay hashes android.cmake should generate from this source."""
    repo = pathlib.Path(repo_root)
    expected = {}
    qml_root = repo / "qml"
    for path in sorted(qml_root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(qml_root).as_posix()
        if any(re.search(pattern, rel) for pattern in QML_EXCLUDES):
            continue
        data = path.read_bytes()
        if ("+android/" not in "/" + rel and rel.endswith(".qml")
                and (path.parent / "+android" / path.name).is_file()):
            data = SELECTOR_PLACEHOLDER
        if rel == "reader2/qmldir":
            data = data.replace(b"Paper Paper.qml\r\n", b"").replace(b"Paper Paper.qml\n", b"")
        expected["qml/" + rel] = _sha256_bytes(data)
    for runtime_dir in ("assets", "resources"):
        for path in sorted((repo / runtime_dir).rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(repo).as_posix()
            if any(re.search(pattern, rel) for pattern in RUNTIME_EXCLUDES):
                continue
            expected[rel] = _sha256_file(str(path))
    return expected


def overlay_drift(expected, actual):
    """Missing, extra and changed members between two {rel: sha256} maps."""
    drift = [f"missing {rel}" for rel in sorted(expected.keys() - actual.keys())]
    drift += [f"extra {rel}" for rel in sorted(actual.keys() - expected.keys())]
    drift += [f"changed {rel}" for rel in sorted(expected.keys() & actual.keys())
              if expected[rel] != actual[rel]]
    return drift


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

    def test_overlay_matches_source_rules(self):
        _, entries = _tree_hashes(OVERLAY_RUNTIME, exclude_names=GENERATED_MANIFESTS)
        drift = overlay_drift(expected_overlay(REPO_ROOT), dict(entries))
        self.assertEqual(drift, [], "generated overlay differs from source: " + "; ".join(drift[:20]))

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


class OverlayDriftNegativeControl(unittest.TestCase):
    """The comparison itself must catch a stale, missing or extra member."""

    def test_stale_missing_and_extra_members_are_reported(self):
        expected = {"qml/Main.qml": "a" * 64, "qml/TopBar.qml": "b" * 64}
        stale = {"qml/Main.qml": "c" * 64, "qml/Extra.qml": "d" * 64}
        self.assertEqual(
            overlay_drift(expected, stale),
            ["missing qml/TopBar.qml", "extra qml/Extra.qml", "changed qml/Main.qml"],
        )
        self.assertEqual(overlay_drift(expected, dict(expected)), [])


if __name__ == "__main__":
    if not REQUIRE_GATE:
        unittest.main(verbosity=2)
    result = unittest.main(verbosity=2, exit=False).result
    if result.skipped or not result.wasSuccessful():
        print(f"PACKAGING_GATE_FAILED skipped={len(result.skipped)} "
              f"failures={len(result.failures)} errors={len(result.errors)}", file=sys.stderr)
        sys.exit(1)
    print(f"PACKAGING_GATE_OK tests={result.testsRun} skipped=0")
