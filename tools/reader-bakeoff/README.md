# Long-Strip Reader Bakeoff — Harness

Executes the bakeoff in `docs/superpowers/specs/2026-07-15-reader-long-strip-bakeoff-design.md`.
Evidence-gathering only; no reader is rewritten here.

## Layout

| Path | Role | Committed? |
|---|---|---|
| `build_fixture.py` | Slice the local source volume → canonical CBZ + byte-identity manifest (spec §4). | yes |
| `extract_verify.py` | Extract CBZ → Colosseum page dir; hash-verify every page vs manifest (spec §4.3 gate). | yes |
| `launch.py` | Isolated per-reader launch + PresentMon capture + run manifest/ledger (spec §5,§6,§9). | yes |
| `report.py` | Fold traces → the final report tables + Max-parity contract (spec §7,§12). | yes |
| `docs/reader-bakeoff/fixture-manifest.json` | Byte identity of the canonical volume. | yes |
| `artifacts/reader-bakeoff/` | CBZ, extracted pages, PresentMon CSVs, internal traces, profiles. | **gitignored** |

## Readers under test

| Reader | Binary | Direct-open into long strip | Isolation |
|---|---|---|---|
| **Colosseum** | isolated worktree `_bakeoff_colosseum/native/build-msvc/colosseum.exe` | `COLOSSEUM_BAKEOFF_STRIP=<pagedir>` → page-only `MangaReader` | env → temp `APPDATA` |
| **Tankoban 2** | `~/Desktop/Tankoban 2/out/Tankoban.exe` | (adapter: dev-control open-comic, TBD) | `TANKOBAN_DATA_DIR` + `TANKOBAN_INSTANCE_ID` |
| **Tankoban-Max** | `npx electron .` in `~/Desktop/Tankoban-Max` | positional `<cbz>` arg | `--user-data-dir=<temp>` |

## Common internal-trace event schema (spec §7.2)

Ring buffer, flushed once post-run. One CSV row per event:

```
timestamp_us, reader, run_id, event,
input_kind, raw_dx, raw_dy, normalized_dy,
pending_delta, consumed_delta, scroll_offset,
page_index, decode_inflight, cache_bytes,
geometry_revision, detail
```

`event` ∈ { input_received, backlog_updated, scroll_step, page_boundary,
decode_queued, decode_started, decode_completed, scale_started,
scale_completed, cache_hit, cache_insert, cache_evict, geometry_changed,
paint_requested, paint_completed }.

## Run matrix (spec §6)

4 motions × {cold, warm} × 3 reps × 3 readers. Motions: slow-wheel,
sustained-wheel, fast-touchpad-swipe, boundary-crossing. Physical input is
captured once, normalized to a replay script, and replayed into each reader.

## Status ledger

- [x] Fixture + manifest (Batman Rebirth Deluxe front-120 slice; spec §4.1 amended: local source, manifest carries byte-identity not provenance).
- [x] Reader surveys + hook-site map (all three).
- [x] Colosseum adapter (`COLOSSEUM_BAKEOFF_STRIP`, page-only host) — built in isolated worktree (main tree non-building due to an unrelated in-flight torrent WIP).
- [ ] PresentMon acquisition (awaiting Hemanth OK).
- [ ] Instrumentation behind bakeoff flag (3 readers) + overhead calibration.
- [ ] Launcher + input replay + traced matrix.
- [ ] Blind A/B/C rounds.
- [ ] Report + donor ruling + Max-parity contract.
