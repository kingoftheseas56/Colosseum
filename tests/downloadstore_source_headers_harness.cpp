#include "player/downloadstore.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {
int fail(const char *message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
}

int main(int argc, char **argv) {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ColosseumAudit"));
    QCoreApplication::setApplicationName(QStringLiteral("Function0008Headers"));

    QTemporaryDir temp;
    if (!temp.isValid())
        return fail("temporary directory unavailable");
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return fail("loopback server failed to listen");

    bool sawExpectedHeaders = false;
    const QByteArray body("function-0008-header-ok");
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();
        auto *requestBytes = new QByteArray;
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, requestBytes]() {
            requestBytes->append(socket->readAll());
            if (!requestBytes->contains("\r\n\r\n"))
                return;
            const QByteArray lower = requestBytes->toLower();
            sawExpectedHeaders = lower.contains("referer: https://origin.example/")
                              && lower.contains("origin: https://origin.example")
                              && lower.contains("x-test-token: abc123");
            const QByteArray status = sawExpectedHeaders ? "HTTP/1.1 200 OK\r\n"
                                                         : "HTTP/1.1 403 Forbidden\r\n";
            socket->write(status + "Content-Length: " + QByteArray::number(body.size())
                          + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            delete requestBytes;
        });
    });
    DownloadStore store;
    const QString id = QStringLiteral("audit8-header-%1").arg(QCoreApplication::applicationPid());
    const QString outputPath = temp.filePath(QStringLiteral("download.mp4"));
    const QString url = QStringLiteral("http://127.0.0.1:%1/video.mp4").arg(server.serverPort());

    QObject::connect(&store, &DownloadStore::libraryChanged, &app, [&]() {
        if (!QFile::exists(outputPath))
            return;
        QFile file(outputPath);
        if (!file.open(QIODevice::ReadOnly)) {
            app.exit(fail("downloaded file cannot be opened"));
            return;
        }
        if (!sawExpectedHeaders) {
            app.exit(fail("DownloadStore did not send source request headers"));
            return;
        }
        if (file.readAll() != body) {
            app.exit(fail("downloaded payload mismatch"));
            return;
        }
        const QVariantList videos = store.downloadedVideos();
        bool found = false;
        for (const QVariant &row : videos)
            found = found || row.toMap().value(QStringLiteral("id")).toString() == id;
        if (!found) {
            app.exit(fail("completed download missing from library projection"));
            return;
        }        std::printf("PASS: DownloadStore preserves source request headers on the wire.\n");
        app.quit();
    });

    store.enqueue({
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), QStringLiteral("movie")},
        {QStringLiteral("title"), QStringLiteral("Audit 8 Header")},
        {QStringLiteral("outputPath"), outputPath}
    });
    store.feedSource(id, url, {
        {QStringLiteral("Referer"), QStringLiteral("https://origin.example/")},
        {QStringLiteral("Origin"), QStringLiteral("https://origin.example")},
        {QStringLiteral("X-Test-Token"), QStringLiteral("abc123")}
    });

    QTimer::singleShot(5000, &app, [&]() {
        app.exit(fail("timed out waiting for header-aware download"));
    });
    return app.exec();
}
