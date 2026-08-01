#include "devtools/LanistaServer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
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
// An item grab answers from a callback on a later frame. Under the client's 5s
// grab budget, so the server is the one that says "no" rather than the client
// giving up on a connection the server still holds. See attachGrab().
constexpr int kGrabTimeoutMs = 4000;
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

int LanistaServer::Replier::seq() const
{
    return m_state ? m_state->seq : -1;
}

void LanistaServer::Replier::setReplyHook(ReplyHook hook)
{
    if (m_state)
        m_state->hook = std::move(hook);
}

void LanistaServer::Replier::reply(QJsonObject body)
{
    if (m_state && m_state->hook) {
        // TAKE the hook before calling it: the token the hook receives is this
        // same token with the hook gone, so its own reply() goes straight to
        // the wire and no path can re-enter the hook.
        ReplyHook hook;
        hook.swap(m_state->hook);
        hook(std::move(body), *this);
        return;
    }
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

    // THE COMBINED REPLY. Any command may carry "grab", so the grab is wired
    // here — once, for every command — rather than in each handler. Installed
    // AFTER the gates: a refused command answers through fail(), which ignores
    // the hook, so nothing the server just said no to gets photographed.
    if (payload.contains(QStringLiteral("grab"))) {
        reply.setReplyHook([this, payload](QJsonObject body, Replier onward) {
            attachGrab(payload, std::move(body), std::move(onward));
        });
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
    // An EMPTY name is not "no filter" here — findChild("") matches the first
    // unnamed item in the tree, which would hand a caller a random rectangle.
    // Callers must reject it before asking; refuse it here as well.
    if (objectName.isEmpty())
        return nullptr;
    for (QObject* root : m_engine->rootObjects()) {
        if (root->objectName() == objectName) {
            if (auto* it = qobject_cast<QQuickItem*>(root))
                return it;
        }
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

// ── the combined reply ─────────────────────────────────────────────────────
// Pixels of the instant the body describes. The state was read in THIS
// event-loop turn and the grab is requested in it too, so the PNG and the JSON
// cannot disagree about which moment they are talking about — which is the one
// thing a screenshot taken separately can never promise.
//
// The app photographs its OWN scene rather than the desktop: it works with the
// window behind others, it works headless, and an item grab renders the item
// through the scene graph rather than cropping the framebuffer.
//
// "window" is synchronous (grabWindow returns a QImage). An objectName is
// ASYNC: grabToImage answers on a later frame, so the token is carried into
// the callback and the connection stays open until it fires.
void LanistaServer::attachGrab(const QJsonObject& payload, QJsonObject body, Replier reply)
{
    const QString target = payload.value(QStringLiteral("grab")).toObject()
                               .value(QStringLiteral("target")).toString();
    // Stamped now, not at save time: it names the instant the body describes,
    // and for an async item grab those are two different turns.
    const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString file = QStringLiteral("/seq%1-%2.png")
                             .arg(reply.seq()).arg(++m_grabCounter);

    if (target.isEmpty()) {
        reply.fail("GRAB_TARGET_NOT_FOUND",
                   QStringLiteral("grab needs a target: an objectName, or \"window\""));
        return;
    }

    if (target == QStringLiteral("window")) {
        QQuickWindow* w = mainWindow();
        if (!w) {
            reply.fail("GRAB_TARGET_NOT_FOUND",
                       QStringLiteral("no root QQuickWindow to grab"));
            return;
        }
        const QImage img = w->grabWindow();
        if (img.isNull()) {
            reply.fail("GRAB_NOT_RENDERABLE",
                       QStringLiteral("window grab came back empty (never rendered?)"));
            return;
        }
        // runDir() is a path; this is the call that actually makes the folder.
        const QString path = ensureRunDir() + file;
        if (!img.save(path)) {
            reply.fail("GRAB_SAVE_FAILED", path);
            return;
        }
        body.insert(QStringLiteral("grabPath"), path);
        body.insert(QStringLiteral("grabbedAt"), stamp);
        reply.reply(std::move(body));
        return;
    }

    QQuickItem* item = findItem(target);
    if (!item) {
        reply.fail("GRAB_TARGET_NOT_FOUND", target);
        return;
    }
    QSharedPointer<QQuickItemGrabResult> grab = item->grabToImage();
    if (grab.isNull()) {
        reply.fail("GRAB_NOT_RENDERABLE",
                   target + QStringLiteral(": item is not in a rendering window, "
                                           "or has no size"));
        return;
    }
    const QString path = ensureRunDir() + file;

    // THE GRAB'S OWN DEADLINE. The idle timeout does NOT cover this: the
    // connection is `spent` the moment its line was taken, so nothing on the
    // server side would ever hang up on a handler that goes quiet. If `ready`
    // never fires — item destroyed mid-grab, window torn down, render loop
    // stopped — this connection and its m_conns entry would leak for the life
    // of an always-on app. Whichever path wins, the token's `sent` latch makes
    // the other a no-op, so the race is safe in both directions.
    QTimer::singleShot(kGrabTimeoutMs, this, [reply, target]() mutable {
        reply.fail("GRAB_TIMEOUT",
                   target + QStringLiteral(": no frame within %1 ms").arg(kGrabTimeoutMs));
    });

    // SingleShotConnection breaks a real cycle: the functor holds the only
    // reference to the grab result, and the connection lives on that same
    // result — without the automatic disconnect neither is ever freed and every
    // grab leaks its image.
    connect(grab.data(), &QQuickItemGrabResult::ready, this,
            [grab, reply, body, path, stamp]() mutable {
                if (!reply.canReply())
                    return;   // already timed out, or the client walked away
                if (!grab->saveToFile(path)) {
                    reply.fail("GRAB_SAVE_FAILED", path);
                    return;
                }
                body.insert(QStringLiteral("grabPath"), path);
                body.insert(QStringLiteral("grabbedAt"), stamp);
                reply.reply(std::move(body));
            },
            Qt::SingleShotConnection);
}
