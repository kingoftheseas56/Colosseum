# Task 0 probe — do manga source searches return multi-volume packs?

- **Date:** 2026-07-30
- **Lane:** Comics & Manga — Agent 1 (Claude)
- **Plan:** `Brotherhood/docs/superpowers/plans/2026-07-30-manga-volume-batch-download.md`, Task 0
- **Spec:** `Brotherhood/docs/superpowers/specs/2026-07-30-colosseum-manga-volume-batch-download-design.md`
- **Builds nothing.** This decides whether Task 5 (the torrent-pack route) exists.

## Method

The plan's Step 2 said to click through the picker on one series. I ran the engine's **exact
search instead**, across eight series, so the answer is measured rather than sampled once.
`tests/manga_volume_pack_probe.py` ports, verbatim:

| Ported from | What |
|---|---|
| `MangaNyaaSource.cpp:195-219` | `queryVariants()` — the 5 query strings per volume |
| `MangaNyaaSource.cpp:391-400` | the RSS endpoint (`page=rss&c=3_1&s=seeders&o=desc`) |
| `MangaNyaaSource.cpp:71-95` | `detectCoverage()` — both regexes, range-wins-over-single |
| `MangaNyaaSource.cpp:157` | `coverageIncludesTarget()` |
| `MangaNyaaSource.cpp:298-302` | the chapter-pack reject (only when coverage is empty) |

Rows are unioned by infoHash across all 5 query variants, exactly as the engine does.

**Honest limit:** the probe does **not** apply `strongSeriesMatch`, `isRaw`, or the uploader
trust tier. Those three only ever *remove* rows, so every count below is an **upper bound on
rows** — and, critically, the two conclusions both survive that, because both rest on rows
being *absent*, which filtering can only make more true.

Reproduce: `python tests/manga_volume_pack_probe.py` (raw rows land in
`tests/manga_volume_pack_probe.json`).

## The three questions the plan asked

### 1. Do any rows cover more than one volume? — **Yes. Universally.**

```
series                   rows  packs   on-tgt  singles
One Piece                  75      6        2        0
Bleach                    149     10        5        1
Naruto                     77     16        5        0
Vagabond                   26      8        6        0
My Hero Academia           79     11        4        0
Death Note                 24      3        3        0
Fullmetal Alchemist        18      2        2        0
20th Century Boys           5      2        1        0

8 of 8 series returned at least one pack COVERING the target volume.
```

Real rows, verbatim:

```
One Piece    v0-v105   35.6 GiB  S=33  One Piece - Digital Colored Comics+Cover Stories V1 Batch (v000-v105 & ch0035-1078)
One Piece    v1-v111   20.3 GiB  S=81  One Piece v001-111 + 1134-1176 (2003-2026) (Digital) (1r0n)
Bleach       v1-v74    11.3 GiB  S=89  Bleach - Digital Colored Comics v01-74 (2021) (Colored Council)
Vagabond     v1-v37     2.4 GiB  S=74  [Pajeet] Vagabond Volume 01-37 & Chapters 323-327 + Art Books Hiatus [CBZ]
MHA          v1-v35     7.7 GiB  S=1   Boku no Hero Academia / My Hero Academia v01-35,351-399 (2015-2023) (Digital)
Death Note   v1-v12     3.3 GiB  S=40  Death Note (v01-v12) (2005-2007) (Digital TPB) (DarkZone-Empire)
FMA          v1-v27     2.6 GiB  S=70  Fullmetal Alchemist (v01-v27) (2005-2011) (Digital) (LostNerevarine-Empire)
20th C Boys  v1-v22     1.7 GiB  S=86  [Pajeet] 20th Century Boys Volume 01-22 + 21st Century Boys Volume 01-02 (Comp…
```

The spec's §2 step 3 imagined a `One Piece v01–v105 (58 GB)` row. **It exists**, at 35.6 GiB,
and it was the *first row of the first query I ran*.

### 2. Does a row identify WHICH volumes it holds? — **Yes, machine-readably, and it already reaches QML.**

