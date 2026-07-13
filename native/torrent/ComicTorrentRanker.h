#pragma once

#include "TorrentResult.h"

#include <QList>
#include <QString>

struct RankedComicTorrent {
    TorrentResult src;
    int matchTier = 0;
    bool archiveHint = false;
};

class ComicTorrentRanker
{
public:
    static QList<RankedComicTorrent> rank(const QString& query,
                                           const QList<TorrentResult>& raw);
    static TorrentResult best(const QString& query, const QList<TorrentResult>& raw);
    static int matchTier(const QString& query, const QString& candidate);
    static bool hasComicArchiveHint(const QString& title);
};
