# Catalogue reference fixture provenance

`catalogue_references.json` is raw input for the test-only `collections_seed` CLI. It uses the CLI's exact `collections: [{name, references}]` schema. The real title registry supplies `mediaId` and `key`; neither is invented or embedded here. The native store supplies collection IDs and persists the canonical references.

## Included references

| Reference | Source evidence | Mapping |
|---|---|---|
| Beta Ray Bill — `locg:150065` | `tests/fixtures/locg/series.json`: `configurator.series_id` is `150065`; `header` contains `/comics/series/150065/beta-ray-bill/` and the exact series artwork URL. `series.title` and `series.publisher_name` supply title/publisher. `LocgApi.parseSeriesDetail` yields start year `2021`. | `source: locg`, `route: comic-locg-detail`. Exact title/artwork retained; publisher and year are preserved in `payload.locgMeta`. The series ID is used, not one of the issue IDs in the response. |
| 100 Bullets — `locg:100101` | `tests/fixtures/locg/popular.json`, parsed with the unchanged `LocgApi.parseSeriesList`, yields `id: locg:100101`, title `100 Bullets`, publisher `DC Comics`, start year `1999`, and the exact `medium-6081343.jpg` cover URL. | Same LOCG series route and metadata mapping. The numeric cover filename is an issue image ID, not the reference identity. |
| The Batman Adventures — `gcd:4374`, pinned GetComics post `11366` | `assets/universes/dcau.json`, JSON Pointer `/universe/sections/3/entries/0`, explicitly supplies `provider: getcomics`, `id: gcd:4374`, `gcdId: 4374`, `posts: [11366]`, title, year `1992`, and cover. | Current `Main.openUniverseComic` preserves the nonzero GCD identity and injects the pinned releases. Therefore the candidate is `source: gcd`, `route: comic-gcd-detail`, `payload.posts: ["11366"]`. |
| Gotham Girls — `gcd:13050`, pinned GetComics post `10470` | `assets/universes/dcau.json`, JSON Pointer `/universe/sections/3/entries/11`, explicitly supplies the same fields for `gcd:13050`, post `10470`, and year `2002`. | Same exact GCD-plus-pinned-post mapping. |
| The High Republic · Phase I — `gc:posts:214639` | `assets/universes/star-wars.json`, JSON Pointer `/universe/sections/16/entries/0`, supplies `provider: getcomics`, `gcdIds: [168398,190281]`, no single `gcdId`, and ordered `posts: [214639,267513,282584]`; year is `2021–2022`. | `Main.openUniverseComic` uses its exact first-post fallback for this cross-run collection. The candidate is `source: getcomics`, `route: comic-posts-detail`, `payload.posts: ["214639","267513","282584"]`. Artwork is absent in the snapshot, so the cover stays empty. Neither GCD run is arbitrarily selected. |

The candidates were generated through the production `CollectionsReferences.locg` and `CollectionsReferences.comic` builders using these snapshot fields. For the universe entries, the title uses the existing owner's `title + " (" + year + ")"` presentation. Post IDs are converted to strings and retain their recorded order. Top-level `creator` and `year` stay empty because these two production builders leave them empty; no author or release status was inferred. LOCG start years remain inside `locgMeta`.

## Route availability and omitted cases

- These files prove exact recorded identities and reopen metadata. They do not prove that a remote provider, artwork URL, native catalogue, or local content is currently available. Runtime results must record that separately.
- Every comic entry in the supplied DCAU snapshot has a positive `gcdId`. None is relabeled `gc:posts:*`; doing so would discard the owner-provided identity merely to exercise another branch. The distinct `comic-posts-detail` case comes from the genuine Star Wars cross-run entry listed above.
- The two GCD references include exact pinned posts, so their owner route does not require a current GCD catalogue row. The separate GCD route without pinned posts must find the same exact `ComicsCatalog.series(gcdId)` row both before and during dispatch. No catalogue database or proven live row is supplied here; this fixture makes no claim that the catalogue-only branch succeeds. A missing row must preserve the member and show the recoverable unavailable message.
- No GetComics tag slug/tag ID is present in the supplied fixtures. No tag candidate is included or inferred from a display title, cover filename, or permalink.
- No native pack `seriesId` or `Comics.packVolumes` result is present. No pack candidate is included. A genuine saved pack whose native volumes are later unavailable must retain its member and report that exact pack as unavailable.
- `popular.json` is the repository's recorded response, despite its filename; this fixture does not claim that its rows are a current popularity ranking.

## Source snapshot fingerprints

These SHA-256 values identify the supplied local snapshots used for extraction, not current website responses.

| Repository-relative source | SHA-256 |
|---|---|
| `tests/fixtures/locg/series.json` | `da16effc3f4110e378909d92a92041c5377cc66ec3c6a6d3f094478b97924b17` |
| `tests/fixtures/locg/popular.json` | `ab5c8dd253824d75cfd16bec27735c19d6e9520c1f93ac6be91d00b41a58cb7b` |
| `assets/universes/dcau.json` | `5ae54104e888a88de85dbeb37d7628582415b6fd67be8ee2bf1ba4ae64e55591` |
| `assets/universes/star-wars.json` | `75cbe1f7343fef1802b56aef4764a0e4e69539682730a277a3a3b720da21e13e` |
| `qml/LocgApi.js` | `938b5e642e63e267562f6f6de44099d3b3664f57cbe46c86c7079c9f59902c9f` |

Validation performed here: parsed the original JSON, ran the actual LOCG parsers and collection candidate builders, and checked the output schema and source-field equality. Canonical native admission, seeded-file reload, and actual-app reopening remain Windows runtime checks.
