# Vinyl recommendation identities receiving record

Date: 2026-10-22. Task 2 only. Base: `ad7d7b8c1`, branch `codex/vinyl-production-flow`.

## Implemented

- Extended the adopted BE1 candidate, BE2 feedback, derivation/exposure and BE3 ranker owners with `RecommendationEntityRef`: entity kind plus a canonical reference or a provider/item reference. Stable keys and equality preserve both namespace and kind. A provider ID that looks like a UUID never becomes a canonical reference automatically.
- Added the pure `VinylRecommendationIdentity::fromCatalogue` adapter. It accepts existing provider references and the first slice's onboarding references, keeps actual catalogue destinations, and retains structured collaborators separately. Joined display names are not split into invented identities.
- A canonical mapping requires a valid MBID and supplied evidence bound to provider, opaque ID, entity kind, supported method and timestamp. Recording mappings additionally require exact artist/version/duration assertions. Names, top-level application UUIDs and an unqualified `mappingStatus: verified` cannot establish equivalence. Two qualified mappings can share a canonical recommendation key while retaining their distinct playable destinations.
- Typed recording candidates default to nonplayable, including mapped canonical recordings. Playback requires a separately qualified destination. The explicit legacy MBID factory preserves its previous recording behavior and kind restrictions; new code should use the typed factory and the adapter's destination playability.
- Provider artist/release-group context participates in diversity without adding canonical graph edges. Verified canonical context takes precedence over opaque fallback context. Duplicate candidate contributions are deduplicated; generator support retains the existing once-per-generator scoring rule.
- Typed Release candidates and exposure records agree about album eligibility. Provider and canonical artist feedback, suppression, behavior and exposure remain distinct when their raw IDs happen to match.

## Serialization and mapping contract

Feedback writer version is **3**, because the adopted backend was already writing version 2. Readers retain canonical versions 1 and 2; migration happens on a later successful write. Typed exposure records advertise schema 2. Unknown versions, malformed data and foreign-profile input fail without replacing saved feedback through the existing native session/preferences port.

The existing domain wire format names the provider namespace `providerId` and its opaque identifier `itemId` inside the returned `ref`. Catalogue/onboarding inputs instead use `provider` for the namespace and `providerId` for the opaque identifier; this adapter performs that conversion explicitly.

Optional input `mapping` contains `status`, `provider`, `providerId`, `kind`, `mbid`, `method` (`external-id` or `corroborated-metadata`) and numeric `fetchedAtMs`. Recording mappings also require strict boolean `artistMatched`, `versionMatched`, `durationMatched`. Optional `playbackMatch` has the same provider binding, exact-match booleans, verified status, method and timestamp; canonical mapping alone does not establish playback readiness. These are validation contracts for qualified catalogue evidence, not a new resolver or proof that a remote service currently supplies that evidence.

## Verification

New behavior was exercised through failing tests before implementation: six missing-contract failures, the real onboarding reference/destination mismatch, all three receiving findings, and canonical-over-provider diversity priority. All were repaired with regressions. The five existing canonical recommendation suites were run, not merely compiled.

**20/20 focused CTest suites passed** in 110.38 seconds: all six recommendation suites, production native facade, existing preferences/catalogue and native QML onboarding/Home/library/search/playback suites. Targets built include the six recommendation executables, existing native session facade, native QML bridge and catalogue source test. The facade test uses the production `VinylBackendSession` feedback API and verifies invalid/future/foreign snapshots preserve the existing preferences-port bytes.

Receiving review found and closed:

1. Typed canonical recordings bypassing the playback gate.
2. Verified Release mappings becoming invalid BE1 candidates.
3. Provider Release candidates being rejected by exposure validation.

External machine-local evidence is under `D:/Colosseum-Vinyl-Identities-20261022/`: `red.log`, `edge-red.log`, `review-red.log`, `diversity-red.log`, build logs and `regression-final.log`. Test data uses fixtures and injected catalogue records; no fresh live canonical mapping transport is claimed.

## Remaining boundary

This slice prepares and tests the identity contracts. Task 3 still owns live mapping/enrichment and recommendation candidate transports; later tasks own ranking orchestration and Home presentation. No new store, Collection identity, queue occurrence or playlist identity was introduced. The open inspection app remains the preceding onboarding build; it was not restarted to imply new recommendation UI or live-provider qualification. Main merge and release remain separate.
