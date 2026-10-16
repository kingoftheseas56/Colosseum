#include "net/CoverDelivery.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTimer>
#include <QTimeZone>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kLedgerVersion = 1;
constexpr int kMaxHostEntries = 128;
constexpr int kMaxUrlEntries = 2048;
constexpr qint64 kMaxLedgerBytes = 256 * 1024;
constexpr qint64 kSaveDebounceMs = 2000;
constexpr qint64 kMaxUtcMs = 253402300799000LL;   // 9999-12-31T23:59:59Z
constexpr qint64 kMaxRetryAfterSeconds = 24LL * 60 * 60;
constexpr qint64 kMinRetryAfterSeconds = 1;

bool pathHasImageExtension(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return false;
    const QString ext = path.mid(dot + 1).toLower();
    static const QSet<QString> known = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("avif")};
    return known.contains(ext);
}

bool isHexDigest(const QString &key)
{
    if (key.size() != 64)
        return false;
    for (const QChar c : key) {
        const char16_t u = c.unicode();
        const bool hex = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f');
        if (!hex)
            return false;
    }
    return true;
}

qint64 validEpochOrNegative(const QJsonValue &value)
{
    if (!value.isDouble())
        return -1;
    const double d = value.toDouble();
    if (!std::isfinite(d) || d < 0 || d > static_cast<double>(kMaxUtcMs))
        return -1;
    return static_cast<qint64>(std::llround(d));
}

} // namespace

// ── RetryAfter parsing ───────────────────────────────────────────────────────

RetryAfterDecision CoverDeliveryState::parseRetryAfter(const QByteArray &raw, qint64 nowUtcMs,
                                                       int fallbackSeconds)
{
    RetryAfterDecision out;
    const QByteArray trimmed = raw.trimmed();
    const qint64 fallback = qBound(kMinRetryAfterSeconds,
                                   qint64(fallbackSeconds), kMaxRetryAfterSeconds);
    out.hadHeader = !trimmed.isEmpty();
    if (trimmed.isEmpty()) {
        out.valid = true;
        out.fallback = true;
        out.deadlineUtcMs = nowUtcMs + fallback * 1000;
        return out;
    }

    bool numeric = false;
    const qlonglong seconds = trimmed.toLongLong(&numeric);
    if (numeric) {
        out.valid = true;
        out.deltaSeconds = seconds;
        // A server-provided zero is legal, but blindly admitting an entire
        // waiting image shelf after one 429 creates an immediate second burst.
        out.expired = seconds <= 0;
        out.deadlineUtcMs = nowUtcMs + qBound(kMinRetryAfterSeconds,
                                             seconds, kMaxRetryAfterSeconds) * 1000;
        return out;
    }

    const QString text = QString::fromLatin1(trimmed);
    QDateTime dt = QDateTime::fromString(text, Qt::RFC2822Date);
    if (!dt.isValid()) {
        static const char *const formats[] = {
            "ddd, dd MMM yyyy HH:mm:ss 'GMT'",
            "ddd, dd MMM yyyy HH:mm:ss zzz",
            "ddd MMM d HH:mm:ss yyyy",
        };
        for (const char *format : formats) {
            const QDateTime parsed = QLocale::c().toDateTime(text, QString::fromLatin1(format));
            if (parsed.isValid()) {
                dt = parsed;
                break;
            }
        }
        if (dt.isValid())
            dt.setTimeZone(QTimeZone::UTC);
    }
    if (dt.isValid()) {
        out.valid = true;
        const qint64 serverDeadline = dt.toMSecsSinceEpoch();
        out.expired = serverDeadline <= nowUtcMs;
        out.deadlineUtcMs = qBound(nowUtcMs + kMinRetryAfterSeconds * 1000,
                                  serverDeadline, nowUtcMs + kMaxRetryAfterSeconds * 1000);
        return out;
    }

    out.valid = true;
    out.fallback = true;
    out.deadlineUtcMs = nowUtcMs + fallback * 1000;
    return out;
}

// ── Classification ───────────────────────────────────────────────────────────

QString CoverDeliveryState::normalizeHost(const QString &host)
{
    return host.trimmed().toLower();
}

QByteArray CoverDeliveryState::urlDigest(const QUrl &url)
{
    QUrl normalized = url;
    normalized.setFragment(QString());
    return QCryptographicHash::hash(normalized.toEncoded(QUrl::FullyEncoded),
                                    QCryptographicHash::Sha256).toHex();
}

