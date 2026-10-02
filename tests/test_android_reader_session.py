"""Run the Android reader's platform-independent token/event boundary on the host JDK."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReaderSessionTest(unittest.TestCase):
    def test_publication_and_event_isolation(self):
        source = ROOT / "native/platform/android/src/org/colosseum/reader/ReaderPublicationSession.java"
        self.assertTrue(source.exists(), "Android publication session boundary is missing")
        jdk = Path(os.environ.get("JAVA_HOME", "")) / "bin"
        suffix = ".exe" if os.name == "nt" else ""
        def java_tool(name):
            candidate = jdk / (name + suffix)
            found = str(candidate) if candidate.is_file() else shutil.which(name)
            self.assertIsNotNone(found, f"{name} is required for the Android reader boundary test")
            return found
        with tempfile.TemporaryDirectory(prefix="colosseum-reader-session-") as directory:
            subprocess.run([java_tool("javac"), "-d", directory, str(source),
                            str(ROOT / "tests/android/ReaderPublicationSessionTest.java")],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([java_tool("java"), "-cp", directory,
                                     "org.colosseum.reader.ReaderPublicationSessionTest"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("READER_PUBLICATION_SESSION_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
