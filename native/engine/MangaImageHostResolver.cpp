#include "MangaImageHostResolver.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QThread>

#include <memory>

MangaImageHostResolver::MangaImageHostResolver(Lookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)), m_lifetime(std::make_shared<Lifetime>())
{
    m_lifetime->resolver = this;
    if (!m_lookup) {
        m_lookup = [this](const QString& host, LookupDone done) {
            QHostInfo::lookupHost(host, this,
                                  [done = std::move(done)](const QHostInfo& info) mutable {
                QString ipv4;
                for (const QHostAddress& address : info.addresses()) {
                    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                        ipv4 = address.toString();
                        break;
                    }
                }
                done(ipv4);
            });
        };
    }
}

MangaImageHostResolver::~MangaImageHostResolver()
{
    // An injected Lookup may retain and invoke its completion after this QObject
    // has gone away. Clear the raw back-pointer before releasing our state so the
    // retained callback becomes a harmless no-op instead of a use-after-free.
    if (m_lifetime) m_lifetime->resolver = nullptr;
}

QString MangaImageHostResolver::normalizedHost(const QString& host)
{
    return host.trimmed().toLower();
}

MangaImageHostResolver::RequestId MangaImageHostResolver::resolve(
    const QString& rawHost, Completion completion)
{
    const QString host = normalizedHost(rawHost);
    if (host.isEmpty()) return 0;

    const RequestId id = m_nextId++;
    m_pending[host].append(Pending{id, std::move(completion)});
    m_requestHosts.insert(id, host);
    if (m_lookupInFlight.value(host, false)) return id;

    m_lookupInFlight.insert(host, true);
    const quint64 generation = ++m_lookupGenerations[host];
    auto callbackGate = std::make_shared<bool>(false);
    const std::shared_ptr<Lifetime> lifetime = m_lifetime;
    m_lookup(host, [lifetime, host, generation, callbackGate](QString ipv4) {
        // A resolver implementation must answer once; keep the seam defensive
        // so a malformed/test resolver cannot dispatch a page twice.
        if (*callbackGate) return;
        *callbackGate = true;
        // Custom Lookup callbacks are required to invoke on the resolver's Qt
        // thread, matching QHostInfo::lookupHost's receiver-bound delivery.
        // The assertion makes that seam contract visible in debug builds; the
        // lifetime gate handles late completion after resolver destruction.
        if (!lifetime || !lifetime->resolver) return;
        Q_ASSERT(QThread::currentThread() == lifetime->resolver->thread());
        lifetime->resolver->finishLookup(host, ipv4, generation);
    });
    return id;
}

bool MangaImageHostResolver::cancel(RequestId id)
{
    const auto hostIt = m_requestHosts.constFind(id);
    if (hostIt == m_requestHosts.constEnd()) return false;
    const QString host = hostIt.value();
    m_requestHosts.erase(hostIt);

    auto pendingIt = m_pending.find(host);
    if (pendingIt == m_pending.end()) return false;
    auto& requests = pendingIt.value();
    for (auto it = requests.begin(); it != requests.end(); ++it) {
        if (it->id == id) {
            requests.erase(it);
            break;
        }
    }
    if (requests.isEmpty()) {
        m_pending.erase(pendingIt);
        // The underlying lookup cannot necessarily be aborted (custom Lookup
        // implementations may retain their callback), but clearing this state
        // lets a later request start a fresh generation instead of joining a
        // cancelled host forever. The old callback is rejected by generation.
        m_lookupInFlight.remove(host);
    }
    return true;
}

bool MangaImageHostResolver::hasInFlight(const QString& rawHost) const
{
    return m_lookupInFlight.value(normalizedHost(rawHost), false);
}

int MangaImageHostResolver::pendingCount(const QString& rawHost) const
{
    return m_pending.value(normalizedHost(rawHost)).size();
}

void MangaImageHostResolver::finishLookup(const QString& host, const QString& ipv4,
                                           quint64 generation)
{
    if (!m_lookupInFlight.value(host, false)
        || m_lookupGenerations.value(host) != generation)
        return;
    m_lookupInFlight.remove(host);
    const QList<Pending> pending = m_pending.take(host);
    for (const Pending& request : pending) {
        m_requestHosts.remove(request.id);
        if (request.completion) request.completion(ipv4);
    }
}
