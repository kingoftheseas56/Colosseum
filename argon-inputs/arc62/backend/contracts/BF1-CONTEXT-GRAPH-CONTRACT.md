# Vinyl Backend BF1 - Context Graph Contract

Status: BF1 contract frozen; BF1-A identity-safe graph core implementation checkpoint
Date: 2026-10-06
Owner: Arc 62 Vinyl BF1

## Purpose

BF1 defines a presentation-independent graph of canonical musical context.

BA0 owns canonical identity. BG1 owns catalogue query shapes. BF1 owns normalized
relationships between canonical entities after catalogue/provider evidence has been
translated into Vinyl's domain vocabulary.

The graph is not a UI model. Credits, Connections, Versions, or any other current
Listening Room section are consumers that may change without changing BF1.

## Laws

1. Every graph node is keyed by BA0 CanonicalEntityRef.
2. Entity kind is part of identity. Work and Recording identities never collapse,
   even if their UUID payloads are textually identical.
3. Relationship kind and direction are explicit data, never inferred from display text.
4. One entity pair may legally have multiple relationship kinds.
5. Artist credited names and role labels are metadata. ArtistMbid remains identity.
6. A Release is a concrete version beneath a Release Group; multiple Release MBIDs
   never collapse into one Release MBID merely because they share a Release Group.
7. Evidence/provenance survives normalization and may be plural.
8. Equivalent duplicate edges normalize deterministically without discarding evidence.
9. Unknown upstream relationship types remain unsupported/opaque. BF1 never maps an
   unknown token to the nearest known semantic kind.
10. Relationship absence means unknown/not supplied, not proof that no relationship exists.

## Context node

ContextNode contains exactly one CanonicalEntityRef.

Display title, artwork, biography, aliases, UI order, and presentation section are
not node identity and do not belong in the node key.

## Relationship edge

A normalized ContextRelationship contains:

- source CanonicalEntityRef;
- target CanonicalEntityRef;
- RelationshipKind;
- RelationshipDirectionality;
- zero or more structured artist credits/roles;
- zero or more provenance/evidence records;
- an opaque upstream relationship token when the kind is unsupported.

Directed edges preserve source and target exactly. Bidirectional edges still retain
their normalized endpoints; bidirectionality means consumers may traverse either way,
not that BF1 invents a second semantic edge.

## Relationship kinds

The initial known vocabulary is PerformanceOf, ArtistCredit, Performer, Producer,
ReleaseVersion, RelatedRecording, RelatedWork, and Unsupported.

This vocabulary is intentionally smaller than MusicBrainz's full relationship-type
catalogue. Future known kinds may be added deliberately. Unrecognized source tokens
must use Unsupported and retain the raw source token.

## Credits and roles

ArtistRoleCredit always contains a required ArtistMbid, an optional credited/display
name, and zero or more CreditRole values.

The initial role vocabulary includes primary artist, featured artist, performer,
producer, composer, lyricist, engineer, and other.

The credited name may differ from the canonical artist name. It can never replace,
derive, or fabricate the ArtistMbid.

## Recording and Work

Recording to Work uses PerformanceOf when evidence supports that relation.

A Work is the composition/work identity. A Recording is a recorded performance.
They remain separate BA0 entity kinds in every graph operation, persistence shape,
normalization key, and traversal result.

## Recording and artists

Recording relationships may express artist credit, performer, and producer context
when the source evidence supports those semantics.

BF1 does not infer performer from generic artist credit, producer from performer,
or any other role by proximity or display wording.

## Release Group and Release versions

ReleaseVersion is directed from Release Group to Release.

Each concrete Release keeps its own ReleaseMbid. Several Releases may sit beneath one
ReleaseGroupMbid without becoming duplicate logical releases or duplicate nodes.

Edition ordering, filtering, and retrieval remain BG1 concerns. BF1 only models the
canonical contextual relation once supplied.

## Related works and recordings

RelatedRecording and RelatedWork are explicit contextual relations only when the
underlying evidence supports the relationship.

BF1 does not guess cover, sample, remix, or live semantics from title text.

## Provenance and evidence

Every normalized edge may carry multiple evidence records.

An evidence record retains provider/source identity when one exists, optional provider
item identity when one exists, the upstream relationship token/type, and an optional
source assertion/evidence key.

Provider identity is provenance only. It never changes either canonical endpoint.

Normalization merges equivalent evidence records deterministically and never drops
distinct provenance merely because two providers assert the same relationship.

## Deterministic edge identity and normalization

Known-edge semantic identity is source canonical ref + target canonical ref +
relationship kind + directionality + normalized structured credit/role payload.

Evidence is not part of semantic edge identity. Equivalent edges merge their evidence.

Directed reverse edges are not equivalent. Two different relationship kinds between
the same endpoints are not equivalent.

Normalization ordering uses stable enum order and canonical entity kind plus normalized
MBID text as tie-breakers. Input/provider order must not affect normalized output.

## Unsupported relationships

An upstream relationship type that BF1 does not understand is represented as
RelationshipKind::Unsupported while retaining the opaque raw source token.

It must not be converted to a known kind by fuzzy wording, substring matching,
provider order, or UI expectations.

Unsupported edges may still preserve canonical endpoints and provenance so future BF1
versions can reinterpret them deliberately.

## Boundaries

BF1 does not implement annotations or lyric meaning, related YouTube/video mapping,
recommendation candidate generation or ranking, live MusicBrainz fetches, Arc 65 corpus
changes, QML sections/rails/tabs/cards, artwork retrieval, or source/playback resolution.

## Prototype and guardrail checkpoints

Path: backend/bf1-context-graph-prototype/

The reference prototype uses Qt Core plus BA0's existing domain types.

- BF1-A: node/edge/direction/role identity core and acceptance invariants 1-4.
- BF1-B: release-version/provenance/deterministic normalization/unsupported handling
  and acceptance invariants 5-8.
- BF1-C exists only if a failed or deferred qualification step requires a separate
  Qt build/harness turn.

Production Colosseum adoption is out of scope.

## Acceptance contract

The completed BF1 native harness must prove:

1. the same entity may participate in multiple relationship kinds;
2. relationship direction is preserved;
3. Work identity never collapses into Recording identity;
4. credited artist names never replace ArtistMbid;
5. multiple Releases remain versions beneath one ReleaseGroupMbid;
6. provenance survives normalization;
7. duplicate equivalent edges normalize deterministically;
8. an unsupported relationship type does not fabricate a known one.

BF1-A owns 1-4. BF1-B owns 5-8.
