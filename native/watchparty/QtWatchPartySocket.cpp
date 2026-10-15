#include "watchparty/QtWatchPartySocket.h"

#include <QtWebSockets/QWebSocket>

#include <utility>

namespace Colosseum::WatchParty {

QtWatchPartySocket::QtWatchPartySocket()
    : m_socket(std::make_unique<QWebSocket>())
{
    QObject::connect(
        m_socket.get(),
        &QWebSocket::connected,
        m_socket.get(),
        [this]() {
            if (m_connectedHandler)
                m_connectedHandler();
        });

    QObject::connect(
        m_socket.get(),
        &QWebSocket::disconnected,
        m_socket.get(),
        [this]() {
            if (m_disconnectedHandler)
                m_disconnectedHandler();
        });

    QObject::connect(
        m_socket.get(),
        &QWebSocket::textMessageReceived,
        m_socket.get(),
        [this](const QString& message) {
            if (m_textHandler)
                m_textHandler(message);
        });

    QObject::connect(
        m_socket.get(),
        &QWebSocket::binaryMessageReceived,
        m_socket.get(),
        [this](const QByteArray& message) {
            if (m_binaryHandler)
                m_binaryHandler(message);
        });

    QObject::connect(
        m_socket.get(),
        QOverload<QAbstractSocket::SocketError>::of(
            &QWebSocket::errorOccurred),
        m_socket.get(),
        [this](QAbstractSocket::SocketError) {
            if (m_errorHandler)
                m_errorHandler(m_socket->errorString());
        });
}

QtWatchPartySocket::~QtWatchPartySocket() = default;

void QtWatchPartySocket::setConnectedHandler(ConnectedHandler handler)
{
    m_connectedHandler = std::move(handler);
}

void QtWatchPartySocket::setDisconnectedHandler(DisconnectedHandler handler)
{
    m_disconnectedHandler = std::move(handler);
}

void QtWatchPartySocket::setTextHandler(TextHandler handler)
{
    m_textHandler = std::move(handler);
}

void QtWatchPartySocket::setBinaryHandler(BinaryHandler handler)
{
    m_binaryHandler = std::move(handler);
}

void QtWatchPartySocket::setErrorHandler(ErrorHandler handler)
{
    m_errorHandler = std::move(handler);
}

void QtWatchPartySocket::setMaxAllowedIncomingMessageSize(quint64 bytes)
{
    m_socket->setMaxAllowedIncomingMessageSize(bytes);
}

void QtWatchPartySocket::open(const QNetworkRequest& request)
{
    m_socket->open(request);
}

qint64 QtWatchPartySocket::sendTextMessage(const QString& message)
{
    return m_socket->sendTextMessage(message);
}

void QtWatchPartySocket::close()
{
    m_socket->close();
}

} // namespace Colosseum::WatchParty
