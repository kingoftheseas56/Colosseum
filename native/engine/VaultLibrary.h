#pragma once
// VaultLibrary — the thin QML read-model over VaultIndex (Slice 10). QML paints the
// Vault (door arrival, empty/scanning/populated states, later the shelves) from THIS
// object and never touches raw VaultIndex: the index is persistence/query infrastructure,
// this is the presentation seam that owns the revision + scanning lifecycle. It mirrors
// the established LocalDownloads shape (revision + series() + items()) so QML only renders
// normalized results and owns no backend orchestration. Decision: Phase-2 Preflight §1
// (build the thin wrapper now — several consumers land in Phase 2, so keeping query/
// invalidation semantics out of QML pays for itself immediately).
//
// revision bumps ONLY on published truth. It is driven by VaultIndex::changed(), which the
// index emits after a SUCCESSFUL publish()/upsert() only (VaultIndex.cpp:142,152) — never
// on a scan merely starting, nor on a failed/cancelled publish. QML binds
// (VaultLibrary.revision, VaultLibrary.series(kind)) so every shelf invalidates together.
//
// scanning is a SEPARATE signal from revision (a populated Vault may legitimately rescan
// while its last good snapshot stays visible). Slice 10 wires no scanner, so scanning stays
// false; Slice 11 drives it from VaultScanner start/finish via setScanning().

#include <QObject>
#include <QString>
#include <QVariantList>

class VaultIndex;

class VaultLibrary : public QObject {
    Q_OBJECT

    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY changed)

public:
    explicit VaultLibrary(VaultIndex* index, QObject* parent = nullptr);

    int revision() const { return m_revision; }
    bool scanning() const { return m_scanning; }
    int itemCount() const;

    // series(kind): a thin normalization of VaultIndex::groupsForKind(kind) into the shelf's
    // series-row shape { key, title, kind, count, subtreePath }, where key == groupKey and
    // title == groupTitle. kind ∈ {"comic","book","video"}.
    Q_INVOKABLE QVariantList series(const QString& kind) const;

    // items(kind, seriesKey): VaultIndex::filesInSubtree(seriesKey), item facts preserved as
    // the index returns them (id/path/title/realName/subfolder/pages/duration/author/format/
    // progress/coverRef). seriesKey == groupKey == subtreePath; kind is kept for API symmetry
    // with LocalDownloads (a subtree belongs to one kind, so it is not needed for the query).
    Q_INVOKABLE QVariantList items(const QString& kind, const QString& seriesKey) const;

    // Slice 11 seam: the scanner drives scanning state through this.
    void setScanning(bool scanning);

signals:
    void changed();
    void scanningChanged();

private:
    VaultIndex* m_index = nullptr;
    int m_revision = 0;
    bool m_scanning = false;
};
