# Vinyl SAR trainer (offline)

Laptop-only tool; not imported by the Vinyl provider or shipped in the app. From the repo root on Windows:

```powershell
py -3.11 -m venv C:/b/vinyl-sar/venv
$py = 'C:/b/vinyl-sar/venv/Scripts/python.exe'
& $py -m pip install --only-binary=:all: --no-deps -r scripts/vinyl/sar/requirements.txt
& $py -B scripts/vinyl/sar/train.py --input recording=recordings.parquet --input artist=artists.csv --input release_group=albums.parquet --input-description 'ListenBrainz slice label' --output-dir ./sar-run
& $py -B scripts/vinyl/sar/score.py ./sar-run/neighbours.sqlite ./affinity.json --level recording --top-n 10
```

`--no-deps` is intentional: the Recommenders wheel declares dependencies for many other algorithms, none of which SAR imports. The requirements list SAR's runtime and the parquet reader; install `pytest==8.4.2` separately for tests. Run `& $py -B -m pytest -q -p no:cacheprovider scripts/vinyl/sar/test_trainer.py`.

## Input and RAM limits

- Supply one `--input LEVEL=PATH` per level to fit (`recording`, `artist`, `release_group`). CSV is UTF-8; parquet is read in batches. Required columns: `user` and `item` as nonempty strings, `timestamp` as integer Unix seconds. If the table has a `level` column, only rows tagged with the requested level(s) are used; a tagged file can be assigned to multiple levels. Use mapped item IDs, not titles or user IDs. Include release groups only from explicit, resolved release context; omit that level when unavailable.
- `--min-user-listens` (default 1) counts plays per user and level before the item cap. `--top-items` (default 3000) keeps items with the most distinct eligible listeners, breaking ties by ascending item ID. No further user filtering occurs after the item cap.
- The trainer streams into a temporary on-disk SQLite database beside the output directory; allow scratch disk for grouped listens and SQL sorting. Scratch contains input IDs and is removed on normal exit/error. Only one filtered level is loaded for fitting. SAR's Jaccard fit builds dense item-squared arrays, so doubling `--top-items` can quadruple that part of memory. `--max-fit-rows` (default 1,000,000) fails rather than silently sampling: it caps retained user-item rows, or distinct user-item-timestamp rows with decay. Narrow the input before raising either cap. These are not byte limits or measured settings for the real ListenBrainz run.
- Plays are unit events. Repeats add to affinity but never multiply distinct-user co-occurrence. `--threshold` defaults to 1. `--timedecay-formula` enables upstream affinity decay (off by default), `--time-decay-coefficient` is the half-life in days (30), and `--time-now` sets the Unix reference time (default: newest retained listen in each fit). Decay changes affinities, not Jaccard similarities.

## Outputs and reference scoring

The output directory must be new, with an existing parent. It contains `neighbours.sqlite` and `manifest.json`. `items(level, item, support)` stores distinct retained listeners, even below the threshold. `neighbours(level, item, neighbour, similarity, support)` stores directed edges with upstream positive finite Jaccard values and distinct co-listener counts. No self-edges. Up to `--top-k` (default 100) per item, chosen by descending similarity then ascending neighbour ID. The primary-key index starts with `(level, item)` for lookups; use explicit ordering if needed. The manifest has model parameters, aggregate counts, size, SHA-256 and provenance. Pass `--dump-timestamp` and `--mapping-revision` for real training; use only public, non-identifying metadata in these flags and `--input-description`.

`affinity.json` is an object like `{"recording-id": 2.5}`, using **already computed** nonnegative affinities. The scorer multiplies each seed weight by each exported edge's similarity, then sums the contributions by candidate. Unknown seeds add nothing; all seed IDs are excluded as seen; positive scores only, sorted by descending score then ascending item ID. With all positive off-diagonal edges present, it matches upstream `recommend_k_items(remove_seen=True)` on positive candidates (upstream may pad with zeros and does not guarantee item-ID tie order). A truncated graph can omit contributions, so it can legitimately change scores and rank. `fixtures/synthetic-6x8.json` contains upstream-verified edges, input affinities and expected scores/contributions for the later C++ parity test.
