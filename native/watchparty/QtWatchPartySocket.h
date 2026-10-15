#pragma once

#include "watchparty/WatchPartySocket.h"

#include <memory>

class QWebSocket;

namespace Colosseum::WatchParty {

class QtWatchPartySocket final : public IWebSocket
{
public:
    QtWatchPartySocket();
    ~QtWatchPartySocket() override;

    void setConnectedHandler(ConnectedHandler handler) override;
    void setDisconnectedHandler(DisconnectedHandler handler) override;
    void setTextHandler(TextHandler handler) override;
    void setBinaryHandler(BinaryHandler handler) override;
    void setErrorHandler(ErrorHandler handler) override;

    void setMaxAllowedIncomingMessageSize(quint64 bytes) override;
    void open(const QNetworkRequest& request) override;
    qint64 sendTextMessage(const QString& message) override;
    void close() override;

private:
    std::unique_ptr<QWebSocket> m_socket;
    ConnectedHandler m_connectedHandler;
    DisconnectedHandler m_disconnectedHandler;
    TextHandler m_textHandler;
    BinaryHandler m_binaryHandler;
    ErrorHandler m_errorHandler;
};

} // namespace Colosseum::WatchParty
