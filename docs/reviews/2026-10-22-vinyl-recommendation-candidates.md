# Vinyl catalogue enrichment and candidate transports

Date: 2026-10-22. Task 3 only. Base: `c6a688d2a`; branch `codex/vinyl-production-flow`.

## Implemented

The existing Python helper and native catalogue facade now expose `artistCandidates`, `recommendationIdentity`, `recordingMatch` and `populationCandidates`. They use the existing provider metadata cache and Task 2 typed identity adapter. No new library, player, profile or identity store was introduced.

- Artist candidates come from one anonymous YouTube Music artist response: at most 20 albums, 20 singles, 10 top recordings and 10 related artists. No discography or playlist pagination is performed. The response retains the selected provider identity, source, timestamp, completeness and per-shelf capabilities. `seedArtistRef` records shelf provenance separately from actual artist credits. Missing credits, genres, languages and exact release dates are not invented; year/type, supplied dates, structured artist references, cover art, artist image and biography are retained where returned.
- `get_artist` returns a **subscription channel ID different from the input browse ID**. Its documented field must not replace the selected identity. Exposed UC/MPAD release-shelf owners are checked against the requested browse ID. This distinction was caught by real responses and is now covered by a regression. See the [ytmusicapi artist contract](https://ytmusicapi.readthedocs.io/en/latest/reference/browsing.html#ytmusicapi.YTMusic.get_artist).
- Bounded MusicBrainz lookup never treats a unique name as proof. Verification requires a matching external channel relation or corroborating dated release groups; conflicting matches remain ambiguous. Partial identity searches do not establish a mapping. It makes one search and at most two detail lookups, without discography enumeration. Optional service failure leaves provider-native candidates intact.
- Recording matching requires title, structured provider artist identities, duration within two seconds and matching version evidence. Covers, live/remastered variants, explicit other versions, conflicting video IDs and incomplete search pages cannot silently become an exact playable match. Native validation checks the result against the request again. Admitted matches register through the existing source registry, preserving string artist names for the existing torrent intent and YouTube fallback path.
- Returned album destinations now open through the existing album route without requiring a previous search/open to register them.
- ListenBrainz's anonymous artist-radio route was checked against its [official API contract](https://listenbrainz.readthedocs.io/en/latest/users/api/core.html) and real responses. The optional adapter is enabled for explicit canonical artist references. It returns validated canonical recording evidence with **no playable destination**. A source failure remains unavailable; no token or listening-history upload is involved.

## Transport and cache policy

Home work has two subprocess slots and an eight-second deadline from admission, including queue time. Identity requests can reach that deadline when optional services are slow; unavailable identity is preferable to delaying provider-native candidates. Retry-After seconds defer another process for that service, with a bounded 1–3600 second window and no immediate retry loop.

Version 2 cache envelopes live in the existing `vinyl/catalogue` cache, with request-key validation and these TTLs: verified identity seven days, artist/release candidates six hours, population evidence 24 hours, confirmed complete no-match one hour. Incomplete empty/partial replies never establish negative cache entries. Positive partial shelves can be cached. Unavailable sources can return validated stale metadata with explicit stale/error markers. Resolved audio URLs/headers remain excluded. Cache writes enforce a 4 MiB entry limit, 128 MiB total budget and 4096-file cap; recommendation envelopes also have a stricter 256 KiB native admission bound.

## Bounded live capability matrix

Anonymous YouTube Music, region IN. Times below are one helper-process sample, including startup; they are observations, not service guarantees. Every real artist returned 20 own-shelf releases, five top recordings and ten related artists (35 total). Bounded shelves are explicitly incomplete.

| Selected artist | Exact YouTube Music browse ID | Candidate status / time | Canonical mapping outcome / time |
|---|---|---|---|
| Nas | `UCPoQYATXIYvN5WB0c4f6jfQ` | ready, 35 / 1614 ms | ambiguous, incomplete search / 2594 ms |
| Ilaiyaraaja | `UCwzzSiogpjsYBMoyVcwDXwQ` | ready, 35 / 1665 ms | unresolved, insufficient corroboration / 3488 ms |
| S. P. Balasubrahmanyam | `UCl4iPtukwe7m0kIxUMskkgA` | ready, 35 / 2016 ms | eight-second deadline / 8018 ms |
| John Williams, first search result | `UCfv8TEQ44ej9uPGF-SNsDBA` | ready, 35 / 2366 ms | ambiguous, incomplete search / 3130 ms |
| John Williams, second search result | `UCZH8NkgXeliBVbpaYY2PoNQ` | ready, 35 / 1965 ms | unavailable, rate limited / 2087 ms |
| Provider-only **offline fixture** | `UC` + 22 `A` characters | ready; releases/top recording/related fixture retained with MusicBrainz unavailable | deliberately unresolved; not live coverage |

No canonical mapping is claimed for these selected YouTube identities. The two same-name browse IDs remain distinct; no names were split or merged to invent identities.

For the independent ListenBrainz transport gate, Nas's canonical artist ID `cfbc0924-0035-4d6c-8197-f024653af823` was read from the [actual MusicBrainz artist record](https://musicbrainz.org/artist/cfbc0924-0035-4d6c-8197-f024653af823), **not assigned to the selected YouTube seed**. The route returned four artist buckets in 740 ms, then the production adapter returned 25 validated canonical recording candidates in 1058 ms. Coverage varied between requests. Rows lacked recording titles and playable provider identities; the adapter did not fabricate either.

A real `N.Y. State of Mind` match request used the Illmatic album track's supplied Nas browse ID and 294-second duration. Search returned ambiguous evidence with a continuation in 1778 ms; it correctly produced no verified video. This qualifies rejection behavior, not successful new recording resolution or playback.

## Verification and receiving

- New Python contracts: **14 passed**, using injected YouTube/MusicBrainz/ListenBrainz transports. Existing provider/catalogue/discovery/artwork Python coverage: **50 passed, one optional legacy live smoke skipped**. Tests were run with the bundled helper runtime; the first broader system-Python attempt lacked ytmusicapi and was rerun with the correct runtime.
- **20/20 focused CTest suites passed**: existing BE1/BE2/derivation/BE3 contracts, typed recommendation identity, preference snapshots/selections, native facade, catalogue and native QML onboarding/Home/library/search/playback/queue regressions. The final changed catalogue executable was rebuilt and rerun after the last receiving fixes: **34 cases passed, one opt-in live case skipped** in the deterministic run.
- That opt-in case was then run separately against the real helper using the production `VinylProvider` and `VinylCatalogueSource`: all three Nas/Ilaiyaraaja/SPB requests returned typed, seed-bound candidates, then opened a returned album and received a nonempty real tracklist. Final candidate timings were **1769 / 1549 / 1572 ms**; the full three-artist journey passed in 10.8 seconds.
- Initial RED checks exposed missing operations, normalization, cache policy and concurrency limits. Luna xhigh supplied bounded Python/native fixture work and a read-only receiving review. Receiving fixes cover album routing, request-bound recording validation, version rejection, malformed nested artist IDs, and preserving artist names for existing playback intents. Root reviewed and accepted the source and actual test results.

Machine-local evidence: `D:/Colosseum-Vinyl-Candidates-20261022/`, including provider/cache RED logs, `provider-green.log`, `provider-regression-all.log`, `regression-final.log`, `native-final.log`, `live-matrix.json`, `live-native-final.out`, recording and ListenBrainz responses, and build logs. Raw public provider responses and machine paths are not committed test fixtures.

## Remaining boundary

This completes candidate/enrichment transport preparation. Automatic refresh from committed selections, cross-source candidate composition, deterministic ranking and personalized Home rows are **Task 4 onward**. The visible inspection app remains the preceding onboarding build, responsive and open; this slice does not claim new Home UI, physical pointer qualification, new remote playback success, a main-branch merge or a release. Existing Tankorent/remote-seek acceptance gaps are unchanged.
