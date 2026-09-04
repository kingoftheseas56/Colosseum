#pragma once

#include "HttpRouter.h"

#include <QObject>
#include <QPointer>
#include <QTcpSocket>

#include <memory>

namespace colosseum::server {

class HttpConnection final : public QObject
{
public:
    HttpConnection(QTcpSocket *socket, std::shared_ptr<HttpRouter> router, QObject *parent = nullptr);
    ~HttpConnection() override;

    void abort();

private:
    static constexpr qint64 MaxHeaderBytes = 64 * 1024;
    // Stremio Server 4.20.17 module 172 createApp(): bodyParser.json({ limit: "3mb" }).
    static constexpr qint64 MaxBodyBytes = 3 * 1024 * 1024;

    void onReadyRead();
    void onDisconnected();
    bool tryParseAndDispatch();
    HttpResponse createResponse(const QByteArray &method);
    void sendSimpleError(int status, const QByteArray &message = {});
    void sendHead(int status, QHash<QByteArray, QByteArray> headers, bool streaming);
    void sendData(const QByteArray &data, bool streaming);
    void sendEnd(bool streaming);

    static QByteArray reasonPhrase(int status);
    static QString decodeComponent(QByteArray value);
    static QHash<QString, QStringList> parsePairs(const QByteArray &encoded);

    QTcpSocket *m_socket = nullptr;
    std::shared_ptr<HttpRouter> m_router;
    QByteArray m_buffer;
    bool m_dispatched = false;
    bool m_headWritten = false;
    bool m_finished = false;
    bool m_aborting = false;
    std::shared_ptr<CancellationToken> m_cancellation;
};

} // namespace colosseum::server