`detectCoverage()` parses `v000-v105`, `v01-74`, `Volume 01-37`, `Vol 1 - Vol 12` into a
numeric `[lo, hi]`. `MangaTankobanService.cpp:616-618` already puts **`coverageLo`,
`coverageHi` and `standalone`** on every row handed to the picker, and `:601-604` already
renders a human `Vol 1–105` string.

**So spec §5's "list only packs covering volumes 35–44, tightest coverage first" is
implementable in QML alone — no C++ needed for the filtering or the sort.**

### 3. Nyaa vs WeebCentral row counts

WeebCentral is always **exactly one row**, appended by the picker itself
(`MangaTankobanSourcesPage.qml:89-100`), never by the search. Nyaa rows are the numbers above
(18–149 raw; a handful survive coverage matching).

---

## The finding that inverts the plan's assumption

Look at the `singles` column: **on-target single-volume torrents are essentially absent.**
Seven of eight series returned **zero**. Bleach returned one — `Bleach - Vol. 05`, 24 MiB,
**0 seeders**, i.e. undownloadable.

This is the opposite of what Task 5 being "optional garnish" assumed:

> **For nearly every series, the pack IS the torrent route. There is no per-volume torrent to fall back to.**

And filtering can only strengthen this — `strongSeriesMatch` / `isRaw` / trust-tier remove
rows, so the true single count is ≤ what I measured.

Two consequences:

1. **Today's per-volume Nyaa download is already acquiring a pack** and isolating one volume
   from it. That is how these volumes reach the device at all today.
2. **Task 5 is not garnish — it is the difference between "one press downloads ten volumes"
   and "one press downloads ten volumes ten times over."** Without it, a batch of ten on
   the Nyaa route means the user picks a 35 GiB pack and the app fetches it once per volume,
   or (per Task 4's guard) the Nyaa row is simply dead in batch mode and every batch falls to
   WeebCentral — chapter-by-chapter compilation of ten volumes.

## What Task 5 actually costs — much less than the plan budgeted

The plan's Task 5 Step 1 says to implement pack-to-volume mapping "using the existing
`setFilePriorities` seam." **That work is already done.** `MangaVolumeTorrentDownloader` is
already multi-intent by construction:

- A `Job` is keyed by **infoHash** and holds a **list of `Intent`s**, one per volume
  (`MangaVolumeTorrentDownloader.cpp:201-238`).
- `resolveJob()` runs `MangaVolumeFilePicker::pick(intent.volumeNumber, job->files)` **per
  intent**, then applies `unionPriorities(picked, …)` to the single torrent.
- `download()` on an existing hash says it plainly: *"Existing torrent: join it, grow the
  union. Never re-add the magnet."*

So the transport already supports "one pack, N volumes." The **only** blocker is one guard in
the façade: `MangaTankobanService::downloadNyaa` (`:283-295`) accepts an infoHash only if it
is in `m_candidates[volumeId]` — the per-volume search cache. A batch searches only its probe
volume, so volumes 36–44 have no cached candidate and each call would be rejected with
*"Unknown source — infoHash is not among the cached search candidates."*

That guard is **correct and worth keeping** (it stops QML handing in an arbitrary magnet). The
batch entry point should validate the hash **once** against the probe volume's candidates and
then attach the intent for each volume — not weaken the guard.

Estimated shape: one `Q_INVOKABLE downloadNyaaBatch(QStringList, QString)` that loops
`m_transport->download(m_volumes[id], chosenCandidate)`, plus the existing per-volume
in-flight/tombstone bookkeeping. No transport change. No file-picker change.

## Recommendation

**Task 5 is buildable, and it should be built** — but it is a *volume-coverage filter + a
façade entry point*, not the C++ excavation the plan sized. I'd reorder it to run right after
Task 4 rather than as a gated maybe, because on the numbers above the Nyaa route is the
primary one, not the exotic one.

Deferring it is survivable — every batch still works via WeebCentral — but it means the
fastest, highest-seeded route (a 74-seeder `v01-37` CBZ pack) is unavailable to exactly the
press the feature is named after.

**Awaiting Hemanth's / A1's go before Task 5. Tasks 1–4 proceed either way.**
