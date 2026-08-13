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

// `key` is a Film browse-row's own key (== its containing folder path == VaultIndex groupKey,
// the "one video file, one group" convention VaultScanner already keeps). Returns an empty map
// with `found: false` when the key resolves to no rows (a stale click after a rescan). Honors
// the same user `scanIgnore` needle layer every other Vault walk does.
QVariantMap detailFor(VaultIndex* index, const QString& key, const QStringList& scanIgnore = {});

} // namespace VaultBrowseDetail
