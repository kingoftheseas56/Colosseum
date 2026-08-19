#pragma once

#include "watchparty/WatchPartyTransport.h"

#include <QList>

namespace Colosseum::WatchParty {

class FakeTransport final : public ITransport
{
public:
    explicit FakeTransport(bool initiallyConnected = true);

    void setReceiveHandler(ReceiveHandler handler) override;
    void setStateHandler(StateHandler handler) override;
    void setErrorHandler(ErrorHandler handler) override;

    bool open(const TransportOpenOptions& options) override;
    TransportState state() const override { return m_state; }
    bool send(const ProtocolMessage& message) override;
    bool retryNow() override;
    void close() override;

    bool isOpen() const { return m_state == TransportState::Connected; }
    void reopen();

    const TransportOpenOptions& lastOpenOptions() const { return m_lastOpenOptions; }
    int openCount() const { return m_openCount; }

    const QList<ProtocolMessage>& sentMessages() const { return m_sentMessages; }
    QList<ProtocolMessage> takeSentMessages();
    void clearSentMessages() { m_sentMessages.clear(); }

    bool injectIncoming(const ProtocolMessage& message);
    void injectError(TransportErrorCode code,
                     const QString& detail,
                     bool terminal = false);
    void simulateDisconnect();

private:
    void setState(TransportState state);
    void reportError(TransportErrorCode code,
                     const QString& detail,
                     bool terminal = false);

    ReceiveHandler m_receiveHandler;
    StateHandler m_stateHandler;
    ErrorHandler m_errorHandler;
    QList<ProtocolMessage> m_sentMessages;
    TransportOpenOptions m_lastOpenOptions;
    TransportState m_state = TransportState::Connected;
    int m_openCount = 0;
};

} // namespace Colosseum::WatchParty
