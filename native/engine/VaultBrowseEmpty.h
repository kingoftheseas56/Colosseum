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
// The fourth design cause ("a filter has excluded everything") is named here for completeness —
// VaultBrowseEmpty.qml still renders its copy — but classify() below NEVER produces it: no filter
// control has shipped on the Browse face (deferred to the parent design's later arc; the execution
// plan is explicit that inventing a live trigger for it here would be exactly the kind of
// unrequested product surface Slice 9 warns against).

#include <QString>

namespace VaultBrowseEmpty {

enum class Cause { None, NoRoots, EmptyFolder, AllAway };

// hasAnyRoots: at least one confirmed/synthetic non-hidden root exists (rootsDetail() non-empty).
// levelHasRows: the level's own browseAt() rows are non-empty — nothing to classify (Cause::None).
// levelAway: the level's owning root is currently away (VaultBrowseAway::ownerRootAway).
Cause classify(bool hasAnyRoots, bool levelHasRows, bool levelAway);

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
