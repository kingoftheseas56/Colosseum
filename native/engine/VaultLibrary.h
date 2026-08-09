#pragma once
// VaultLibrary — the Vault's single QML façade: the Slice-10 read-model plus the Slice-11
// scan/confirm commands. QML paints from THIS object and fires gestures AT it; C++ owns the
// scan/publish threading and the multi-step confirm sequence, so QML never sequences
// addRoot→scan or setKind→confirm→publish itself (QML paints, C++ decides). It wraps
// VaultIndex (queryable truth), VaultScanner (the cancellable off-thread census + aggregate
// publish), and VaultConfig (user intent: roots + kind overrides). The read half mirrors
// LocalDownloads (revision + series + items) so shelves only render normalized results.
//
// revision bumps ONLY on committed truth (VaultIndex::changed(), emitted after a successful
// publish/upsert). scanning/scanningRoot drive the scan pill; candidate drives the confirmation
// card. Commands: addFolder (add an unconfirmed root + census it), confirmRoot (persist the
// card's chip reassignments, mark confirmed, publish the UNION of all confirmed roots),
// dismissCard, cancelScan.

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class VaultIndex;
class VaultScanner;
class VaultConfig;

class VaultLibrary : public QObject {
    Q_OBJECT

    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY changed)
    // ── scan pill ──
    Q_PROPERTY(QString scanningRoot READ scanningRoot NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanDone READ scanDone NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    // ── confirmation card ──
    Q_PROPERTY(QVariantList candidate READ candidate NOTIFY candidateChanged)
    Q_PROPERTY(QString candidateRoot READ candidateRoot NOTIFY candidateChanged)
    Q_PROPERTY(bool cardVisible READ cardVisible NOTIFY candidateChanged)

public:
    explicit VaultLibrary(VaultIndex* index, VaultScanner* scanner, VaultConfig* config,
                          QObject* parent = nullptr);

    int revision() const { return m_revision; }
    bool scanning() const { return m_scanning; }
    int itemCount() const;

    QString scanningRoot() const { return m_scanningRoot; }
    int scanDone() const { return m_scanDone; }
    int scanTotal() const { return m_scanTotal; }

    QVariantList candidate() const { return m_candidate; }
    QString candidateRoot() const { return m_candidateRoot; }
    bool cardVisible() const { return !m_candidate.isEmpty(); }

    // Confirmed-root count for the marquee "· N folders" (revision-driven: a confirm publishes).
    Q_INVOKABLE int rootCount() const;

    // series(kind): normalization of VaultIndex::groupsForKind → { key, title, kind, count,
    // subtreePath }. items(kind, seriesKey): VaultIndex::filesInSubtree, facts preserved.
    Q_INVOKABLE QVariantList series(const QString& kind) const;
    Q_INVOKABLE QVariantList items(const QString& kind, const QString& seriesKey) const;

    // ── commands (C++ owns the multi-step sequences) ──
    // Add a folder as an UNCONFIRMED root and census it off-thread; scanFinished raises the card.
    Q_INVOKABLE void addFolder(const QString& pathOrUrl);
    // Called when the Vault opens: if a root was added but never confirmed (a picked-then-
    // abandoned folder, or a crash mid-ceremony), resume its founding card — but only ONCE
    // per app run, so dismissing it and reopening the Vault the same session does not nag.
    Q_INVOKABLE void offerUnconfirmedRoots();
    // Confirm the candidate root: persist the card's chip reassignments (subtreePath → kind),
    // mark the root confirmed, then re-census + publish the UNION of ALL confirmed roots.
    Q_INVOKABLE void confirmRoot(const QString& root, const QVariantMap& kindOverrides);
    // Not now — drop the candidate card; the root stays added-but-unconfirmed.
    Q_INVOKABLE void dismissCard();
    // Cancel an in-flight census.
    Q_INVOKABLE void cancelScan();
    // Reveal a Vault folder (or file) in the OS file manager. A directory opens; a file is
    // selected in its parent. Windows-only for now; returns false if the path is gone.
    Q_INVOKABLE bool revealInExplorer(const QString& path) const;

signals:
    void changed();
    void scanningChanged();
    void scanProgressChanged();
    void candidateChanged();

private:
    void setScanning(bool scanning);
    // Kick off an off-thread census of an already-added root and clear any stale candidate
    // (shared by addFolder and offerUnconfirmedRoots).
    void beginCensus(const QString& path);

    VaultIndex* m_index = nullptr;
    VaultScanner* m_scanner = nullptr;
    VaultConfig* m_config = nullptr;
    int m_revision = 0;
    bool m_scanning = false;
    QString m_scanningRoot;
    int m_scanDone = 0;
    int m_scanTotal = 0;
    QVariantList m_candidate;
    QString m_candidateRoot;
    QSet<QString> m_offeredThisRun; // normalized roots whose card we already raised this launch
};