bool CoverDeliveryState::isManagedCoverUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    if (!url.isValid() || !url.userName().isEmpty() || !url.password().isEmpty())
        return false;

    const QString host = normalizeHost(url.host());
    const QString path = url.path();
    if (host == QLatin1String("i0.wp.com"))
        return path.startsWith(QLatin1String("/getcomics.org/share/uploads/"))
               && pathHasImageExtension(path);
    if (host == QLatin1String("getcomics.org"))
        return path.startsWith(QLatin1String("/share/uploads/")) && pathHasImageExtension(path);
    if (host == QLatin1String("live.metahub.space") || host == QLatin1String("images.metahub.space"))
        return path.startsWith(QLatin1String("/poster/"));
    if (host == QLatin1String("covers.openlibrary.org"))
        return path.startsWith(QLatin1String("/b/"));
    return false;
}

bool CoverDeliveryState::isManagedCover(const QUrl &url) const
{
    if (isManagedCoverUrl(url))
        return true;
    const QString host = normalizeHost(url.host());
    if (host.isEmpty())
        return false;
    QMutexLocker locker(&m_mutex);
    return m_testManagedHosts.contains(host) && url.path().startsWith(QLatin1String("/cover/"));
}

void CoverDeliveryState::addManagedHostForTesting(const QString &host)
{
    const QString normalized = normalizeHost(host);
    if (normalized.isEmpty())
        return;
    QMutexLocker locker(&m_mutex);
    m_testManagedHosts.insert(normalized, 1);
}

// ── Construction / test seams ────────────────────────────────────────────────

CoverDeliveryState::CoverDeliveryState(QObject *parent)
    : QObject(parent)
{
    m_monoOrigin.start();
    m_utcAtMonoOrigin = QDateTime::currentMSecsSinceEpoch();
}

CoverDeliveryState::~CoverDeliveryState()
{
    bool dirty = false;
    {
        QMutexLocker locker(&m_mutex);
        dirty = m_dirty;
    }
    if (dirty && !m_ledgerPath.isEmpty())
        saveLedgerNow();
}

void CoverDeliveryState::setActiveCapForTesting(int cap)
{
    QMutexLocker locker(&m_mutex);
    if (cap > 0)
        m_activeCap = cap;
}

void CoverDeliveryState::setQueueLimitsForTesting(int perHost, int total)
{
    QMutexLocker locker(&m_mutex);
    if (perHost > 0)
        m_perHostQueueCap = perHost;
    if (total > 0)
        m_totalQueueCap = total;
}

void CoverDeliveryState::setFallbackSecondsForTesting(int seconds)
{
    QMutexLocker locker(&m_mutex);
    if (seconds >= 0)
        m_fallbackSeconds = seconds;
}

void CoverDeliveryState::setBeforeWakeSignalForTesting(std::function<void()> hook)
{
    QMutexLocker locker(&m_mutex);
    m_beforeWakeSignalForTesting = std::move(hook);
}

void CoverDeliveryState::setClockForTesting(std::function<qint64()> nowUtcMs)
{
    QMutexLocker locker(&m_mutex);
    m_clockUtcMs = std::move(nowUtcMs);
}

