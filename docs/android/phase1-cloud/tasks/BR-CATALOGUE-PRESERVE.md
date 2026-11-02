# BR-CATALOGUE-PRESERVE — Beta adds validated Manhwa filtering and richer manga shelves

Class E; priority Preserve/decision. Authorization/order comes from START.md, not classification alone.

## Why this matters
Preserves newer Tankoban shelf/Manhwa discovery and recovery when a cached publication predates the query. Replacing with Alpha would narrow discovery and can turn valid newer filters into empty results.

## Alpha behavior
MAL discoverPageFiltered accepts the older shelf set and genre/demographic facets; remote declaration/fallback lacks newer MAL filtered-page/format shelf handling.

## Beta behavior
Adds completed/publishing/short-completed/one-shot/decade shelves, validated manga/manhwa/manhua format filtering, membership queries that avoid duplicate classification rows, retryLanding and semantic fallback when older publications lack new lists.

## Actual owner/caller
Common native engine graph constructs/exposes MAL catalogue in main; Tankoban discovery calls MalCatalog filtered-page APIs, which use local catalogue or CatalogRemote/StaticCatalogueResolver remote publication/fallback route. These native owners are not in the desktop-only exclusion block.

## Donor evidence
native/engine/CatalogRemote.cpp:46-49; MalCatalog.cpp:732-786 discoverPageFiltered; MalCatalog.h:38-41 landing API; StaticCatalogueResolver.cpp:594-609 and :702-705 publication-page fallback. Corresponding old/new hunks retained in native__engine__*.diff.

## Beta evidence
native/engine/MalCatalog.cpp:732-822; MalCatalog.h:41; StaticCatalogueResolver.cpp:595-620,710-719; CatalogRemote.cpp:49

## Evidence layer
committed-alpha-beta

## Dependencies
MalCatalog fixed SQL allowlists/bound facet values; CatalogRemote query declaration; StaticCatalogueResolver publication revision and semantic fallback; existing Tankoban callers.

## Reuse/adaptation
preserve Beta; no Alpha catalogue backport

## Files and integration points
No implementation pending. Retain discoverPageFiltered/retryLanding and matching remote query/fallback contracts when editing discovery callers.

## Preserve
Keep validated format values, fixed SQL with bound parameters, manga-family shelf predicates, deterministic membership/order and older-publication semantic fallback.

## Bounded unit
No implementation unit for this preservation finding; catalogue/query/caller compatibility is one coupled constraint on future accepted changes.

## Risks
N/A: no transfer proposed; overwriting current owners would violate the stated preservation/exclusion boundary.

## Recommended change
Keep native format filter/fixed SQL and semantic fallback for older publications; no Alpha replacement.

## Existing tests
Catalogue/Manhwa query and remote fallback tests identified by shared test inventory; no new test execution or claim of live-provider acceptance.

## Required host checks
If later changed, check new shelves and format facets on local data, duplicate classifications, invalid filters and older publications falling through to semantic resolution; preserve legacy query results.

## Local device acceptance (pending)
Later Tankoban work should verify Manhwa/shelf/filter/retry journeys on the actual installed Android route; no fresh device check in audit.

## Recovery
No changes made. Future catalogue correction must retain canonical library/progress identities and cached data; revert query/caller code together without clearing user stores.

## Evidence confidence
high source/graph confidence; deeper excluded algorithms unexamined

## Unresolved
Provider/network availability and current catalogue publication contents were not newly queried; preservation conclusion is source-supported.

## Execution checklist
- [ ] Inspect the referenced production functions/callers and donor contract.
- [ ] Reproduce the specified gap with the smallest actual-owner fixture or deterministic production-function check; record command and result.
- [ ] Implement the bounded adaptation in the identified owners, preserving the stated invariants.
- [ ] Run the listed applicable regressions plus missing host cases; test desktop invariants and relevant stale/cancel/error paths.
- [ ] Review exact changed-file diff, update progress/evidence, commit explicit owned files and push.
- [ ] Leave physical acceptance pending and continue the next authorized unit.
