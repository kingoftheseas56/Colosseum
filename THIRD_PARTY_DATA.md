# Third-party runtime data — keyless anime ordering

Colosseum's anime Absolute/Seasons ordering (spec 2026-07-15) reconciles two
public community datasets at runtime. This document records what they are, what
Colosseum consumes, how the data is fetched and cached, and how to remove it.

## Sources

| Repository | Direct source URL | Used for |
|---|---|---|
| [Fribb/anime-lists](https://github.com/Fribb/anime-lists) | `https://raw.githubusercontent.com/Fribb/anime-lists/master/anime-list-mini.json` | Cross-provider identity merge |
| [Anime-Lists/anime-lists](https://github.com/Anime-Lists/anime-lists) | `https://raw.githubusercontent.com/Anime-Lists/anime-lists/master/anime-list-master.xml` | AniDB→TVDB season / offset / special mappings |

## Fields consumed

- **Fribb `anime-list-mini.json`** — `type`, `anidb_id`, `mal_id`, `anilist_id`,
  `kitsu_id`, `imdb_id` (string or list), and `themoviedb_id` (`{tv}` or
  `{movie:[...]}`). These are joined by AniDB id and used to resolve the
  identities a caller honestly possesses (MAL / AniList / AniDB / Kitsu / IMDb /
  TMDB) to a single AniDB work. Title text is never used for matching.
- **Anime-Lists `anime-list-master.xml`** — per `<anime>`: `anidbid`, `tvdbid`,
  `defaulttvdbseason`, `episodeoffset`, and `<mapping-list>/<mapping>` bodies
  (explicit episode pairs, ranges, offsets, and season-0 specials). These invert
  a provider's TVDB-style season/episode into an AniDB absolute number.

Only these fields are read. No other part of either dataset is interpreted, and
neither is used to fabricate, drop, or replace a provider stream id.

## Runtime fetch — no snapshot is packaged

Colosseum does **not** commit, package, or redistribute a snapshot or any
generated derivative of either dataset. The user's app downloads the two files
directly from the upstream raw GitHub URLs above, into the user's local
application-data cache, the first time it runs. The UI works immediately with its
existing provider order; canonical Absolute ordering activates once the first
valid pair has downloaded and parsed. A previously validated cache works offline
on later launches. All download and parse work runs off the GUI thread; the app
makes no dataset network request from QML.

## Cache layout, refresh, and limits

Cache root: `<AppDataLocation>/anime-order/`

```text
anime-order/
  current.json                       {"schemaVersion":1,"active":"<generation-id>"}
  generations/
    <sha256-generation-id>/
      fribb-anime-list.json
      anime-list-master.xml
      generation.json                schema, fetch time, source URLs, byte counts, SHA-256s
```

- Each generation directory is immutable and named from the SHA-256 pair of its
  two source files. `current.json` is swapped with an atomic write only after a
  new generation is fully written, hash-verified, and parsed, so a crash or a
  partial/corrupt refresh can never damage the last good generation.
- A valid generation older than **seven days** triggers one background refresh
  per process. A refresh is an all-or-nothing pair operation; any failure leaves
  the previous generation active (state `stale`), never a blocking dialog.
- Download size caps: **16 MiB** for the Fribb JSON, **12 MiB** for the
  Anime-Lists XML. Responses must be successful HTTPS 2xx; redirects stay HTTPS.
- After a successful swap, generations older than the active and the immediately
  previous one are pruned.

## Licensing boundary

As observed on **2026-07-15**, neither upstream repository exposes an explicit
root license file. Colosseum therefore avoids redistribution in V1: it does not
bundle, commit, or generate a derivative snapshot of either dataset, and instead
fetches them at runtime into the user's own cache. If either project publishes an
explicit license, this boundary can be revisited.

## Removing the data

Delete the cache directory and restart the app:

```text
<AppDataLocation>/anime-order/
```

On the next launch Colosseum simply falls back to its existing provider ordering
and, if online, re-downloads a fresh pair. Deleting the folder removes every
cached copy of the datasets.

## Attribution

Anime identity and mapping data © their respective upstream projects
([Fribb/anime-lists](https://github.com/Fribb/anime-lists),
[Anime-Lists/anime-lists](https://github.com/Anime-Lists/anime-lists)). Colosseum
is an independent consumer of these public datasets and claims no affiliation
with or endorsement by either project.

---

# Third-party bundled fonts — reader2 reading typefaces

The fresh ebook reader (reader2, Task 10 appearance panel) ships static TrueType
font files, bundled in `assets/fonts/` and committed to the repo. They render the
native chrome (QML `FontLoader`) AND the book text (an `@font-face` the paper glue
injects into the WebEngine page, `resources/reader2/paper_glue.js`).

## Font

| Family | Files (in `assets/fonts/`) | Source | License |
|---|---|---|---|
| **Literata** | `Literata-Regular.ttf`, `Literata-Italic.ttf` | [googlefonts/literata](https://github.com/googlefonts/literata) — `fonts/ttf/` static instances | SIL Open Font License 1.1 (OFL) |

Fraunces and Inter (already bundled for the chrome from earlier tasks) are also
declared as book `@font-face` faces so their typeface cards apply to the book text;
they carry their own upstream OFL/OFL-style licenses.

## Why STATIC instances (not the variable font)

Qt's font loader registers a variable TTF under a "`<Name> Variable`" family, so a
request for the plain family (`Literata`) silently falls back to Tahoma. The static
`fonts/ttf/Literata-Regular.ttf` / `Literata-Italic.ttf` register as the plain family
**"Literata"** (verified via the name table), so both the QML card and the book text
resolve the real face.

## License — SIL Open Font License, Version 1.1

Copyright 2017 The Literata Project Authors
(https://github.com/googlefonts/literata).

This Font Software is licensed under the SIL Open Font License, Version 1.1. The
license is available with a FAQ at https://scripts.sil.org/OFL and is reproduced in
full in the upstream repository's `OFL.txt`. Under the OFL the fonts may be bundled,
embedded, and redistributed with the software; they may not be sold on their own, and
the reserved names must not be used to promote derivative fonts. Colosseum bundles the
unmodified font files as a runtime asset and claims no affiliation with or endorsement
by the Literata project.