qint64 CoverDeliveryState::nowUtcMs() const
{
    if (m_clockUtcMs)
        return m_clockUtcMs();
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 CoverDeliveryState::nowMonoMs() const
{
    if (m_clockUtcMs)
        return m_clockUtcMs();
    return m_monoOrigin.elapsed();
}

// ── Cooldown surface ─────────────────────────────────────────────────────────

void CoverDeliveryState::pruneExpiredLocked(qint64 nowMono, qint64 nowUtc)
{
    Q_UNUSED(nowUtc);
    for (auto it = m_hostDeadlineMono.begin(); it != m_hostDeadlineMono.end();) {
        if (it.value() <= nowMono)
            it = m_hostDeadlineMono.erase(it);
        else
            ++it;
    }
    for (auto it = m_urlDeadlineMono.begin(); it != m_urlDeadlineMono.end();) {
        if (it.value() <= nowMono)
            it = m_urlDeadlineMono.erase(it);
        else
            ++it;
    }
    if (m_laneDeadlineMono <= nowMono)
        m_laneDeadlineMono = 0;
}

qint64 CoverDeliveryState::cooldownRemainingMs(const QString &host, const QByteArray &digest) const
{
    const qint64 nowMono = nowMonoMs();
    QMutexLocker locker(&m_mutex);
    qint64 deadline = m_laneDeadlineMono;
    deadline = std::max(deadline, m_hostDeadlineMono.value(normalizeHost(host), 0));
    deadline = std::max(deadline, m_urlDeadlineMono.value(digest, 0));
    if (deadline <= nowMono)
        return 0;
    return deadline - nowMono;
}

void CoverDeliveryState::record429(const QString &host, const QByteArray &digest,
                                   const QByteArray &retryAfterRaw, bool *usedFallback)
{
    const qint64 nowUtc = nowUtcMs();
    const qint64 nowMono = nowMonoMs();
    int fallbackSeconds = 300;
    {
        QMutexLocker locker(&m_mutex);
        fallbackSeconds = m_fallbackSeconds;
    }
    const RetryAfterDecision decision = parseRetryAfter(retryAfterRaw, nowUtc, fallbackSeconds);
    if (usedFallback)
        *usedFallback = decision.fallback;
    if (!decision.valid)
        return;
    const qint64 deltaMs = decision.deadlineUtcMs - nowUtc;
    if (deltaMs <= 0)
        return;
    const qint64 monoDeadline = nowMono + deltaMs;

    QMutexLocker locker(&m_mutex);
    pruneExpiredLocked(nowMono, nowUtc);
    const QString normalized = normalizeHost(host);
    auto hostIt = m_hostDeadlineMono.find(normalized);
    if (hostIt != m_hostDeadlineMono.end()) {
        *hostIt = std::max(*hostIt, monoDeadline);
    } else if (m_hostDeadlineMono.size() < kMaxHostEntries) {
        m_hostDeadlineMono.insert(normalized, monoDeadline);
    } else {
        // Host capacity exceeded: never evict an unexpired host cooldown. Fall
        // back to one conservative cover-lane deadline instead.
        m_laneDeadlineMono = std::max(m_laneDeadlineMono, monoDeadline);
    }

    auto urlIt = m_urlDeadlineMono.find(digest);
    if (urlIt != m_urlDeadlineMono.end()) {
        *urlIt = std::max(*urlIt, monoDeadline);
    } else {
        if (m_urlDeadlineMono.size() >= kMaxUrlEntries) {
            auto soonest = m_urlDeadlineMono.begin();
            for (auto it = m_urlDeadlineMono.begin(); it != m_urlDeadlineMono.end(); ++it) {
                if (it.value() < soonest.value())
                    soonest = it;
            }
            m_urlDeadlineMono.erase(soonest);
        }
        m_urlDeadlineMono.insert(digest, monoDeadline);
    }
    m_dirty = true;
    ++m_ledgerRevision;
    locker.unlock();
    // A 429 must survive an immediate hard restart, not only a graceful quit
    // or an eventual timer callback. The shared-state lock is NOT held for I/O.
    if (!saveLedgerNow())
        scheduleLedgerSave();
}

void CoverDeliveryState::noteSuccess(const QString &host, const QByteArray &digest)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_hostDeadlineMono.remove(normalizeHost(host)))
            changed = true;
        if (m_urlDeadlineMono.remove(digest))
            changed = true;
        if (changed) {
            m_dirty = true;
            ++m_ledgerRevision;
        }
    }
    if (changed && !saveLedgerNow())
        scheduleLedgerSave();
}

// ── Admission ────────────────────────────────────────────────────────────────

void CoverDeliveryState::pumpNextLocked(const QString &host, QList<PendingWake> &wakes)
{
    while (m_activeCount.value(host) < m_activeCap) {
        auto queueIt = m_queues.find(host);
        if (queueIt == m_queues.end() || queueIt->isEmpty())
            break;
        DeferredCoverReply *next = queueIt->takeFirst();
        --m_totalQueued;
        const auto connection = m_wakeConnections.constFind(next);
        // A queued reply always registers its context-bound connection before
        // entering the FIFO; destruction/abort removes it and the queue entry.
        Q_ASSERT(connection != m_wakeConnections.cend());
        if (connection == m_wakeConnections.cend())
            continue; // do not reserve a permit without a wake receiver
        m_activeCount[host] += 1;
        m_activeReplies.insert(next, host);
        wakes.append(PendingWake{connection->ticket});
    }
}

void CoverDeliveryState::postWakes(const QList<PendingWake> &wakes)
{
    if (wakes.isEmpty())
        return;
    std::function<void()> testHook;
    {
        QMutexLocker locker(&m_mutex);
        testHook = m_beforeWakeSignalForTesting;
    }
    if (testHook)
        testHook(); // outside the state lock, so cancellation can race a grant
    for (const PendingWake &wake : wakes)
        emit permitGranted(wake.ticket);
}

