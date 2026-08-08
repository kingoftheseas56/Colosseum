#pragma once
// VaultScanner — the cancellable off-thread census (Slice 4). Turns a root path
// into a census: kind-pure slices for the confirmation card (Slice 11) and
// per-file rows for VaultIndex, off the GUI thread so a big drive never freezes
// the app. The filesystem walk + classify runs on a pool thread (the slow part);
// the thread-affine work — VaultIdentity reconcile + VaultIndex publish — runs
// back on the GUI thread. Modelled on ComicDownloader::runPackOrCopyThenPublish:
// by-value captures into the pool, a generation guard so a stale result from a
// superseded scan is dropped, and results marshalled back via QFutureWatcher.
//
// Testability: buildScan() is a pure static census (synchronous, no threads) and
// applyResult() is the generation-guarded commit — both are driven directly by
// the Qt Test. scanRoot()/cancel() are the thin async wrapper.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "VaultIdentity.h" // FileFacts
#include "VaultIndex.h"    // FileRow
#include "VaultKit.h"      // CancellationToken

class VaultScanner : public QObject
{
    Q_OBJECT

public:
    explicit VaultScanner(VaultIndex* index, VaultIdentity* identity,
                          QObject* parent = nullptr);

    bool isScanning() const { return m_scanning; }

    // ── Async API ──
    // Census `root` off-thread (scanIgnore needles usually from VaultConfig). A
    // scan requested while one runs BUFFERS and runs after — never dropped.
    Q_INVOKABLE void scanRoot(const QString& root, const QStringList& scanIgnore = {});
    Q_INVOKABLE void cancel();

    // ── Testable seams (also the internal scan lifecycle) ──
    struct RawResult {
        QString root;
        quint64 generation = 0;
        bool cancelled = false;
        QVariantList sliceModel;                 // confirmation-card rows
        QList<VaultIndex::FileRow> rows;         // index rows (ids filled in applyResult)
        QList<VaultIdentity::FileFacts> facts;   // for identity reconcile
    };
    // Pure, synchronous census — no DB, no identity, no threads. Safe on a pool
    // thread. Honors the cancellation token at every group + file.
    static RawResult buildScan(QString root, QStringList scanIgnore, quint64 generation,
                               std::shared_ptr<VaultKit::CancellationToken> cancel);
    // Commit a census result: dropped if its generation is stale; otherwise
    // reconciles identity, fills ids, and publishes to the index. GUI thread.
    void applyResult(const RawResult& result);
    // Begin a new scan generation (supersedes any in-flight result).
    quint64 nextGeneration() { return ++m_generation; }
    quint64 currentGeneration() const { return m_generation; }

signals:
    void progress(const QString& root, int done, int total, const QString& currentName);
    void scanFinished(const QString& root, const QVariantList& slices, bool cancelled);
    void indexPublished(const QString& root, int itemCount);

private:
    static QString subfolderOf(const QString& subtree, const QString& filePath);

    VaultIndex* m_index;
    VaultIdentity* m_identity;
    bool m_scanning = false;
    quint64 m_generation = 0;
    std::shared_ptr<VaultKit::CancellationToken> m_cancel;
    QList<QPair<QString, QStringList>> m_pending; // buffered scans (never dropped)
};
