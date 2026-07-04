#!/usr/bin/env python3
"""
biblio_wikidata_discovered_seed_builder.py

Generate a LARGE, non-handpicked, series-heavy seed CSV from Wikidata.

This is seed generation only.
It does NOT call LibGen.
It does NOT download books.
It does NOT decide Biblio canon.
It discovers candidate book/literary series from Wikidata dynamically, then expands
those series into title/author/expected_series/expected_position rows.

Why this exists:
  You need 500-2000+ series-heavy title/author rows for a LibGen metadata pass.
  Hand-picked franchises are too small and biased.
  Public-domain books are the wrong test.
  This script asks Wikidata: "show me many book/literary series with multiple works",
  then expands those series into seed rows.

Output CSV columns:
  title,author,expected_series,expected_position,franchise,source,work_qid,series_qid

Typical usage:

  python biblio_wikidata_discovered_seed_builder.py ^
    --out C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\tools\\wikidata_discovered_series_seed.csv ^
    --target-rows 1000 ^
    --discover-series 250 ^
    --min-members 3 ^
    --max-members-per-series 30 ^
    --include-qids

Then run:

  python libgen_large_pass_no_openlibrary.py ^
    --input-seeds C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\tools\\wikidata_discovered_series_seed.csv ^
    --limit 1000 ^
    --out-dir C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\tools\\_libgen_discovered_series_pass ^
    --replace

Notes:
  - Wikidata SPARQL can timeout. This script retries and writes partial progress.
  - Discovery is broad and imperfect by design. The point is to stress LibGen.
  - If a discovered series is weird, that is useful signal, not a failure.
"""

from __future__ import annotations

import argparse
import csv
import html
import json
import random
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


SPARQL_ENDPOINT = "https://query.wikidata.org/sparql"
USER_AGENT = "BiblioWikidataDiscoveredSeedBuilder/0.2 (local desktop metadata research)"

# These are not hand-picked seed franchises. They are type filters for discovery.
# Q277759 = book series
# Q571 = book
# Q7725634 = literary work
# Q47461344 = written work
DEFAULT_SERIES_TYPES = ["Q277759"]
DEFAULT_WORK_TYPES = ["Q571", "Q7725634", "Q47461344"]

BAD_TITLE_TERMS = [
    "list of",
    "category:",
    "template:",
    "wikipedia:",
    "wikimedia",
]

BAD_SERIES_TERMS = [
    "list of",
    "category:",
    "template:",
    "wikipedia:",
    "wikimedia",
    "episode",
    "season",
    "film series",
    "television",
]


@dataclass(frozen=True)
class SeriesCandidate:
    series_qid: str
    series_label: str
    member_count: int


@dataclass(frozen=True)
class Seed:
    title: str
    author: str
    expected_series: str
    expected_position: str = ""
    franchise: str = ""
    source: str = "wikidata_discovered_series"
    work_qid: str = ""
    series_qid: str = ""


