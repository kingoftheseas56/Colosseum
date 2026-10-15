#include "watchparty/FakeWatchPartyTransport.h"

#include <utility>

namespace Colosseum::WatchParty {

FakeTransport::FakeTransport(bool initiallyConnected)
    : m_state(initiallyConnected
                  ? TransportState::Connected
                  : TransportState::Closed)
{
}

void FakeTransport::setReceiveHandler(ReceiveHandler handler)
{
    m_receiveHandler = std::move(handler);
}

void FakeTransport::setStateHandler(StateHandler handler)
{
    m_stateHandler = std::move(handler);
}

void FakeTransport::setErrorHandler(ErrorHandler handler)
{
    m_errorHandler = std::move(handler);
}

bool FakeTransport::open(const TransportOpenOptions& options)
{
    QString error;
    if (!options.isValid(&error)) {
        reportError(TransportErrorCode::InvalidConfiguration, error);
        return false;
    }

    m_lastOpenOptions = options;
    ++m_openCount;
    setState(TransportState::Connected);
    return true;
}

bool FakeTransport::send(const ProtocolMessage& message)
{
    if (!isOpen()) {
        reportError(
            TransportErrorCode::NotConnected,
            QStringLiteral("fake transport is not connected"));
        return false;
    }

    m_sentMessages.append(message);
    return true;
}

bool FakeTransport::retryNow()
{
    if (m_state != TransportState::WaitingToReconnect)
        return false;

    ++m_openCount;
    setState(TransportState::Connected);
    return true;
}

void FakeTransport::close()
{
    setState(TransportState::Closed);
}

void FakeTransport::reopen()
{
    ++m_openCount;
    setState(TransportState::Connected);
}

QList<ProtocolMessage> FakeTransport::takeSentMessages()
{
    QList<ProtocolMessage> messages = m_sentMessages;
    m_sentMessages.clear();
    return messages;
}

bool FakeTransport::injectIncoming(const ProtocolMessage& message)
{
    if (!isOpen() || !m_receiveHandler)
        return false;

    m_receiveHandler(message);
    return true;
}

void FakeTransport::injectError(TransportErrorCode code,
                                const QString& detail,
                                bool terminal)
{
    reportError(code, detail, terminal);
}

void FakeTransport::simulateDisconnect()
{
    if (m_state == TransportState::Closed)
        return;
    setState(TransportState::WaitingToReconnect);
}

void FakeTransport::setState(TransportState state)
{
    if (m_state == state)
        return;

    m_state = state;
    if (m_stateHandler)
        m_stateHandler(m_state);
}

void FakeTransport::reportError(TransportErrorCode code,
                                const QString& detail,
                                bool terminal)
{
    if (m_errorHandler)
        m_errorHandler(TransportError{code, detail, terminal});
}

} // namespace Colosseum::WatchParty
