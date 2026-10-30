#include "TrackerConnectionRouter.h"

#include "AniListConnectionController.h"
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
    : TrackerConnectionRouter(simkl, mal, trakt, nullptr, parent)
{
}

TrackerConnectionRouter::TrackerConnectionRouter(
    SimklConnectionController *simkl,
    MalConnectionController *mal,
    TraktConnectionController *trakt,
    AniListConnectionController *aniList,
    QObject *parent)
    : QObject(parent), m_simkl(simkl), m_mal(mal), m_trakt(trakt),
      m_aniList(aniList)
{
    setObjectName(QStringLiteral("trackerConnectionRouter"));
    if (m_simkl) {
        connect(m_simkl, &SimklConnectionController::stateChanged,
                this, [this] {
                    if (!malActive() && !traktActive() && !aniListActive())
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
    if (m_aniList) {
        connect(m_aniList, &AniListConnectionController::stateChanged,
                this, [this] {
                    if (aniListActive())
                        emit stateChanged();
                });
        // connectionEstablished is deliberately not forwarded: the AniList
        // sync runtime is connected to its controller directly, and this
        // router's signal feeds the other providers' runtimes.
    }
}

bool TrackerConnectionRouter::available() const
{
    if (aniListActive()) return m_aniList && m_aniList->available();
    if (traktActive()) return m_trakt && m_trakt->available();
    return malActive()
        ? m_mal && m_mal->available()
        : m_simkl && m_simkl->available();
}

bool TrackerConnectionRouter::busy() const
{
    if (aniListActive()) return m_aniList && m_aniList->busy();
    if (traktActive()) return m_trakt && m_trakt->busy();
    return malActive()
        ? m_mal && m_mal->busy()
        : m_simkl && m_simkl->busy();
}

bool TrackerConnectionRouter::moveAvailable() const
{
    if (aniListActive()) return false;
    if (traktActive()) return m_trakt && m_trakt->moveAvailable();
    return malActive()
        ? m_mal && m_mal->moveAvailable()
        : m_simkl && m_simkl->moveAvailable();
}

QString TrackerConnectionRouter::phase() const
{
    if (aniListActive()) return m_aniList ? m_aniList->phase() : QStringLiteral("idle");
    if (traktActive()) return m_trakt ? m_trakt->phase() : QStringLiteral("idle");
    return malActive()
        ? (m_mal ? m_mal->phase() : QStringLiteral("idle"))
        : (m_simkl ? m_simkl->phase() : QStringLiteral("idle"));
}

QString TrackerConnectionRouter::userCode() const
{
    if (aniListActive()) return m_aniList ? m_aniList->userCode() : QString();
    if (traktActive()) return m_trakt ? m_trakt->userCode() : QString();
    return malActive()
        ? (m_mal ? m_mal->userCode() : QString())
        : (m_simkl ? m_simkl->userCode() : QString());
}

QString TrackerConnectionRouter::verificationUrl() const
{
    if (aniListActive()) return m_aniList ? m_aniList->verificationUrl() : QString();
    if (traktActive()) return m_trakt ? m_trakt->verificationUrl() : QString();
    return malActive()
        ? (m_mal ? m_mal->verificationUrl() : QString())
        : (m_simkl ? m_simkl->verificationUrl() : QString());
}

QString TrackerConnectionRouter::statusMessage() const
{
    if (aniListActive()) return m_aniList ? m_aniList->statusMessage() : QString();
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
        && normalized != QLatin1String("trakt")
        && normalized != QLatin1String("anilist")) {
        return false;
    }
    if (busy())
        return false;
    if (normalized == QLatin1String("trakt") && !m_trakt)
        return false;
    if (normalized == QLatin1String("anilist") && !m_aniList)
        return false;
    if (m_activeProviderKey != normalized) {
        m_activeProviderKey = normalized;
        emit stateChanged();
    }
    if (aniListActive()) return m_aniList && m_aniList->beginConnection(normalized);
    if (traktActive()) return m_trakt && m_trakt->beginConnection(normalized);
    return malActive()
        ? m_mal && m_mal->beginConnection(normalized)
        : m_simkl && m_simkl->beginConnection(normalized);
}

bool TrackerConnectionRouter::openApprovalPage()
{
    if (aniListActive()) return m_aniList && m_aniList->openApprovalPage();
    if (traktActive()) return m_trakt && m_trakt->openApprovalPage();
    return malActive()
        ? m_mal && m_mal->openApprovalPage()
        : m_simkl && m_simkl->openApprovalPage();
}

bool TrackerConnectionRouter::moveConnectionToThisProfile()
{
    if (aniListActive()) return false;
    if (traktActive()) return m_trakt && m_trakt->moveConnectionToThisProfile();
    return malActive()
        ? m_mal && m_mal->moveConnectionToThisProfile()
        : m_simkl && m_simkl->moveConnectionToThisProfile();
}

void TrackerConnectionRouter::cancel()
{
    if (aniListActive()) {
        if (m_aniList) m_aniList->cancel();
    } else if (traktActive()) {
        if (m_trakt) m_trakt->cancel();
    } else if (malActive()) {
        if (m_mal) m_mal->cancel();
    } else if (m_simkl) {
        m_simkl->cancel();
    }
}

void TrackerConnectionRouter::dismiss()
{
    if (aniListActive()) {
        if (m_aniList) m_aniList->dismiss();
    } else if (traktActive()) {
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
    const bool aniListReady = !m_aniList
        || m_aniList->prepareForProfileDeactivation();
    return malReady && simklReady && traktReady && aniListReady;
}
