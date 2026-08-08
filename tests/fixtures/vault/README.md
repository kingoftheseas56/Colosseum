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

**Deferred to their consuming slices** (they need real bytes, not stubs): a
corrupt CBZ and a decode-valid tiny MP4 + a non-video `.bin` arrive with Slice 5
(enricher) and Slice 6 (admission probe); the MP4 is generated with the bundled
ffmpeg at that point, since a decodable frame can't be hand-authored.
