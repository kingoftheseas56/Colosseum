# Vault fixtures

Consumed by `tst_vault_kit` (Slice 1) and later Vault slices. Baked into test
targets at configure time via the `VAULT_FIXTURES_DIR` compile definition (house
pattern: `TANKOBAN_FIXTURES_DIR`).

Slice 1 exercises the pure-logic classifier/cleaner/grammar, which key off file
**extensions and structure only** — so these are tiny text stubs, not real
media. What each tree proves:

- `mixed-root/` — a mixed root of kind-pure leaves: `Berserk/` (3 comics),
  `The Sopranos/Season 1|2/` (video across seasons), `Dune/` (2 books),
  `SAMPLE/` (a subtree a `scanIgnore` needle excludes), and
  `loose-comic-at-root.cbz` (loose-file capture).
- `mixed-leaf/Akira/` — one leaf mixing comics + a `.txt`: flagged `mixed`,
  dominant comic, the `.txt` in the leftover line (never scatter-shelved).
- `accent-root/Poke Series/` — a comic whose filename carries U+00B4 (ACUTE
  ACCENT); proves the walker survives a non-ASCII path.

**Browse-collapse planner fixtures (browse-face execution plan, Slice 1)** — the five real
library shapes from Hemanth's own library, consumed by `tst_vault_kit`'s
`browse_collapse_*` cases:

- `browse-film/` — Spider-Man No Way Home: one film, a subtitle + `Subs/` (companions), an
  `Extras/` + `Featurettes/` (folded, never counted, never a tile), and two junk files
  (`.txt`/`.jpg`) that were never media. Must collapse to exactly one `film` node.
- `browse-show-siblings/` — Loki Season 1 and Season 2 as SEPARATE sibling folders. Must
  collapse to exactly one `show` node ("Loki", 2 seasons); drilling the show's synthesized key
  hands back the two real season folders.
- `browse-show-nested/` — The Wire: one folder whose own name claims Season 1-5, but disk
  holds only a nested `Season 4/` subfolder. Must collapse to one `show` node whose
  `physicalFact` reads "Season 4 only" — honest presence, not the claimed count.
- `browse-show-absolute/` — Gintama: absolute-numbered episode files (`- 001`, `- 002`,
  `- 003`) directly in one folder, no season subfolders, no SxxExx. Proves the Slice 1
  absolute-numbering grammar (`- 003` → episode 3) and the flat-show collapse.
- `browse-clips/Cricket/` — four loose, unrelated local video clips. No episode grammar
  applies to any of them, so this must stay a plain `folder` node (never a show, never a
  film) — its clips are local-only once you drill in.

**Deferred to their consuming slices** (they need real bytes, not stubs): a
corrupt CBZ and a decode-valid tiny MP4 + a non-video `.bin` arrive with Slice 5
(enricher) and Slice 6 (admission probe); the MP4 is generated with the bundled
ffmpeg at that point, since a decodable frame can't be hand-authored.
