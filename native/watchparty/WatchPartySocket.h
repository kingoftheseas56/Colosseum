#pragma once

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace Colosseum::WatchParty {

class IWebSocket
{
public:
    using ConnectedHandler = std::function<void()>;
    using DisconnectedHandler = std::function<void()>;
    using TextHandler = std::function<void(const QString&)>;
    using BinaryHandler = std::function<void(const QByteArray&)>;
    using ErrorHandler = std::function<void(const QString&)>;

    virtual ~IWebSocket() = default;

    virtual void setConnectedHandler(ConnectedHandler handler) = 0;
    virtual void setDisconnectedHandler(DisconnectedHandler handler) = 0;
    virtual void setTextHandler(TextHandler handler) = 0;
    virtual void setBinaryHandler(BinaryHandler handler) = 0;
    virtual void setErrorHandler(ErrorHandler handler) = 0;

    virtual void setMaxAllowedIncomingMessageSize(quint64 bytes) = 0;
    virtual void open(const QNetworkRequest& request) = 0;
    virtual qint64 sendTextMessage(const QString& message) = 0;
    virtual void close() = 0;
};

} // namespace Colosseum::WatchParty
