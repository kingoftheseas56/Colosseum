# Soak Digest

## Coverage

- Worlds visited: local-video, tankoban, unknown, vault
- Different things it touched: 14
- Events recorded: 23
- Time span covered: 1h 15m 0s (from 2026-08-12T00:00:00.000 to 2026-08-12T01:15:00.000)

If this span looks short for how long the soak was meant to run, treat this as a partial or aborted run, not a clean pass.

## What it did

- open: 5
- ready: 3
- fail: 3
- download: 2
- mem: 4
- frames: 3
- nav: 3

## How long things took

Typical (p50) is what most operations look like; bad-case (p95) is the one-in-twenty worst case; worst is the single slowest thing seen.

| Operation | Count | Typical (p50) | Bad case (p95) | Worst |
|---|---|---|---|---|
| open | 5 | 300 ms | 500 ms | 500 ms |
| ready | 3 | 150 ms | 250 ms | 250 ms |
| download | 2 | 800 ms | 1200 ms | 1200 ms |

## How long things took, by world

| Operation | World | Count | Typical (p50) | Bad case (p95) | Worst |
|---|---|---|---|---|---|
| download | vault | 2 | 800 ms | 1200 ms | 1200 ms |
| open | tankoban | 3 | 200 ms | 300 ms | 300 ms |
| open | vault | 2 | 400 ms | 500 ms | 500 ms |
| ready | local-video | 3 | 150 ms | 250 ms | 250 ms |

## Failures

3 failure event(s) total.

| Subject | Times it failed | First time | Last time |
|---|---|---|---|
| tankoban:Naruto/Vol 1 | 2 | 2026-08-12T00:10:05.000 | 2026-08-12T01:00:00.000 |
| vault:Movie B | 1 | 2026-08-12T00:42:00.000 | 2026-08-12T00:42:00.000 |

## Memory

- Start: 500 MB
- End: 600 MB
- Peak: 600 MB
- Trend: climbing, about 100.0 MB per hour over this run.

## Dropped frames

2 episode(s), 17 dropped frame(s) total.

| Subject | When | Dropped frames |
|---|---|---|
| local-video:clip1.mp4 | 2026-08-12T00:32:05.000 | 12 |
| local-video:clip3.mp4 | 2026-08-12T00:55:05.000 | 5 |

## Top 10 slowest things

| Subject | Worst time seen | Operation |
|---|---|---|
| vault:fixture-download-2 | 1200 ms | download |
| vault:fixture-download-1 | 800 ms | download |
| vault:Movie B | 500 ms | open |
| vault:Movie A | 400 ms | open |
| tankoban:Naruto/Vol 1 | 300 ms | open |
| local-video:clip3.mp4 | 250 ms | ready |
| tankoban:One Piece/Vol 2 | 200 ms | open |
| local-video:clip2.mp4 | 150 ms | ready |
| tankoban:One Piece/Vol 1 | 100 ms | open |
| local-video:clip1.mp4 | 50 ms | ready |

## Warning gate (W0)

Not available for this run - no warnings.txt was found.
