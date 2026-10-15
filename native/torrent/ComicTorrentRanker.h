#pragma once

#include "ComicEditionIdentity.h"
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
    QStringList evidence;       // TITLE, ISBN, COVERAGE, ISSUES, ARCHIVE, UPLOADER
    bool coverageMatch = false; // format-scoped range (ComicCoverage) covers the target ordinal
    QString uploaderName;       // bounded release-tag text, empty when none found
    int trustTier = 99;         // 1/2 trusted, 99 unknown (blocked rows never reach here)
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
    // evidence deterministically. Order is by identity, not seed count. The
    // target is built once at the facade boundary (ComicEditionIdentity::
    // buildTarget) and carries format/ordinal/ISBN/collected-issue evidence
    // that coverage- and issue-range-scoring read directly, format-scoped.
    static QList<RankedComicTorrent> rankForEdition(
        const ComicEditionIdentity::ComicEditionTarget& target,
        const QList<TorrentResult>& raw);
    // Project ranked rows into the QML row contract consumed by the picker.
    static QVariantList toVariantRows(const QList<RankedComicTorrent>& ranked);
};
