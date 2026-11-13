#pragma once

#include <QDate>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Pure receiving/projection helpers for the existing recommendation owner.
// No transport, persistence, feedback state or replacement personal ranker.
class VinylSourcePool final {
public:
    static bool metadataOperation(const QString &operation);
    static QString metadataSource(const QString &operation, const QString &value, const QVariantMap &options);
    static bool validMetadataRequest(const QString &operation, const QString &value, const QVariantMap &options);
    static bool normalizeMetadataEnvelope(const QString &operation, const QString &value,
        const QVariantMap &options, const QVariant &raw, QVariantMap *out);
    static QStringList artistNames(const QVariantMap &verifiedIdentity);
    static QVariantMap catalogueCandidate(const QVariantMap &catalogueCard);
    static QVariantList mergeCandidates(const QVariantList &items);
    static QVariantList mergeCharts(const QVariantList &feeds, const QString &kind);
    static QVariantList updateCharts(const QVariantList &cachedFeeds, const QVariantList &receivedFeeds);
    static bool isNewRelease(const QString &date, const QDate &today);
    static QVariantList newReleaseNeighbors(const QVariantList &seeds,
        const QVariantMap &similarBySeed, int ownRecentCount, int minimumItems = 8);
    static QVariantList homeRows(const QVariantMap &buckets, bool hasRetainedHistory);
};
