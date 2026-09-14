"""Negative controls for the repaired Server 1.0 W2 evidence gate."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


MODULE_PATH = Path(__file__).with_name("reproduce_gate.py")
SPEC = importlib.util.spec_from_file_location("server1_w2_reproduce_gate", MODULE_PATH)
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class ManifestVerifierTests(unittest.TestCase):
    def make_repo(self):
        tmp = tempfile.TemporaryDirectory()
        root = Path(tmp.name)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.name", "Evidence Test"], cwd=root, check=True)
        subprocess.run(["git", "config", "core.autocrlf", "false"], cwd=root, check=True)
        self.addCleanup(tmp.cleanup)
        return root

    def commit(self, root, message):
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        subprocess.run(["git", "commit", "-q", "-m", message], cwd=root, check=True)
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()

    def write_manifest(self, root, content_ref, files):
        manifest = {
            "schema": "colosseum-server1-git-blob-manifest/v1",
            "content_ref": content_ref,
            "hash_algorithm": "sha256-git-blob",
            "files": files,
        }
        path = root / "HASHES.json"
        path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return path

    def base_repo(self, content=b"alpha\n"):
        root = self.make_repo()
        (root / "payload.txt").write_bytes(content)
        content_ref = self.commit(root, "content")
        digest = hashlib.sha256(content).hexdigest()
        manifest = self.write_manifest(root, content_ref, {"payload.txt": digest})
        self.commit(root, "manifest")
        return root, manifest, content_ref, digest

    def verify(self, root, manifest):
        self.assertTrue(hasattr(gate, "verify_hash_manifest"),
                        "packet-local Git-blob manifest verifier is missing")
        return gate.verify_hash_manifest(root, manifest, ("files",))

    def integrity_error(self):
        self.assertTrue(hasattr(gate, "EvidenceIntegrityError"),
                        "evidence-integrity failure type is missing")
        return gate.EvidenceIntegrityError

    def test_wrong_content_ref_is_rejected(self):
        root, manifest, _, digest = self.base_repo()
        self.write_manifest(root, "0" * 40, {"payload.txt": digest})
        self.commit(root, "wrong ref")
        with self.assertRaises(self.integrity_error()):
            self.verify(root, manifest)

    def test_one_byte_committed_blob_mutation_is_rejected(self):
        root, manifest, _, digest = self.base_repo()
        (root / "payload.txt").write_bytes(b"alphb\n")
        mutated_ref = self.commit(root, "mutate blob")
        self.write_manifest(root, mutated_ref, {"payload.txt": digest})
        self.commit(root, "stale digest")
        with self.assertRaises(self.integrity_error()):
            self.verify(root, manifest)

    def test_missing_manifest_path_is_rejected(self):
        root, manifest, content_ref, _ = self.base_repo()
        self.write_manifest(root, content_ref, {"missing.txt": "0" * 64})
        self.commit(root, "missing path")
        with self.assertRaises(self.integrity_error()):
            self.verify(root, manifest)

    def test_uncommitted_manifest_modification_is_rejected(self):
        root, manifest, content_ref, _ = self.base_repo()
        self.write_manifest(root, content_ref, {"payload.txt": "0" * 64})
        with self.assertRaises(self.integrity_error()):
            self.verify(root, manifest)

    def test_crlf_digest_cannot_stand_in_for_lf_git_blob(self):
        root, manifest, content_ref, _ = self.base_repo(b"one\ntwo\n")
        crlf_digest = hashlib.sha256(b"one\r\ntwo\r\n").hexdigest()
        self.write_manifest(root, content_ref, {"payload.txt": crlf_digest})
        self.commit(root, "wrong worktree-style digest")
        with self.assertRaises(self.integrity_error()):
            self.verify(root, manifest)

    def test_worktree_newline_variance_does_not_change_blob_verdict(self):
        root, manifest, _, _ = self.base_repo(b"one\ntwo\n")
        (root / "payload.txt").write_bytes(b"one\r\ntwo\r\n")
        result = self.verify(root, manifest)
        self.assertEqual(result["checked_files"], 1)
        self.assertEqual(result["result"], "PASS")

    def test_aggregate_manifest_must_bind_preceding_sealing_artifacts(self):
        root, manifest, _, _ = self.base_repo()
        self.assertTrue(hasattr(gate, "AGGREGATE_REQUIRED_PATHS"),
                        "aggregate sealing-chain requirements are missing")
        with self.assertRaises(self.integrity_error()):
            gate.verify_hash_manifest(
                root,
                manifest,
                ("files",),
                required_paths=("packet-HASHES.json", "test_reproduce_gate.py"),
            )


class MandatoryVerdictTests(unittest.TestCase):
    def good_fixture(self):
        expected_tests = [f"T{number:02d}" for number in range(16)]
        repeat_lines = "\n".join(
            f"      Start {number}: {expected_tests[(number - 1) % 16]}"
            for number in range(1, 49)
        )
        outputs = {
            "AGG-INVENTORY": json.dumps({
                "tests": [{"name": name} for name in expected_tests],
            }),
            "AGG-CTEST": "100% tests passed, 0 tests failed out of 16\n",
            "AGG-CTEST-REPEAT": (
                "100% tests passed, 0 tests failed out of 16\n" + repeat_lines + "\n"
            ),
            "K01-SOURCE": "\n".join(f"k01-{number}" for number in range(72)),
            "K01-NATIVE": "\n".join(f"k01-{number}" for number in range(72)),
            "K13A-SOURCE": "\n".join(f"k13a-{number}" for number in range(11)),
            "K13A-NATIVE": "\n".join(f"k13a-{number}" for number in range(11)),
            "K13B-SOURCE": "\n".join(f"k13b-{number}" for number in range(9)),
            "K13B-NATIVE": "\n".join(f"k13b-{number}" for number in range(9)),
            "H00-NATIVE-TRACE": "candidate one\ncandidate two\n",
            "H00-RAW-WIRE": "H00-01 PASS\n",
            "H00-STREAM-DISCONNECT": "H00-03 PASS\n",
            "COMBINED-RUN": "oversized-chunk-status=413\n",
        }
        m00 = {
            "differences": 0,
            "source_lines": 11,
            "candidate_lines": 11,
            "mutated_oracle_rejected": True,
        }
        expected_http = ["candidate one", "candidate two"]
        return outputs, expected_tests, expected_http, m00

    def verify(self, outputs, expected_tests, expected_http, m00):
        self.assertTrue(hasattr(gate, "verify_mandatory_verdicts"),
                        "explicit mandatory verdict verifier is missing")
        return gate.verify_mandatory_verdicts(
            outputs, expected_tests, expected_http, m00,
        )

    def test_valid_gate_outputs_pass_explicit_verdict_checks(self):
        outputs, expected_tests, expected_http, m00 = self.good_fixture()
        result = self.verify(outputs, expected_tests, expected_http, m00)
        self.assertEqual(result["ctest"], "16/16")
        self.assertEqual(result["repeated_test_executions"], 48)
        self.assertEqual(result["source_native_lines"], {
            "K01": 72,
            "K13-A": 11,
            "K13-B": 9,
            "M00": 11,
        })

    def test_every_mandatory_verdict_fails_closed(self):
        cases = {}

        outputs, expected, http, m00 = self.good_fixture()
        outputs["AGG-INVENTORY"] = json.dumps({"tests": [{"name": name} for name in expected[:-1]]})
        cases["inventory"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["AGG-CTEST"] = "99% tests passed\n"
        cases["single ctest"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["AGG-CTEST-REPEAT"] = outputs["AGG-CTEST-REPEAT"].replace(
            "100% tests passed, 0 tests failed out of 16", "99% tests passed", 1)
        cases["repeated ctest"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["AGG-CTEST-REPEAT"] = outputs["AGG-CTEST-REPEAT"].replace(
            "      Start 48: T15\n", "", 1)
        cases["repeat count"] = (outputs, expected, http, m00)

        for pair in ("K01", "K13A", "K13B"):
            outputs, expected, http, m00 = self.good_fixture()
            outputs[pair + "-NATIVE"] += "\nmutation"
            cases[pair + " differential"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["H00-NATIVE-TRACE"] = "candidate one\nmutation\n"
        cases["H00 trace"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["H00-RAW-WIRE"] = ""
        cases["H00 raw wire"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["H00-STREAM-DISCONNECT"] = ""
        cases["H00 disconnect"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        outputs["COMBINED-RUN"] = "oversized-chunk-status=400\n"
        cases["H00 413"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        m00["differences"] = 1
        cases["M00 differential"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        m00["differences"] = False
        cases["M00 typed differential"] = (outputs, expected, http, m00)

        outputs, expected, http, m00 = self.good_fixture()
        m00["mutated_oracle_rejected"] = False
        cases["M00 mutation"] = (outputs, expected, http, m00)

        for name, values in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(gate.EvidenceIntegrityError):
                    self.verify(*copy.deepcopy(values))


class RecursiveScrubberTests(unittest.TestCase):
    def make_tree(self):
        tmp = tempfile.TemporaryDirectory()
        root = Path(tmp.name) / "logs"
        root.mkdir()
        self.addCleanup(tmp.cleanup)
        return root

    def test_nested_path_spellings_are_detected_then_scrubbed(self):
        for name in ("find_private_path_leaks", "assert_no_private_path_leaks",
                     "scrub_generated_tree", "EvidenceIntegrityError"):
            self.assertTrue(hasattr(gate, name), f"recursive scrub seam is missing: {name}")
        root = self.make_tree()
        sensitive = [
            (r"C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired", "<REPO_ROOT>"),
            (r"D:\server1\scratch", "<SCRATCH_ROOT>"),
            (r"E:\oracle\server.js", "<ORACLE_PATH>"),
            (r"C:\Users\PrivateUser", "<USER_HOME>"),
        ]
        fixtures = {
            "a/backslash.txt": r"C:\b\Colosseum-Server-1.0-sol\INT-W2-repaired\native",
            "b/slash.txt": "D:/server1/scratch/build/output.txt",
            "c/deeper/file-url.txt": "file:///E:/oracle/server.js:18:8",
            "d/case.txt": r"c:\USERS\privateuser\AppData\Local",
        }
        for relative, value in fixtures.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value + "\n", encoding="utf-8")

        findings = gate.find_private_path_leaks(root, [item[0] for item in sensitive])
        self.assertEqual({item["path"] for item in findings}, set(fixtures))
        with self.assertRaises(gate.EvidenceIntegrityError):
            gate.assert_no_private_path_leaks(root, [item[0] for item in sensitive])

        scrubbed = gate.scrub_generated_tree(root, sensitive)
        self.assertEqual(scrubbed, 4)
        self.assertEqual(gate.find_private_path_leaks(root, [item[0] for item in sensitive]), [])
        for replacement in (item[1] for item in sensitive):
            self.assertTrue(any(replacement in path.read_text(encoding="utf-8")
                                for path in root.rglob("*") if path.is_file()))


if __name__ == "__main__":
    unittest.main()
