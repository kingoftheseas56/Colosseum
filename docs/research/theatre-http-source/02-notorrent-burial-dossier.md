# Mini-dossier: NoTorrent rows invisible in the Sources sheet

- **Diagnosis status:** Root cause confirmed · **Date:** 2026-08-07 · **Agent 4 (Claude)**
- **Repo/commit:** Colosseum master, post-`2e5c0a4` + NoTorrent seed (gen 9, uncommitted at diagnosis)
- **Symptom:** Hemanth opened Spider-Man: No Way Home → Sources ("64 sources · via 3 extensions",
  1080p filter active) and saw only Torrentio rows (+ the VidKing hosted row). No NoTorrent row
  visible anywhere he scrolled.

## Falsifiable hypothesis (survived)

**H1 — burial, not loss:** `AddonClient._sortRows` (AddonClient.js:250-257) sorts by
`addonPriority` FIRST (ask order = installed order); the gen-9 migration APPENDS NoTorrent as the
20th extension, so every NoTorrent row sorts below every Torrentio row.

## Discriminating experiment

The sheet item was no longer alive (daily-pipe `dump-ui` had no `sourcesSheet`), so the check ran
deterministically: the REAL `parseStream`/`_rowKey`/`_sortRows` from `qml/AddonClient.js` executed
in Node against LIVE responses from both addons for the same title (tt10872600).

**Result — exact match with the screenshot:**
| Fact | Replication | Hemanth's screen |
|---|---|---|
| Total rows | 94 | "All 94" pill |
| 1080p rows | 63 | "1080p 63" pill |
| NoTorrent rows present | 24 (of 25 raw; 1 dropped by parse/dedup) | — |
| First NoTorrent position (unfiltered) | 70 of 94 | below the fold |
| First NoTorrent position (1080p filter) | 43 of 63 | below the fold |

Profile ground truth: `installed.json` at `defaultsVersion: 9`, NoTorrent enabled, correct
transportUrl — the seed migration worked; "via 3 extensions" was VidKing + Torrentio + NoTorrent.

**Rejected en route:** fetch failure (probe returns 25 streams), accepts()/idPrefix mismatch
(row passes), dedup collision (`u:<url>` keys unique), parse nulls (24 of 25 survive).

## Fix direction (Hemanth, 2026-08-07)

"There is a reason we have the 'all' extension picker — wire NoTorrent there." The Sources sheet's
top "All ▾" bar (SourcesSheet.qml `topBar`) is DECORATIVE today — a MouseArea with hover styling
and no onClicked. Wire it into a real extension filter (All / per-extension, with counts) so any
extension's rows are one click away regardless of sort order. Row ordering itself is untouched —
no silent re-ranking.