def normalize(text: str) -> str:
    text = html.unescape(text or "").lower()
    text = text.replace("&", " and ")
    text = re.sub(r"[^a-z0-9]+", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def normalize_title(text: str) -> str:
    text = normalize(text)
    text = re.sub(r"\b(book|volume|vol|novel|trilogy|edition|series)\b", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def normalize_author(text: str) -> str:
    text = html.unescape(text or "")
    text = re.sub(r"\[[^\]]+\]", "", text)
    if "," in text:
        parts = [p.strip() for p in text.split(",", 1)]
        if len(parts) == 2 and parts[0] and parts[1]:
            text = f"{parts[1]} {parts[0]}"
    return re.sub(r"\s+", " ", text).strip()


def qid_from_uri(uri: str) -> str:
    return uri.rstrip("/").split("/")[-1]


def is_bad_label(label: str, bad_terms: List[str]) -> bool:
    n = normalize(label)
    if not n:
        return True
    if re.fullmatch(r"q\d+", n):
        return True
    return any(term in n for term in bad_terms)


def seed_key(seed: Seed) -> Tuple[str, str]:
    return normalize_title(seed.title), normalize(normalize_author(seed.author))


def fetch_json(url: str, timeout: int, retries: int, retry_sleep: float, rate_limit_sleep: float) -> Dict[str, Any]:
    last: Optional[Exception] = None
    for attempt in range(retries + 1):
        try:
            req = urllib.request.Request(
                url,
                headers={
                    "User-Agent": USER_AGENT,
                    "Accept": "application/sparql-results+json",
                },
            )
            with urllib.request.urlopen(req, timeout=timeout) as response:
                return json.loads(response.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as exc:
            last = exc
            if attempt < retries:
                sleep_for = rate_limit_sleep if exc.code == 429 else retry_sleep * (attempt + 1)
                print(f"  [retry {attempt + 1}] HTTP {exc.code}: {exc.reason}; sleeping {sleep_for:.1f}s")
                time.sleep(sleep_for)
        except Exception as exc:
            last = exc
            if attempt < retries:
                sleep_for = retry_sleep * (attempt + 1)
                print(f"  [retry {attempt + 1}] {exc}; sleeping {sleep_for:.1f}s")
                time.sleep(sleep_for)
    raise RuntimeError(str(last))


def sparql(query: str, timeout: int, retries: int, retry_sleep: float, rate_limit_sleep: float) -> Dict[str, Any]:
    params = urllib.parse.urlencode({"query": query, "format": "json"})
    return fetch_json(
        f"{SPARQL_ENDPOINT}?{params}",
        timeout=timeout,
        retries=retries,
        retry_sleep=retry_sleep,
        rate_limit_sleep=rate_limit_sleep,
    )


def values_block(var_name: str, qids: List[str]) -> str:
    values = " ".join(f"wd:{qid}" for qid in qids)
    return f"VALUES ?{var_name} {{ {values} }}"


def discover_series_by_type(
    series_types: List[str],
    work_types: List[str],
    min_members: int,
    max_members: int,
    limit: int,
    timeout: int,
    retries: int,
    retry_sleep: float,
    rate_limit_sleep: float,
) -> List[SeriesCandidate]:
    """
    Discovery strategy:
      Find items that are instance/subclass of book series, and count works that use P179.
      This intentionally discovers broad candidates instead of using a hand-picked list.
    """
    query = f"""
SELECT ?series ?seriesLabel (COUNT(?work) AS ?memberCount) WHERE {{
  {values_block("seriesType", series_types)}
  {values_block("workType", work_types)}
  ?series wdt:P31/wdt:P279* ?seriesType .
  ?work wdt:P179 ?series .
  ?work wdt:P31/wdt:P279* ?workType .
  ?work wdt:P50 ?author .
  SERVICE wikibase:label {{ bd:serviceParam wikibase:language "en". }}
}}
GROUP BY ?series ?seriesLabel
HAVING(COUNT(?work) >= {min_members} && COUNT(?work) <= {max_members})
ORDER BY DESC(?memberCount)
LIMIT {limit}
"""
    data = sparql(query, timeout=timeout, retries=retries, retry_sleep=retry_sleep, rate_limit_sleep=rate_limit_sleep)
    bindings = data.get("results", {}).get("bindings", [])
    out: List[SeriesCandidate] = []

    for row in bindings:
        label = row.get("seriesLabel", {}).get("value", "").strip()
        if is_bad_label(label, BAD_SERIES_TERMS):
            continue
        qid = qid_from_uri(row.get("series", {}).get("value", ""))
        count_raw = row.get("memberCount", {}).get("value", "0")
        try:
            count = int(count_raw)
        except ValueError:
            count = 0
        if qid and count >= min_members:
            out.append(SeriesCandidate(qid, label, count))

    return out


def discover_series_by_usage(
    work_types: List[str],
    min_members: int,
    max_members: int,
    limit: int,
    timeout: int,
    retries: int,
    retry_sleep: float,
    rate_limit_sleep: float,
) -> List[SeriesCandidate]:
    """
    Fallback discovery strategy:
      Just find P179 targets with many members. This can include non-book media,
      so labels are filtered and later member expansion asks for authored works.
    """
    query = f"""
SELECT ?series ?seriesLabel (COUNT(?work) AS ?memberCount) WHERE {{
  {values_block("workType", work_types)}
  ?work wdt:P179 ?series .
  ?work wdt:P31/wdt:P279* ?workType .
  ?work wdt:P50 ?author .
  SERVICE wikibase:label {{ bd:serviceParam wikibase:language "en". }}
}}
GROUP BY ?series ?seriesLabel
HAVING(COUNT(?work) >= {min_members} && COUNT(?work) <= {max_members})
ORDER BY DESC(?memberCount)
LIMIT {limit}
"""
    data = sparql(query, timeout=timeout, retries=retries, retry_sleep=retry_sleep, rate_limit_sleep=rate_limit_sleep)
    bindings = data.get("results", {}).get("bindings", [])
    out: List[SeriesCandidate] = []

    for row in bindings:
        label = row.get("seriesLabel", {}).get("value", "").strip()
        if is_bad_label(label, BAD_SERIES_TERMS):
            continue
        qid = qid_from_uri(row.get("series", {}).get("value", ""))
        count_raw = row.get("memberCount", {}).get("value", "0")
        try:
            count = int(count_raw)
        except ValueError:
            count = 0
        if qid and count >= min_members:
            out.append(SeriesCandidate(qid, label, count))

    return out


def expand_series(
    candidate: SeriesCandidate,
    work_types: List[str],
    max_members: int,
    require_author: bool,
    timeout: int,
    retries: int,
    retry_sleep: float,
    rate_limit_sleep: float,
) -> List[Seed]:
    """
    Expand one series into work rows.

    We use p:P179 so we can capture P1545 series ordinal when present.
    """
    author_filter = "FILTER(BOUND(?authorLabel))" if require_author else ""

    query = f"""
SELECT ?work ?workLabel ?authorLabel ?ordinal WHERE {{
  {values_block("workType", work_types)}
  ?work p:P179 ?seriesStatement .
  ?seriesStatement ps:P179 wd:{candidate.series_qid} .
  ?work wdt:P31/wdt:P279* ?workType .
  OPTIONAL {{ ?seriesStatement pq:P1545 ?ordinal. }}
  OPTIONAL {{ ?work wdt:P50 ?author. }}
  SERVICE wikibase:label {{ bd:serviceParam wikibase:language "en". }}
  {author_filter}
}}
ORDER BY ?ordinal ?workLabel
LIMIT {max_members}
"""
    data = sparql(query, timeout=timeout, retries=retries, retry_sleep=retry_sleep, rate_limit_sleep=rate_limit_sleep)
    bindings = data.get("results", {}).get("bindings", [])
    out: List[Seed] = []

    for row in bindings:
        title = row.get("workLabel", {}).get("value", "").strip()
        author = row.get("authorLabel", {}).get("value", "").strip()
        ordinal = row.get("ordinal", {}).get("value", "").strip()
        work_qid = qid_from_uri(row.get("work", {}).get("value", ""))

        if is_bad_label(title, BAD_TITLE_TERMS):
            continue
        if author and re.fullmatch(r"Q\d+", author):
            author = ""
        if require_author and not author:
            continue

        out.append(
            Seed(
                title=title,
                author=normalize_author(author),
                expected_series=candidate.series_label,
                expected_position=ordinal,
                franchise=candidate.series_label,
                source="wikidata_discovered_series",
                work_qid=work_qid,
                series_qid=candidate.series_qid,
            )
        )

    return out


def expand_series_batch(
    candidates: List[SeriesCandidate],
    work_types: List[str],
    max_members_per_series: int,
    require_author: bool,
    timeout: int,
    retries: int,
    retry_sleep: float,
    rate_limit_sleep: float,
) -> Dict[str, List[Seed]]:
    """
    Expand many series in one SPARQL call. This is the main defense against WDQS
    429 throttling: one chunk request replaces dozens of per-series requests.
    """
    if not candidates:
        return {}

    label_by_qid = {c.series_qid: c.series_label for c in candidates}
    author_filter = "FILTER(BOUND(?authorLabel))" if require_author else ""
    query = f"""
SELECT ?series ?work ?workLabel ?authorLabel ?ordinal WHERE {{
  {values_block("series", [c.series_qid for c in candidates])}
  {values_block("workType", work_types)}
  ?work p:P179 ?seriesStatement .
  ?seriesStatement ps:P179 ?series .
  ?work wdt:P31/wdt:P279* ?workType .
  OPTIONAL {{ ?seriesStatement pq:P1545 ?ordinal. }}
  OPTIONAL {{ ?work wdt:P50 ?author. }}
  SERVICE wikibase:label {{ bd:serviceParam wikibase:language "en". }}
  {author_filter}
}}
ORDER BY ?series ?ordinal ?workLabel
"""
    data = sparql(query, timeout=timeout, retries=retries, retry_sleep=retry_sleep, rate_limit_sleep=rate_limit_sleep)
    bindings = data.get("results", {}).get("bindings", [])
    out: Dict[str, List[Seed]] = {c.series_qid: [] for c in candidates}

    for row in bindings:
        series_qid = qid_from_uri(row.get("series", {}).get("value", ""))
        if series_qid not in out or len(out[series_qid]) >= max_members_per_series:
            continue

        title = row.get("workLabel", {}).get("value", "").strip()
        author = row.get("authorLabel", {}).get("value", "").strip()
        ordinal = row.get("ordinal", {}).get("value", "").strip()
        work_qid = qid_from_uri(row.get("work", {}).get("value", ""))

        if is_bad_label(title, BAD_TITLE_TERMS):
            continue
        if author and re.fullmatch(r"Q\d+", author):
            author = ""
        if require_author and not author:
            continue

        series_label = label_by_qid.get(series_qid, series_qid)
        out[series_qid].append(
            Seed(
                title=title,
                author=normalize_author(author),
                expected_series=series_label,
                expected_position=ordinal,
                franchise=series_label,
                source="wikidata_discovered_series",
                work_qid=work_qid,
                series_qid=series_qid,
            )
        )

    return out


def dedupe_candidates(candidates: Iterable[SeriesCandidate]) -> List[SeriesCandidate]:
    seen = set()
    out: List[SeriesCandidate] = []
    for c in candidates:
        if c.series_qid in seen:
            continue
        seen.add(c.series_qid)
        out.append(c)
    return out


def dedupe_seeds(seeds: Iterable[Seed]) -> List[Seed]:
    seen = set()
    out: List[Seed] = []
    for seed in seeds:
        key = seed_key(seed)
        if not key[0] or key in seen:
            continue
        seen.add(key)
        out.append(seed)
    return out


def write_series_candidates(path: Path, candidates: List[SeriesCandidate]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["series_qid", "series_label", "member_count"])
        writer.writeheader()
        for c in candidates:
            writer.writerow(
                {
                    "series_qid": c.series_qid,
                    "series_label": c.series_label,
                    "member_count": c.member_count,
                }
            )


def write_seed_csv(path: Path, seeds: List[Seed], include_qids: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["title", "author", "expected_series", "expected_position", "franchise", "source"]
    if include_qids:
        fields.extend(["work_qid", "series_qid"])

    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for seed in seeds:
            row = asdict(seed)
            if not include_qids:
                row.pop("work_qid", None)
                row.pop("series_qid", None)
            writer.writerow(row)


def summarize(seeds: List[Seed], candidates: List[SeriesCandidate]) -> None:
    by_series: Dict[str, int] = {}
    missing_author = 0
    missing_ordinal = 0

    for seed in seeds:
        by_series[seed.expected_series] = by_series.get(seed.expected_series, 0) + 1
        if not seed.author:
            missing_author += 1
        if not seed.expected_position:
            missing_ordinal += 1

    print("")
    print(f"Discovered series candidates: {len(candidates)}")
    print(f"Final seed rows: {len(seeds)}")
    print(f"Rows missing author: {missing_author}")
    print(f"Rows missing ordinal: {missing_ordinal}")
    print("")
    print("Top expanded series:")
    for series, count in sorted(by_series.items(), key=lambda x: (-x[1], x[0]))[:30]:
        print(f"  {series[:45]:45s} {count}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Discover large Wikidata series seed set, not hand-picked.")
    parser.add_argument("--out", required=True, help="Output seed CSV path.")
    parser.add_argument("--candidate-out", help="Optional output CSV of discovered series candidates.")
    parser.add_argument("--target-rows", type=int, default=1000, help="Target seed rows.")
    parser.add_argument("--discover-series", type=int, default=300, help="How many series candidates to discover.")
    parser.add_argument("--min-members", type=int, default=3, help="Minimum P179 members for discovered series.")
    parser.add_argument("--max-candidate-members", type=int, default=80, help="Maximum authored book-like members for discovered series. Filters huge non-book/media sets.")
    parser.add_argument("--max-members-per-series", type=int, default=30, help="Max rows per expanded series.")
    parser.add_argument("--series-types", default=",".join(DEFAULT_SERIES_TYPES), help="Comma-separated Wikidata QIDs used as series type filters.")
    parser.add_argument("--work-types", default=",".join(DEFAULT_WORK_TYPES), help="Comma-separated Wikidata QIDs used as member work type filters.")
    parser.add_argument("--usage-fallback", action="store_true", help="Also discover by broad P179 usage if type-based discovery is sparse.")
    parser.add_argument("--require-author", action="store_true", help="Only keep rows with author labels.")
    parser.add_argument("--include-qids", action="store_true")
    parser.add_argument("--shuffle-series", action="store_true", help="Shuffle discovered series before expansion.")
    parser.add_argument("--batch-expand", action="store_true", default=True, help="Expand series in chunked SPARQL batches instead of one request per series.")
    parser.add_argument("--no-batch-expand", action="store_false", dest="batch_expand")
    parser.add_argument("--batch-size", type=int, default=35, help="Series count per batch expansion request.")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--retries", type=int, default=4)
    parser.add_argument("--retry-sleep", type=float, default=2.5)
    parser.add_argument("--rate-limit-sleep", type=float, default=65.0, help="Sleep after WDQS HTTP 429.")
    parser.add_argument("--sleep", type=float, default=1.0)
    args = parser.parse_args()

    series_types = [s.strip() for s in args.series_types.split(",") if s.strip()]
    work_types = [s.strip() for s in args.work_types.split(",") if s.strip()]

    print("Discovering series from Wikidata. No hand-picked seed list is used.")
    print(f"Series type filters: {series_types}")
    print(f"Work type filters: {work_types}")
    print(f"Candidate member window: {args.min_members}..{args.max_candidate_members}")
    print(f"Expansion mode: {'batch' if args.batch_expand else 'per-series'}")
    print("")

    candidates: List[SeriesCandidate] = []

    try:
        candidates.extend(
            discover_series_by_type(
                series_types=series_types,
                work_types=work_types,
                min_members=args.min_members,
                max_members=args.max_candidate_members,
                limit=args.discover_series,
                timeout=args.timeout,
                retries=args.retries,
                retry_sleep=args.retry_sleep,
                rate_limit_sleep=args.rate_limit_sleep,
            )
        )
    except Exception as exc:
        print(f"[warn] type-based discovery failed: {exc}")

    if args.usage_fallback or len(candidates) < max(25, args.discover_series // 5):
        try:
            print("Running broad usage fallback discovery...")
            candidates.extend(
                discover_series_by_usage(
                    work_types=work_types,
                    min_members=args.min_members,
                    max_members=args.max_candidate_members,
                    limit=args.discover_series,
                    timeout=args.timeout,
                    retries=args.retries,
                    retry_sleep=args.retry_sleep,
                    rate_limit_sleep=args.rate_limit_sleep,
                )
            )
        except Exception as exc:
            print(f"[warn] usage fallback discovery failed: {exc}")

    candidates = dedupe_candidates(candidates)

    if args.shuffle_series:
        random.shuffle(candidates)

    if args.candidate_out:
        write_series_candidates(Path(args.candidate_out), candidates)

    print(f"Candidate series discovered: {len(candidates)}")
    print("Expanding series...")
    print("")

    seeds: List[Seed] = []
    if args.batch_expand:
        for start in range(0, len(candidates), args.batch_size):
            if len(seeds) >= args.target_rows:
                break
            chunk = candidates[start:start + args.batch_size]
            print(f"[batch {start // args.batch_size + 1}] expanding {len(chunk)} series")
            try:
                by_series = expand_series_batch(
                    candidates=chunk,
                    work_types=work_types,
                    max_members_per_series=args.max_members_per_series,
                    require_author=args.require_author,
                    timeout=args.timeout,
                    retries=args.retries,
                    retry_sleep=args.retry_sleep,
                    rate_limit_sleep=args.rate_limit_sleep,
                )
                for candidate in chunk:
                    rows = by_series.get(candidate.series_qid, [])
                    if rows:
                        print(f"  + {candidate.series_label} ({candidate.series_qid}) -> {len(rows)}")
                    seeds.extend(rows)
                seeds = dedupe_seeds(seeds)
                print(f"  total seeds so far: {len(seeds)}")
            except Exception as exc:
                print(f"  [warn] batch expansion failed: {exc}")
            time.sleep(args.sleep)
    else:
        for idx, candidate in enumerate(candidates, 1):
            if len(seeds) >= args.target_rows:
                break
            print(f"[{idx}/{len(candidates)}] {candidate.series_label} ({candidate.series_qid}, {candidate.member_count} members)")
            try:
                rows = expand_series(
                    candidate=candidate,
                    work_types=work_types,
                    max_members=args.max_members_per_series,
                    require_author=args.require_author,
                    timeout=args.timeout,
                    retries=args.retries,
                    retry_sleep=args.retry_sleep,
                    rate_limit_sleep=args.rate_limit_sleep,
                )
                print(f"  + {len(rows)} seed rows")
                seeds.extend(rows)
                seeds = dedupe_seeds(seeds)
            except Exception as exc:
                print(f"  [warn] expansion failed: {exc}")
            time.sleep(args.sleep)

    seeds = dedupe_seeds(seeds)[: args.target_rows]

    out = Path(args.out)
    write_seed_csv(out, seeds, include_qids=args.include_qids)

    print(f"\nWrote seed CSV: {out}")
    summarize(seeds, candidates)

    print("\nNext command:")
    print(
        f'python "C:\\Users\\Suprabha\\Downloads\\libgen_large_pass_no_openlibrary.py" '
        f'--input-seeds "{out}" --limit {len(seeds)} '
        f'--out-dir "{out.parent / "_libgen_discovered_series_pass"}" --replace'
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
