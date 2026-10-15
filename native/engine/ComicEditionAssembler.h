#pragma once

// Converts a selected comic-edition payload (design: docs/superpowers/specs/
// 2026-07-15-colosseum-tankorent-comic-volume-mode-design.md, "Assembly and
// publication") into ONE complete, validated page staging directory without
// ever publishing partial output. Publication — moving a finished staging
// directory into the comics library under its catalog chId — is a separate
// task; this module only stages.
//
// Extraction mirrors ComicDownloader's bsdtar-then-7-Zip executable-discovery
// and extraction policy (native/engine/ComicDownloader.cpp). Image validation
// is the same magic-byte sniff MangaTankoban::MangaVolumeArchiveIngestor uses
// (native/engine/MangaVolumeArchiveIngestor.cpp) — a cheap "is this a real
// image" gate that stays Qt::Core-only.

#include "torrent/ComicEditionFileSelector.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

struct ComicAssembleRequest {
    QString editionId;
    QString jobRoot;      // torrent save dir the selected files live under
    ComicEditionFileSelector::ComicPayloadKind kind = ComicEditionFileSelector::ComicPayloadKind::None;
    // Ordered; for IssueArchiveSet each file's `order` groups pages by source issue.
    QList<ComicEditionFileSelector::ComicSelectedFile> files;
    QString stagingRoot;  // "<stagingRoot>/<editionId>.staging" is built here
    // Optional cancellation shared by a detached worker. The assembler never
    // dereferences a caller-owned QObject from the worker thread.
    std::shared_ptr<std::atomic_bool> cancelFlag;
};

class ComicEditionAssembler : public QObject
{
    Q_OBJECT
public:
    explicit ComicEditionAssembler(QObject* parent = nullptr);

    struct Result {
        bool ok = false;
        QString stagingDir;
        QStringList orderedFiles;   // page_NNN.<ext> names, staged reading order
        QList<int> groups;          // parallel per-page group: -1 (none), 0 (loose), or source-issue index
        QString error;
    };

    // Runs synchronously (extraction blocks the calling thread); emits
    // finished()/failed() at the end so a caller can verify via the return
    // value AND the signal. NEVER leaves a partial "<editionId>.staging"
    // directory behind on failure.
    Result assemble(const ComicAssembleRequest& req);

    // Runs the same disk/extraction operation on a detached worker-owned
    // assembler. The returned Result is value data and is safe to marshal back
    // through QFutureWatcher to the owning QObject thread.
    static Result assembleDetached(const ComicAssembleRequest& req,
                                   const std::shared_ptr<std::atomic_bool>& cancelFlag);

    // Aborts an in-flight or just-finished assembly for editionId and removes
    // its staging directory if one is known. A cancel() that lands before the
    // matching assemble() call makes that call fail immediately with no I/O;
    // a cancel() that lands after a successful assemble() removes the
    // staging directory that call produced.
    void cancel(const QString& editionId);

signals:
    void progress(const QString& editionId, int done, int total);
    void finished(const QString& editionId, const QString& stagingDir,
                  const QStringList& orderedFiles, const QList<int>& groups);
    void failed(const QString& editionId, const QString& reason);

private:
    QHash<QString, QString> m_stagingDirFor;   // last known staging dir per editionId (for cancel())
    QSet<QString> m_cancelRequested;
};
