"""Synthetic upstream parity and artifact tests; no ListenBrainz input needed."""

from contextlib import closing
import csv
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
from types import SimpleNamespace

import pandas as pd
import pyarrow as pa
import pyarrow.parquet as pq
import pytest
from recommenders.models.sar.sar_singlenode import SARSingleNode

from score import score
from train import train

HERE = Path(__file__).resolve().parent
LEVEL = "recording"
NOW = 1_700_000_000
HALF_LIFE = 10  # days
TOP_N = 3
LISTENS = (
    ("private-test-user-1", "aaab"),
    ("private-test-user-2", "acd"),
    ("private-test-user-3", "ace"),
    ("private-test-user-4", "bcdf"),
    ("private-test-user-5", "ceg"),
    ("private-test-user-6", "adgh"),
)
USER = LISTENS[0][0]


def synthetic_listens():
    return pd.DataFrame([
        (user, "track-" + item, NOW - HALF_LIFE * 86400 if user == USER and i == 0 else NOW)
        for user, items in LISTENS for i, item in enumerate(items)
    ], columns=["user", "item", "timestamp"])


def upstream(data, decay, threshold=1):
    # Independently construct one rating per (user,item[,timestamp]) for SAR.
    keys = ["user", "item", "timestamp"] if decay else ["user", "item"]
    ratings = data.groupby(keys).size().reset_index(name="rating").astype({"rating": float})
    model = SARSingleNode(col_user="user", col_item="item", col_rating="rating",
                          col_timestamp="timestamp", similarity_type="jaccard", threshold=threshold,
                          timedecay_formula=decay, time_decay_coefficient=HALF_LIFE, time_now=NOW)
    model.fit(ratings)
    return model


def run_fixture(tmp_path, data, decay, threshold=1, k=7, max_rows=1000):
    path = tmp_path / "listens.csv"
    data.to_csv(path, index=False)
    output = tmp_path / "model"
    train({LEVEL: path}, output, SimpleNamespace(
        top_k=k, threshold=threshold, min_user_listens=1, top_items=8, max_fit_rows=max_rows,
        input_description="synthetic 6x8", dump_timestamp=None, mapping_revision=None,
        timedecay_formula=decay, time_decay_coefficient=HALF_LIFE, time_now=NOW,
    ))
    return output


def expected_edges(data, model, threshold, k):
    listeners = data.groupby("item")["user"].agg(set).to_dict()
    rows = {}
    for item, users in listeners.items():
        top = []
        for neighbour, other_users in listeners.items():
            support = len(users & other_users)
            if item != neighbour and support >= threshold:
                similarity = float(model.item_similarity[model.item2index[item], model.item2index[neighbour]])
                top.append((neighbour, similarity, support))
        for neighbour, similarity, support in sorted(top, key=lambda row: (-row[1], row[0]))[:k]:
            rows[item, neighbour] = (similarity, support)
    return rows


@pytest.mark.parametrize(("decay", "threshold", "k"), [(False, 1, 7), (True, 1, 7), (False, 2, 2)])
def test_exported_similarity_matches_upstream(tmp_path, decay, threshold, k):
    data = synthetic_listens()
    model = upstream(data, decay, threshold)
    output = run_fixture(tmp_path, data, decay, threshold, k)
    with closing(sqlite3.connect(output / "neighbours.sqlite")) as db:
        rows = list(db.execute("SELECT item, neighbour, similarity, support FROM neighbours WHERE level = ?", (LEVEL,)))
        items = dict(db.execute("SELECT item, support FROM items WHERE level = ?", (LEVEL,)))
    expected = expected_edges(data, model, threshold, k)
    assert {(item, neighbour) for item, neighbour, _, _ in rows} == expected.keys()
    for item, neighbour, similarity, support in rows:
        want_similarity, want_support = expected[item, neighbour]
        assert similarity == want_similarity, (item, neighbour)
        assert support == want_support
    assert items == data.groupby("item")["user"].nunique().to_dict()


def reference_scores(model):
    weights = model.user_affinity[model.user2index[USER], :].toarray()[0]
    affinity = {item: float(weights[i]) for item, i in model.item2index.items() if weights[i] > 0}
    recommendations = model.recommend_k_items(pd.DataFrame({"user": [USER]}), top_k=TOP_N, remove_seen=True)
    results = []
    for item, prediction in recommendations[["item", "prediction"]].itertuples(index=False, name=None):
        parts = []
        for seed, weight in sorted(affinity.items()):
            contribution = weight * model.item_similarity[model.item2index[seed], model.item2index[item]]
            if contribution > 0:
                parts.append({"seed": seed, "contribution": float(contribution)})
        results.append({"item": item, "score": prediction, "contributions": parts})
    return affinity, results


