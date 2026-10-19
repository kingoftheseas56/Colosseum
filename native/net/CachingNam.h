#pragma once
//
// CachingNam — the existing QML image/XHR QNetworkAccessManager owner, extracted
// mechanically from native/main.cpp so focused harnesses can exercise the REAL
// createRequest path (no scheduler-only imitation, no main.cpp in test targets).
//
// It owns the 1 GiB QNetworkDiskCache, IPv4 pin/Host/peerVerifyName rewrite,
// browser UA, PreferCache, the scoped Jikan gzip branch, watch()/BiblioImageDiag
// tracking and failure eviction. The cover gate extends this same owner: cover
// GETs are wrapped in DeferredCoverReply and admitted through the app-wide
// CoverDeliveryState; every other request keeps the original dispatch path.
//

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlNetworkAccessManagerFactory>

#include <QString>
#include <QStringList>

#include "net/CoverDelivery.h"

class BiblioImageDiag;
class Ipv4PinStore;
class PosterScoreboard;
class PosterTimingProbe;

class CachingNam : public QNetworkAccessManager, public CoverDispatcher
{
public:
    // useCache=false gives a pin+UA NAM with NO disk cache / no PreferCache — for live
    // lanes (torrent search) where a stale cached response would freeze seeder counts.
    // cacheDirOverride exists for harness isolation only; empty keeps production path.
    CachingNam(QStringList pinnedHosts, Ipv4PinStore *pinStore, QObject *parent = nullptr,
               bool useCache = true, PosterScoreboard *scoreboard = nullptr,
               BiblioImageDiag *imageDiag = nullptr,
               PosterTimingProbe *posterTiming = nullptr,
               CoverDeliveryState *coverState = nullptr,
               QString cacheDirOverride = QString());

    // ── CoverDispatcher (managed cover lane only) ──
    QNetworkReply *dispatchCoverRequest(QNetworkAccessManager::Operation op,
                                        const QNetworkRequest &prepared, QIODevice *outgoing,
                                        const QUrl &logicalUrl,
                                        const QString &logicalHost) override;
    QNetworkRequest prepareCoverHop(const QUrl &target) const override;
    bool coverCacheContains(const QUrl &effectiveUrl) override;

    CoverDeliveryState *coverDeliveryState() const { return m_coverState; }

    // Harness seam: the production Jikan host is a compile-time constant; tests
    // point it at a loopback fixture to exercise the existing gunzip branch.
    void setJikanHostForTesting(const QString &host) { m_jikanHost = host; }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req,
                                 QIODevice *outgoing) override;

private:
    QNetworkRequest prepareRequest(const QNetworkRequest &req) const;
    QNetworkReply *dispatchPreparedRequest(Operation op, const QNetworkRequest &prepared,
                                           QIODevice *outgoing, const QUrl &diagUrl,
                                           const QString &logicalHost);
    void watch(const QString &host, QNetworkReply *reply, int timingId);
    void evictOnFailure(QNetworkReply *reply, const QUrl &cacheKey);

    QStringList m_pinnedHosts;
    Ipv4PinStore *m_pinStore = nullptr;
    bool m_useCache = true;
    PosterScoreboard *m_scoreboard = nullptr;
    BiblioImageDiag *m_imageDiag = nullptr;
    PosterTimingProbe *m_posterTiming = nullptr;
    CoverDeliveryState *m_coverState = nullptr;
    QString m_jikanHost = QStringLiteral("api.jikan.moe");
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory
{
public:
    CachingNamFactory(QStringList pinnedHosts, Ipv4PinStore *pinStore,
                      PosterScoreboard *scoreboard, BiblioImageDiag *imageDiag = nullptr,
                      PosterTimingProbe *posterTiming = nullptr,
                      CoverDeliveryState *coverState = nullptr);

    QNetworkAccessManager *create(QObject *parent) override;

private:
    QStringList m_pinnedHosts;
    Ipv4PinStore *m_pinStore = nullptr;
    PosterScoreboard *m_scoreboard = nullptr;   // owned by the app, outlives every NAM
    BiblioImageDiag *m_imageDiag = nullptr;     // same ownership contract as the scoreboard
    PosterTimingProbe *m_posterTiming = nullptr;
    CoverDeliveryState *m_coverState = nullptr; // app-lifetime shared cover delivery state
};
