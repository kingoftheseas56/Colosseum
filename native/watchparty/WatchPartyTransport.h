#pragma once

#include "watchparty/WatchPartyProtocol.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>

namespace Colosseum::WatchParty {

enum class TransportState {
    Closed,
    Connecting,
    Connected,
    WaitingToReconnect,
    Reconnecting
};

enum class TransportErrorCode {
    None,
    InvalidConfiguration,
    NotConnected,
    ProtocolRejected,
    ProtocolVersionMismatch,
    MessageTooLarge,
    RateLimited,
    SocketError,
    SendFailed
};

struct TransportError {
    TransportErrorCode code = TransportErrorCode::None;
    QString detail;
    bool terminal = false;
};

struct TransportOpenOptions {
    QUrl serviceUrl;

    // Optional account bearer token used only as a WebSocket handshake header.
    // It is transport-private and must never enter ProtocolMessage room state.
    QByteArray bearerToken;

    bool isValid(QString* error = nullptr) const;
};

class ITransport
{
public:
    using ReceiveHandler = std::function<void(const ProtocolMessage&)>;
    using StateHandler = std::function<void(TransportState)>;
    using ErrorHandler = std::function<void(const TransportError&)>;

    virtual ~ITransport() = default;

    virtual void setReceiveHandler(ReceiveHandler handler) = 0;
    virtual void setStateHandler(StateHandler handler) = 0;
    virtual void setErrorHandler(ErrorHandler handler) = 0;

    // open() accepts the configuration and starts an asynchronous connection.
    // A true return means the request was accepted, not that the socket is
    // already connected.
    virtual bool open(const TransportOpenOptions& options) = 0;

    virtual TransportState state() const = 0;
    virtual bool send(const ProtocolMessage& message) = 0;

    // retryNow() advances a transport already waiting to reconnect. Production
    // transports also retry automatically; this seam keeps reconnect deterministic
    // for fake transports and lets a caller respond immediately to restored network.
    virtual bool retryNow() = 0;

    virtual void close() = 0;
};

QString transportStateName(TransportState state);
QString transportErrorCodeName(TransportErrorCode code);

} // namespace Colosseum::WatchParty