def cpp_fixture(data, model, affinity, results):
    return {
        "level": LEVEL, "k": 7, "threshold": 1, "top_n": TOP_N,
        "timedecay_formula": True, "time_decay_coefficient": HALF_LIFE, "time_now": NOW,
        "affinity": affinity,
        "items": [dict(item=item, support=support) for item, support
                  in data.groupby("item")["user"].nunique().items()],
        "neighbours": [dict(item=item, neighbour=neighbour, similarity=similarity, support=support)
                       for (item, neighbour), (similarity, support)
                       in sorted(expected_edges(data, model, 1, 7).items())],
        "recommendations": results,
    }


@pytest.mark.parametrize("decay", [False, True])
def test_score_matches_upstream_recommendations(tmp_path, decay):
    data = synthetic_listens()
    model = upstream(data, decay)
    affinity, expected = reference_scores(model)
    assert affinity == {"track-a": 2.5 if decay else 3.0, "track-b": 1.0}
    output = run_fixture(tmp_path, data, decay)
    actual = score(output / "neighbours.sqlite", LEVEL, affinity, TOP_N)
    assert [row["item"] for row in actual] == [row["item"] for row in expected]
    for got, want in zip(actual, expected):
        assert got["score"] == pytest.approx(want["score"], rel=1e-14)
        assert got["contributions"] == want["contributions"]
        assert sum(part["contribution"] for part in got["contributions"]) == got["score"]
    if decay:
        fixture = json.loads((HERE / "fixtures" / "synthetic-6x8.json").read_text(encoding="utf-8"))
        assert fixture == cpp_fixture(data, model, affinity, expected)


def test_manifest_and_no_user_identifiers(tmp_path):
    output = run_fixture(tmp_path, synthetic_listens(), True)
    assert {path.name for path in output.iterdir()} == {"neighbours.sqlite", "manifest.json"}
    manifest_file = output / "manifest.json"
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    database = output / "neighbours.sqlite"
    raw_db = database.read_bytes()
    assert manifest["sqlite_sha256"] == hashlib.sha256(raw_db).hexdigest()
    assert manifest["sqlite_bytes"] == len(raw_db)
    assert manifest["levels"] == [LEVEL]
    assert manifest["k"] == 7 and manifest["threshold"] == 1
    assert manifest["licence"] == "derived from ListenBrainz listens, CC0"
    with closing(sqlite3.connect(database)) as db:
        tables = {name for name, in db.execute("SELECT name FROM sqlite_schema WHERE type = 'table'")}
        assert tables == {"items", "neighbours"}
        counts = {table: db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0] for table in sorted(tables)}
    assert manifest["row_counts"] == counts
    assert manifest["row_counts_by_level"] == {LEVEL: counts}
    raw_manifest = manifest_file.read_bytes()
    assert all(user.encode() not in raw_db and user.encode() not in raw_manifest for user, _ in LISTENS)


def test_mixed_inputs_caps_and_scorer_cli(tmp_path):
    csv_path = tmp_path / "mixed.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["user", "item", "timestamp", "level"])
        for level in ("recording", "artist"):
            for user, items in [("u1", "abc"), ("u2", "bb"), ("u3", "a")]:
                writer.writerows((user, level + item, NOW, level) for item in items)
    parquet = tmp_path / "albums.parquet"
    pq.write_table(pa.table({"user": ["u1", "u1"], "item": ["album-a", "album-b"], "timestamp": [NOW, NOW]}),
                   parquet, row_group_size=1)
    output = tmp_path / "output"
    subprocess.run([
        sys.executable, "-B", str(HERE / "train.py"), "--input", f"recording={csv_path}",
        "--input", f"artist={csv_path}", "--input", f"release_group={parquet}",
        "--output-dir", str(output), "--input-description", "synthetic", "--min-user-listens", "2", "--top-items", "2",
    ], check=True, capture_output=True, text=True)
    database = output / "neighbours.sqlite"
    with closing(sqlite3.connect(database)) as db:
        assert list(db.execute("SELECT level, item, support FROM items ORDER BY level, item")) == [
            ("artist", "artista", 1), ("artist", "artistb", 2), ("recording", "recordinga", 1),
            ("recording", "recordingb", 2), ("release_group", "album-a", 1), ("release_group", "album-b", 1),
        ]
    affinity = tmp_path / "affinity.json"
    affinity.write_text('{"album-a": 2, "unknown": 1}', encoding="utf-8")
    completed = subprocess.run([
        sys.executable, "-B", str(HERE / "score.py"), str(database), str(affinity), "--level", "release_group", "--top-n", "5",
    ], check=True, capture_output=True, text=True)
    assert json.loads(completed.stdout) == [
        {"item": "album-b", "score": 2.0, "contributions": [{"seed": "album-a", "contribution": 2.0}]},
    ]


def test_fit_row_cap_fails_without_publishing(tmp_path):
    with pytest.raises(ValueError, match="--max-fit-rows exceeded"):
        run_fixture(tmp_path, synthetic_listens(), False, max_rows=2)
    assert {path.name for path in tmp_path.iterdir()} == {"listens.csv"}
