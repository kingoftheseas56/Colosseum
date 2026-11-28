"""Fit upstream SAR once per requested Vinyl item level and export its graph."""

import argparse
from collections import defaultdict
from contextlib import closing
import csv
from datetime import datetime, timezone
import hashlib
from heapq import nsmallest
from importlib.metadata import version
import json
import math
from pathlib import Path
import sqlite3
from tempfile import TemporaryDirectory

import pandas as pd
from recommenders.models.sar.sar_singlenode import SARSingleNode

from score import LEVELS

BATCH_ROWS = 32_768
SCHEMA = """
CREATE TABLE items (
    level TEXT NOT NULL, item TEXT NOT NULL, support INTEGER NOT NULL,
    PRIMARY KEY (level, item)
) WITHOUT ROWID;
-- The primary-key index also serves all (level, item) lookups.
CREATE TABLE neighbours (
    level TEXT NOT NULL, item TEXT NOT NULL, neighbour TEXT NOT NULL,
    similarity REAL NOT NULL, support INTEGER NOT NULL,
    PRIMARY KEY (level, item, neighbour)
) WITHOUT ROWID;
"""


class ExportSAR(SARSingleNode):
    """Keep the co-occurrence counts from the upstream fit for export only."""

    def compute_cooccurrence_matrix(self, df):
        counts = super().compute_cooccurrence_matrix(df)
        self.cooccurrence = counts.tocsr()
        return counts


def input_rows(path, levels):
    def columns(names):
        required = ["user", "item", "timestamp"]
        if not names or len(set(names)) != len(names) or not set(required) <= set(names):
            raise ValueError(f"{path}: expected unique columns user, item, timestamp")
        if "level" in names:
            return required + ["level"]
        if len(levels) != 1:
            raise ValueError(f"{path}: assigning one input to multiple levels requires a level column")
        return required

    if path.suffix.lower() == ".csv":
        with path.open(encoding="utf-8-sig", newline="") as f:
            reader = csv.DictReader(f)
            names = columns(reader.fieldnames)
            for row in reader:
                if None in row or any(value is None for value in row.values()):
                    raise ValueError(f"{path}: wrong column count on CSV line {reader.line_num}")
                yield [row[name] for name in names]
    elif path.suffix.lower() == ".parquet":
        from pyarrow.parquet import ParquetFile

        with ParquetFile(path) as table:
            names = columns(table.schema_arrow.names)
            for batch in table.iter_batches(batch_size=BATCH_ROWS, columns=names):
                for row in batch.to_pylist():
                    yield [row[name] for name in names]
    else:
        raise ValueError(f"{path}: expected a .csv or .parquet file")


def epoch_seconds(value):
    if type(value) not in (int, str):
        raise ValueError("expected integer Unix seconds")
    seconds = int(value)
    if not 0 <= seconds < 2**63:
        raise ValueError("Unix seconds out of range")
    return seconds


def events(path, levels, timedecay_formula):
    for line, row in enumerate(input_rows(path, levels), 1):
        user, item, timestamp, *tag = row
        level = tag[0] if tag else next(iter(levels))
        if level not in LEVELS:
            raise ValueError(f"{path}: invalid level at data row {line}")
        if level not in levels:
            continue
        if not all(isinstance(key, str) and key.strip() for key in (user, item)):
            raise ValueError(f"{path}: expected nonempty text user and item at data row {line}")
        try:
            timestamp = epoch_seconds(timestamp)
        except ValueError:
            raise ValueError(f"{path}: invalid timestamp at data row {line}") from None
        # Without decay only one row per user/item is needed, regardless of play time.
        yield level, user, item, timestamp if timedecay_formula else 0


