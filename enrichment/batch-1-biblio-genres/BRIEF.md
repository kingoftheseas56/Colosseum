# Batch 1 — Biblio genre blurbs (web-sourced)

You are enriching the genre-browse blurbs for a books app. For each of the 10 genres in
`input.json`, research the web and write **the best possible short genre description**, then
write everything to `output.json` in this folder. This is a **collect-for-review** job: a human
approves the results later and fills them in by hand.

## Hard rules

- **Write ONLY `output.json` in this folder.** Do not create, edit, or delete any file outside
  `enrichment/batch-1-biblio-genres/`. Never touch the app's `qml/`, `data/`, or source — the
  target file `qml/BiblioGenreApi.js` is named for context only and must NOT be modified.
- **Web-sourced, not from memory.** For each genre, actually search the web and read 2-4
  reputable sources (e.g. Wikipedia's genre article, established library/publisher genre guides,
  literary-reference sites). Synthesize — do not copy. Avoid SEO spam and content farms.
- **Improve on the current blurb, don't rephrase it.** `current_blurb` is the house voice to
  match *and beat*. If your version isn't clearly better or better-grounded, say so in `notes`.
- **Cite every genre.** Record the source URLs you actually used.

## The blurb — quality bar

- **2-3 short sentences, ~35-55 words.** It is a browse-page blurb, not an essay.
- **Present tense, evocative but grounded.** Name what the genre *is*, what it *does to a reader*,
  and its real range — informed by what you read, not generic filler.
- **Match the house voice:** literary, precise, a little poetic; em-dash style uses a spaced
  hyphen ` - ` as in the current blurbs. No marketing hype, no "dive into", no clichés.
- Plain text only (no markdown, no quotes-within that would break a JS string).

## Output — write exactly this shape to `output.json`

```json
{
  "batch": "biblio-genres",
  "items": [
    {
      "genre": "Romance",
      "current_blurb": "<copied verbatim from input.json>",
      "new_blurb": "<your 2-3 sentence web-informed blurb>",
      "sources": [
        { "title": "<source title>", "url": "https://..." }
      ],
      "notes": "<optional: any caveat, or 'current blurb already strong' if you couldn't beat it>"
    }
  ]
}
```

- One `items` entry per genre, **in the same order as `input.json`**, keyed by the exact `genre`
  string (so the review maps 1:1 onto the app's `GENRE_DESC`).
- 2-4 `sources` per genre. If a genre genuinely needs no change, still fill `new_blurb` with your
  best version and flag it in `notes`.

When done, print a one-line summary: how many genres written, and any you flagged.
