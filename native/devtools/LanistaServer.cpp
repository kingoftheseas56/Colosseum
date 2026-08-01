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
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace {
// The listener is ALWAYS ON in Hemanth's daily app, so the two things a local
// client can do to it without sending a single valid command — flood it and sit
// on it — both need a ceiling. These are the only knobs that matter.
constexpr int kMaxLineBytes = 1 << 20;        // 1 MiB; no honest command is close
constexpr int kDefaultIdleTimeoutMs = 10000;  // connected but silent -> hang up
}  // namespace

// ── Replier ────────────────────────────────────────────────────────────────
// The whole point: a handler can hold this across event-loop turns and the
// client is still free to vanish. See the contract comment in the header.

LanistaServer::Replier::Replier(QLocalSocket* sock, int seq)
    : m_state(QSharedPointer<State>::create())
{
    m_state->sock = sock;
    m_state->seq = seq;
}

bool LanistaServer::Replier::canReply() const
{
    return m_state && !m_state->sent && !m_state->sock.isNull()
           && m_state->sock->state() == QLocalSocket::ConnectedState;
}

void LanistaServer::Replier::reply(QJsonObject body)
{
    body.insert(QStringLiteral("type"), QStringLiteral("reply"));
    body.insert(QStringLiteral("seq"), m_state ? m_state->seq : -1);
    send(std::move(body));
}

void LanistaServer::Replier::fail(const char* code, const QString& message)
{
    send(QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                     {QStringLiteral("seq"), m_state ? m_state->seq : -1},
                     {QStringLiteral("code"), QString::fromLatin1(code)},
                     {QStringLiteral("message"), message}});
}

void LanistaServer::Replier::send(QJsonObject line)
{
    if (!canReply())
        return;
    m_state->sent = true;
    QLocalSocket* sock = m_state->sock.data();
    sock->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + "\n");
    sock->flush();
    sock->disconnectFromServer();
}

// ── server ─────────────────────────────────────────────────────────────────

QString LanistaServer::pipeName()
{
    const QByteArray env = qgetenv("COLOSSEUM_LANISTA_PIPE");
    return env.isEmpty() ? QStringLiteral("ColosseumLanista")
                         : QString::fromUtf8(env);
}

bool LanistaServer::driveOpen()
{
    return qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_DRIVE") == 1;
}

bool LanistaServer::writeOpen()
{
    return qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_WRITE") == 1;
}