CoverDeliveryState::Admission CoverDeliveryState::acquire(const QString &host,
                                                          DeferredCoverReply *reply)
{
    const QString normalized = normalizeHost(host);
    QMutexLocker locker(&m_mutex);
    if (m_activeCount.value(normalized) < m_activeCap) {
        m_activeCount[normalized] += 1;
        m_activeReplies.insert(reply, normalized);
        return Admission::Admitted;
    }
    if (m_totalQueued >= m_totalQueueCap
        || m_queues[normalized].size() >= m_perHostQueueCap)
        return Admission::Overflow;
    // Connect while the reply is definitely alive on its owning thread.
    // Qt delivers this functor only in that receiver's thread and automatically
    // drops pending delivery when its QObject context is destroyed. The raw
    // capture is never dereferenced by the sender or outside that context.
    const quint64 ticket = ++m_lastWakeTicket;
    const auto connection = QObject::connect(
        this, &CoverDeliveryState::permitGranted, reply,
        [reply, ticket](quint64 grantedTicket) {
            if (grantedTicket == ticket)
                reply->onGranted();
        }, Qt::QueuedConnection);
    m_wakeConnections.insert(reply, WakeConnection{ticket, connection});
    m_queues[normalized].append(reply);
    ++m_totalQueued;
    return Admission::Queued;
}

void CoverDeliveryState::acknowledgeGrant(DeferredCoverReply *reply)
{
    QMetaObject::Connection connection;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_wakeConnections.find(reply);
        if (it != m_wakeConnections.end()) {
            connection = it->connection;
            m_wakeConnections.erase(it);
        }
    }
    if (connection)
        QObject::disconnect(connection);
}

void CoverDeliveryState::forget(DeferredCoverReply *reply)
{
    if (!reply)
        return;
    QList<PendingWake> wakes;
    QMetaObject::Connection connection;
    {
        QMutexLocker locker(&m_mutex);
        auto waiter = m_wakeConnections.find(reply);
        if (waiter != m_wakeConnections.end()) {
            connection = waiter->connection;
            m_wakeConnections.erase(waiter);
        }
        const QString activeHost = m_activeReplies.value(reply);
        if (!activeHost.isEmpty()) {
            m_activeReplies.remove(reply);
            const int remaining = m_activeCount.value(activeHost) - 1;
            if (remaining <= 0)
                m_activeCount.remove(activeHost);
            else
                m_activeCount[activeHost] = remaining;
            pumpNextLocked(activeHost, wakes);
        } else {
            for (auto qIt = m_queues.begin(); qIt != m_queues.end(); ++qIt) {
                if (qIt->removeOne(reply) != 0) {
                    --m_totalQueued;
                    break;
                }
            }
        }
    }
    // Disconnect before emitting another permit signal. The event delivery
    // itself remains protected by Qt's receiver/context lifetime machinery.
    if (connection)
        QObject::disconnect(connection);
    postWakes(wakes);
}

// ── Ledger ───────────────────────────────────────────────────────────────────

QString CoverDeliveryState::ledgerPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_ledgerPath;
}

int CoverDeliveryState::ledgerHostCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_hostDeadlineMono.size();
}

int CoverDeliveryState::ledgerUrlCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_urlDeadlineMono.size();
}

bool CoverDeliveryState::loadLedger(const QString &path)
{
    if (path.isEmpty())
        return false;
    {
        QMutexLocker locker(&m_mutex);
        // Even a corrupt ledger must retain the path, so a fresh successful
        // 429 can atomically replace it instead of silently becoming volatile.
        m_ledgerPath = path;
        m_hostDeadlineMono.clear();
        m_urlDeadlineMono.clear();
        m_laneDeadlineMono = 0;
    }
    QFile file(path);
    if (!file.exists()) {
        QMutexLocker locker(&m_mutex);
        m_ledgerPath = path;
        return true;    // absent ledger is a normal first run, not a failure
    }
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (file.size() > kMaxLedgerBytes)
        return false;
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != kLedgerVersion)
        return false;

    const qint64 nowUtc = nowUtcMs();
    const qint64 nowMono = nowMonoMs();

    QList<QPair<qint64, QString>> hosts;
    const QJsonObject hostObj = root.value(QStringLiteral("hosts")).toObject();
    for (auto it = hostObj.constBegin(); it != hostObj.constEnd(); ++it) {
        const qint64 deadline = validEpochOrNegative(it.value());
        if (deadline < 0)
            continue;
        const QString host = normalizeHost(it.key());
        if (host.isEmpty())
            continue;
        hosts.append({deadline, host});
    }
    QList<QPair<qint64, QByteArray>> urls;
    const QJsonObject urlObj = root.value(QStringLiteral("urls")).toObject();
    for (auto it = urlObj.constBegin(); it != urlObj.constEnd(); ++it) {
        const qint64 deadline = validEpochOrNegative(it.value());
        if (deadline < 0)
            continue;
        if (!isHexDigest(it.key()))
            continue;
        urls.append({deadline, it.key().toLatin1()});
    }
    const qint64 laneUtc = validEpochOrNegative(root.value(QStringLiteral("lane")));

    std::sort(hosts.begin(), hosts.end());
    std::sort(urls.begin(), urls.end());

    QMutexLocker locker(&m_mutex);
    m_ledgerPath = path;
    m_hostDeadlineMono.clear();
    m_urlDeadlineMono.clear();
    m_laneDeadlineMono = 0;
    for (int i = 0; i < hosts.size() && i < kMaxHostEntries; ++i) {
        const qint64 remaining = hosts.at(i).first - nowUtc;
        if (remaining > 0)
            m_hostDeadlineMono.insert(hosts.at(i).second, nowMono + remaining);
    }
    for (int i = 0; i < urls.size() && i < kMaxUrlEntries; ++i) {
        const qint64 remaining = urls.at(i).first - nowUtc;
        if (remaining > 0)
            m_urlDeadlineMono.insert(urls.at(i).second, nowMono + remaining);
    }
    if (laneUtc >= 0) {
        const qint64 remaining = laneUtc - nowUtc;
        if (remaining > 0)
            m_laneDeadlineMono = nowMono + remaining;
    }
    return true;
}

