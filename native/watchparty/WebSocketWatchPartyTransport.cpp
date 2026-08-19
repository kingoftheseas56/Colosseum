#include "watchparty/WebSocketWatchPartyTransport.h"

#include "watchparty/QtWatchPartySocket.h"

#include <QNetworkRequest>

#include <utility>

namespace Colosseum::WatchParty {

WebSocketTransport::WebSocketTransport(QObject* parent)
    : WebSocketTransport(std::make_unique<QtWatchPartySocket>(), parent)
{
}

WebSocketTransport::WebSocketTransport(
    std::unique_ptr<IWebSocket> socket,
    QObject* parent)
    : QObject(parent),
      m_socket(std::move(socket))
{
    m_reconnectTimer.setSingleShot(true);
    QObject::connect(
        &m_reconnectTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_shouldReconnect
                && m_state == TransportState::WaitingToReconnect) {
                attemptOpen(true);
            }
        });

    m_rateClock.start();
    bindSocket();
}

WebSocketTransport::~WebSocketTransport()
{
    m_shouldReconnect = false;
    m_reconnectTimer.stop();
    if (m_socket)
        m_socket->close();
}

void WebSocketTransport::setReceiveHandler(ReceiveHandler handler)
{
    m_receiveHandler = std::move(handler);
}

void WebSocketTransport::setStateHandler(StateHandler handler)
{
    m_stateHandler = std::move(handler);
}

void WebSocketTransport::setErrorHandler(ErrorHandler handler)
{
    m_errorHandler = std::move(handler);
}

bool WebSocketTransport::open(const TransportOpenOptions& options)
{
    if (m_state != TransportState::Closed) {
        reportError(
            TransportErrorCode::InvalidConfiguration,
            QStringLiteral(
                "transport must be closed before opening a new endpoint"));
        return false;
    }

    QString error;
    if (!options.isValid(&error)) {
        reportError(TransportErrorCode::InvalidConfiguration, error);
        return false;
    }

    if (!m_socket) {
        reportError(
            TransportErrorCode::InvalidConfiguration,
            QStringLiteral("transport has no WebSocket implementation"));
        return false;
    }

    m_options = options;
    m_shouldReconnect = true;
    m_reconnectAttempt = 0;
    clearRateWindows();
    m_reconnectTimer.stop();
    attemptOpen(false);
    return true;
}

bool WebSocketTransport::send(const ProtocolMessage& message)
{
    if (m_state != TransportState::Connected) {
        reportError(
            TransportErrorCode::NotConnected,
            QStringLiteral("Watch Party transport is not connected"));
        return false;
    }

    const ValidationResult validation =
        validateMessage(message, MessageDirection::ClientToServer);
    if (!validation.ok) {
        reportError(
            TransportErrorCode::ProtocolRejected,
            QStringLiteral("outbound message rejected: %1")
                .arg(validation.error));
        return false;
    }

    const QByteArray encoded = encodeMessage(message);
    if (encoded.size() > kMaxWireMessageBytes) {
        reportError(
            TransportErrorCode::MessageTooLarge,
            QStringLiteral("outbound message exceeds wire limit"));
        return false;
    }

    if (!consumeRateSlot(&m_outboundMessageTimes)) {
        reportError(
            TransportErrorCode::RateLimited,
            QStringLiteral("outbound Watch Party message rate exceeded"));
        return false;
    }

    const qint64 sent =
        m_socket->sendTextMessage(QString::fromUtf8(encoded));
    if (sent < 0) {
        reportError(
            TransportErrorCode::SendFailed,
            QStringLiteral("WebSocket rejected outbound text message"));
        return false;
    }

    return true;
}

bool WebSocketTransport::retryNow()
{
    if (!m_shouldReconnect
        || m_state != TransportState::WaitingToReconnect) {
        return false;
    }

    m_reconnectTimer.stop();
    attemptOpen(true);
    return true;
}

void WebSocketTransport::close()
{
    m_shouldReconnect = false;
    m_reconnectTimer.stop();
    if (m_socket)
        m_socket->close();
    setState(TransportState::Closed);
}

int WebSocketTransport::reconnectDelayMs(int attempt)
{
    static constexpr int delays[] = {
        500,
        1'000,
        2'000,
        5'000,
        10'000
    };

    if (attempt <= 0)
        return delays[0];

    constexpr int last =
        static_cast<int>(sizeof(delays) / sizeof(delays[0])) - 1;
    if (attempt >= last)
        return delays[last];
    return delays[attempt];
}

void WebSocketTransport::bindSocket()
{
    if (!m_socket)
        return;

    m_socket->setConnectedHandler(
        [this]() { onSocketConnected(); });
    m_socket->setDisconnectedHandler(
        [this]() { onSocketDisconnected(); });
    m_socket->setTextHandler(
        [this](const QString& text) { onSocketText(text); });
    m_socket->setBinaryHandler(
        [this](const QByteArray& bytes) { onSocketBinary(bytes); });
    m_socket->setErrorHandler(
        [this](const QString& detail) { onSocketError(detail); });

    m_socket->setMaxAllowedIncomingMessageSize(
        static_cast<quint64>(kMaxWireMessageBytes));
}

