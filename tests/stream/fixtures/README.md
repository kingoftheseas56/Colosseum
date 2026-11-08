# Real-response fixtures

Captured on 2026-11-08 from public, unconfigured endpoints. These are metadata-only responses, not media downloads or playback tests. There are 20 Torrentio responses (10 films, 10 anime episode requests) containing 1,996 stream rows.

`manifest.json` records each request endpoint, retrieval timestamp, raw-response SHA-256, saved-file SHA-256, title context and its Cinemeta provenance. `releaseDate` is normalized to an ISO calendar date; original metadata values remain under `context`. All anime requests are season 1, episode 1, verified against Cinemeta video IDs.

## Human-review boundary

**No human-reviewed choices have been supplied.** `humanPick` and `agentPick` are null. A ranker output, an automated assertion, or an agent-reviewed choice must not be relabeled a human choice. The requested human-comparison acceptance gate remains pending.

## Cases

| Case | Kind | Rows |
|---|---|---|
| Inception | Film | 99 |
| The Matrix | Film | 130 |
| The Shawshank Redemption | Film | 95 |
| The Godfather | Film | 101 |
| The Dark Knight | Film | 96 |
| Interstellar | Film | 145 |
| Parasite | Film | 58 |
| Spirited Away | Film | 77 |
| Dune | Film | 142 |
| Clayface | Film | 14 |
| One Piece | Anime S01E01 | 500 |
| Naruto | Anime S01E01 | 50 |
| Attack on Titan | Anime S01E01 | 81 |
| Death Note | Anime S01E01 | 53 |
| Fullmetal Alchemist: Brotherhood | Anime S01E01 | 54 |
| Demon Slayer: Kimetsu no Yaiba | Anime S01E01 | 50 |
| Jujutsu Kaisen | Anime S01E01 | 60 |
| Cowboy Bebop | Anime S01E01 | 50 |
| Frieren: Beyond Journey's End | Anime S01E01 | 91 |
| Steins; Gate | Anime S01E01 | 50 |

## Recent-film case

**Clayface** (`tt34890576`) has 14 actual Torrentio rows. Its official [Warner Bros page](https://www.warnerbros.com/movies/clayface) states a North American theatrical release of **2026-10-23**, 16 days before the fixture date. The [IMDb title](https://www.imdb.com/title/tt34890576/) corroborates the ID. This replaces obsolete September scheduling information found in some search results. Returned rows are CAM/TS/TC releases, including a Dragon Money Studio row. An honest safe outcome may therefore be no pick; do not manufacture a good 1080p source.

## Metadata limits

- Cinemeta series-level dates and runtimes are metadata, not verified per-episode facts. Episode identities and release timestamps are retained under `context.episode_metadata`.
- Fullmetal Alchemist: Brotherhood has a suspicious series runtime of 54 minutes. `runtimeMinutes` is deliberately null; the raw metadata value is retained for review.
- `expectedTitles` currently contains the verified requested/display name only (and a harmless punctuation variant when names differ). It does not invent multilingual aliases from returned stream names. This can expose alias-support gaps.

## Secondary providers

- Comet manifest is reachable; its public unconfigured Inception stream endpoint returned the text `Forbidden`, not JSON. No login, CAPTCHA, or bypass was attempted.
- MediaFusion manifest is reachable; all three unconfigured Inception, Matrix and One Piece stream requests returned genuine empty `streams` lists.
- See `secondary-provider-probes.json` for exact endpoint, timestamp, hashes, and response status. Empty/unavailable responses are not evidence of positive ranking coverage for those providers.

## Reproduction and privacy

`python3 collect.py` explicitly refetches the 20 public Torrentio/Cinemeta responses; it is **not** part of the offline test suite and may change results. It uses four workers with one 25-second request per endpoint. It never starts playback.

The collector scrubs secret-named fields, secret URL query parameters, URL userinfo and URL fragments. The 20 saved Torrentio responses contain **no HTTP URL fields and no authentication fields**; scrubbing changed no stream metadata. They contain public torrent identity hashes returned by the add-on. No user account or configured add-on URL was used.
