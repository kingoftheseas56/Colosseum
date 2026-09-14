"""Negative controls for the repaired Server 1.0 W2 evidence gate."""

from __future__ import annotations

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
