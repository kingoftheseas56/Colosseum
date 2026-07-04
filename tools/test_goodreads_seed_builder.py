import csv
import gzip
import importlib.util
import json
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("goodreads_seed_builder.py")


def load_module():
    spec = importlib.util.spec_from_file_location("goodreads_seed_builder", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_jsonl_gz(path, rows):
    with gzip.open(path, "wt", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row) + "\n")


def test_extracts_series_seed_rows_from_goodreads_dump(tmp_path):
    mod = load_module()
    authors = tmp_path / "authors.json.gz"
    series = tmp_path / "series.json.gz"
    books = tmp_path / "books.json.gz"
    out = tmp_path / "seeds.csv"

    write_jsonl_gz(authors, [{"author_id": "1", "name": "Isaac Asimov"}])
    write_jsonl_gz(series, [{"series_id": "250807", "title": "Foundation"}])
    write_jsonl_gz(
        books,
        [
            {
                "book_id": "10",
                "work_id": "100",
                "title": "Foundation (Foundation, #1)",
                "title_without_series": "Foundation (Foundation, #1)",
                "language_code": "eng",
                "ratings_count": "1000",
                "average_rating": "4.13",
                "isbn13": "9781415917763",
                "series": ["250807"],
                "authors": [{"author_id": "1", "role": ""}],
            }
        ],
    )

    count = mod.build_seed_csv(
        books_path=books,
        authors_path=authors,
        series_path=series,
        out_path=out,
        limit=10,
        min_ratings=10,
        include_unknown_language=False,
    )

    assert count == 1
    rows = list(csv.DictReader(out.open(encoding="utf-8")))
    assert rows == [
        {
            "title": "Foundation",
            "author": "Isaac Asimov",
            "expected_series": "Foundation",
            "expected_position": "1",
            "franchise": "Foundation",
            "source": "goodreads_dump",
            "book_id": "10",
            "work_id": "100",
            "series_id": "250807",
            "isbn13": "9781415917763",
            "ratings_count": "1000",
            "average_rating": "4.13",
        }
    ]


def test_filters_junk_and_series_ranges_by_default(tmp_path):
    mod = load_module()
    authors = tmp_path / "authors.json.gz"
    series = tmp_path / "series.json.gz"
    books = tmp_path / "books.json.gz"
    out = tmp_path / "seeds.csv"

    write_jsonl_gz(authors, [{"author_id": "1", "name": "Barbara Hambly"}])
    write_jsonl_gz(series, [{"series_id": "189911", "title": "Sun Wolf and Starhawk"}])
    write_jsonl_gz(
        books,
        [
            {
                "book_id": "1",
                "work_id": "1",
                "title": "The Unschooled Wizard (Sun Wolf and Starhawk, #1-2)",
                "title_without_series": "The Unschooled Wizard (Sun Wolf and Starhawk, #1-2)",
                "language_code": "eng",
                "ratings_count": "140",
                "series": ["189911"],
                "authors": [{"author_id": "1"}],
            },
            {
                "book_id": "2",
                "work_id": "2",
                "title": "Summary of Foundation (Foundation, #1)",
                "title_without_series": "Summary of Foundation (Foundation, #1)",
                "language_code": "eng",
                "ratings_count": "10000",
                "series": ["189911"],
                "authors": [{"author_id": "1"}],
            },
        ],
    )

    count = mod.build_seed_csv(
        books_path=books,
        authors_path=authors,
        series_path=series,
        out_path=out,
        limit=10,
        min_ratings=10,
        include_unknown_language=False,
    )

    assert count == 0
    rows = list(csv.DictReader(out.open(encoding="utf-8")))
    assert rows == []
