# Vinyl recommendation reference study

Date: 2026-10-22. Source inspection and planning only; no reference app was installed or runtime-qualified, no API availability/coverage benchmark was run, and no product code changed.

Colosseum baseline: `d1086e74afd4bbb01d3823fb696ac9e09260e826`, `codex/vinyl-production-flow`. Current task prioritizes artist onboarding, catalogue enrichment, recommendations and Home before further Tankorent work.

## Decision

Adapt Troi's independent candidate streams, blending and identity discipline, and Music Assistant's factual library shelves. Extend Vinyl's existing C++ recommendation backend and native store adapters. Keep YouTube Music as the primary browse/playable catalogue. Neither reference becomes the authority for personal data, identity or ranking.

## Troi: inspected implementation

Pinned revision: [`044398b5b6c6c99fc301f3417e62570b2a408a40`](https://github.com/metabrainz/troi-recommendation-playground/tree/044398b5b6c6c99fc301f3417e62570b2a408a40). Inspected a shallow source checkout outside Colosseum. [GPL version 2 license text](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/LICENSE).

| Observation | Reuse in Vinyl | Boundary |
| --- | --- | --- |
| Artist streams collect seed-artist and similar-artist recordings, with explicit per-source bounds and messages for thin results. [Artist element](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/patches/lb_radio_classes/artist.py). | Independent seed-release and adjacent-artist generators, each retaining seed/source evidence. | Missing similarity is missing evidence, not evidence that a regional artist is unimportant. |
| The radio patch builds a graph of sources, recording lookup, feedback filtering, blending and playlist output. [Patch](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/patches/lb_radio.py). | Separate collection of candidates, ranking, presentation and playback resolution. | User-specific ListenBrainz statistics/recommendation terms require a user identity. They are not anonymous personal recommendations for a Colosseum profile. |
| Round-robin and weighted blending prevent one source from wholly owning the output. The weighted implementation uses random draws and an artist-credit limit. [Blend](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/patches/lb_radio_classes/blend.py). | Versioned source budgets plus diversity after merging, with deterministic stable ties for our first implementation. | Do not copy its random ordering, numeric quotas, or popularity-percentile modes as an established optimum for Vinyl. |
| Recording-ID duplicate filters preserve input order; artist-credit filtering limits dominance. [Filters](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/filters.py). | Merge repeated contributions through BE1; retain one card per confirmed recording/release group. | Card deduplication must never remove deliberate duplicate playlist/queue occurrences. Distinct versions remain distinct unless equivalence is verified. |
| Artist recording search calls a ListenBrainz artist-radio endpoint using an artist MBID; its authorization header is optional in the source. [Service](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/recording_search_service.py). | Optional public population-evidence adapter for verified mappings. | Source support is not a live endpoint/auth/latency/Indian-coverage proof. Qualify that adapter before enabling it. |
| Its HTTP helper has timeout/retry/rate-limit logic. [HTTP helper](https://github.com/metabrainz/troi-recommendation-playground/blob/044398b5b6c6c99fc301f3417e62570b2a408a40/troi/http_request.py). | Background requests, bounded retries and explicit partial results. | Do not inherit a 60-second HTTP timeout or blocking sleeps on the QML thread. Honor the current [ListenBrainz rate-limit and User-Agent contract](https://listenbrainz.readthedocs.io/en/latest/users/api/index.html). |

Use these as independently implemented patterns. Embedding Troi would add a second Python recommendation runtime and MBID-first output that still needs YouTube resolution. Direct code reuse would require a separate license/packaging decision; none is proposed here.

## Music Assistant: received and directly checked

Luna xhigh performed the bounded source/documentation review. Root directly inspected the library provider, matching parser and license at [`7d5a0245660c749b7908479fe89873682040d536`](https://github.com/music-assistant/server/tree/7d5a0245660c749b7908479fe89873682040d536). The pinned server license is [Apache 2.0](https://github.com/music-assistant/server/blob/7d5a0245660c749b7908479fe89873682040d536/LICENSE).

| Observation | Reuse in Vinyl | Boundary |
| --- | --- | --- |
| Built-in recommendations query the library for bounded rows with stable IDs. [Provider](https://github.com/music-assistant/server/blob/7d5a0245660c749b7908479fe89873682040d536/music_assistant/providers/recommendations/__init__.py). | Query existing Collection and History/Activity owners; render existing Vinyl shelves. | These rows rediscover owned/saved music; they do not discover music outside the library. |
| Forgotten items require prior playback and sort by oldest last play. Lowest-play-count rows implement never/rarely played. [Provider](https://github.com/music-assistant/server/blob/7d5a0245660c749b7908479fe89873682040d536/music_assistant/providers/recommendations/__init__.py). | Distinguish previously played rediscovery from low-count items. | Our retained history is not necessarily complete. Use labels such as "Less played" over retained evidence; do not infer lifetime "never played" from absence. |
| The official [library recommendations documentation](https://www.music-assistant.io/plugins/library-recommendations/) separates library rows from external music discovery. | Separate factual rows and ranked external candidates in the Home read model, with independent state. | A library-only Home is a useful foundation, not completion of the requested personalized catalogue experience. |
| The matching parser prefers external identity routes, then bounded provider searches; it distinguishes incomplete search from confirmed misses. [Parser](https://github.com/music-assistant/server/blob/7d5a0245660c749b7908479fe89873682040d536/music_assistant/providers/lastfm_recommendations/parsers.py). | Cache resolution outcomes with provenance and explicit unresolved/ambiguous/unavailable states. | Vinyl must retain its stricter artist/version checks; the reference's permissive missing-artist fallback is not our recording-identity rule. |
| A separate [Last.fm provider](https://www.music-assistant.io/metadata-providers/lastfm-recommendations/) supplies external recommendations. | Evidence-provider interface and cache invalidation pattern only. | Do not copy its key, infer API rights from the repository license, or silently send local listening identifiers to Last.fm. Last.fm is not a selected dependency for this plan. |

Row toggles, autoplay and sonic analysis are optional later lessons. They are not prerequisites for artist onboarding and the initial Home. No reference UI replaces our QML.

## Existing Vinyl reuse map

| Existing owner / code | Live state at baseline | Planned adaptation |
| --- | --- | --- |
| `ProfilePreferencesStore`: `setVinylTaste`, `vinylFeedbackSnapshot` | Profile persistence and feedback validation exist. Picker accepts library-projected IDs only. | Preserve IDs; persist typed artist selections atomically and migrate existing answers without asking again. |
| `VinylTastePicker.qml`, `VinylLiveWorldData.qml` | Picker exists but opens only on explicit reset; offers library artists; picks do not influence Home. | First-Home state machine, remote artist search, explicit staged edits and cancellation. |
| `VinylProvider`, `VinylCatalogueSource`, `provider.py` | YouTube search/discovery, artist mapping/artwork, request fencing and catalogue response cache exist. | New taste/candidate lanes; identity/enrichment results outside the currently open artist-page state. Cache bounds and expiry need strengthening. |
| BE1 candidate contract | Provenance-bearing candidate merging, canonical entities. | Retain it; add typed provider identities without presenting generated YouTube UUIDs as MusicBrainz IDs. |
| BE2 taste + persistence | Explicit feedback, behavioural observations and suppression exist. | Profile host adapter; preserve onboarding, save and explicit feedback as separate evidence kinds. |
| BE derivation + BE3 ranker | Versioned derivation, exposure log, deterministic ranking/diversity exist in the adopted backend target. No live Home consumer. | Feed truthful evidence; extend identity support and seed-affinity projection; publish reasons and configuration version. |
| BF1 context graph, BG1 types, BG2 discovery | Typed relationships/catalogue/enrichment contracts exist. BG2 is not a live data-fetch service. | Feed verified artist/release/recording facts and freshness/provenance, with unknown fields left unknown. |
| Collection, Activity, History, Progress | Collection owns saves/playlists; Activity owns durable facts; ConsumptionHistoryBridge already projects local Vinyl facts into History; Progress owns resume. | Reuse all four. BA4 currently qualifies playback transiently and is reset on retirement; it is not a retained recommendation history database. |
| Existing Home/Discover/Library QML, routing and session | Actual app flows exist. Home has factual resume/saved shelves, Discover public shelves. | Add personalized rows to the Home projection and keep canonical route/queue actions. |

## Important implementation findings

1. Canonical recommendation references currently require MusicBrainz IDs. The live provider's deterministic UUID IDs are application IDs, not verified MBIDs. Extend the recommendation identity contract explicitly; do not cast one into the other.
2. Artist enrichment currently depends on the open artist page and may enumerate a whole discography. Home needs bounded, page-independent requests; onboarding must not wait for full-discography mapping.
3. Raw popularity scores from different services cannot be added directly. BE3 already supports versioned calibration and one support award per generator.
4. Existing history does not retain all BA4 semantic events. Completion/early-skip/replay features need trustworthy projections or scoped Activity extensions; stop/error/seek events must not fabricate dislike or completion.
5. Inferred preferences and exposure must obey history retention/reset. Explicit artist choices and feedback remain user-controlled preferences when history is disabled. No private history upload is required.

## Proposed execution

See [design](../plans/2026-10-22-vinyl-personalized-home-design.md) and [implementation plan](../plans/2026-10-22-vinyl-personalized-home.md). Plans use the repository's tracked `docs/plans/` location because `docs/superpowers/` is ignored. No provider route was live-qualified by this study. The plan places source-capability and cross-market identity checks before claims of working recommendations.
