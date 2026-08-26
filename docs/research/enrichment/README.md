# Colosseum enrichment — batched, review-gated

Holding area for AI-assisted catalog enrichment (descriptions, blurbs, tags, summaries)
run through DeepSeek/Reasonix. **Nothing here is live.** Each batch is generated into its
own folder, reviewed by Hemanth, and only the approved values are hand-filled into the app.

## Rules (do not violate)

- **Collect only. Never write to app code or data.** Enrichment runs write ONLY inside
  `docs/research/enrichment/<batch>/output.json`. `qml/`, `data/`, and every other app path are off-limits.
- **Review gate.** A human reviews `output.json` before any value reaches the product. The
  runner never edits the live catalog.
- **Cite.** Web-sourced batches record their sources per item so the review can trust them.

## Priority batches

1. **batch-1-biblio-genres** — Biblio genre-browse blurbs (10 genres). *In dire need; first.*
2. _(later)_ per-item book / comic / manga descriptions where entries are thin.

## How a batch runs

Point Reasonix at the batch folder, e.g. from `docs/research/enrichment/batch-1-biblio-genres/`:

```
reasonix run "Read BRIEF.md and input.json in this folder, do the work, and write output.json here. Do not touch any file outside this folder."
```

Then a brother reviews `output.json` and fills the approved blurbs into the app by hand.
