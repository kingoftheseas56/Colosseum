import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_notes", ROOT / "scripts/update/build_release_notes.py")
notes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notes)


class ReleaseNotesPage(unittest.TestCase):
    def test_served_page_matches_the_tracked_document(self):
        source = (ROOT / "docs/release-notes/v1.1.9.md").read_text(encoding="utf-8")
        self.assertEqual(notes.build(source), (ROOT / "scripts/update/release_notes_119.mjs").read_text(encoding="utf-8"))

    def test_1110_page_matches_the_tracked_presentation_notes(self):
        source = (ROOT / "docs/release-notes/v1.1.10.md").read_text(encoding="utf-8")
        generated = (ROOT / "scripts/update/release_notes_1110.mjs").read_text(encoding="utf-8")
        self.assertEqual(notes.build(source, "1.1.10"), generated)
        self.assertIn("<title>Colosseum 1.1.10", generated)
        self.assertNotIn("<title>Colosseum 1.1.9", generated)
        for expected in ("Colosseum 1.1.10", "Secret Service", "MangaSin", "newer than 1.1.9"):
            self.assertIn(expected, generated)

    def test_worker_config_keeps_both_versioned_notes_routes(self):
        config = (ROOT / "scripts/update/wrangler.jsonc").read_text(encoding="utf-8")
        self.assertIn("https://getcolosseum.world/releases/v1.1.9*", config)
        self.assertIn("https://getcolosseum.world/releases/v1.1.10*", config)

    def test_renderer_escapes_content_and_preserves_wrapped_bullets(self):
        page = notes.render("# Version\n\n## Content\n\n- First line\n  continued `code`\n- <script>alert(1)</script>\n\nParagraph & finish.")
        self.assertIn("<h1>Version</h1>", page)
        self.assertIn("<li>First line continued <code>code</code></li>", page)
        self.assertIn("&lt;script&gt;alert(1)&lt;/script&gt;", page)
        self.assertNotIn("<script>", page)
        self.assertIn("Paragraph &amp; finish.", page)


if __name__ == "__main__":
    unittest.main()
