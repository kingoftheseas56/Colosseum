#include "TrackerConnectionRouter.h"

#include "MalConnectionController.h"
#include "SimklConnectionController.h"
#include "TraktConnectionController.h"

TrackerConnectionRouter::TrackerConnectionRouter(
    SimklConnectionController *simkl,
    MalConnectionController *mal,
    QObject *parent)
    : TrackerConnectionRouter(simkl, mal, nullptr, parent)
{
}

TrackerConnectionRouter::TrackerConnectionRouter(
    SimklConnectionController *simkl,
    MalConnectionController *mal,
    TraktConnectionController *trakt,
    QObject *parent)
    : QObject(parent), m_simkl(simkl), m_mal(mal), m_trakt(trakt)
{
    setObjectName(QStringLiteral("trackerConnectionRouter"));
    if (m_simkl) {
        connect(m_simkl, &SimklConnectionController::stateChanged,
                this, [this] {
                    if (!malActive() && !traktActive())
                        emit stateChanged();
                });
        connect(m_simkl, &SimklConnectionController::connectionEstablished,
                this, &TrackerConnectionRouter::connectionEstablished);
    }
    if (m_mal) {
        connect(m_mal, &MalConnectionController::stateChanged,
                this, [this] {
                    if (malActive())
                        emit stateChanged();
                });
        connect(m_mal, &MalConnectionController::connectionEstablished,
                this, &TrackerConnectionRouter::connectionEstablished);
    }
    if (m_trakt) {
        connect(m_trakt, &TraktConnectionController::stateChanged,
                this, [this] {
                    if (traktActive())
                        emit stateChanged();
                });
        connect(m_trakt, &TraktConnectionController::connectionEstablished,
                this, &TrackerConnectionRouter::connectionEstablished);
    }
}

bool TrackerConnectionRouter::available() const
{
    if (traktActive()) return m_trakt && m_trakt->available();
    return malActive()
        ? m_mal && m_mal->available()
        : m_simkl && m_simkl->available();
}

bool TrackerConnectionRouter::busy() const
{
    if (traktActive()) return m_trakt && m_trakt->busy();
    return malActive()
        ? m_mal && m_mal->busy()
        : m_simkl && m_simkl->busy();
}

bool TrackerConnectionRouter::moveAvailable() const
{
    if (traktActive()) return m_trakt && m_trakt->moveAvailable();
    return malActive()
        ? m_mal && m_mal->moveAvailable()
        : m_simkl && m_simkl->moveAvailable();
}

QString TrackerConnectionRouter::phase() const
{
    if (traktActive()) return m_trakt ? m_trakt->phase() : QStringLiteral("idle");
    return malActive()
        ? (m_mal ? m_mal->phase() : QStringLiteral("idle"))
        : (m_simkl ? m_simkl->phase() : QStringLiteral("idle"));
}

QString TrackerConnectionRouter::userCode() const
{
    if (traktActive()) return m_trakt ? m_trakt->userCode() : QString();
    return malActive()
        ? (m_mal ? m_mal->userCode() : QString())
        : (m_simkl ? m_simkl->userCode() : QString());
}

QString TrackerConnectionRouter::verificationUrl() const
{
    if (traktActive()) return m_trakt ? m_trakt->verificationUrl() : QString();
    return malActive()
        ? (m_mal ? m_mal->verificationUrl() : QString())
        : (m_simkl ? m_simkl->verificationUrl() : QString());
}

QString TrackerConnectionRouter::statusMessage() const
{
    if (traktActive()) return m_trakt ? m_trakt->statusMessage() : QString();
    return malActive()
        ? (m_mal ? m_mal->statusMessage() : QString())
        : (m_simkl ? m_simkl->statusMessage() : QString());
}

bool TrackerConnectionRouter::beginConnection(
    const QString &providerKey)
{
    const QString normalized = providerKey.trimmed().toLower();
    if (normalized != QLatin1String("simkl")
        && normalized != QLatin1String("mal")
        && normalized != QLatin1String("trakt")) {
        return false;
    }
    if (busy())
        return false;
    if (normalized == QLatin1String("trakt") && !m_trakt)
        return false;
    if (m_activeProviderKey != normalized) {
        m_activeProviderKey = normalized;
        emit stateChanged();
    }
    if (traktActive()) return m_trakt && m_trakt->beginConnection(normalized);
    return malActive()
        ? m_mal && m_mal->beginConnection(normalized)
        : m_simkl && m_simkl->beginConnection(normalized);
}

bool TrackerConnectionRouter::openApprovalPage()
{
    if (traktActive()) return m_trakt && m_trakt->openApprovalPage();
    return malActive()
        ? m_mal && m_mal->openApprovalPage()
        : m_simkl && m_simkl->openApprovalPage();
}

bool TrackerConnectionRouter::moveConnectionToThisProfile()
{
    if (traktActive()) return m_trakt && m_trakt->moveConnectionToThisProfile();
    return malActive()
        ? m_mal && m_mal->moveConnectionToThisProfile()
        : m_simkl && m_simkl->moveConnectionToThisProfile();
}

void TrackerConnectionRouter::cancel()
{
    if (traktActive()) {
        if (m_trakt) m_trakt->cancel();
    } else if (malActive()) {
        if (m_mal) m_mal->cancel();
    } else if (m_simkl) {
        m_simkl->cancel();
    }
}

void TrackerConnectionRouter::dismiss()
{
    if (traktActive()) {
        if (m_trakt) m_trakt->dismiss();
    } else if (malActive()) {
        if (m_mal) m_mal->dismiss();
    } else if (m_simkl) {
        m_simkl->dismiss();
    }
}

bool TrackerConnectionRouter::prepareForProfileDeactivation()
{
    const bool malReady = !m_mal
        || m_mal->prepareForProfileDeactivation();
    const bool simklReady = !m_simkl
        || m_simkl->prepareForProfileDeactivation();
    const bool traktReady = !m_trakt
        || m_trakt->prepareForProfileDeactivation();
    return malReady && simklReady && traktReady;
}
