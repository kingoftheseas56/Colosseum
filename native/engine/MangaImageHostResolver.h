#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

// Small asynchronous seam around first-use manga CDN resolution. The production
// lookup delegates to QHostInfo::lookupHost; tests inject a delayed callback so
// this coalescing/cancellation contract can be proven without live DNS.
class MangaImageHostResolver final : public QObject
{
public:
    using RequestId = quint64;
    using LookupDone = std::function<void(QString)>;
    using Lookup = std::function<void(const QString&, LookupDone)>;
    using Completion = std::function<void(const QString&)>;

    explicit MangaImageHostResolver(Lookup lookup = Lookup(), QObject* parent = nullptr);
    ~MangaImageHostResolver() override;

    // Queue one completion for host. Requests for a host with a lookup already
    // running share that lookup and each completion is invoked exactly once.
    RequestId resolve(const QString& host, Completion completion);

    // Remove one queued request. A late resolver callback cannot invoke its
    // completion after cancellation. Returns false for an unknown/already-cancelled id.
    bool cancel(RequestId id);

    bool hasInFlight(const QString& host) const;
    int pendingCount(const QString& host) const;

private:
    struct Pending {
        RequestId id = 0;
        Completion completion;
    };

    static QString normalizedHost(const QString& host);
    void finishLookup(const QString& host, const QString& ipv4, quint64 generation);

    struct Lifetime {
        MangaImageHostResolver* resolver = nullptr;
    };

    Lookup m_lookup;
    RequestId m_nextId = 1;
    QHash<QString, QList<Pending>> m_pending;
    QHash<RequestId, QString> m_requestHosts;
    QHash<QString, bool> m_lookupInFlight;
    QHash<QString, quint64> m_lookupGenerations;
    std::shared_ptr<Lifetime> m_lifetime;
};
