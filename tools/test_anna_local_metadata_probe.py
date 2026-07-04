from pathlib import Path

from anna_local_metadata_probe import best_matches, iter_json_records, read_seed_csv


def test_anna_local_probe_matches_unified_record(tmp_path: Path) -> None:
    seed = tmp_path / "seed.csv"
    seed.write_text(
        "title,author,expected_series,expected_position\n"
        "Against intellectual monopoly,\"Michele Boldrin, David K. Levine\",,\n",
        encoding="utf-8",
    )
    record = tmp_path / "record.json"
    record.write_text(
        """
        {
          "id": "md5:test",
          "file_unified_data": {
            "title_best": "Against intellectual monopoly",
            "author_best": "Michele Boldrin, David K. Levine",
            "publisher_best": "Cambridge University Press",
            "year_best": "2008",
            "extension_best": "pdf",
            "filesize_best": 4283327,
            "cover_url_best": "https://example.test/cover.jpg"
          }
        }
        """,
        encoding="utf-8",
    )

    rows = best_matches(read_seed_csv(seed), list(iter_json_records(record)))

    assert rows[0]["anna_search_hit"] is True
    assert rows[0]["anna_title_exact"] is True
    assert rows[0]["anna_author_exact"] is True
    assert rows[0]["anna_has_publisher"] is True
    assert rows[0]["anna_extension"] == "pdf"
