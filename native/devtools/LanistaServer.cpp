#include "devtools/LanistaServer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>

QString LanistaServer::pipeName()
{
    const QByteArray env = qgetenv("COLOSSEUM_LANISTA_PIPE");
    return env.isEmpty() ? QStringLiteral("ColosseumLanista")
                         : QString::fromUtf8(env);
}

LanistaServer::LanistaServer(QQmlApplicationEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    m_runDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/lanista/runs/")
               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QDir().mkpath(m_runDir);

    // ── command registry ────────────────────────────────────────────────
    auto ro = [](std::function<QJsonObject(const QJsonObject&)> f) {
        return Command{Read, [f](const QJsonObject& p, QLocalSocket*, int, bool*) {
                                 return f(p); }};
    };
    m_commands.insert(QStringLiteral("ping"),
        ro([this](const QJsonObject&) { return cmdPing(); }));
    m_commands.insert(QStringLiteral("get-state"),
        ro([this](const QJsonObject&) { return cmdGetState(); }));

    // A stale pipe from a crashed instance blocks listen(); clear it first.
    QLocalServer::removeServer(pipeName());
    m_server = new QLocalServer(this);
    if (!m_server->listen(pipeName())) {
        qWarning("lanista: listen failed on %s: %s",
                 qUtf8Printable(pipeName()), qUtf8Printable(m_server->errorString()));
        return;
    }
    connect(m_server, &QLocalServer::newConnection,
            this, &LanistaServer::onNewConnection);
    qInfo("lanista: listening on %s (reads always on; drive/write gated)",
          qUtf8Printable(pipeName()));
}

void LanistaServer::onNewConnection()
{
    while (QLocalSocket* sock = m_server->nextPendingConnection()) {
        m_buf.insert(sock, QByteArray());
        connect(sock, &QLocalSocket::readyRead, this,
                [this, sock]() { onReadyRead(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
            m_buf.remove(sock);
            sock->deleteLater();
        });
    }
}

void LanistaServer::onReadyRead(QLocalSocket* sock)
{
    m_buf[sock] += sock->readAll();
    const int nl = m_buf[sock].indexOf('\n');
    if (nl < 0) return;                       // wait for the full line
    const QByteArray line = m_buf[sock].left(nl);
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendError(sock, -1, "BAD_JSON", err.errorString());
        return;
    }
    dispatch(sock, doc.object());
}

void LanistaServer::dispatch(QLocalSocket* sock, const QJsonObject& req)
{
    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    const int seq = req.value(QStringLiteral("seq")).toInt();
    const QJsonObject payload = req.value(QStringLiteral("payload")).toObject();

    const auto it = m_commands.constFind(cmd);
    if (it == m_commands.constEnd()) {
        sendError(sock, seq, "UNKNOWN_CMD", QStringLiteral("no such command: ") + cmd);
        return;
    }
    if (it->gate == Drive && qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_DRIVE") != 1) {
        sendError(sock, seq, "DRIVE_DISABLED",
                  QStringLiteral("set COLOSSEUM_LANISTA_DRIVE=1 to drive the UI"));
        return;
    }
    if (it->gate == Write && qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_WRITE") != 1) {
        sendError(sock, seq, "WRITE_DISABLED",
                  QStringLiteral("set COLOSSEUM_LANISTA_WRITE=1 to mutate state"));
        return;
    }

    bool async = false;
    QJsonObject body = it->fn(payload, sock, seq, &async);
    if (async) return;                        // handler owns the reply
    if (body.contains(QStringLiteral("__error"))) {
        sendError(sock, seq, "CMD_FAILED",
                  body.value(QStringLiteral("__error")).toString());
        return;
    }
    // Task 2 wires attachGrab() here (combined reply); until then, plain reply.
    sendReply(sock, seq, body);
}

void LanistaServer::sendReply(QLocalSocket* sock, int seq, QJsonObject body)
{
    body.insert(QStringLiteral("type"), QStringLiteral("reply"));
    body.insert(QStringLiteral("seq"), seq);
    sock->write(QJsonDocument(body).toJson(QJsonDocument::Compact) + "\n");
    sock->flush();
    sock->disconnectFromServer();
}

void LanistaServer::sendError(QLocalSocket* sock, int seq, const char* code,
                              const QString& msg)
{
    QJsonObject e{{QStringLiteral("type"), QStringLiteral("error")},
                  {QStringLiteral("seq"), seq},
                  {QStringLiteral("code"), QLatin1String(code)},
                  {QStringLiteral("message"), msg}};
    sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + "\n");
    sock->flush();
    sock->disconnectFromServer();
}

QJsonObject LanistaServer::cmdPing() const
{
    QJsonArray cmds;
    for (auto it = m_commands.constBegin(); it != m_commands.constEnd(); ++it)
        cmds.append(it.key());
    return {{QStringLiteral("schema"), QLatin1String(kSchema)},
            {QStringLiteral("pid"), QCoreApplication::applicationPid()},
            {QStringLiteral("commands"), cmds}};
}

QQuickWindow* LanistaServer::mainWindow() const
{
    for (QObject* root : m_engine->rootObjects())
        if (auto* w = qobject_cast<QQuickWindow*>(root))
            return w;
    return nullptr;
}

QQuickItem* LanistaServer::findItem(const QString& objectName) const
{
    for (QObject* root : m_engine->rootObjects()) {
        if (root->objectName() == objectName)
            if (auto* it = qobject_cast<QQuickItem*>(root)) return it;
        if (auto* found = root->findChild<QQuickItem*>(objectName))
            return found;
    }
    return nullptr;
}

QJsonObject LanistaServer::cmdGetState() const
{
    QJsonArray windows;
    for (QObject* root : m_engine->rootObjects()) {
        auto* w = qobject_cast<QQuickWindow*>(root);
        if (!w) continue;
        windows.append(QJsonObject{
            {QStringLiteral("objectName"), w->objectName()},
            {QStringLiteral("title"), w->title()},
            {QStringLiteral("x"), w->x()}, {QStringLiteral("y"), w->y()},
            {QStringLiteral("width"), w->width()},
            {QStringLiteral("height"), w->height()},
            {QStringLiteral("visible"), w->isVisible()},
            {QStringLiteral("active"), w->isActive()}});
    }
    return {{QStringLiteral("windows"), windows},
            {QStringLiteral("runDir"), m_runDir}};
}