LanistaServer::LanistaServer(QQmlApplicationEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    // Path only — the directory is created lazily by ensureRunDir(). The pid
    // keeps two instances started in the same second from sharing a run.
    m_runDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/lanista/runs/")
               + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
               + QStringLiteral("-") + QString::number(QCoreApplication::applicationPid());

    m_idleTimeoutMs = qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_IDLE_MS");
    if (m_idleTimeoutMs <= 0)
        m_idleTimeoutMs = kDefaultIdleTimeoutMs;   // the override exists for tests

    // ── command registry ────────────────────────────────────────────────
    addRead(QStringLiteral("ping"),
            [this](const QJsonObject&, Replier reply) { reply.reply(cmdPing()); });
    addRead(QStringLiteral("get-state"),
            [this](const QJsonObject&, Replier reply) { reply.reply(cmdGetState()); });

    if (qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_SELFTEST") == 1)
        registerSelfTestCommands();

    m_server = new QLocalServer(this);
    // State the boundary instead of inheriting whatever the default happens to
    // be: this pipe is for the user who owns this desktop session, nobody else.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    // Clears a LEFTOVER endpoint from a crashed instance. That is a Unix concern
    // — a stale socket file outlives its process and blocks bind(). On Windows a
    // named pipe dies with its owner, so this is a no-op there. Kept for
    // portability. It does NOT let a second instance steal the pipe from a live
    // one: that listen() simply fails, and isListening()/listenError() say so.
    QLocalServer::removeServer(pipeName());
    if (!m_server->listen(pipeName())) {
        m_listenError = m_server->errorString();
        qWarning("lanista: listen failed on %s: %s",
                 qUtf8Printable(pipeName()), qUtf8Printable(m_listenError));
        return;
    }
    connect(m_server, &QLocalServer::newConnection,
            this, &LanistaServer::onNewConnection);
    qInfo("lanista: listening on %s (reads always on; drive/write gated)",
          qUtf8Printable(pipeName()));
}

bool LanistaServer::isListening() const
{
    return m_server && m_server->isListening();
}

QString LanistaServer::ensureRunDir()
{
    if (!m_runDirCreated) {
        QDir().mkpath(m_runDir);
        m_runDirCreated = true;
    }
    return m_runDir;
}

void LanistaServer::addCommand(Gate gate, const QString& name, Handler fn)
{
    m_commands.insert(name, Command{gate, std::move(fn)});
}

void LanistaServer::addRead(const QString& name, Handler fn)
{
    addCommand(Gate::Read, name, std::move(fn));
}

void LanistaServer::addDrive(const QString& name, Handler fn)
{
    addCommand(Gate::Drive, name, std::move(fn));
}

void LanistaServer::addWrite(const QString& name, Handler fn)
{
    addCommand(Gate::Write, name, std::move(fn));
}

void LanistaServer::registerSelfTestCommands()
{
    // Fixtures for tests/lanista_harness.cpp and NOTHING else. Registered only
    // when COLOSSEUM_LANISTA_SELFTEST=1, so the daily app never exposes them.
    // They exist so that the gate denials, the coded-error path and above all
    // the ASYNC reply path are proven by a test BEFORE Tasks 2-5 build on them.
    addRead(QStringLiteral("selftest-dispatches"), [this](const QJsonObject&, Replier reply) {
        // How many command lines this server has dispatched, ever. The framing
        // regression test watches this: a re-dispatched line is invisible on the
        // wire (the duplicate reply lands after the socket has been closed) but
        // it is plain to see here.
        reply.reply({{QStringLiteral("dispatches"), m_dispatchCount}});
    });
    addRead(QStringLiteral("selftest-slow"), [this](const QJsonObject&, Replier reply) {
        // The exact shape Task 2's grabs will use: keep the token, answer later.
        QTimer::singleShot(250, this, [reply]() mutable {
            reply.reply({{QStringLiteral("slow"), true}});
        });
    });
    addRead(QStringLiteral("selftest-orphan"), [this](const QJsonObject&, Replier reply) {
        // The client is expected to hang up BEFORE this fires. A crash is not a
        // dependable signal here (writing to a freed socket often just succeeds
        // quietly), so record what the token itself decided: once the client is
        // gone the answer must be "no". A raw QLocalSocket* would say "yes".
        m_orphanChecked = false;
        m_orphanCouldReply = false;
        QTimer::singleShot(250, this, [this, reply]() mutable {
            m_orphanCouldReply = reply.canReply();
            m_orphanChecked = true;
            reply.reply({{QStringLiteral("orphan"), true}});   // must be a no-op
        });
    });
    addRead(QStringLiteral("selftest-orphan-result"), [this](const QJsonObject&, Replier reply) {
        reply.reply({{QStringLiteral("checked"), m_orphanChecked},
                     {QStringLiteral("couldReply"), m_orphanCouldReply}});
    });
    addRead(QStringLiteral("selftest-fail"), [](const QJsonObject&, Replier reply) {
        reply.fail("SELFTEST_FAILURE", QStringLiteral("deliberate failure"));
    });
    addDrive(QStringLiteral("selftest-drive"), [](const QJsonObject&, Replier reply) {
        reply.reply({{QStringLiteral("drove"), true}});
    });
    addWrite(QStringLiteral("selftest-write"), [](const QJsonObject&, Replier reply) {
        reply.reply({{QStringLiteral("wrote"), true}});
    });
}

void LanistaServer::onNewConnection()
{
    while (QLocalSocket* sock = m_server->nextPendingConnection()) {
        m_conns.insert(sock, Conn{});
        connect(sock, &QLocalSocket::readyRead, this,
                [this, sock]() { onReadyRead(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
            m_conns.remove(sock);
            sock->deleteLater();
        });
        // A client that connects and never speaks must not be held forever.
        // `sock` is the context object, so this timer dies with the socket.
        QTimer::singleShot(m_idleTimeoutMs, sock, [this, sock]() {
            const auto it = m_conns.constFind(sock);
            if (it == m_conns.constEnd() || it->spent)
                return;   // already answered, or a command is still in flight
            Replier(sock, -1).fail("IDLE_TIMEOUT",
                QStringLiteral("no command within %1 ms").arg(m_idleTimeoutMs));
        });
    }
}

void LanistaServer::onReadyRead(QLocalSocket* sock)
{
    const auto it = m_conns.find(sock);
    if (it == m_conns.end()) {
        // Torn down already. Never use operator[] here: it would silently
        // re-insert an entry that nothing will ever remove.
        sock->readAll();
        return;
    }
    Conn& conn = *it;

    if (conn.spent) {
        // One command per connection. Its reply is already sent, or still in
        // flight on an async handler, so there is no honest way to answer these
        // bytes. Discard them — never buffer them, and above all never let them
        // re-dispatch a line that has already been served.
        sock->readAll();
        conn.buf.clear();
        return;
    }

    conn.buf += sock->readAll();

    const qsizetype nl = conn.buf.indexOf('\n');
    if (nl < 0) {
        if (conn.buf.size() > kMaxLineBytes) {
            conn.spent = true;
            conn.buf.clear();
            Replier(sock, -1).fail("LINE_TOO_LONG",
                QStringLiteral("command line exceeded %1 bytes").arg(kMaxLineBytes));
        }
        return;   // an incomplete line: wait for the rest
    }

    const QByteArray line = conn.buf.left(nl);
    conn.buf.remove(0, nl + 1);   // CONSUME it — a line is dispatched exactly once
    conn.spent = true;

    if (!conn.buf.trimmed().isEmpty()) {
        // A pipelined second command is a protocol error, not something to drop
        // in silence — the client would wait forever for a reply that is never
        // coming. (Trailing whitespace is tolerated: CRLF clients are fine.)
        conn.buf.clear();
        Replier(sock, -1).fail("EXTRA_INPUT",
            QStringLiteral("one command per connection; extra bytes followed the first line"));
        return;
    }
    conn.buf.clear();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        Replier(sock, -1).fail("BAD_JSON",
            err.error != QJsonParseError::NoError
                ? err.errorString()
                : QStringLiteral("top-level value is not an object"));
        return;
    }
    dispatch(sock, doc.object());
}

void LanistaServer::dispatch(QLocalSocket* sock, const QJsonObject& req)
{
    ++m_dispatchCount;

    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    const int seq = req.value(QStringLiteral("seq")).toInt();
    const QJsonObject payload = req.value(QStringLiteral("payload")).toObject();
    Replier reply(sock, seq);

    const auto it = m_commands.constFind(cmd);
    if (it == m_commands.constEnd()) {
        reply.fail("UNKNOWN_CMD", QStringLiteral("no such command: ") + cmd);
        return;
    }
    if (it->gate == Gate::Drive && !driveOpen()) {
        reply.fail("DRIVE_DISABLED",
                   QStringLiteral("set COLOSSEUM_LANISTA_DRIVE=1 to drive the UI"));
        return;
    }
    if (it->gate == Gate::Write && !writeOpen()) {
        reply.fail("WRITE_DISABLED",
                   QStringLiteral("set COLOSSEUM_LANISTA_WRITE=1 to mutate state"));
        return;
    }
    if (!it->fn) {   // fail closed rather than crash
        reply.fail("INTERNAL", QStringLiteral("command has no handler: ") + cmd);
        return;
    }

    // From here the handler owns the reply — inline or on a later turn.
    it->fn(payload, reply);
}

QJsonObject LanistaServer::cmdPing() const
{
    // Sorted: QHash iteration order is randomised per process in Qt 6, and
    // agents will diff this list.
    QStringList names = m_commands.keys();
    std::sort(names.begin(), names.end());
    QJsonArray cmds;
    for (const QString& name : std::as_const(names))
        cmds.append(name);

    return {{QStringLiteral("schema"), QString::fromLatin1(kSchema)},
            {QStringLiteral("pid"), QCoreApplication::applicationPid()},
            {QStringLiteral("pipe"), pipeName()},
            // So a client can learn what it is allowed to do without knowing the
            // names of our environment variables.
            {QStringLiteral("gates"), QJsonObject{
                {QStringLiteral("read"), true},
                {QStringLiteral("drive"), driveOpen()},
                {QStringLiteral("write"), writeOpen()}}},
            {QStringLiteral("commands"), cmds}};
}

QQuickWindow* LanistaServer::mainWindow() const
{
    // See the header note: ROOT windows only, and "first" is not "main".
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
    // ROOT windows only — see the header note on mainWindow()/findItem().
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
            // Reported as a path; it is not created until an artifact is written.
            {QStringLiteral("runDir"), m_runDir}};
}
