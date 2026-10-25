# Vinyl M2 implementation

Base: `409f225fce3fd7dcca102b020de3358be56994d7`. Scope is the existing M2 roadmap and Hemanth's instruction to connect and test the real catalogue/Artist flow in Colosseum.

## Tasks

1. Extend the existing helper and native transport for artist search/read/releases/videos/identity, explicit completeness/errors, cancellation and a portable runtime contract. Test malformed/partial/ambiguous responses before live use.
2. Add a native catalogue facade over existing local source and provider, retaining local import/playback and canonical Collection ownership. Adapt the accepted Artist bridge where authoritative MusicBrainz identity exists; keep provider identities distinct when mapping is absent.
3. Bind provider-neutral online search and real artist/album/edition/media projections to the existing world and Artist page. Preserve local browsing and loaded playback while catalogue channels load or fail. Use cached metadata only as an explicit offline view.
4. Build affected native consumers, run focused contracts, then exercise real search → Artist → tabs → Album in the actual Main.qml app, plus narrow/offline/profile and local-listening regressions. Inspect captures and record exact gaps. Fresh receiving review, fix important findings, commit and push the development branch.

## Review focus

Provider/source IDs must never become canonical MBIDs merely by UUID shape. No signed stream URLs in durable snapshots/cache. Profile and latest-request fences apply to every channel. A truncated shelf must not be labelled complete. Missing dependencies/providers preserve local use. Do not replace Collection, session, history or playback owners with donor in-memory ports.

## Progress

- Inspected M1 clean branch, current provider allowlist, helper artist operations and composed-service interfaces. Existing ArtistServiceBridge is canonical-MBID-only, so provider-only artists require an explicit provider projection until authoritative mapping is available.
- Ruling: YouTube Music remains the primary catalogue; MusicBrainz supplies optional identity/group/version enrichment. Use the accepted ArtistServiceBridge with current Collection ports instead of the donor factory's standalone personal bytes and playback ownership. Canonical recording/edition commands fail closed without an authoritative mapping; provider albums use the existing session. Cost if wrong: an unmapped canonical command remains unavailable rather than selecting a guessed recording.
- Ruling: artist saves retain the stable provider key while MusicBrainz IDs are external metadata. This preserves an early save across asynchronous enrichment. Cost if wrong: separate provider channels can remain separate saved entries until an explicit identity reconciliation is designed.
- Portable Windows runtime prepared and import-tested: official Python 3.14.8 archive digest plus seven hash-locked wheels; no PATH Python fallback in production transport.
- Provider baseline contracts demonstrated 4 failures/errors on pre-M2 source, then passed; further diacritic/credit alignment regressions demonstrated red before fixes. Existing helper tests pass with their optional live-smoke skip.
- First actual-app live run passed real search, Artist tabs, all 317 canonical group pages, exact provider version load, narrow/unknown artists and uninterrupted local Nas playback. Offline restart exposed a synchronous FailedToStart completion race; a native facade regression reproduced the lost completion before the launch was deferred.
- Fresh Astra receiving review reported three Important findings: unstable save key, misaligned credit IDs, and dropped related-artist identity. Each has a reproduced regression and an authored fix. Affected native consumers were rebuilt and the app relinked; all nine focused targets and both helper suites passed (one optional helper live-smoke skipped).
- Hemanth's selected-state follow-up was checked against the existing world tab bars and shared focus primitives. Artist tabs now use the gold selected pill, dark captions and a correctly spaced outside focus ring. All 41 world QML cases pass. Real and cached catalogue journeys on the final build passed; final selected Albums and Lifafa captures were inspected.
- A live receiving repeat hit an optional MusicBrainz timeout; the failed receipt remains. The following repeat passed enrichment and all canonical pages. The native suite separately verifies that the primary catalogue survives optional identity failure.
- Final exact-build receiving passed all eight app phases: live/cached catalogue, local tagged-library record/restore/isolation with helper disabled, and playback queue/history record/restore/empty. Evidence and remaining M3/distribution boundaries are recorded in the M2 receiving report and source manifest.
