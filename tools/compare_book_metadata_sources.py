#!/usr/bin/env python3
"""Compare Biblio metadata-source benchmark outputs."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any, Callable


def pct(n: int, d: int) -> str:
    return f"{(n / d * 100):.1f}%" if d else "0.0%"


def load(path: Path) -> list[dict[str, Any]]:
    return json.loads(path.read_text(encoding="utf-8"))


def metric(rows: list[dict[str, Any]], pred: Callable[[dict[str, Any]], bool]) -> int:
    return sum(1 for row in rows if pred(row))


def normalize_key(row: dict[str, Any]) -> tuple[str, str]:
    return (str(row.get("title") or "").casefold().strip(), str(row.get("author") or "").casefold().strip())


def source_rows(rows: list[dict[str, Any]]) -> dict[tuple[str, str], dict[str, Any]]:
    return {normalize_key(row): row for row in rows}


def write_report(libgen_rows: list[dict[str, Any]], ocean_rows: list[dict[str, Any]], out: Path) -> None:
    total = min(len(libgen_rows), len(ocean_rows))
    lib = source_rows(libgen_rows)
    ocean = source_rows(ocean_rows)
    common_keys = sorted(set(lib) & set(ocean))

    metrics = [
        ("Search hit", lambda r: bool(r.get("libgen_search_count")), lambda r: bool(r.get("ocean_search_hit"))),
        ("Exact title", lambda r: bool(r.get("libgen_detail_title_exact") or r.get("libgen_search_title_exact")), lambda r: bool(r.get("ocean_title_exact"))),
        ("Exact author", lambda r: bool(r.get("libgen_detail_author_exact") or r.get("libgen_search_author_exact")), lambda r: bool(r.get("ocean_author_exact"))),
        ("Series present", lambda r: bool(r.get("libgen_has_series")), lambda r: bool(r.get("ocean_has_series"))),
        ("Expected series correct", lambda r: bool(r.get("libgen_series_correct")), lambda r: bool(r.get("ocean_series_correct"))),
        ("Expected position correct", lambda r: bool(r.get("libgen_position_correct")), lambda r: bool(r.get("ocean_position_correct"))),
        ("ISBN present", lambda r: bool(r.get("libgen_has_isbn")), lambda r: bool(r.get("ocean_has_isbn"))),
        ("Publication year/date present", lambda r: bool(r.get("libgen_has_year")), lambda r: bool(r.get("ocean_has_year"))),
        ("Download/format candidate visible", lambda r: bool(r.get("libgen_selected_md5")), lambda r: bool(r.get("ocean_has_pdf") or r.get("ocean_has_epub"))),
    ]

    lines = [
        "# Biblio Metadata Source Comparison",
        "",
        "## Inputs",
        "",
        f"- LibGen rows: {len(libgen_rows)}",
        f"- OceanofPDF rows: {len(ocean_rows)}",
        f"- Common title+author keys: {len(common_keys)}",
        "",
        "## Headline",
        "",
        "OceanofPDF is much stronger for series/order assertions. LibGen is stronger for ISBN and direct file-hash/download assertions. Neither is safe as canonical truth by itself.",
        "",
        "## Metrics",
        "",
        "| Metric | LibGen | OceanofPDF | Winner |",
        "|---|---:|---:|---|",
    ]

    for label, lib_pred, ocean_pred in metrics:
        lib_count = metric(libgen_rows, lib_pred)
        ocean_count = metric(ocean_rows, ocean_pred)
        winner = "tie"
        if lib_count > ocean_count:
            winner = "LibGen"
        elif ocean_count > lib_count:
            winner = "OceanofPDF"
        lines.append(
            f"| {label} | {lib_count}/{len(libgen_rows)} ({pct(lib_count, len(libgen_rows))}) | "
            f"{ocean_count}/{len(ocean_rows)} ({pct(ocean_count, len(ocean_rows))}) | {winner} |"
        )

    both_series = sum(1 for key in common_keys if lib[key].get("libgen_series_correct") and ocean[key].get("ocean_series_correct"))
    ocean_only_series = sum(1 for key in common_keys if not lib[key].get("libgen_series_correct") and ocean[key].get("ocean_series_correct"))
    lib_only_series = sum(1 for key in common_keys if lib[key].get("libgen_series_correct") and not ocean[key].get("ocean_series_correct"))
    neither_series = sum(1 for key in common_keys if not lib[key].get("libgen_series_correct") and not ocean[key].get("ocean_series_correct"))

    lines += [
        "",
        "## Agreement Matrix: Expected Series",
        "",
        f"- Both correct: {both_series}/{len(common_keys)} ({pct(both_series, len(common_keys))})",
        f"- Ocean only correct: {ocean_only_series}/{len(common_keys)} ({pct(ocean_only_series, len(common_keys))})",
        f"- LibGen only correct: {lib_only_series}/{len(common_keys)} ({pct(lib_only_series, len(common_keys))})",
        f"- Neither correct: {neither_series}/{len(common_keys)} ({pct(neither_series, len(common_keys))})",
        "",
        "## Suggested Source Weights",
        "",
        "| Claim Type | Goodreads seed | OceanofPDF | LibGen |",
        "|---|---:|---:|---:|",
        "| canonical work title/author seed | 0.90 | 0.45 | 0.45 |",
        "| series membership | 0.85 | 0.78 | 0.45 |",
        "| series ordinal | 0.85 | 0.80 | 0.48 |",
        "| ISBN/edition identity | 0.65 | 0.35 | 0.78 |",
        "| downloadable edition candidate | 0.00 | 0.40 | 0.85 |",
        "",
        "## Sample Source Disagreements",
        "",
    ]

    shown = 0
    for key in common_keys:
        lrow = lib[key]
        orow = ocean[key]
        if bool(lrow.get("libgen_series_correct")) == bool(orow.get("ocean_series_correct")):
            continue
        lines.append(
            "- "
            f"{lrow.get('title')} / {lrow.get('author')}: "
            f"expected {lrow.get('expected_series')} #{lrow.get('expected_position')}; "
            f"LibGen='{lrow.get('libgen_series_raw')}', "
            f"Ocean='{orow.get('ocean_series_raw')}'"
        )
        shown += 1
        if shown >= 30:
            break

    ocean_errors = Counter(str(r.get("ocean_error") or "")[:120] for r in ocean_rows if r.get("ocean_error"))
    libgen_errors = Counter(str(r.get("libgen_error") or "")[:120] for r in libgen_rows if r.get("libgen_error"))
    lines += [
        "",
        "## Error Buckets",
        "",
        f"- LibGen errors: {sum(libgen_errors.values())}",
        f"- OceanofPDF errors: {sum(ocean_errors.values())}",
    ]
    for message, count in ocean_errors.most_common(10):
        lines.append(f"- OceanofPDF: {count} x `{message}`")

    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--libgen-json", required=True)
    parser.add_argument("--ocean-json", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    write_report(load(Path(args.libgen_json)), load(Path(args.ocean_json)), Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
