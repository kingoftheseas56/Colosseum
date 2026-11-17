"""Synthetic IMDb bake; fixture_fetch makes any public/network fetch impossible."""
from contextlib import closing
import gzip
import importlib.util
import pathlib
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

SOURCE = pathlib.Path(__file__).resolve().parents[1] / "scripts/theatre_brain/build_imdb_db.py"
spec = importlib.util.spec_from_file_location("imdb_bake_fixture", SOURCE)
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


class ImdbBakeTest(unittest.TestCase):
    def test_tv_movie_coverage_preserves_adult_and_vote_gates(self):
        self.assertTrue(bake.keep_title("tvMovie", "0", bake.VOTE_FLOOR))
        self.assertFalse(bake.keep_title("tvMovie", "1", 99999))
        self.assertFalse(bake.keep_title("tvMovie", "0", bake.VOTE_FLOOR - 1))
        self.assertFalse(bake.keep_title("tvEpisode", "0", 99999))
        self.assertFalse(bake.keep_title("short", "0", 99999))

    def test_alias_provenance_and_feature_coverage(self):
        # id, type, primary, original, adult, votes, genres
        titles = [
            ("tt1", "movie", "Canonical Film", "Original Film", "0", 1000, "Drama"),
            ("tt2", "movie", "Below Floor", "Below Floor", "0", 999, "Drama"),
            ("tt3", "movie", "Adult", "Adult", "1", 9000, "Drama"),
            ("tt4", "tvEpisode", "Episode", "Episode", "0", 9000, "Drama"),
            ("tt5", "video", "Video Feature", "Video Feature", "0", 1000, "Animation,Action"),
            ("tt6", "video", "Music Video", "Music Video", "0", 9000, "Music,Drama"),
            ("tt7", "video", "Short Video", "Short Video", "0", 9000, "Animation,Short"),
            ("tt8", "tvMovie", "TV Feature", "TV Feature", "0", 1000, "Drama"),
            ("tt9", "video", "Adult Video", "Adult Video", "1", 9000, "Drama"),
            ("tt10", "video", "Low Vote Video", "Low Vote Video", "0", 999, "Drama"),
            ("tt11", "short", "Standalone Short", "Standalone Short", "0", 9000, "Drama"),
        ]
        def tsv(header, rows):
            return header + "\n" + "".join("\t".join(map(str, row)) + "\n" for row in rows)

        fixtures = {
            "title.ratings.tsv.gz": tsv(
                "tconst\taverageRating\tnumVotes", [(t[0], 8, t[5]) for t in titles]),
            "title.basics.tsv.gz": tsv(
                "tconst\ttitleType\tprimaryTitle\toriginalTitle\tisAdult\tstartYear\tendYear\truntimeMinutes\tgenres",
                [(t[0], t[1], t[2], t[3], t[4], 2001, "\\N", 100, t[6]) for t in titles]),
            "title.episode.tsv.gz": tsv("tconst\tparentTconst\tseasonNumber\tepisodeNumber", []),
            "title.akas.tsv.gz": tsv(
                "titleId\tordering\ttitle\tregion\tlanguage\ttypes\tattributes\tisOriginalTitle", [
                    ("tt1", 1, "Alternate Film", "GB", "en", "imdbDisplay", "\\N", 0),
                    ("tt1", 2, "Alternate Film", "GB", "en", "imdbDisplay", "\\N", 0),
                    ("tt1", 3, "Alternate Film", "US", "en", "imdbDisplay", "\\N", 0),
                    ("tt2", 1, "Forbidden Alias", "US", "en", "imdbDisplay", "\\N", 0),
                ]),
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for name, text in fixtures.items():
                with gzip.open(root / name, "wt", encoding="utf-8", newline="") as stream:
                    stream.write(text)
            output = root / "candidate.sqlite"
            def fixture_fetch(name):
                if name not in fixtures:
                    raise AssertionError("Unexpected fetch " + name)
                return str(root / name)
            with patch.object(bake, "fetch", side_effect=fixture_fetch), \
                 patch.object(bake, "load_anime_tt", return_value=set()), \
                 patch.object(bake, "DB_PATH", str(output)):
                self.assertEqual(bake.bake(), 0)
            with closing(sqlite3.connect(output)) as db:
                self.assertEqual(db.execute("SELECT tt FROM title ORDER BY tt").fetchall(),
                                 [("tt1",), ("tt5",), ("tt8",)])
                self.assertEqual(db.execute("SELECT tt,type,sourceType FROM title ORDER BY tt").fetchall(),
                                 [("tt1", "movie", "movie"), ("tt5", "movie", "video"),
                                  ("tt8", "movie", "tvMovie")])
                self.assertEqual(db.execute(
                    "SELECT title,source FROM title_alias WHERE norm_title='original film'").fetchall(),
                    [("Original Film", "title.basics.tsv:originalTitle")])
                self.assertEqual(db.execute(
                    "SELECT tt,title,region,language,source FROM title_alias "
                    "WHERE norm_title='alternate film' ORDER BY region").fetchall(),
                    [("tt1", "Alternate Film", "GB", "en", "title.akas.tsv"),
                     ("tt1", "Alternate Film", "US", "en", "title.akas.tsv")])
                self.assertEqual(db.execute(
                    "SELECT COUNT(*) FROM title_alias WHERE tt NOT IN ('tt1','tt5','tt8')").fetchone()[0], 0)
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone()[0], "ok")
                self.assertEqual(db.execute(
                    "SELECT value FROM meta WHERE key='vote_floor'").fetchone()[0], str(bake.VOTE_FLOOR))


if __name__ == "__main__":
    unittest.main()
