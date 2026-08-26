import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GUARD = REPO_ROOT / "scripts" / "check_public_paths.py"


class PublicPathGuardTests(unittest.TestCase):
    def make_repo(self):
        tmp = tempfile.TemporaryDirectory()
        root = Path(tmp.name)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.name", "TestUser"], cwd=root, check=True)
        self.addCleanup(tmp.cleanup)
        return root

    def track(self, root, relative, content):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        subprocess.run(["git", "add", relative], cwd=root, check=True)
        return path

    def run_guard(self, root):
        return subprocess.run(
            [sys.executable, str(GUARD)], cwd=root, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)

    def test_allows_neutral_testuser_fixture(self):
        root = self.make_repo()
        self.track(root, "fixture.txt", r"C:\Users\TestUser\AppData\fixture.json")
        result = self.run_guard(root)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_rejects_private_windows_user_path_without_echoing_username(self):
        root = self.make_repo()
        private_user = "Supra" + "bha"
        self.track(root, "leak.txt", f"C:/Users/{private_user}/Desktop/Colosseum")
        result = self.run_guard(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("leak.txt:1", result.stdout)
        self.assertNotIn(private_user.lower(), result.stdout.lower())

    def test_rejects_private_username_token_without_echoing_it(self):
        root = self.make_repo()
        private_user = "Supra" + "bha"
        self.track(root, "note.txt", f"owner={private_user}\n")
        result = self.run_guard(root)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("note.txt:1", result.stdout)
        self.assertNotIn(private_user.lower(), result.stdout.lower())

    def test_ignores_untracked_files(self):
        root = self.make_repo()
        self.track(root, "tracked.txt", r"C:\Users\TestUser\fixture.txt")
        private_user = "Supra" + "bha"
        (root / "scratch.txt").write_text(
            f"C:/Users/{private_user}/Desktop/scratch", encoding="utf-8")
        result = self.run_guard(root)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
