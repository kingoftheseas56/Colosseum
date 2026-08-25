#pragma once
// VaultBrowseEmpty — Vault Browse projection spine, Slice 9 (empty states + keyboard reach
// execution plan): which of the design's four distinct empty causes (locked design §4.5) applies
// to a browse level. Pure classification over facts the rest of the projection already computes —
// whether ANY confirmed/synthetic root exists (VaultLibrary::rootsDetail()), whether the level's
// OWN browseAt() rows are non-empty, and whether the level's owning root is currently away
// (VaultBrowseAway::ownerRootAway) — this file infers nothing new about the filesystem or the
// index; it only NAMES the cause so QML paints instead of guessing (the execution plan's own
// instruction: "the projection already knows which cause applies — key the component off the
// cause, do not infer it in QML"). Same "pure logic kit" spirit as VaultBrowseAway, one layer up
// (this one composes ON TOP of VaultBrowseAway's away verdict, never reimplements it).
//
// The fourth design cause ("a filter has excluded everything") was named here for
// completeness — VaultBrowseEmpty.qml renders its copy — but classify() could never produce
// it: no filter control had shipped on the Browse face. Vault ux uplift S13 IS that filter
// surface, so classify() now takes `filteredOut` (the caller's job to compute: the level's
// UNFILTERED rows are non-empty while the filtered projection is empty) and produces
// Cause::Filtered for the production trigger the component always waited for.

#include <QString>

namespace VaultBrowseEmpty {

enum class Cause { None, NoRoots, EmptyFolder, AllAway, Filtered };

// hasAnyRoots: at least one confirmed/synthetic non-hidden root exists (rootsDetail() non-empty).
// levelHasRows: the level's own (filtered, when a filter is active) browseAt() rows are non-empty
// — nothing to classify (Cause::None).
// levelAway: the level's owning root is currently away (VaultBrowseAway::ownerRootAway).
// filteredOut: the level HAS rows unfiltered; the active filter excluded them all (default
// false — the no-filter caller keeps the Slice 9 contract byte-for-byte).
Cause classify(bool hasAnyRoots, bool levelHasRows, bool levelAway, bool filteredOut = false);

// "none" | "noRoots" | "emptyFolder" | "allAway" — the QML-facing vocabulary
// (VaultBrowseEmpty.qml's `cause` property, the Lanista/Quick-Test string).
QString causeName(Cause cause);

// The "is this level away" combinator VaultLibrary::browseEmptyCause() actually uses.
// VaultBrowseAway::ownerRootAway reads the away flag off an EXISTING index row (a root-wide fact
// VaultIndex::markRootAway() only ever flips on rows that are already there) — a root that was
// NEVER scanned while present has no row to carry that flag at all, so the row-based check alone
// always reports "not away" for it even though it plainly is (found live driving the
// all-away-empty Lanista fixture, Slice 9 — a never-scanned away root read as "emptyFolder", not
// "allAway", before this combinator existed). `hasOwnerRoot`/`ownerDirectoryExists` mirror the
// same live QDir::exists() check VaultLibrary::rootsDetail()'s own `available` field already
// uses, so both signals agree with what the rail shows.
bool isLevelAway(bool indexSaysAway, bool hasOwnerRoot, bool ownerDirectoryExists);

} // namespace VaultBrowseEmpty
