#pragma once
// VaultDownloadsRoot — Slice 18. Derives the synthetic downloads root's
// VaultIndex::FileRows from Colosseum's own download backbones, so the Vault
// can shelf downloads as ONE quiet, pre-confirmed root alongside user folders.
//
// This class is a pure JOIN over the backbones' published read APIs. It reads
// the download indices + localPages()/localBook() to recover the on-disk paths
// of CONTAINER downloads (videos, CBZ comics, CBZ tankoban volumes, epub/pdf
// books). It shelves ONLY container files — the Vault scanner + launch router
// classify by file extension and cannot open loose .jpg page dirs, so manga
// chapters stay on the Downloads page exactly as today.
//
// Backbone calls go through QMetaObject::invokeMethod (the same path QML uses),
// which decouples this class from the concrete backbone types and makes it
// unit-testable with trivial QObject fakes that expose matching Q_INVOKABLE
// slots. This class EDITS NOTHING on the Downloads lane — the hard constraint
// from the handoff. It reads; the Downloads page stays the transfer surface.

#include <QObject>
#include <QString>

#include "VaultIndex.h" // FileRow

class VaultDownloadsRoot : public QObject
{
    Q_OBJECT

public:
    // `videos`, `books`, `comics`, `volumes` are the four backbone aggregates
    // (DownloadStore, BookDownloader, ComicDownloader, MangaTankobanService in
    // production). Any may be nullptr — a null backbone contributes zero rows.
    VaultDownloadsRoot(QObject* videos, QObject* books, QObject* comics,
                       QObject* volumes, QObject* parent = nullptr);

    // Derives one FileRow per container download, grouped by series so the Vault
    // shelves them the same way scanned folders shelf (a group per series/show).
    // `rootPath` is the synthetic root's normalized path — stamped on every row
    // so the chip + publish can attribute provenance. `id` is left empty;
    // VaultScanner::applyPublish assigns it via VaultIdentity (same as user-root
    // rows), which makes the double-count guard automatic: the SAME file
    // reachable via a user root AND the downloads root shelves ONCE (the index's
    // INSERT OR REPLACE on file id deduplicates).
    QList<VaultIndex::FileRow> rowsForDownloads(const QString& rootPath) const;

    // True when ANY backbone has at least one container download — the boot-step
    // gate that decides whether to addSyntheticRoot on first Vault open.
    bool hasContainerDownloads() const;

private:
    // Each backbone contributes rows via a uniform derive step. The lambda-style
    // helpers are kept here as private methods so the test can exercise the same
    // path with fakes (QMetaObject dispatch is virtual on the meta-object).
    static QList<VaultIndex::FileRow> rowsFromVideos(QObject* videos, const QString& rootPath);
    static QList<VaultIndex::FileRow> rowsFromBooks(QObject* books, const QString& rootPath);
    static QList<VaultIndex::FileRow> rowsFromComics(QObject* comics, const QString& rootPath);
    static QList<VaultIndex::FileRow> rowsFromVolumes(QObject* volumes, const QString& rootPath);

    // Invokes a Q_INVOKABLE method returning QVariantList on `obj`. Returns an
    // empty list if `obj` is null or the method is absent (graceful no-op).
    static QVariantList invokeList(QObject* obj, const char* method);
    // Invokes a Q_INVOKABLE method taking one QString arg, returning QVariantList.
    static QVariantList invokeListWithString(QObject* obj, const char* method, const QString& arg);

    QObject* m_videos;
    QObject* m_books;
    QObject* m_comics;
    QObject* m_volumes;
};
