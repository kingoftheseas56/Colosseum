#pragma once

// AnimeOrderService — the QML-facing owner of anime-ordering cache lifecycle,
// bounded downloads, off-thread parsing, and seven-day refresh. It wraps an
// immutable AnimeOrderIndex generation and hands QML a cheap synchronous
// resolve(). Network + parse work never runs on the GUI thread; a fully
// validated generation is installed atomically and `revision` bumps so open
// surfaces can recompute without reloading.
//
// House rule "QML paints, C++ decides": this class owns transport, cache,
// threading, and completeness — QML only reads `state`/`revision` and calls
// resolve()/refreshIfStale().

#include "anime/AnimeOrderIndex.h"

#include <QObject>
#include <QReadWriteLock>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QNetworkAccessManager;

class AnimeOrderService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed) // empty|loading|ready|stale|error
public:
    struct Sources {
        QUrl fribb;
        QUrl mappings;
        bool allowHttpForTests = false;
    };

    // Production: the two exact HTTPS URLs and <AppDataLocation>/anime-order.
    explicit AnimeOrderService(QNetworkAccessManager* nam, QObject* parent = nullptr);
    // Test/injectable: explicit cache root and sources (may allow http).
    AnimeOrderService(QNetworkAccessManager* nam, QString cacheRoot, Sources sources,
                      QObject* parent = nullptr);

    int revision() const;
    QString state() const;

    Q_INVOKABLE QVariantMap resolve(const QVariantMap& identities,
                                    const QVariantList& providerEpisodes) const;
    Q_INVOKABLE void refreshIfStale();

signals:
    void changed();

private:
    struct CacheHit {
        QByteArray fribb;
        QByteArray xml;
        QString genId;
        qint64 fetchedAt = 0;
    };

    void initialize();
    void startDownload();
    void fetchUrl(const QUrl& url, qint64 cap, int redirectDepth,
                  const std::function<void(bool, QByteArray)>& done);
    void onDownloadsReady(const QByteArray& fribb, const QByteArray& xml);
    void finishFailure();
    void launchParse(const QByteArray& fribb, const QByteArray& xml, const QString& genId,
                     qint64 fetchedAt, bool activate);
    void installIndex(std::shared_ptr<const AnimeOrderIndex> index);
    void setStateAndNotify(const QString& state);

    bool hasIndex() const;
    bool loadFromCache(CacheHit* out) const;
    QString writeGeneration(const QByteArray& fribb, const QByteArray& xml, qint64 fetchedAt) const;
    bool writeCurrentPointer(const QString& genId) const;
    void pruneGenerations(const QString& keepActive, const QString& keepPrevious) const;

    static QString genIdFor(const QByteArray& fribb, const QByteArray& xml);
    static bool isStale(qint64 fetchedAt);
    static QVariantMap unavailableResult(const QVariantList& providerEpisodes);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_cacheRoot;
    Sources m_sources;

    int m_revision = 0;
    QString m_state;

    mutable QReadWriteLock m_indexLock;
    std::shared_ptr<const AnimeOrderIndex> m_index;

    bool m_refreshInFlight = false;
    bool m_autoRefreshDone = false;
    QString m_currentGenId;
    QString m_previousGenId;
    qint64 m_currentFetchedAt = 0;
};
