#include "streamserver.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <iostream>
#include <memory>

// Runs the application's real StreamServer bridge without a QML/player shell.
// It must select the in-process composition even if an external service exists.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ColosseumServerTests");
    QCoreApplication::setApplicationName("EmbeddedBridge");
    QStandardPaths::setTestModeEnabled(true);
    qputenv("COLOSSEUM_SERVER1", "1");
    auto stream = std::make_unique<StreamServer>();
    QObject::connect(stream.get(), &StreamServer::streamError, &app,
        [](const QString &message) { std::cerr << message.toStdString() << std::endl; });
    stream->warmUp();
    if (!stream->ready() || stream->starting() || stream->engineUnavailable())
        return 1;
    const QUrl first(stream->streamUrl(QString(40, 'a'), 0));
    stream->warmUp();
    if (QUrl(stream->streamUrl(QString(40, 'a'), 0)).port() != first.port())
        return 2;
    std::cout << "LISTEN " << first.port() << std::endl;
    // A real clean destruction occurs while the Qt loop remains alive, proving
    // an owned listener is retired rather than left to process exit.
    QTimer::singleShot(12000, &app, [&] {
        stream.reset();
        auto *network = new QNetworkAccessManager(&app);
        auto *reply = network->get(QNetworkRequest(QUrl(
            QString("http://127.0.0.1:%1/heartbeat").arg(first.port()))));
        QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply] {
            const bool stopped = reply->error() == QNetworkReply::ConnectionRefusedError;
            reply->deleteLater();
            if (stopped) std::cout << "STOPPED" << std::endl;
            app.exit(stopped ? 0 : 3);
        });
    });
    QTimer::singleShot(20000, &app, [&] { app.exit(4); });
    return app.exec();
}
