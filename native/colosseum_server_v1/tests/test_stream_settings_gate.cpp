// Narrow test-only access: no production API or visibility changes.
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>
#define private public
#include "streamserver.h"
#undef private
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>
#include <cstdlib>

static void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
class HeldReply final : public QNetworkReply {
public:
    HeldReply(QNetworkAccessManager::Operation operation, const QNetworkRequest &request, QObject *parent)
        : QNetworkReply(parent) {
        setOperation(operation); setRequest(request); setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() override {}
    qint64 readData(char *, qint64) override { return -1; }
    void complete(bool success = true) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, success ? 200 : 500);
        if (!success) setError(QNetworkReply::InternalServerError, "controlled failure");
        setFinished(true); Q_EMIT finished();
    }
};
class HeldNetwork final : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QList<HeldReply *> requests;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                QIODevice *) override {
        auto *reply = new HeldReply(operation, request, this);
        requests.append(reply); return reply;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory; check(directory.isValid(), "temporary settings directory");
    qputenv("APP_PATH", directory.path().toUtf8());
    qputenv("COLOSSEUM_SERVER1", "1");
    QStandardPaths::setTestModeEnabled(true);
    StreamServer stream;
    delete stream.m_nam;
    auto *network = new HeldNetwork(&stream); stream.m_nam = network;
    const QString hash(40, 'a');
    int ready = 0, played = 0, prefetched = 0;
    QObject::connect(&stream, &StreamServer::streamReady, &app, [&] { ++played; });
    QObject::connect(&stream, &StreamServer::fetchReady, &app, [&] { ++prefetched; });
    QObject::connect(&stream, &StreamServer::readyChanged, &app, [&] {
        ++ready;
        check(stream.m_tuning, "tuning must be gated before synchronous readyChanged");
        stream.play(hash, 0); stream.prefetch(hash, 1);
        check(stream.m_pending.size() == 2, "reentrant play and prefetch stay queued");
        check(network->requests.isEmpty(), "no create before settings POST starts");
    });
    stream.warmUp();
    check(ready == 1 && network->requests.size() == 1, "one synchronous ready and one settings request");
    check(network->requests[0]->url().path() == "/settings", "settings POST precedes create");
    check(stream.m_pending.size() == 2 && played == 0 && prefetched == 0,
          "held settings response cannot release pending streams");
    network->requests[0]->complete();
    check(!stream.m_tuning && stream.m_pending.isEmpty() && network->requests.size() == 3,
          "settings completion releases both pending streams exactly once");
    check(network->requests[1]->url().path() == "/" + hash + "/create"
          && network->requests[2]->url().path() == "/" + hash + "/create", "create routes after tuning");
    network->requests[1]->complete(); network->requests[2]->complete();
    check(played == 1 && prefetched == 1, "play and prefetch retain their delivery mode");

    stream.pushTunedSettings(); auto *old = network->requests.back();
    stream.play(hash, 2);
    stream.pushTunedSettings(); auto *current = network->requests.back();
    const auto count = network->requests.size();
    old->complete();
    check(stream.m_tuning && stream.m_pending.size() == 1 && network->requests.size() == count,
          "stale settings epoch cannot release newer pending work");
    current->complete(false);
    check(!stream.m_tuning && stream.m_pending.isEmpty() && network->requests.size() == count + 1,
          "current settings failure still releases work best-effort");
    network->requests.back()->complete();
    check(played == 2 && prefetched == 1, "best-effort release delivered once");

    stream.pushTunedSettings(); old = network->requests.back();
    stream.play(hash, 3);
    const auto pendingCount = network->requests.size();
    const auto port = stream.m_port; ++stream.m_port;
    old->complete();
    check(stream.m_tuning && stream.m_pending.size() == 1 && network->requests.size() == pendingCount,
          "stale settings port cannot release work for a replacement listener");
    stream.m_port = port;
    stream.markEngineUnavailable("controlled terminal failure");
    check(!stream.m_tuning && stream.m_pending.isEmpty(), "terminal failure retires tuning and queue");
    std::cout << "PASS synchronous ready reentry, held settings, success/failure, epoch/port and teardown gates\n";
}
