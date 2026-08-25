#pragma once
// VaultBrowseAway — Vault Browse projection spine, Slice 6 (living tile states execution plan):
// which confirmed root owns a browse path, whether that root is currently away, and the durable-
// index fallback for a level whose owning root can no longer be walked at all.
//
// Pulled out of VaultLibrary into its own seam so this logic is unit-testable against a real
// VaultIndex + VaultConfig WITHOUT constructing the full VaultLibrary façade — VaultLibrary's
// constructor unconditionally builds a VaultWatcher (which in turn drags in the scanner/
// downloads-root/identifier dependency tree), unnecessary weight for logic that only reads
// VaultIndex rows and VaultConfig's root list. Pure Qt6::Core/Sql, no QObject of its own —
// same "pure logic kit" spirit as VaultKit, one layer up (VaultKit knows nothing of VaultIndex;
// this does, because away is an INDEX fact, not a filesystem fact).

#include <QString>
#include <QVariantList>

class VaultIndex;

namespace VaultBrowseAway {

// The confirmed/synthetic root whose path is a prefix of (or equal to) `path` — a show sentinel
// ("<parent>::show::<slug>", VaultKit::planBrowseLevel's own convention) is resolved via its
// recoverable parent path first, since it has no real filesystem path of its own. Empty when no
// confirmed root owns it. `roots` is VaultConfig::roots()'s own row shape.
QString ownerRootPath(const QVariantList& roots, const QString& path);

// True when the owning root's rows are currently marked away in the index. away is a ROOT-WIDE
// fact (VaultIndex::markRootAway() flips every row under one rootPath in one statement), so one
// representative row answers for the whole level.
bool ownerRootAway(VaultIndex* index, const QVariantList& roots, const QString& path);

// browseAt()'s fallback when VaultKit::planBrowseLevel can't walk `levelPath` (the owning root's
// directory is gone) but the durable index still remembers what was there. One row per direct
// child group under `levelPath`, ALWAYS away, unopenable — the locked design's "nothing
// disappears" contract (§4.7) for a vanished drive. A structural simplification versus the live
// walker: films collapse the same way (one file, one tile), but show/season fidelity is not
// re-derived offline (the parent ownership arc's business) — a multi-file group folds to a plain
// folder tile instead of a show/season tile. Row shape matches VaultLibrary::browseAt()'s
// contract: {key, nodeType, displayTitle, physicalFact, path, counts:{items}, coverRef, kind,
// state, away} — `kind` being the group's stored comic|book|video, same meaning and same
// most-common-row derivation VaultLibrary::browseAt() documents.
QVariantList offlineBrowseAt(VaultIndex* index, const QVariantList& roots, const QString& levelPath);

} // namespace VaultBrowseAway
