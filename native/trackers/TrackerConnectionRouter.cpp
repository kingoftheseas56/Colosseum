#include "TrackerConnectionRouter.h"

#include "MalConnectionController.h"
#include "SimklConnectionController.h"

TrackerConnectionRouter::TrackerConnectionRouter(
    SimklConnectionController *simkl,
    MalConnectionController *mal,
    QObject *parent)
    : QObject(parent), m_simkl(simkl), m_mal(mal)
{
    setObjectName(QStringLiteral("trackerConnectionRouter"));
    if (m_simkl) {
        connect(m_simkl, &SimklConnectionController::stateChanged,
                this, [this] {
                    if (!malActive())
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
}

bool TrackerConnectionRouter::available() const
{
    return malActive()
        ? m_mal && m_mal->available()
        : m_simkl && m_simkl->available();
}

bool TrackerConnectionRouter::busy() const
{
    return malActive()
        ? m_mal && m_mal->busy()
        : m_simkl && m_simkl->busy();
}

bool TrackerConnectionRouter::moveAvailable() const
{
    return malActive()
        ? m_mal && m_mal->moveAvailable()
        : m_simkl && m_simkl->moveAvailable();
}

QString TrackerConnectionRouter::phase() const
{
    return malActive()
        ? (m_mal ? m_mal->phase() : QStringLiteral("idle"))
        : (m_simkl ? m_simkl->phase() : QStringLiteral("idle"));
}

QString TrackerConnectionRouter::userCode() const
{
    return malActive()
        ? (m_mal ? m_mal->userCode() : QString())
        : (m_simkl ? m_simkl->userCode() : QString());
}

QString TrackerConnectionRouter::verificationUrl() const
{
    return malActive()
        ? (m_mal ? m_mal->verificationUrl() : QString())
        : (m_simkl ? m_simkl->verificationUrl() : QString());
}

QString TrackerConnectionRouter::statusMessage() const
{
    return malActive()
        ? (m_mal ? m_mal->statusMessage() : QString())
        : (m_simkl ? m_simkl->statusMessage() : QString());
}

bool TrackerConnectionRouter::beginConnection(
    const QString &providerKey)
{
    const QString normalized = providerKey.trimmed().toLower();
    if (normalized != QLatin1String("simkl")
        && normalized != QLatin1String("mal")) {
        return false;
    }
    if (busy())
        return false;
    if (m_activeProviderKey != normalized) {
        m_activeProviderKey = normalized;
        emit stateChanged();
    }
    return malActive()
        ? m_mal && m_mal->beginConnection(normalized)
        : m_simkl && m_simkl->beginConnection(normalized);
}

bool TrackerConnectionRouter::openApprovalPage()
{
    return malActive()
        ? m_mal && m_mal->openApprovalPage()
        : m_simkl && m_simkl->openApprovalPage();
}

bool TrackerConnectionRouter::moveConnectionToThisProfile()
{
    return malActive()
        ? m_mal && m_mal->moveConnectionToThisProfile()
        : m_simkl && m_simkl->moveConnectionToThisProfile();
}

void TrackerConnectionRouter::cancel()
{
    if (malActive()) {
        if (m_mal) m_mal->cancel();
    } else if (m_simkl) {
        m_simkl->cancel();
    }
}

void TrackerConnectionRouter::dismiss()
{
    if (malActive()) {
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
    return malReady && simklReady;
}
