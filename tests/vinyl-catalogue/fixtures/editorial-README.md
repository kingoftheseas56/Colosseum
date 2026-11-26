# Editorial chart fixtures

Single captures, trimmed to representative rows and fields. Tests replay them through the real provider without network access.

- `kworb-songs.html`: [Spotify weekly global](https://kworb.net/spotify/country/global_weekly.html), chart dated 2026-11-19. The fourth retained row deliberately keeps its original rank (56) and overlaps the YouTube chart.
- `kworb-albums.html`: [Worldwide iTunes Album Chart](https://kworb.net/aww/), dated 2026-11-26.
- `kworb-artists.html`: [Global Digital Artist Ranking](https://kworb.net/itunes/), dated 2026-11-26.
- `youtube-zz.json`: public `ytmusicapi.get_charts("ZZ")` result. `youtube-zz-top100.json` is the track excerpt from its Top 100 playlist, fetched through the same library.
- `wikipedia-hot100-directory.json` and `wikipedia-rock-directory.json`: English Wikipedia MediaWiki API `list=allpages` results, trimmed to target and neighbouring periods. The provider uses the returned page IDs for `action=parse&prop=wikitext`.
- `wikipedia-hot100.json`: [List of Billboard Hot 100 number ones of 2025](https://en.wikipedia.org/?curid=78542955), including a title/artist rowspan.
- `wikipedia-rock.json`: [List of Billboard Mainstream Rock number-one songs of the 2020s](https://en.wikipedia.org/?curid=62774351), including the transition from 2020 to 2021.

Wikipedia contributors' table excerpts are shared under [CC BY-SA](https://creativecommons.org/licenses/by-sa/4.0/); see the linked pages' histories for contributors. Excerpts omit surrounding prose, other rows and citation contents.
