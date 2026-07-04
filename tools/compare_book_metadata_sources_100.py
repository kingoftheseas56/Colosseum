#!/usr/bin/env python3
"""Compare LibGen, OceanofPDF, and Z-Library on the shared first 100 seeds."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Callable


def load(path: Path) -> list[dict[str, Any]]:
    return json.loads(path.read_text(encoding="utf-8"))


def pct(n: int, d: int) -> str:
    return f"{(n / d * 100):.1f}%" if d else "0.0%"


def count(rows: list[dict[str, Any]], pred: Callable[[dict[str, Any]], bool]) -> int:
    return sum(1 for row in rows if pred(row))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--libgen-json", required=True)
    parser.add_argument("--ocean-json", required=True)
    parser.add_argument("--zlib-json", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    libgen = load(Path(args.libgen_json))[:100]
    ocean = load(Path(args.ocean_json))[:100]
    zlib = load(Path(args.zlib_json))[:100]
    total = min(len(libgen), len(ocean), len(zlib))
    libgen = libgen[:total]
    ocean = ocean[:total]
    zlib = zlib[:total]

    metrics: list[tuple[str, tuple[str, int], tuple[str, int], tuple[str, int]]] = [
        (
            "Search hit",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_search_count")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_search_hit")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_search_hit")))),
        ),
        (
            "Exact title",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_detail_title_exact") or r.get("libgen_search_title_exact")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_title_exact")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_title_exact")))),
        ),
        (
            "Exact/nearby author",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_detail_author_exact") or r.get("libgen_search_author_exact")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_author_exact")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_author_nearby")))),
        ),
        (
            "Series present",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_has_series")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_has_series")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_has_series")))),
        ),
        (
            "Expected series correct",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_series_correct")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_series_correct")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_series_correct")))),
        ),
        (
            "Expected position correct",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_position_correct")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_position_correct")))),
            ("Z-Library", count(zlib, lambda r: bool(r.get("zlib_position_correct")))),
        ),
        (
            "ISBN present",
            ("LibGen", count(libgen, lambda r: bool(r.get("libgen_has_isbn")))),
            ("OceanofPDF", count(ocean, lambda r: bool(r.get("ocean_has_isbn")))),
            ("Z-Library", 0),
        ),
    ]

    lines = [
        "# Biblio Metadata Source Comparison: First 100 Seeds",
        "",
        "## Boundary",
        "",
        "Z-Library benchmark used visible public search-page metadata only. It did not log in, open detail pages, click download buttons, or fetch files.",
        "",
        "## Headline",
        "",
        "Z-Library is useful as a title/discovery hit source, but visible public search pages do not expose enough reliable series or ISBN metadata to improve the canonical graph. OceanofPDF remains the strongest tested source for series/order assertions; LibGen remains strongest for ISBN/download evidence.",
        "",
        "## Metrics",
        "",
        "| Metric | LibGen | OceanofPDF | Z-Library | Winner |",
        "|---|---:|---:|---:|---|",
    ]
    for label, lib, ocean_row, zrow in metrics:
        values = [lib, ocean_row, zrow]
        best = max(v for _, v in values)
        winners = [name for name, value in values if value == best]
        lines.append(
            f"| {label} | {lib[1]}/{total} ({pct(lib[1], total)}) | "
            f"{ocean_row[1]}/{total} ({pct(ocean_row[1], total)}) | "
            f"{zrow[1]}/{total} ({pct(zrow[1], total)}) | {', '.join(winners)} |"
        )

    lines += [
        "",
        "## Source Role Update",
        "",
        "- Keep Goodreads dump as canonical seed for work and series graph.",
        "- Keep OceanofPDF as high-weight series/order assertion source.",
        "- Keep LibGen as high-weight ISBN/download-candidate source.",
        "- Treat Z-Library public search as optional discovery/title corroboration only unless a cleaner metadata surface is found.",
        "- Anna's Archive should be tested from datasets/API-like exports rather than the public search UI, because the live mirrors are noisy, domain-rotating, and partially protected.",
    ]
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
