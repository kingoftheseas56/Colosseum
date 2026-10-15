#include <QCoreApplication>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QRegularExpression>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray mode = qgetenv("COLOSSEUM_FAKE_STREMIO_MODE");
    if (mode == QByteArrayLiteral("hang-before-ready"))
        return app.exec();

    if (argc < 2)
        return 3;
    QFile script(QString::fromLocal8Bit(argv[1]));
    if (!script.open(QIODevice::ReadOnly))
        return 4;
    const QRegularExpression portExpression(QStringLiteral(R"(port\s*=\s*(\d+))"));
    const auto portMatch = portExpression.match(QString::fromUtf8(script.readAll()));
    if (!portMatch.hasMatch())
        return 5;
    const int configuredPort = portMatch.captured(1).toInt();
    if (configuredPort <= 0 || configuredPort > 65535)
        return 6;
    const quint16 port = static_cast<quint16>(configuredPort);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, port))
        return 2;

    std::printf("EngineFS server started at http://127.0.0.1:%u\n", port);
    std::fflush(stdout);

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, mode]() {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &server, mode]() {
                const QByteArray request = socket->readAll();
                if (!request.contains("\r\n\r\n"))
                    return;

                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
                socket->disconnectFromHost();

                const QString markerPath =
                    QString::fromUtf8(qgetenv("COLOSSEUM_FAKE_STREMIO_MARKER"));
                if (mode == QByteArrayLiteral("drop-once") && !markerPath.isEmpty()
                    && !QFile::exists(markerPath)) {
                    QFile marker(markerPath);
                    if (marker.open(QIODevice::WriteOnly)) {
                        marker.write("dropped");
                        marker.close();
                    }
                    QTimer::singleShot(0, &server, &QTcpServer::close);
                }
            });
        }
    });

    return app.exec();
}
