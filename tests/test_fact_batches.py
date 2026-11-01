import csv
import hashlib
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/build_fact_batches.py"
spec = importlib.util.spec_from_file_location("fact_batches", SCRIPT)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BatchTests(unittest.TestCase):
    def test_unknown_timezone_is_not_invented(self):
        self.assertEqual(builder.sort_time("2026-10-13T01:00:00"), "9999")
        self.assertEqual(builder.sort_time("2026-10-13T01:00:00+05:30"),
                         "2026-10-12T19:30:00+00:00")

    def test_fragments_reconstruct_original_and_stay_bounded(self):
        text = "# Transcript\n" + ("## User\n\nQuestion.\n## Assistant\n\nAnswer.\n" * 200)
        parts = list(builder.split_transcript(text, 2000))
        self.assertEqual("".join(p["body"] for p in parts), text)
        self.assertTrue(all(len(p["body"]) <= 2000 for p in parts))
        self.assertEqual(parts[-1]["clean_char_end"], len(text))

    def test_packet_manifest_hashes_and_coverage(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            text = ("## User\n\n**Raw reference:** L1\nQuestion\n" * 200).encode()
            transcript = root / "one.md"
            transcript.write_bytes(text)
            manifest = root / "MANIFEST.csv"
            row = {"source": "raw-session.jsonl", "markdown": str(transcript),
                   "sha256": hashlib.sha256(text).hexdigest(), "raw_sha256": "a" * 64,
                   "first_message_at": "2026-10-13T00:00:00Z"}
            with manifest.open("w", newline="", encoding="utf-8") as file:
                writer = csv.DictWriter(file, fieldnames=list(row))
                writer.writeheader()
                writer.writerow(row)
            report = builder.build([manifest], root / "packets", 2000, 2)
            self.assertGreater(report["batches"], 1)
            rebuilt = b""
            for packet in report["packets"]:
                self.assertLessEqual(packet["characters"], 2000)
                with zipfile.ZipFile(packet["zip"]) as archive:
                    self.assertIn("PROMPT-DOTS.md", archive.namelist())
                    data = json.loads(archive.read("BATCH-MANIFEST.json"))
                    for part in data["transcripts"]:
                        body = archive.read(part["fragment"])
                        self.assertEqual(hashlib.sha256(body).hexdigest(), part["fragment_sha256"])
                        rebuilt += body
            self.assertEqual(rebuilt, text)


if __name__ == "__main__":
    unittest.main()
