#pragma once

#include "TorrentResult.h"

#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

class QNetworkReply;

enum class IndexerHealth {
    Unknown,            // never queried
    Ok,                 // last query succeeded within TTL
    Disabled,           // user-disabled via Sources panel
    AuthRequired,       // missing/invalid credentials
    CloudflareBlocked,  // CF challenge unsolvable (Phase 4 fills 1337x)
    RateLimited,        // recent 429 / similar
    Unreachable         // network error / timeout / 5xx
};

class TorrentIndexer : public QObject
{
    Q_OBJECT

public:
    explicit TorrentIndexer(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~TorrentIndexer() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual void    search(const QString& query, int limit = 30, const QString& categoryId = {}) = 0;

    // Health + telemetry contract (Phase 3). Subclasses expose their own
    // state; the default protected members and helpers below let each
    // override be a one-liner returning m_health etc.
    virtual IndexerHealth health() const = 0;
    virtual QDateTime     lastSuccess() const = 0;
    virtual QString       lastError() const = 0;
    virtual qint64        lastResponseMs() const = 0;

    // Credentials contract. Default impls are no-ops for indexers that don't
    // need authentication (the majority). EZTV overrides to expose its cookie
    // key; Phase 4 will likely add a 1337x cf_clearance credential.
    virtual bool        requiresCredentials() const { return false; }
    virtual QStringList credentialKeys() const { return {}; }
    virtual void        setCredential(const QString& /*key*/, const QString& /*value*/) {}
    virtual QString     credential(const QString& /*key*/) const { return {}; }

    // Re-read persisted health state from QSettings. Used by the status panel
    // to refresh a sentinel indexer after an external search has updated the
    // store from a different TorrentIndexer instance.
    void reloadPersistedState() { loadPersistedHealth(); }

    // v1.5 Phase D.3 (2026-05-19) — agent-readable label for IndexerHealth.
    // Used by sources-get-indexer-health JSON serialization.
    static QString healthToString(IndexerHealth h);

    // v1.5 Phase D.3 (2026-05-19) — reset persisted state back to Unknown
    // (used by sources-force-indexer-refresh). Caller is expected to fire a
    // search to repopulate live state.
    void clearPersistedHealth();

signals:
    void searchFinished(const QList<TorrentResult>& results);
    void searchError(const QString& error);

protected:
    // Start the response timer. Call at the top of each outbound request.
    void startRequestTimer();

    // Mark the most recent request successful. Clears lastError, sets
    // lastSuccess to now, persists. Call once the reply has parsed cleanly.
    void markSuccess();

    // Classify a failing QNetworkReply into the right IndexerHealth bucket,
    // store the error string, and persist. Safe to call with a null reply
    // (defaults to Unreachable + generic error string).
    void markError(QNetworkReply* reply);

    // Persistence against QSettings key tankorent/indexers/<id>/health/*.
    // Call loadPersistedHealth() in subclass constructors.
    void loadPersistedHealth();
    void savePersistedHealth();

    IndexerHealth m_health = IndexerHealth::Unknown;
    QDateTime     m_lastSuccess;
    QString       m_lastError;
    qint64        m_lastResponseMs = 0;
    QElapsedTimer m_requestTimer;
};
