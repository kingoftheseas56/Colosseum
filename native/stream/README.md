# Smart source picker (Arc 81)

A pure Qt Core/C++17 library: parse → trust → corpus statistics → score → rank/pick.
It contains no network, QML, UI state or playback calls. No existing Sources screen
or player behavior is changed by building this library.

## Use

Convert each raw add-on JSON object or existing `AddonClient`/`Torrentio` sheet row
with `stream_from_json`, retaining its input index and add-on installed-order priority.
Set `TrustOptions` with movie/series identity, expected title/aliases, year, season,
episode and release date. Set `ScoreOptions` with preferred language prefixes,
runtime and any bandwidth constraint. `SourceScorer::evaluate` returns:

- `all`: retained rows sorted by descending score, with every scoring reason;
- `rejected`: removed rows with the trust-filter reason;
- `by_tier`: the highest-scoring retained row in each tier, not a safety guarantee;
- `primary`: one eligible recommendation or none;
- `selection_reasons`: explanation even when there is no primary.

Dates default to the current date. Set both option structs' `today` explicitly
when deterministic replay is required. Missing numeric metadata is distinct from
an explicit zero. Raw JSON is retained for caller identity; do not log or publish
credential-bearing raw rows.

## Primary policy

All rows remain score-ranked; primary selection is a separate policy:

1. Exclude rough/CAM/screener, known-zero-seeder torrent, hard scam, severe fraud,
   unplayable, nonfinite and negative-score candidates.
2. For an episode request, prefer eligible singles over packs. A 720p single can
   beat a 1080p pack. Packs remain a fallback if no eligible single exists.
3. Prefer good 1080p, then 720p, then 4K, then SD. Within that pool choose the
   highest score. Dual audio breaks exact score ties, followed by stable add-on
   priority/return order/input order.

The library does not probe whether a tracker, URL or claimed codec actually works.
Trust and score metadata are heuristics, not authenticity or playback guarantees.

## Source adaptation

Harbor `431c447977f8bc422d49ede47ad9b2681edb1649` supplied parser, trust,
corpus and scoring behavior. Debrid/cache tiers, Easynews boosts, direct-URL
bonuses, WebView2 and HTML5 multi-audio penalties are removed. Tankoban 2
`d6d804d6ce899c2c4aef37a2345e4d67de970acc` supplied batch, dual-audio and
full-show-identity safeguards. Both MIT notices are in `THIRD_PARTY_NOTICES.md`.

Intentional improvements include authoritative filename selection, stats-badge
fallback, underscore-delimited technical markers, broader episode ranges, explicit
AV1/VP9, safe numeric metadata, and a stricter full-show-identity gate. A mild
movie-sized parser size warning does not bar legitimate small anime episodes from
primary selection. Scoring still reports that warning.

### Inherited behavior to review before UI adoption

- Harbor trusts an explicit `fileIdx` enough to bypass episode/season mismatch;
  anime also bypasses those comparisons. This is not independent file verification.
- The fresh-cinema trust gate rejects WEB/BluRay claims inside 60 days, including
  potentially legitimate early digital releases. This was retained deliberately.
- Trust flags `allow_season_packs`, `allow_size_outliers` and language fields are
  inherited but not read by Harbor's trust logic. Language affects scoring instead.
- `respect_addon_order` does not override score ranking; add-on priority breaks ties
  and retains Harbor's small score contribution.
- Country-specific metadata dates can affect the inherited year penalty.
- Only current add-on row metadata is evaluated. The caller must keep actual
  playback support and any authenticated URL handling in its existing owner.

## Build and test

```sh
cmake -S native -B build-stream -DCOLOSSEUM_STREAM_PICKER_TESTS_ONLY=ON
cmake --build build-stream --parallel
ctest --test-dir build-stream --output-on-failure
```

Requires Qt 6 Core development files and a C++17 compiler. The same library and
five suites are registered in the ordinary native/test build graph. This focused
configuration does not qualify the full desktop application or Windows/Android.

`stream_fixture_test [output.json]` optionally writes the deterministic fixture
report. Its ordinary CTest run is offline and writes nothing. The 20 checked-in
actual responses contain 1,996 rows. Human pick agreement is **not** tested: no
human-labelled choices were supplied. See `tests/stream/fixtures/README.md`.
