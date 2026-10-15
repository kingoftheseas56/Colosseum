#pragma once
// VaultBrowseDetail — the browse-face execution plan's Slice 7: the detail sheet's ONE
// projection, `browseDetail(key)`. Physical truth only — the locked design's decision #11 and
// its three explicit refusals of cast/synopsis/related titles (that is Theatre's job). Answers:
// every copy you hold (same canonical identity across roots where identity exists, else the
// single physical group), its companions, its extras, and an honest evidence line for why Vault
// believes the identity it does.
//
// Pulled out of VaultLibrary for the same reason VaultBrowseAway was (Slice 6): a lean Qt Test
// drives this against a real VaultIndex WITHOUT the full VaultLibrary façade (whose constructor
// unconditionally builds a VaultWatcher and drags in the scanner/downloads-root/identifier
// tree). Filesystem-structural work (companions/extras) is VaultKit::describeFilmFolder's job;
// this module composes that with VaultIndex's identity query and the presentation facts (human
// size, a best-quality line parsed from the filename, the evidence sentence) the sheet needs.
#include <QString>
#include <QStringList>
#include <QVariantMap>

class VaultIndex;

namespace VaultBrowseDetail {

// `key` is a Film browse-row's group key, or an Episode/Clip browse-row's exact file path.
// File details select only that indexed row, never a sibling or every episode sharing a show
// identity. Returns `found: false` when neither lookup resolves (a stale click after a rescan).
// Honors the same user `scanIgnore` needle layer every other Vault walk does.
//
// Exact-file details additionally carry `isFileDetail`, `groupKey` (provenance, not an action
// target), `path`, `available`, and `playable`. Play requires a readable present file, no away
// or error state, and a durable Admitted video verdict; unavailable/unprobed/rejected files
// retain their physical path and copy facts but have an empty `playPath`. Copy entries also
// carry available/playable. Group-wide companions/extras are not attributed to a file detail.
//
// Vault ux uplift S8 additions (EXTENSIONS of the shape, never a reshape): `runtimeText`
// ("1h 47m" / "48m", the AccountActivityFormat.durationText grammar's C++ twin) — key present
// ONLY when the clicked copy's durationSec holds a printable minute count; and each copy entry
// now carries `admissionVerdict` + a composed `statusDetail` (the rejection's human
// admissionDetail, else any errorDetail; empty for a healthy copy) so the sheet can state a
// rejected/errored copy's reason instead of a bare verdict code.
QVariantMap detailFor(VaultIndex* index, const QString& key, const QStringList& scanIgnore = {});

} // namespace VaultBrowseDetail
