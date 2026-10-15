#pragma once
// VaultArtworkResolver — the browse-artwork ladder (Vault browse-artwork execution
// plan, Slice 3 part 1 of 2, 2026-08-13). Pure decision core for ONE browse row: walks
// a fixed-priority ladder — locked pick (reserved) → local artwork the row already
// carries → canonical poster via VaultPosterFetcher → frame-grab via VaultThumbnailer
// → typographic fallback — and returns the FIRST available LOCAL ref, kicking off
// whichever producer's async fetch is missing along the way. Only a local file ref
// ever leaves this class: a remote `posterUrl` is consulted (to key/kick the fetch)
// but never handed back, so a caller can bind resolve()'s return value straight to an
// Image source with no separate "is this remote" check.
//
// Sibling to VaultThumbnailer (Slice 1) and VaultPosterFetcher (Slice 2), composed
// here by constructor injection (raw pointers, not owned — the caller already
// constructed both against the shared Vault cache dir; same lifetime discipline as
// VaultEnricher's injected collaborators). Wiring this into VaultLibrary/QML (browse
// grid repaint on artResolved) is a SEPARATE, later task (part 2) — this slice is the
// pure, testable core only.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class VaultThumbnailer;
class VaultPosterFetcher;

class VaultArtworkResolver : public QObject
{
    Q_OBJECT

public:
    // The facts of one browse group/row the ladder needs — never re-derived here;
    // the caller (part 2: VaultLibrary's browse projection) already knows all of
    // these. `rowKey` is the caller's own stable identifier for the row (a group key
    // or a file path) so a later artResolved() signal tells the caller exactly which
    // tile to repaint.
    struct RowFacts {
        QString rowKey;
        // "video" is the only value that enables rung 4 (frame-grab) — a Folder/Show/
        // Season/comic/book row simply never matches it and skips straight past.
        QString kind;
        QString path;       // absolute file path; meaningful for video rows only
        QString localRef;   // an already-resolved local ref the row carries, or ""
        QString identityId; // adopted catalogue id (FileRow::identityId), or ""
        QString posterUrl;  // canonical poster URL (FileRow::identityCoverUrl), or ""
        qint64 size = 0;
        qint64 mtimeMs = 0;
        double durationSec = -1.0;
    };

    // `thumbnailer`/`posterFetcher` are injected, not owned. Either may be nullptr in
    // a context that never needs that rung — the corresponding rung then simply falls
    // through instead of dereferencing a null producer.
    explicit VaultArtworkResolver(VaultThumbnailer* thumbnailer, VaultPosterFetcher* posterFetcher,
                                   QObject* parent = nullptr);

    // Walks the ladder for one row and returns the first available local ref, or ""
    // (the caller shows the typographic fallback). Never blocks: a rung whose art
    // isn't cached yet kicks that producer's async fetch/grab and falls through to
    // the next rung; the result lands later via artResolved(rowKey).
    QString resolve(const RowFacts& facts);

Q_SIGNALS:
    // Fired once a rung's async fetch (poster or thumb) lands a NEW local file for a
    // row previously resolved with `rowKey` — the caller re-resolves that one row to
    // pick up the fresh art and repaint just that tile.
    void artResolved(const QString& rowKey);

private:
    void onPosterReady(const QString& identityId, const QString& filePath);
    void onThumbReady(const QString& key, const QString& filePath);

    VaultThumbnailer* m_thumbnailer = nullptr;
    VaultPosterFetcher* m_posterFetcher = nullptr;
    // Rung 3 kicked a poster fetch for this identityId on behalf of these rowKeys —
    // a QSet because more than one row (multiple copies) can share one identity.
    QHash<QString, QSet<QString>> m_pendingPosterRowKeys;
    // Rung 4 kicked a thumb grab for this VaultCacheKey (path,size,mtimeMs) on behalf
    // of these rowKeys.
    QHash<QString, QSet<QString>> m_pendingThumbRowKeys;
};