bool CoverDeliveryState::saveLedgerNow()
{
    QMutexLocker ioLocker(&m_ledgerIoMutex);
    QString path;
    quint64 snapshotRevision = 0;
    QJsonObject hosts;
    QJsonObject urls;
    qint64 laneUtc = 0;
    qint64 nowUtc = 0;
    qint64 nowMono = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (m_ledgerPath.isEmpty())
            return false;
        path = m_ledgerPath;
        snapshotRevision = m_ledgerRevision;
        nowUtc = nowUtcMs();
        nowMono = nowMonoMs();
        pruneExpiredLocked(nowMono, nowUtc);
        for (auto it = m_hostDeadlineMono.constBegin(); it != m_hostDeadlineMono.constEnd(); ++it) {
            const qint64 utc = nowUtc + (it.value() - nowMono);
            hosts.insert(it.key(), static_cast<double>(utc));
        }
        for (auto it = m_urlDeadlineMono.constBegin(); it != m_urlDeadlineMono.constEnd(); ++it) {
            const qint64 utc = nowUtc + (it.value() - nowMono);
            urls.insert(QString::fromLatin1(it.key()), static_cast<double>(utc));
        }
        if (m_laneDeadlineMono > nowMono)
            laneUtc = nowUtc + (m_laneDeadlineMono - nowMono);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kLedgerVersion);
    root.insert(QStringLiteral("hosts"), hosts);
    root.insert(QStringLiteral("urls"), urls);
    if (laneUtc > 0)
        root.insert(QStringLiteral("lane"), static_cast<double>(laneUtc));
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxLedgerBytes) {
        QMutexLocker locker(&m_mutex);
        m_dirty = true;
        return false;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        QMutexLocker locker(&m_mutex);
        m_dirty = true;
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit()) {
        QMutexLocker locker(&m_mutex);
        m_dirty = true;
        return false;
    }
    {
        QMutexLocker locker(&m_mutex);
        // A concurrent 429/success may have updated the ledger while this
        // snapshot was written. Only acknowledge the revision actually saved.
        if (m_ledgerRevision == snapshotRevision)
            m_dirty = false;
    }
    return true;
}

void CoverDeliveryState::scheduleLedgerSave()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_ledgerPath.isEmpty())
            return;
    }
    bool expected = false;
    if (!m_saveArmed.compare_exchange_strong(expected, true))
        return;
    QMetaObject::invokeMethod(this, [this] { startSaveTimer(); }, Qt::QueuedConnection);
}

void CoverDeliveryState::startSaveTimer()
{
    if (!m_saveTimer) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        QObject::connect(m_saveTimer, &QTimer::timeout, this, [this] {
            m_saveArmed.store(false);
            saveLedgerNow();
        });
    }
    if (!m_saveTimer->isActive())
        m_saveTimer->start(static_cast<int>(kSaveDebounceMs));
}

// ── DeferredCoverReply ───────────────────────────────────────────────────────

DeferredCoverReply::DeferredCoverReply(CoverDeliveryState *state, CoverDispatcher *dispatcher,
                                       QNetworkAccessManager::Operation op,
                                       const QNetworkRequest &original,
                                       const QNetworkRequest &prepared, QIODevice *outgoing,
                                       QObject *parent)
    : QNetworkReply(parent),
      m_state(state),
      m_dispatcher(dispatcher),
      m_op(op),
      m_original(original),
      m_prepared(prepared),
      m_outgoing(outgoing)
{
    setOperation(op);
    setRequest(original);
    setUrl(original.url());
    setOpenMode(QIODevice::ReadOnly);
    m_logicalUrl = original.url();
    m_logicalHost = CoverDeliveryState::normalizeHost(m_logicalUrl.host());
    m_digest = CoverDeliveryState::urlDigest(m_logicalUrl);
}

DeferredCoverReply::~DeferredCoverReply()
{
    if (m_state)
        m_state->forget(this);
}

