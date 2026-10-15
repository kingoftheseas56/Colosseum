#include "streamserver.h"

namespace {
const QString kUnavailableMessage = QStringLiteral(
    "Colosseum Server is not available on Android yet");
}

StreamServer::StreamServer(QObject *parent)
    : QObject(parent)
{
    m_engineUnavailable = true;
}

StreamServer::~StreamServer() = default;

void StreamServer::setEngineUnavailable(bool unavailable)
{
    if (m_engineUnavailable == unavailable)
        return;
    m_engineUnavailable = unavailable;
    emit engineUnavailableChanged();
}

void StreamServer::play(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    emit streamError(kUnavailableMessage);
}

void StreamServer::prefetch(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    emit streamError(kUnavailableMessage);
}

void StreamServer::warmUp()
{
    emit streamError(kUnavailableMessage);
}

QString StreamServer::streamUrl(const QString &infoHash, int fileIdx) const
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
    return {};
}

void StreamServer::watchStats(const QString &infoHash, int fileIdx)
{
    Q_UNUSED(infoHash)
    Q_UNUSED(fileIdx)
}

void StreamServer::unwatchStats()
{
}
