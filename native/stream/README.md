# Smart source picker (Arc 81)

A pure Qt Core/C++17 library: parse → trust → corpus statistics → score → rank/pick.
It contains no network, QML, UI state or playback calls. No existing Sources screen
or player behavior is changed by building this library.

## Use

Convert each raw add-on JSON object or existing `AddonClient`/`Torrentio` sheet row
with `stream_from_json`, retaining its input index and add-on installed-order priority.
Set `TrustOptions` with movie/series identity, expected title/aliases, year, season,
episode and release date. Set `ScoreOptions` with preferred language prefixes,
runtime and any bandwidth constraint. `dolby_vision_supported` defaults to `false`;
set it only when the receiving playback stack is known to support DV.
`ScoreOptions::target_resolution` defaults to `P1080`; `UHD` selects 4K, then 1080p, then 720p, with every other guard unchanged.

The application uses `SmartPlayBridge::pick(rows, quality, context)` to retain the input
row index and return a short selection reason. Context carries the requested title,
episode and year, available `metadata` (`released`, `runtime`, `aliases`, `sourceId`),
and existing preferred audio-language codes. This adapter supplies the library's
existing trust and scoring inputs; it owns no settings, network calls or playback.

`SourceScorer::evaluate` returns:

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
   beat a 1080p pack. A batch torrent with a valid `fileIdx` and a selected filename
   identifying exactly the requested episode is a single for this policy. A range,
   wrong episode/season, missing file selection or ambiguous filename remains a pack.
   `evaluate` propagates expected season/episode from trust context; direct
   `rankAndPick` callers must supply them in `ScoreOptions`.
3. Prefer good 1080p, then 720p, then 4K, then SD. Within that resolution pool,
   unsupported DV-only releases are fallback-only. DV with HDR10 fallback (including
   explicit DV Hybrid metadata) scores as HDR10; DV-only has no HDR bonus. Enabling
   `dolby_vision_supported` restores Harbor's DV points and normal eligibility.
4. Prefer a streaming-friendly size/bitrate band within that pool. This changes
   only the primary selection, not numeric scores, full-list ordering or per-tier
   bests. Zero-delta `streaming-size` reasons explain the band. Larger candidates
   remain a fallback rather than being removed.
5. Within the resulting band, prefer non-French-led metadata unless French is
   preferred, then non-upscaled rows, then source quality (disc/WEB above unknown,
   above HDTV/DVD/SDTV), then healthy torrents within that source-quality pool.
   Each preference falls back if no matching row exists. The source gate precedes
   health: a heavily seeded HDTV capture does not displace an in-band BluRay/WEB
   encode merely because that encode has fewer seeders. An out-of-band premium
   source cannot displace an in-band capture.
6. Choose the highest score within the resulting pool. Dual audio breaks exact
   score ties, followed by stable add-on priority/return order/input order.

The tunable constants in `SourceScorer.cpp` use decimal GB (1,000,000,000 bytes):

| Resolution | Film size cap | Episode size cap | Bitrate ceiling |
|---|---:|---:|---:|
| 1080p | 15 GB | 3 GB | 20 Mbps |
| 720p | 6 GB | 1.5 GB | 8 Mbps |
| 4K | 30 GB | 6 GB | 40 Mbps |
| SD/480p | 3 GB | 1 GB | 5 Mbps |

When positive runtime is supplied, a known-size row must satisfy both size and
estimated average bitrate (`bytes * 8 / seconds / 1,000,000`). Without runtime,
only the size cap applies. Known-friendly rows beat unknown-size rows, which beat
known oversized rows. Unknown runtime/size is not fabricated. This is a starting
streaming policy, not a measurement of swarm throughput or peak bitrate.

Bare two-letter language codes require clear tag context; storage units such as
`6.45 GB` and ordinary title words do not establish an audio language. Flags remain
language evidence. The preferred-language bonus is +12 for primary/only audio,
but +3 (`preferred-language-secondary`) when multi-dub metadata indicates another
language leads. These are metadata heuristics, not verified track ordering.

French-led markers (`VOSTFR`, `VOST`, `VF`, `VFF`, `VFQ`, `TRUEFRENCH`, `FRENCH`)
are retained as `ParsedStream::french_led`. Subtitle flags alongside Multi Subs,
adjacent subtitle flag lines and explicit subtitle metadata go into
`subtitle_languages`, not `audio_languages`; independent explicit audio names
remain audio evidence. Subtitle-only English does not earn an audio bonus.
French-led rows may still be primary when French is preferred or no alternative
survives the preceding gates.

Round three intentionally changes **numeric ranking**, not only primary selection:
- Seeders now contribute `min(cap, weight * log2(1 + seeders))`. `ScoreOptions`
  exposes `healthy_seeders = 20`, `seeder_log_weight = 3.0`, and
  `seeder_bonus_cap = 24.0`. Negative weights/caps clamp to zero; nonfinite values
  use defaults. The healthy floor is primary-only; direct streams do not require
  a torrent swarm, while missing torrent seeders do not establish health.
- Explicit AI/upscale/conversion/Topaz/Gigapixel metadata receives a -15 penalty
  and is primary fallback-only when a non-upscaled alternative exists in-band.
  Full selected-file paths are inspected. Plain regrade, ordinary word substrings
  and the film title A.I. Artificial Intelligence are negative controls.
- A narrow `suspected-preHD-TV-upscale` -15 signal applies to pre-2000 UHD WEBRip
  series with episode context, using parsed year or context release date. Movies,
  modern series, discs and explicit remaster/restoration/film-scan markers are
  exempt. This is suspicion from metadata, not proof of native resolution.

Zero-delta source-quality, French-deferral and torrent-health reasons explain the
primary policy without silently changing scores. Seed counts and source labels
are unverified provider metadata; the library never probes a swarm or media file.

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