QString DeferredCoverReply::cooldownMessage(const QString &host, qint64 remainingMs)
{
    return QStringLiteral("cover-cooldown: host=%1 remaining_ms=%2 (local Retry-After suppression)")
        .arg(host)
        .arg(remainingMs);
}

void DeferredCoverReply::begin()
{
    const qint64 remaining = m_state->cooldownRemainingMs(m_logicalHost, m_digest);
    if (remaining > 0) {
        m_phase = Phase::Active;
        if (m_dispatcher->coverCacheContains(m_prepared.url())) {
            qInfo("[cover] cooldown cache-hit host=%s remaining_ms=%lld",
                  qUtf8Printable(m_logicalHost), remaining);
            startInner(QNetworkRequest::AlwaysCache, /*needsPermit=*/false);
        } else {
            qInfo("[cover] blocked host=%s remaining_ms=%lld (cooldown, no request)",
                  qUtf8Printable(m_logicalHost), remaining);
            scheduleSynthetic(QNetworkReply::InternalServerError,
                              cooldownMessage(m_logicalHost, remaining), 429,
                              (remaining + 999) / 1000);
        }
        return;
    }
    const CoverDeliveryState::Admission admission = m_state->acquire(m_logicalHost, this);
    if (admission == CoverDeliveryState::Admission::Admitted) {
        m_admitted = true;
        m_phase = Phase::Active;
        startInner(QNetworkRequest::PreferCache, /*needsPermit=*/true);
    } else if (admission == CoverDeliveryState::Admission::Queued) {
        qInfo("[cover] queued host=%s", qUtf8Printable(m_logicalHost));
        m_phase = Phase::Queued;
    } else {
        qInfo("[cover] queue-overflow host=%s (local failure)", qUtf8Printable(m_logicalHost));
        scheduleSynthetic(QNetworkReply::TemporaryNetworkFailureError,
                          QStringLiteral("cover-queue-overflow: host=%1 (local bound)")
                              .arg(m_logicalHost),
                          0, 0);
    }
}

void DeferredCoverReply::onGranted()
{
    // Runs only through a Qt queued connection whose context is this reply.
    // Retire the subscription so each grant does not broadcast to old waiters.
    m_state->acknowledgeGrant(this);
    if (m_done) {
        m_state->forget(this);
        return;
    }
    m_admitted = true;
    m_phase = Phase::Active;
    if (m_aborted) {
        releasePermit();
        scheduleSynthetic(QNetworkReply::OperationCanceledError,
                          QStringLiteral("Operation canceled"), 0, 0);
        return;
    }
    // A queued reply may be promoted just as another active cover receives
    // 429. Admission time is NOT permission to bypass the new host cooldown.
    const qint64 remaining = m_state->cooldownRemainingMs(m_logicalHost, m_digest);
    if (remaining > 0) {
        const bool cached = m_dispatcher->coverCacheContains(m_prepared.url());
        releasePermit();
        if (cached)
            startInner(QNetworkRequest::AlwaysCache, /*needsPermit=*/false);
        else
            scheduleSynthetic(QNetworkReply::InternalServerError,
                              cooldownMessage(m_logicalHost, remaining), 429,
                              (remaining + 999) / 1000);
        return;
    }
    startInner(QNetworkRequest::PreferCache, /*needsPermit=*/true);
}

void DeferredCoverReply::releasePermit()
{
    if (m_admitted) {
        m_admitted = false;
        if (m_state)
            m_state->forget(this);
    }
}

void DeferredCoverReply::startInner(QNetworkRequest::CacheLoadControl load, bool needsPermit)
{
    Q_UNUSED(needsPermit);
    m_phase = Phase::Active;
    QNetworkRequest finalRequest = m_prepared;
    finalRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, load);
    m_inner = m_dispatcher->dispatchCoverRequest(m_op, finalRequest, m_outgoing,
                                                 m_logicalUrl, m_logicalHost);
    QNetworkReply *inner = m_inner;

    QObject::connect(inner, &QNetworkReply::metaDataChanged, this, [this] {
        if (m_inner)
            copyMetaFromInner(true);
    });
    QObject::connect(inner, &QNetworkReply::readyRead, this, [this] {
        if (!m_inner)
            return;
        copyMetaFromInner(true);
        emit readyRead();
    });
    QObject::connect(inner, &QNetworkReply::downloadProgress, this,
                     [this](qint64 bytes, qint64 total) {
                         if (m_inner)
                             emit downloadProgress(bytes, total);
                     });
    QObject::connect(inner, &QNetworkReply::uploadProgress, this,
                     [this](qint64 bytes, qint64 total) {
                         if (m_inner)
                             emit uploadProgress(bytes, total);
                     });
    QObject::connect(inner, &QNetworkReply::redirected, this,
                     [this](const QUrl &target) { onInnerRedirected(target); });
    QObject::connect(inner, &QNetworkReply::sslErrors, this,
                     [this](const QList<QSslError> &errors) {
                         if (m_inner)
                             emit sslErrors(errors);
                     });
    QObject::connect(inner, &QNetworkReply::preSharedKeyAuthenticationRequired, this,
                     [this](QSslPreSharedKeyAuthenticator *authenticator) {
                         if (m_inner)
                             emit preSharedKeyAuthenticationRequired(authenticator);
                     });
    QObject::connect(inner, &QNetworkReply::encrypted, this, [this] {
        if (m_inner)
            emit encrypted();
    });
    QObject::connect(inner, &QNetworkReply::finished, this,
                     [this] { onInnerFinished(); });
}

