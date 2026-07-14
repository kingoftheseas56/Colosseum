#pragma once

#include "TorrentResult.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct RankedComicTorrent {
    TorrentResult src;
    int matchTier = 0;
    bool archiveHint = false;
    int identityScore = 0;      // edition-identity weight; the manual picker's sort key
    QString confidence;         // strong | possible | weak
    QStringList evidence;       // TITLE, ISBN, ISSUES, ARCHIVE (why this row ranks here)
};

class ComicTorrentRanker
{
public:
    static QList<RankedComicTorrent> rank(const QString& query,
                                           const QList<TorrentResult>& raw);
    static TorrentResult best(const QString& query, const QList<TorrentResult>& raw);
    static int matchTier(const QString& query, const QString& candidate);
    static bool hasComicArchiveHint(const QString& title);

    // v2 manual picker: rank every canonical universal-filter result for a
    // collected edition, keeping weak rows visible and grading identity
    // evidence deterministically. Order is by identity, not seed count.
    static QList<RankedComicTorrent> rankForEdition(const QString& seriesTitle,
                                                    const QString& editionTitle,
                                                    const QString& isbn,
                                                    const QString& collects,
                                                    const QList<TorrentResult>& raw);
    // Project ranked rows into the QML row contract consumed by the picker.
    static QVariantList toVariantRows(const QList<RankedComicTorrent>& ranked);
};
