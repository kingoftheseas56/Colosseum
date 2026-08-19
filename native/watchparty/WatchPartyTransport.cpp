#include "watchparty/WatchPartyTransport.h"

namespace Colosseum::WatchParty {

bool TransportOpenOptions::isValid(QString* error) const
{
    const auto fail = [error](const QString& detail) {
        if (error)
            *error = detail;
        return false;
    };

    if (!serviceUrl.isValid() || serviceUrl.host().trimmed().isEmpty())
        return fail(QStringLiteral("serviceUrl must contain a valid host"));

    if (serviceUrl.scheme().compare(
            QStringLiteral("wss"), Qt::CaseInsensitive) != 0) {
        return fail(QStringLiteral("serviceUrl must use wss"));
    }

    if (!serviceUrl.userName().isEmpty() || !serviceUrl.password().isEmpty()) {
        return fail(
            QStringLiteral(
                "serviceUrl must not contain embedded credentials"));
    }

    if (serviceUrl.hasFragment())
        return fail(QStringLiteral("serviceUrl must not contain a fragment"));

    if (bearerToken.contains('\r') || bearerToken.contains('\n')) {
        return fail(
            QStringLiteral(
                "bearerToken must not contain header line breaks"));
    }

    return true;
}

QString transportStateName(TransportState state)
{
    switch (state) {
    case TransportState::Closed:
        return QStringLiteral("closed");
    case TransportState::Connecting:
        return QStringLiteral("connecting");
    case TransportState::Connected:
        return QStringLiteral("connected");
    case TransportState::WaitingToReconnect:
        return QStringLiteral("waitingToReconnect");
    case TransportState::Reconnecting:
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("closed");
}

QString transportErrorCodeName(TransportErrorCode code)
{
    switch (code) {
    case TransportErrorCode::None:
        return QStringLiteral("none");
    case TransportErrorCode::InvalidConfiguration:
        return QStringLiteral("invalidConfiguration");
    case TransportErrorCode::NotConnected:
        return QStringLiteral("notConnected");
    case TransportErrorCode::ProtocolRejected:
        return QStringLiteral("protocolRejected");
    case TransportErrorCode::ProtocolVersionMismatch:
        return QStringLiteral("protocolVersionMismatch");
    case TransportErrorCode::MessageTooLarge:
        return QStringLiteral("messageTooLarge");
    case TransportErrorCode::RateLimited:
        return QStringLiteral("rateLimited");
    case TransportErrorCode::SocketError:
        return QStringLiteral("socketError");
    case TransportErrorCode::SendFailed:
        return QStringLiteral("sendFailed");
    }
    return QStringLiteral("unknown");
}

} // namespace Colosseum::WatchParty
