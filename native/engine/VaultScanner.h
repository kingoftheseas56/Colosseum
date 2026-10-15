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
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>
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
    // Keep off-thread census work running, but park GUI-side candidate/publish commits
    // while an immersive reader/player owns the frame budget.
    void setApplySuspended(bool on);
    bool applySuspended() const { return m_applySuspended; }

    // Publication (Slice 11): a confirm re-censuses EVERY confirmed root off-thread
    // and publishes their UNION in one atomic VaultIndex::publish — never a single
    // root's rows alone, which the whole-index replace would use to wipe sibling
    // roots. scanRoot/applyResult only DELIVER a candidate census for the card;
    // publication is this separate, confirm-triggered step.
    // extraRows (Slice 18): pre-built rows NOT scanned from the filesystem — the
    // synthetic downloads root's derived rows. Folded into the UNION publish so
    // they shelf alongside scanned user roots in the same atomic transaction.
    // NOTE: not Q_INVOKABLE — the QList<FileRow> parameter carries a nested struct
    // the moc can't resolve, and this method is C++-only (called by VaultLibrary).
    // No default args for the same reason (the moc parses them and fails on QList<FileRow>={}).
    void publishConfirmed(const QStringList& confirmedRoots,
                          const QStringList& scanIgnore,
                          const QMap<QString, QString>& kindOverrides,
                          const QList<VaultIndex::FileRow>& extraRows);

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
    // thread. Honors the cancellation token at every group + file. kindOverrides
    // ({normSubtreePath → kind}) reshelve a slice to the user's chip choice; onProgress
    // (if set) fires per first-level subtree so the pill counts live.
    static RawResult buildScan(QString root, QStringList scanIgnore, quint64 generation,
                               std::shared_ptr<VaultKit::CancellationToken> cancel,
                               const QMap<QString, QString>& kindOverrides = {},
                               const std::function<void(int, int, const QString&)>& onProgress = {});
    // Deliver a candidate census: dropped if its generation is stale; otherwise
    // emits scanFinished with the confirmation-card model. Does NOT publish — a
    // single root's census must never reach VaultIndex::publish() alone. GUI thread.
    void applyResult(const RawResult& result);
    // Aggregate publish: reconcile identity across EVERY result, assign ids, and
    // publish their union in one transaction. Dropped on a stale generation or if
    // any result is cancelled (no partial publish). Emits indexPublished ONLY on a
    // successful publish. GUI thread; the sync seam the Qt Test drives directly.
    // extraRows (Slice 18): pre-built rows folded into the union; they are NOT
    // reconciled through identity (their ids are assigned by the caller or left
    // empty for idForFile assignment here).
    void applyPublish(const QList<RawResult>& results, quint64 generation,
                      const QList<VaultIndex::FileRow>& extraRows);
    // 2-arg overload (pre-Slice-18 callers): no synthetic rows. Inline forward
    // avoids touching every existing test call site.
    void applyPublish(const QList<RawResult>& results, quint64 generation)
    { applyPublish(results, generation, {}); }
    // Begin a new scan generation (supersedes any in-flight result).
    quint64 nextGeneration() { return ++m_generation; }
    quint64 currentGeneration() const { return m_generation; }

    // Path of a file relative to its subtree ("" for a file directly in the subtree) —
    // shared with VaultWatcher so live-shelf rows match census rows exactly.
    static QString subfolderOf(const QString& subtree, const QString& filePath);

signals:
    void progress(const QString& root, int done, int total, const QString& currentName);
    void scanFinished(const QString& root, const QVariantList& slices, bool cancelled);
    void indexPublished(const QString& root, int itemCount);

private:
    VaultIndex* m_index;
    VaultIdentity* m_identity;
    enum class DeferredApplyKind { None, Result, Publish };
    bool m_scanning = false;
    bool m_applySuspended = false;
    DeferredApplyKind m_deferredApplyKind = DeferredApplyKind::None;
    RawResult m_deferredResult;
    QList<RawResult> m_deferredPublishResults;
    quint64 m_deferredPublishGeneration = 0;
    QList<VaultIndex::FileRow> m_deferredPublishExtraRows;
    quint64 m_generation = 0;
    std::shared_ptr<VaultKit::CancellationToken> m_cancel;
    QList<QPair<QString, QStringList>> m_pending; // buffered scans (never dropped)
};