void DeferredCoverReply::discardInner()
{
    if (!m_inner)
        return;
    QNetworkReply *old = m_inner;
    m_inner = nullptr;
    old->disconnect(this);
    old->abort();
    old->deleteLater();
}

void DeferredCoverReply::copyMetaFromInner(bool forceEmit)
{
    if (!m_inner)
        return;
    auto copyAttribute = [this](QNetworkRequest::Attribute attribute) {
        setAttribute(attribute, m_inner->attribute(attribute));
    };
    copyAttribute(QNetworkRequest::HttpStatusCodeAttribute);
    copyAttribute(QNetworkRequest::HttpReasonPhraseAttribute);
    copyAttribute(QNetworkRequest::SourceIsFromCacheAttribute);
    copyAttribute(QNetworkRequest::Http2WasUsedAttribute);
    copyAttribute(QNetworkRequest::ConnectionEncryptedAttribute);
    copyAttribute(QNetworkRequest::RedirectionTargetAttribute);
    const auto pairs = m_inner->rawHeaderPairs();
    for (const auto &pair : pairs)
        setRawHeader(pair.first, pair.second);
    const QVariant contentType = m_inner->header(QNetworkRequest::ContentTypeHeader);
    if (contentType.isValid())
        setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    const QVariant contentLength = m_inner->header(QNetworkRequest::ContentLengthHeader);
    if (contentLength.isValid())
        setHeader(QNetworkRequest::ContentLengthHeader, contentLength);
    if (forceEmit || !m_metaSeen) {
        m_metaSeen = true;
        emit metaDataChanged();
    }
}

void DeferredCoverReply::onInnerFinished()
{
    if (m_done || !m_inner)
        return;
    QNetworkReply *inner = m_inner;
    const int status = inner->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Record suppression BEFORE releasing the permit / pumping queues.
    if (status == 429) {
        bool fallback = false;
        m_state->record429(m_logicalHost, m_digest, inner->rawHeader("Retry-After"), &fallback);
        qInfo("[cover] 429 on %s recorded (retry-after %s)", qUtf8Printable(m_logicalHost),
              fallback ? "policy-fallback" : "server-value");
    } else if (inner->error() == QNetworkReply::NoError && status == 200
               && !inner->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool()) {
        // Only a REAL wire success clears stale Retry-After entries: a warm cache
        // hit during cooldown is not evidence the origin is healthy again.
        m_state->noteSuccess(m_logicalHost, m_digest);
    }
    copyMetaFromInner(true);
    if (inner->error() != QNetworkReply::NoError) {
        setError(inner->error(), inner->errorString());
        emit errorOccurred(inner->error());
    }
    m_done = true;
    releasePermit();
    setFinished(true);
    emit finished();
}

void DeferredCoverReply::onInnerRedirected(const QUrl &target)
{
    if (m_done || !m_inner)
        return;
    const QUrl resolved = target.isRelative() ? m_logicalUrl.resolved(target) : target;
    const QString scheme = resolved.scheme().toLower();
    const bool secureOrigin = m_logicalUrl.scheme().compare(QLatin1String("https"),
                                                            Qt::CaseInsensitive) == 0;
    if (!resolved.isValid() || resolved.host().isEmpty()
        || !resolved.userName().isEmpty() || !resolved.password().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || (secureOrigin && scheme == QLatin1String("http"))) {
        QNetworkReply *inner = m_inner;
        inner->disconnect(this);
        m_inner = nullptr;
        inner->abort();
        inner->deleteLater();
        releasePermit();
        scheduleSynthetic(QNetworkReply::InsecureRedirectError,
                          QStringLiteral("cover-redirect-rejected: unsafe target"),
                          0, 0);
        return;
    }
    const QString destHost = CoverDeliveryState::normalizeHost(resolved.host());
    if (destHost == m_logicalHost
        && scheme == m_logicalUrl.scheme().toLower()) {
        // The logical reply URL/digest must move even for an auto-followed
        // same-host redirect; it is the identity of a subsequent 429.
        m_logicalUrl = resolved;
        m_digest = CoverDeliveryState::urlDigest(resolved);
        setUrl(resolved);
        m_inner->redirectAllowed();
        return;
    }
    hopTo(resolved, destHost);
}

