#!/usr/bin/env python3
"""Metadata-only Anna's Archive local JSON probe.

This consumes local Anna `aarecord_elasticsearch` JSON/JSONL records. It does
not call Anna's live search UI, log in, click download links, or fetch files.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any, Iterable


def normalize(text: str) -> str:
    text = (text or "").casefold().replace("&", " and ")
    text = re.sub(r"[^a-z0-9]+", " ", text)
    text = re.sub(r"\bthe\b", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def read_seed_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def iter_json_records(path: Path) -> Iterable[dict[str, Any]]:
    if path.is_dir():
        for child in sorted(path.rglob("*.json")):
            yield from iter_json_records(child)
        for child in sorted(path.rglob("*.jsonl")):
            yield from iter_json_records(child)
        return
    if path.suffix.lower() == ".jsonl":
        with path.open(encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    yield json.loads(line)
        return
    text = path.read_text(encoding="utf-8")
    if text.lstrip().startswith("["):
        yield from json.loads(text)
    else:
        yield json.loads(text)


def unified(record: dict[str, Any]) -> dict[str, Any]:
    return record.get("file_unified_data") or record.get("_source", {}).get("file_unified_data") or {}


def pick(record: dict[str, Any], name: str) -> str:
    value = unified(record).get(name)
    if value is None:
        return ""
    if isinstance(value, list):
        return "; ".join(str(x) for x in value if x is not None)
    return str(value)


def record_title(record: dict[str, Any]) -> str:
    return pick(record, "title_best")


def record_author(record: dict[str, Any]) -> str:
    return pick(record, "author_best")


def score(seed: dict[str, str], record: dict[str, Any]) -> int:
    st = normalize(seed.get("title", ""))
    sa = normalize(seed.get("author", ""))
    rt = normalize(record_title(record))
    ra = normalize(record_author(record))
    value = 0
    if st and rt == st:
        value += 100
    elif st and (rt in st or st in rt):
        value += 55
    if sa and ra == sa:
        value += 80
    elif sa and any(part for part in sa.split() if len(part) > 3 and part in ra):
        value += 25
    return value


def best_matches(seeds: list[dict[str, str]], records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for seed in seeds:
        candidates = sorted(((score(seed, record), record) for record in records), key=lambda x: x[0], reverse=True)
        best_score, best = candidates[0] if candidates else (0, {})
        row = {
            "title": seed.get("title", ""),
            "author": seed.get("author", ""),
            "expected_series": seed.get("expected_series", ""),
            "expected_position": seed.get("expected_position", ""),
            "anna_search_hit": best_score >= 80,
            "anna_match_score": best_score,
            "anna_selected_id": best.get("id") or best.get("_id") or "",
            "anna_selected_title": record_title(best),
            "anna_selected_author": record_author(best),
            "anna_title_exact": normalize(record_title(best)) == normalize(seed.get("title", "")),
            "anna_author_exact": normalize(record_author(best)) == normalize(seed.get("author", "")),
            "anna_has_publisher": bool(pick(best, "publisher_best")),
            "anna_publisher": pick(best, "publisher_best"),
            "anna_has_year": bool(pick(best, "year_best")),
            "anna_year": pick(best, "year_best"),
            "anna_has_extension": bool(pick(best, "extension_best")),
            "anna_extension": pick(best, "extension_best"),
            "anna_has_filesize": bool(pick(best, "filesize_best")),
            "anna_filesize": pick(best, "filesize_best"),
            "anna_cover_url": pick(best, "cover_url_best"),
        }
        rows.append(row)
    return rows


def pct(n: int, d: int) -> str:
    return f"{(n / d * 100):.1f}%" if d else "0.0%"


def write_csv(rows: list[dict[str, Any]], path: Path) -> None:
    columns = list(rows[0].keys()) if rows else []
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def write_report(rows: list[dict[str, Any]], path: Path) -> None:
    total = len(rows)
    count = lambda field: sum(1 for row in rows if row.get(field))
    lines = [
        "# Anna's Archive Local Metadata Probe Report",
        "",
        "## Boundary",
        "",
        "This pass consumes local Anna metadata JSON/JSONL records only. It does not use live Anna search, login, download links, or file fetching.",
        "",
        "## Summary",
        "",
        f"- Seeds processed: {total}",
        f"- Anna local match hit: {count('anna_search_hit')}/{total} ({pct(count('anna_search_hit'), total)})",
        f"- Title exact: {count('anna_title_exact')}/{total} ({pct(count('anna_title_exact'), total)})",
        f"- Author exact: {count('anna_author_exact')}/{total} ({pct(count('anna_author_exact'), total)})",
        f"- Publisher present: {count('anna_has_publisher')}/{total} ({pct(count('anna_has_publisher'), total)})",
        f"- Year present: {count('anna_has_year')}/{total} ({pct(count('anna_has_year'), total)})",
        f"- Extension present: {count('anna_has_extension')}/{total} ({pct(count('anna_has_extension'), total)})",
        f"- Filesize present: {count('anna_has_filesize')}/{total} ({pct(count('anna_has_filesize'), total)})",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed-csv", required=True)
    parser.add_argument("--anna-records", required=True, help="Local .json, .jsonl, or directory of Anna records")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    seeds = read_seed_csv(Path(args.seed_csv))[: args.limit]
    records = list(iter_json_records(Path(args.anna_records)))
    rows = best_matches(seeds, records)
    (out_dir / "anna_local_metadata_probe.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False), encoding="utf-8")
    write_csv(rows, out_dir / "anna_local_metadata_probe.csv")
    write_report(rows, out_dir / "anna_local_metadata_probe_report.md")
    print(f"Seeds: {len(seeds)}")
    print(f"Records: {len(records)}")
    print(f"Report: {out_dir / 'anna_local_metadata_probe_report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
