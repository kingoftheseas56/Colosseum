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

} // namespace VaultBrowseEmpty
