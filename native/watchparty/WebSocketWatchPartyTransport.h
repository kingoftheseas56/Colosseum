#pragma once

#include "watchparty/WatchPartySocket.h"
#include "watchparty/WatchPartyTransport.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>

#include <memory>

namespace Colosseum::WatchParty {

class WebSocketTransport final : public QObject, public ITransport
{
public:
    static constexpr int kMessageRateWindowMs = 10'000;
    static constexpr int kMaxMessagesPerRateWindow = 120;

    explicit WebSocketTransport(QObject* parent = nullptr);
    explicit WebSocketTransport(std::unique_ptr<IWebSocket> socket,
                                QObject* parent = nullptr);
    ~WebSocketTransport() override;

    void setReceiveHandler(ReceiveHandler handler) override;
    void setStateHandler(StateHandler handler) override;
    void setErrorHandler(ErrorHandler handler) override;

    bool open(const TransportOpenOptions& options) override;
    TransportState state() const override { return m_state; }
    bool send(const ProtocolMessage& message) override;
    bool retryNow() override;
    void close() override;

    // Bounded exponential reconnect schedule. attempt is zero-based.
    static int reconnectDelayMs(int attempt);

private:
    void bindSocket();
    void attemptOpen(bool reconnecting);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketText(const QString& text);
    void onSocketBinary(const QByteArray& bytes);
    void onSocketError(const QString& detail);

    void terminalProtocolFailure(TransportErrorCode code,
                                 const QString& detail);
    bool consumeRateSlot(QList<qint64>* timestamps);
    void clearRateWindows();

    void setState(TransportState state);
    void reportError(TransportErrorCode code,
                     const QString& detail,
                     bool terminal = false);

    std::unique_ptr<IWebSocket> m_socket;
    QTimer m_reconnectTimer;
    QElapsedTimer m_rateClock;

    ReceiveHandler m_receiveHandler;
    StateHandler m_stateHandler;
    ErrorHandler m_errorHandler;

    TransportOpenOptions m_options;
    TransportState m_state = TransportState::Closed;
    bool m_shouldReconnect = false;
    int m_reconnectAttempt = 0;

    QList<qint64> m_inboundMessageTimes;
    QList<qint64> m_outboundMessageTimes;
};

} // namespace Colosseum::WatchParty