void DeferredCoverReply::hopTo(const QUrl &target, const QString &destHost)
{
    const QByteArray digest = CoverDeliveryState::urlDigest(target);
    const qint64 remaining = m_state->cooldownRemainingMs(destHost, digest);
    if (remaining > 0) {
        discardInner();
        releasePermit();
        scheduleSynthetic(QNetworkReply::InternalServerError,
                          cooldownMessage(destHost, remaining), 429, (remaining + 999) / 1000);
        return;
    }
    // The old host's response is complete at the redirect point: release its
    // permit and never hold it while waiting for another host.
    releasePermit();
    // Drop origin metadata before the new host's response; Location, Content-
    // Length and origin-specific headers may not bleed into the final image.
    const QList<QByteArray> previousHeaders = rawHeaderList();
    for (const QByteArray &header : previousHeaders)
        setRawHeader(header, QByteArray());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, QVariant());
    setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QVariant());
    m_logicalUrl = target;
    m_logicalHost = destHost;
    m_digest = digest;
    setUrl(target);
    m_prepared = m_dispatcher->prepareCoverHop(target);
    const CoverDeliveryState::Admission admission = m_state->acquire(destHost, this);
    if (admission == CoverDeliveryState::Admission::Queued) {
        m_phase = Phase::HopQueued;
        discardInner();
        return;
    }
    if (admission == CoverDeliveryState::Admission::Overflow) {
        discardInner();
        scheduleSynthetic(QNetworkReply::TemporaryNetworkFailureError,
                          QStringLiteral("cover-queue-overflow: host=%1 (redirect destination)")
                              .arg(destHost),
                          0, 0);
        return;
    }
    m_admitted = true;
    discardInner();
    startInner(QNetworkRequest::PreferCache, /*needsPermit=*/true);
}

void DeferredCoverReply::scheduleSynthetic(QNetworkReply::NetworkError error,
                                           const QString &message, int httpStatus,
                                           qint64 retryAfterSeconds)
{
    if (m_syntheticScheduled || m_done)
        return;
    m_syntheticScheduled = true;
    QTimer::singleShot(0, this, [this, error, message, httpStatus, retryAfterSeconds] {
        finalizeSynthetic(error, message, httpStatus, retryAfterSeconds);
    });
}

void DeferredCoverReply::finalizeSynthetic(QNetworkReply::NetworkError error,
                                           const QString &message, int httpStatus,
                                           qint64 retryAfterSeconds)
{
    if (m_done)
        return;
    m_done = true;
    releasePermit();
    if (httpStatus > 0)
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
    if (retryAfterSeconds > 0)
        setRawHeader("Retry-After", QByteArray::number(retryAfterSeconds));
    setError(error, message);
    if (!m_metaSeen) {
        m_metaSeen = true;
        emit metaDataChanged();
    }
    if (error != QNetworkReply::NoError)
        emit errorOccurred(error);
    setFinished(true);
    emit finished();
}

void DeferredCoverReply::abort()
{
    if (m_done)
        return;
    m_aborted = true;
    if (m_phase == Phase::Queued || m_phase == Phase::HopQueued) {
        // Evict the abandoned queue entry immediately, not after a slot frees.
        // Otherwise a canceled shelf can exhaust the bounded queue.
        if (m_state)
            m_state->forget(this);
        m_admitted = false;
        scheduleSynthetic(QNetworkReply::OperationCanceledError,
                          QStringLiteral("Operation canceled"), 0, 0);
        return;
    }
    if (m_inner) {
        m_inner->abort();
        return;
    }
    scheduleSynthetic(QNetworkReply::OperationCanceledError,
                      QStringLiteral("Operation canceled"), 0, 0);
}

qint64 DeferredCoverReply::bytesAvailable() const
{
    return QNetworkReply::bytesAvailable() + (m_inner ? m_inner->bytesAvailable() : 0);
}

qint64 DeferredCoverReply::readData(char *data, qint64 maxlen)
{
    if (m_inner)
        return m_inner->read(data, maxlen);
    return m_done ? -1 : 0;
}

void DeferredCoverReply::ignoreSslErrors()
{
    if (m_inner)
        m_inner->ignoreSslErrors();
}

void DeferredCoverReply::ignoreSslErrors(const QList<QSslError> &errors)
{
    if (m_inner)
        m_inner->ignoreSslErrors(errors);
}