def stage_inputs(stage, inputs, timedecay_formula):
    stage.executescript("""
        PRAGMA temp_store = FILE;
        PRAGMA cache_size = -16384;
        CREATE TABLE events (
            level TEXT, user TEXT, item TEXT, timestamp INTEGER, plays INTEGER,
            PRIMARY KEY (level, user, item, timestamp)
        ) WITHOUT ROWID;
    """)
    paths = defaultdict(set)
    for level, path in inputs.items():
        paths[path.resolve()].add(level)
    with stage:
        for path, levels in paths.items():
            stage.executemany("""
                INSERT INTO events VALUES (?, ?, ?, ?, 1)
                ON CONFLICT DO UPDATE SET plays = plays + 1
            """, events(path, levels, timedecay_formula))


def training_frame(stage, level, args):
    stage.executescript("""
        DROP TABLE IF EXISTS eligible_users;
        DROP TABLE IF EXISTS selected_items;
        CREATE TEMP TABLE eligible_users (user TEXT PRIMARY KEY) WITHOUT ROWID;
        CREATE TEMP TABLE selected_items (item TEXT PRIMARY KEY, support INTEGER) WITHOUT ROWID;
    """)
    stage.execute("""
        INSERT INTO eligible_users
        SELECT user FROM events WHERE level = ? GROUP BY user HAVING SUM(plays) >= ?
    """, (level, args.min_user_listens))
    stage.execute("""
        INSERT INTO selected_items
        SELECT e.item, COUNT(DISTINCT e.user) AS support
        FROM events AS e JOIN eligible_users USING (user) WHERE level = ?
        GROUP BY e.item ORDER BY support DESC, e.item LIMIT ?
    """, (level, args.top_items))
    # LIMIT is a memory guard, not sampling: fail rather than fit a partial population.
    frame = pd.read_sql_query("""
        SELECT user, item, timestamp, plays AS rating
        FROM events JOIN eligible_users USING (user) JOIN selected_items USING (item)
        WHERE level = ? ORDER BY user, item, timestamp LIMIT ?
    """, stage, params=(level, args.max_fit_rows + 1), dtype={"rating": "float64"})
    if len(frame) > args.max_fit_rows:
        raise ValueError(f"{level}: --max-fit-rows exceeded; narrow the input or increase the cap")
    if frame.empty:
        raise ValueError(f"{level}: no interactions after filtering; omit this level or adjust the input")
    return frame


def train_level(stage, output, level, args):
    frame = training_frame(stage, level, args)
    model = ExportSAR(
        col_user="user", col_item="item", col_timestamp="timestamp", col_rating="rating",
        similarity_type="jaccard", threshold=args.threshold, time_now=args.time_now,
        timedecay_formula=args.timedecay_formula, time_decay_coefficient=args.time_decay_coefficient,
    )
    model.fit(frame)
    del frame

    items = [(level, item, support) for item, support in
             stage.execute("SELECT item, support FROM selected_items ORDER BY item")]
    output.executemany("INSERT INTO items VALUES (?, ?, ?)", items)
    edges = 0
    for _, item, _ in items:
        i = model.item2index[item]
        counts = model.cooccurrence.getrow(i)
        candidates = (
            (level, item, model.index2item[j], float(model.item_similarity[i, j]), int(support))
            for j, support in zip(counts.indices, counts.data) if j != i and support > 0
        )
        neighbours = nsmallest(
            args.top_k, (row for row in candidates if math.isfinite(row[3]) and row[3] > 0),
            key=lambda row: (-row[3], row[2]),
        )
        output.executemany("INSERT INTO neighbours VALUES (?, ?, ?, ?, ?)", neighbours)
        edges += len(neighbours)
    return {"items": len(items), "neighbours": edges}


