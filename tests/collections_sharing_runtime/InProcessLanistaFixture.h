#pragma once
// Test-only transport adapter: same real Lanista handlers/gates/replies and QML.
// No replacement app/store, no network listener, absent from shipping builds.
#ifdef COLOSSEUM_ENABLE_COLLECTIONS_TEST_FIXTURES
#include "devtools/LanistaServer.h"
#include "devtools/CollectionsTestProfileController.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <memory>

inline void startCollectionsSharingFixture(LanistaServer *server, const QString &root)
{
    const QString run = qEnvironmentVariable("COLOSSEUM_COLLECTIONS_RUNTIME_RUN");
    if (run.isEmpty()) return;
    CollectionsTestProfileController guard(root, {}, {});
    if (!guard.enabled() || qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_DRIVE") != 1
        || !QRegularExpression(QStringLiteral("^[a-z0-9_-]{1,48}$")).match(run).hasMatch()) {
        qCritical("COLLECTIONS_RUNTIME fixture containment refused");
        QTimer::singleShot(0, [] { QCoreApplication::exit(73); });
        return;
    }
    const QString directory = QDir(root).filePath("runtime-" + run);
    if (!QDir().mkpath(directory) || QFileInfo(directory).isSymLink()) {
        QTimer::singleShot(0, [] { QCoreApplication::exit(73); }); return;
    }
    struct State { int seq = 1; bool pending = false; QElapsedTimer elapsed; };
    auto state = std::make_shared<State>(); state->elapsed.start();
    auto *timer = new QTimer(server); timer->setInterval(40);
    QObject::connect(timer, &QTimer::timeout, server, [server, root, directory, timer, state] {
        if (state->elapsed.elapsed() > 600000 || state->seq > 1000) {
            timer->stop(); QCoreApplication::exit(74); return;
        }
        if (state->pending) return;
        if (QFileInfo::exists(QDir(directory).filePath("finish"))) {
            timer->stop(); QCoreApplication::quit(); return;
        }
        const QString stem = QStringLiteral("%1").arg(state->seq, 6, 10, QLatin1Char('0'));
        const QString input = QDir(directory).filePath(stem + ".request.json");
        const QString output = QDir(directory).filePath(stem + ".reply.json");
        if (!QFileInfo::exists(input)) return;
        CollectionsTestProfileController guard(root, {}, {});
        QFile file(input);
        if (!guard.enabled() || QFileInfo(input).isSymLink() || QFileInfo(output).exists()
            || !file.open(QIODevice::ReadOnly) || file.size() > 131072) {
            timer->stop(); QCoreApplication::exit(73); return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        const auto request = doc.object();
        static const QSet<QString> allowed = {"ping", "get-state", "qml-get", "ui-query",
            "ui-click", "ui-keypress", "ui-text-input", "ui-scroll", "ui-wait-for",
            "dump-ui", "ui-snapshot", "log-tail", "window-set-state", "collections-test-profile", "invoke-read"};
        if (error.error != QJsonParseError::NoError || !doc.isObject()
            || request.value("seq").toInt() != state->seq
            || !allowed.contains(request.value("cmd").toString())) {
            timer->stop(); QCoreApplication::exit(73); return;
        }
        state->pending = true;
        server->dispatchCollectionsFixture(request, [output, state](QJsonObject reply) {
            QSaveFile file(output);
            const QByteArray bytes = QJsonDocument(reply).toJson(QJsonDocument::Indented);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                QCoreApplication::exit(73); return;
            }
            ++state->seq; state->pending = false;
        });
    });
    timer->start();
    qInfo("COLLECTIONS_RUNTIME real Lanista in-process transport active");
}
#endif
