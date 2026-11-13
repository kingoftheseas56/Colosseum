#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <memory>
#include <iostream>
namespace {
QUrl fixtureUrl;
class LoopbackNetwork : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override {
        QNetworkRequest local(request);
        local.setUrl(fixtureUrl);
        return QNetworkAccessManager::createRequest(operation, local, outgoingData);
    }
};
}
// Test-only transport substitution retains the production allowlist, callbacks,
// and QObject ownership; no public-network dependency or public API seam.
#define QNetworkAccessManager LoopbackNetwork
#include "../src/youtube/YouTubeResolver.cpp"
#undef QNetworkAccessManager
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
    fixtureUrl = QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
    int requests = 0, completed = 0;
    bool errorSeen = false, timedOut = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 500 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        }
    });
    auto resolver = std::make_unique<server1::youtube::YouTubeResolver>();
    resolver->resolve("abcdefghijk", [&](auto result) {
        ++completed; errorSeen = !result.error.isEmpty();
        resolver.reset(); // Deletes QNAM and the emitting reply synchronously.
        QTimer::singleShot(20, &app, &QCoreApplication::quit);
    });
    QTimer::singleShot(5000, &app, [&] { timedOut = true; app.quit(); });
    app.exec();
    if (timedOut || completed != 1 || requests != 3 || !errorSeen || resolver) {
        std::cerr << "callback destruction failed: " << completed << " callbacks, " << requests << " requests\n";
        return 1;
    }
    std::cout << "YouTube transport callback owner destruction passed\n";
}