def train(inputs, output_dir, args):
    if output_dir.exists():
        raise ValueError(f"output directory already exists: {output_dir}")
    levels = [level for level in LEVELS if level in inputs]
    with TemporaryDirectory(prefix="vinyl-sar-", dir=output_dir.parent) as work:
        work = Path(work)
        artifacts = work / "artifacts"
        artifacts.mkdir()
        database = artifacts / "neighbours.sqlite"
        with closing(sqlite3.connect(work / "interactions.sqlite")) as stage:
            stage_inputs(stage, inputs, args.timedecay_formula)
            with closing(sqlite3.connect(database)) as output, output:
                output.executescript(SCHEMA)
                by_level = {level: train_level(stage, output, level, args) for level in levels}
        with database.open("rb") as f:
            digest = hashlib.file_digest(f, "sha256").hexdigest()
        manifest = {
            "model_version": f"vinyl-sar-v1/recommenders-{version('recommenders')}",
            "levels": levels,
            "k": args.top_k,
            "threshold": args.threshold,
            "timedecay_formula": args.timedecay_formula,
            "time_decay_coefficient": args.time_decay_coefficient,
            "time_now": args.time_now,
            "min_user_listens": args.min_user_listens,
            "top_items": args.top_items,
            "input_description": args.input_description,
            "dump_timestamp": args.dump_timestamp,
            "mapping_revision": args.mapping_revision,
            "created_at": datetime.now(timezone.utc).isoformat(),
            "row_counts": {table: sum(counts[table] for counts in by_level.values())
                           for table in ("items", "neighbours")},
            "row_counts_by_level": by_level,
            "sqlite_bytes": database.stat().st_size,
            "sqlite_sha256": digest,
            "licence": "derived from ListenBrainz listens, CC0",
        }
        (artifacts / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        artifacts.rename(output_dir)


def positive_int(value):
    number = int(value)
    if not 0 < number < 2**63 - 1:
        raise argparse.ArgumentTypeError("expected a positive integer below 2^63 - 1")
    return number


def positive_float(value):
    number = float(value)
    if not math.isfinite(number * 86400) or number <= 0:
        raise argparse.ArgumentTypeError("expected a positive finite number of days")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", metavar="LEVEL=PATH", action="append", required=True,
                        help=f"CSV or parquet; repeat once per level ({', '.join(LEVELS)})")
    parser.add_argument("--output-dir", type=Path, required=True, help="new directory (parent must exist)")
    parser.add_argument("--input-description", required=True, help="public input/slice label, no user identifiers")
    parser.add_argument("--dump-timestamp", help="source dump timestamp for provenance")
    parser.add_argument("--mapping-revision", help="source item-ID mapping revision for provenance")
    parser.add_argument("--top-k", type=positive_int, default=100, help="neighbours per item (default: %(default)s)")
    parser.add_argument("--min-user-listens", type=positive_int, default=1,
                        help="minimum listens per user per level (default: %(default)s)")
    parser.add_argument("--top-items", type=positive_int, default=3000,
                        help="maximum items per level by distinct listeners (default: %(default)s)")
    parser.add_argument("--max-fit-rows", type=positive_int, default=1_000_000,
                        help="fail if a level would fit more rows than this (default: %(default)s)")
    parser.add_argument("--threshold", type=positive_int, default=1,
                        help="SAR minimum co-occurrence (default: %(default)s)")
    parser.add_argument("--timedecay-formula", action="store_true", help="enable SAR's affinity time decay (default: off)")
    parser.add_argument("--time-decay-coefficient", type=positive_float, default=30,
                        help="SAR half-life in days (default: %(default)s)")
    parser.add_argument("--time-now", type=epoch_seconds,
                        help="SAR decay reference in Unix seconds (default: latest retained timestamp in each fit)")
    args = parser.parse_args()
    inputs = {}
    for assignment in args.input:
        level, _, path = assignment.partition("=")
        if level not in LEVELS or not path or level in inputs:
            parser.error("--input requires LEVEL=PATH, once per selected level")
        inputs[level] = Path(path)
    try:
        train(inputs, args.output_dir, args)
    except (OSError, ValueError, csv.Error, sqlite3.Error) as exc:
        parser.exit(1, f"train: {exc}\n")


if __name__ == "__main__":
    main()