void WebSocketTransport::attemptOpen(bool reconnecting)
{
    if (!m_socket || !m_shouldReconnect)
        return;

    QNetworkRequest request(m_options.serviceUrl);
    if (!m_options.bearerToken.isEmpty()) {
        request.setRawHeader(
            QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") + m_options.bearerToken);
    }
    request.setRawHeader(
        QByteArrayLiteral("X-Colosseum-Watch-Party-Protocol"),
        QByteArray::number(kProtocolVersion));
    request.setRawHeader(
        QByteArrayLiteral("Cache-Control"),
        QByteArrayLiteral("no-cache"));

    setState(
        reconnecting
            ? TransportState::Reconnecting
            : TransportState::Connecting);
    m_socket->open(request);
}

void WebSocketTransport::onSocketConnected()
{
    if (!m_shouldReconnect)
        return;

    m_reconnectTimer.stop();
    m_reconnectAttempt = 0;
    clearRateWindows();
    setState(TransportState::Connected);
}

void WebSocketTransport::onSocketDisconnected()
{
    if (m_state == TransportState::Closed)
        return;

    if (!m_shouldReconnect) {
        setState(TransportState::Closed);
        return;
    }

    if (m_state == TransportState::WaitingToReconnect)
        return;

    setState(TransportState::WaitingToReconnect);
    const int delay = reconnectDelayMs(m_reconnectAttempt);
    ++m_reconnectAttempt;
    m_reconnectTimer.start(delay);
}

void WebSocketTransport::onSocketText(const QString& text)
{
    if (m_state != TransportState::Connected)
        return;

    const QByteArray bytes = text.toUtf8();
    if (bytes.size() > kMaxWireMessageBytes) {
        terminalProtocolFailure(
            TransportErrorCode::MessageTooLarge,
            QStringLiteral("inbound message exceeds wire limit"));
        return;
    }

    if (!consumeRateSlot(&m_inboundMessageTimes)) {
        terminalProtocolFailure(
            TransportErrorCode::RateLimited,
            QStringLiteral("inbound Watch Party message rate exceeded"));
        return;
    }

    const DecodeResult decoded = decodeMessage(bytes);
    if (!decoded.ok) {
        TransportErrorCode code = TransportErrorCode::ProtocolRejected;
        if (decoded.errorCode == DecodeError::MessageTooLarge)
            code = TransportErrorCode::MessageTooLarge;
        else if (decoded.errorCode == DecodeError::UnsupportedVersion)
            code = TransportErrorCode::ProtocolVersionMismatch;

        terminalProtocolFailure(code, decoded.error);
        return;
    }

    const ValidationResult validation =
        validateMessage(
            decoded.message,
            MessageDirection::ServerToClient);
    if (!validation.ok) {
        terminalProtocolFailure(
            TransportErrorCode::ProtocolRejected,
            validation.error);
        return;
    }

    if (m_receiveHandler)
        m_receiveHandler(decoded.message);
}

void WebSocketTransport::onSocketBinary(const QByteArray&)
{
    if (m_state != TransportState::Connected)
        return;

    terminalProtocolFailure(
        TransportErrorCode::ProtocolRejected,
        QStringLiteral(
            "binary Watch Party messages are not supported"));
}

void WebSocketTransport::onSocketError(const QString& detail)
{
    reportError(
        TransportErrorCode::SocketError,
        detail.trimmed().isEmpty()
            ? QStringLiteral("WebSocket error")
            : detail);

    if (m_state == TransportState::Connecting
        || m_state == TransportState::Reconnecting) {
        if (m_socket)
            m_socket->close();
        onSocketDisconnected();
    }
}

void WebSocketTransport::terminalProtocolFailure(
    TransportErrorCode code,
    const QString& detail)
{
    m_shouldReconnect = false;
    m_reconnectTimer.stop();
    reportError(code, detail, true);
    if (m_socket)
        m_socket->close();
    setState(TransportState::Closed);
}

bool WebSocketTransport::consumeRateSlot(QList<qint64>* timestamps)
{
    if (!timestamps)
        return false;

    if (!m_rateClock.isValid())
        m_rateClock.start();

    const qint64 now = m_rateClock.elapsed();
    const qint64 cutoff = now - kMessageRateWindowMs;

    while (!timestamps->isEmpty()
           && timestamps->first() <= cutoff) {
        timestamps->removeFirst();
    }

    if (timestamps->size() >= kMaxMessagesPerRateWindow)
        return false;

    timestamps->append(now);
    return true;
}

void WebSocketTransport::clearRateWindows()
{
    m_inboundMessageTimes.clear();
    m_outboundMessageTimes.clear();
    m_rateClock.restart();
}

void WebSocketTransport::setState(TransportState state)
{
    if (m_state == state)
        return;

    m_state = state;
    if (m_stateHandler)
        m_stateHandler(m_state);
}

void WebSocketTransport::reportError(
    TransportErrorCode code,
    const QString& detail,
    bool terminal)
{
    if (m_errorHandler)
        m_errorHandler(TransportError{code, detail, terminal});
}

} // namespace Colosseum::WatchParty
