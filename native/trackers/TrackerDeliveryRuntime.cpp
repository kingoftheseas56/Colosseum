#include "TrackerDeliveryRuntime.h"

#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include "ProgressStore.h"

TrackerDeliveryRuntime::TrackerDeliveryRuntime(const ProfilePaths &profile,
                                               ProgressStore *progress,
                                               ActivityStore *activity,
                                               HistoryStore *history,
                                               QObject *parent)
    : QObject(parent),
      m_connections(profile),
      m_mappings(profile),
      m_delivery(profile, &m_mappings, &m_connections),
      m_source(progress, activity, history),
      m_connectionService(&m_connections, &m_mappings, &m_delivery, &m_source,
                          trackerBuiltInProviderCatalog()),
      m_progress(progress),
      m_activity(activity),
      m_history(history)
{}

bool TrackerDeliveryRuntime::start(QString *error)
{
    if (error)
        error->clear();
    if (m_started)
        return true;
    if (!healthy(error))
        return false;

    if (hasSendEnabledProvider()) {
        QString recoveryError;
        if (!m_delivery.recoverSourceGap(&m_source, nullptr, &recoveryError)) {
            setError(recoveryError);
            if (error)
                *error = m_error;
            return false;
        }
    }

    if (m_progress) {
        connect(m_progress, &ProgressStore::durableEntryCurrent, this,
                [this](const QString &kind, const QString &id) {
                    if (!hasSendEnabledProvider())
                        return;
                    observeFact(m_source.currentProgressFact(kind, id));
                });
    }
    if (m_activity) {
        connect(m_activity, &ActivityStore::factCommitted, this,
                [this](const QVariantMap &event) {
                    if (!hasSendEnabledProvider()
                        || event.value(QStringLiteral("type")).toString()
                            != QLatin1String("media_completed"))
                        return;
                    observeFact(m_source.currentActivityCompletionFact(
                        event.value(QStringLiteral("eventId")).toString()));
                });
    }
    if (m_history) {
        connect(m_history, &HistoryStore::trackerLocalCompletionCommitted, this,
                [this](const QVariantMap &event) {
                    if (!hasSendEnabledProvider())
                        return;
                    observeFact(m_source.currentHistoryCompletionFact(
                        event.value(QStringLiteral("eventId")).toString()));
                });
    }
    m_started = true;
    return true;
}

bool TrackerDeliveryRuntime::healthy(QString *error) const
{
    QString detail;
    if (!m_connections.healthy(&detail)
        || !m_mappings.healthy(&detail)
        || !m_delivery.healthy(&detail)
        || (hasSendEnabledProvider() && !m_source.isReady())) {
        if (error) {
            *error = detail.isEmpty()
                ? QStringLiteral("Canonical tracker delivery sources are unavailable.")
                : detail;
        }
        return false;
    }
    if (error)
        error->clear();
    return true;
}

QString TrackerDeliveryRuntime::lastError() const
{
    return m_error;
}

TrackerConnectionStore *TrackerDeliveryRuntime::connectionStore()
{
    return &m_connections;
}

TrackerMappingStore *TrackerDeliveryRuntime::mappingStore()
{
    return &m_mappings;
}

TrackerDeliveryStore *TrackerDeliveryRuntime::deliveryStore()
{
    return &m_delivery;
}

TrackerCanonicalDeliverySource *TrackerDeliveryRuntime::canonicalSource()
{
    return &m_source;
}

TrackerConnectionService *TrackerDeliveryRuntime::connectionService()
{
    return &m_connectionService;
}

bool TrackerDeliveryRuntime::refreshCurrentFacts(QString *error)
{
    return observeCurrentFacts(error);
}

bool TrackerDeliveryRuntime::observeCurrentFacts(QString *error)
{
    if (error)
        error->clear();
    if (!m_started) {
        setError(QStringLiteral("Tracker delivery runtime is not active."));
        if (error)
            *error = m_error;
        return false;
    }
    if (!hasSendEnabledProvider()) {
        m_error.clear();
        return true;
    }
    if (!m_source.isReady()) {
        setError(QStringLiteral("Canonical tracker delivery sources are unavailable."));
        if (error)
            *error = m_error;
        return false;
    }
    for (const TrackerDeliveryFact &fact : m_source.currentCommittedFacts()) {
        QString deliveryError;
        if (!m_delivery.observeCommittedFact(fact, &m_source, nullptr, &deliveryError)
            && !deliveryError.isEmpty()) {
            setError(deliveryError);
            if (error)
                *error = m_error;
            return false;
        }
    }
    m_error.clear();
    return true;
}

void TrackerDeliveryRuntime::observeFact(
    const std::optional<TrackerDeliveryFact> &fact)
{
    if (!m_started || !fact || !hasSendEnabledProvider())
        return;
    QString deliveryError;
    if (!m_delivery.observeCommittedFact(*fact, &m_source, nullptr, &deliveryError)
        && !deliveryError.isEmpty()) {
        setError(deliveryError);
        return;
    }
    m_error.clear();
}

bool TrackerDeliveryRuntime::hasSendEnabledProvider() const
{
    for (const TrackerConnection &connection : m_connections.connections()) {
        if (connection.state == TrackerConnectionState::Connected
            && m_delivery.providerSendEnabled(connection.providerId, connection.remoteAccountId))
            return true;
    }
    return false;
}

void TrackerDeliveryRuntime::setError(const QString &error)
{
    m_error = error.isEmpty()
        ? QStringLiteral("Tracker delivery could not persist the current local intent.")
        : error;
}
