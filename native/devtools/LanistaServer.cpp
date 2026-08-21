#include "devtools/LanistaServer.h"

#include "devtools/LanistaEventLog.h"
#include "engine/VaultForensics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QWheelEvent>

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

// L1-Bridge (2026-08-13): dump-ui's own bounds, independent of the wire's 1 MiB
// line ceiling above. 96 KiB leaves a wide margin under that ceiling — room for
// JSON framing overhead plus the rest of a combined reply (a grab path, etc.)
// — and deliberately favors several small, PAGEABLE replies over one huge one:
// the client is an agent, and a bounded page is easier to reason about than a
// single multi-hundred-KB blob even when the wire could technically carry it.
// maxDepth's ceiling matches walkVisual's own hard recursion guard (depth > 64
// below) — a client cannot ask for more depth than the walker itself allows.
constexpr qint64 kDumpUiByteBudget = 96 * 1024;
constexpr int kDumpUiMaxDepthCeiling = 64;
constexpr int kDumpUiMaxItemsCeiling = 5000;

// Depth-first search over the VISUAL tree (QQuickItem::childItems), which is
// where Repeater/ListView/GridView delegate items live — they are NOT in the
// QObject tree, so root->findChild() cannot see them. In Colosseum every
// shelf/list/grid is delegate-built, so without this walk findItem() resolves
// nothing on a real page. The depth guard bounds a pathological or
// cyclic-looking tree.
//
// This is the ONE visual-tree DFS primitive. `visit` is called on every item
// with its depth; returning true STOPS the walk (walkVisual returns true up the
// stack). Its two consumers differ only in that predicate: findItem's finder
// stops at the first name match, dump-ui's collector never stops. One
// traversal, two behaviours — so there is no second copy-pasted childItems()
// loop to drift out of sync (reduction reflex).
bool walkVisual(QQuickItem* item, int depth,
                const std::function<bool(QQuickItem*, int)>& visit)
{
    if (!item || depth > 64)
        return false;
    if (visit(item, depth))
        return true;
    const QList<QQuickItem*> kids = item->childItems();
    for (QQuickItem* child : kids) {
        if (walkVisual(child, depth + 1, visit))
            return true;
    }
    return false;
}

// The finder: first item whose objectName matches, or nullptr. findItem() calls
// this from the main window's content item.
QQuickItem* walkNamed(QQuickItem* item, const QString& objectName, int depth)
{
    QQuickItem* found = nullptr;
    walkVisual(item, depth, [&](QQuickItem* it, int) {
        if (it->objectName() == objectName) {
            found = it;
            return true;   // stop: we have our match
        }
        return false;
    });
    return found;
}

// J1-Tray-Bridge (2026-08-14): the ONE place a window's live QWindow::visibility()
// is turned into the wire vocabulary — used by BOTH get-state (observed) and
// window-set-state's reply (requested-then-observed), so a client's "minimized"
// request and a later get-state read can never disagree about what to call the
// same state.
QString windowStateName(const QQuickWindow* w)
{
    switch (w->visibility()) {
    case QWindow::Hidden:      return QStringLiteral("hidden");
    case QWindow::Minimized:   return QStringLiteral("minimized");
    case QWindow::Maximized:   return QStringLiteral("maximized");
    case QWindow::FullScreen:  return QStringLiteral("fullscreen");
    case QWindow::Windowed:    return QStringLiteral("normal");
    case QWindow::AutomaticVisibility:
    default:                   return QStringLiteral("unknown");
    }
}
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

    // Task 10: the rotating JSONL event stream lives beside the run dirs, but is
    // ONE file across launches (not per-run) so an agent tailing it sees the whole
    // recent history. Created lazily on first append (mkpath in the ctor). NOTE:
    // rotation is NOT multi-process-safe — two instances writing the same file
    // could race the rename; fine under this bridge's single-user dev assumption.
    m_events = std::make_unique<LanistaEventLog>(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/lanista/events.jsonl"));

    m_idleTimeoutMs = qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_IDLE_MS");
    if (m_idleTimeoutMs <= 0)
        m_idleTimeoutMs = kDefaultIdleTimeoutMs;   // the override exists for tests

    m_grabTimeoutMs = qEnvironmentVariableIntValue("COLOSSEUM_LANISTA_GRAB_MS");
    if (m_grabTimeoutMs <= 0)
        m_grabTimeoutMs = kGrabTimeoutMs;   // the override exists for the timeout test

    // The monotonic clock every synthetic driving event stamps itself from.
    m_inputClock.start();

    // ── command registry ────────────────────────────────────────────────
    addRead(QStringLiteral("ping"),
            [this](const QJsonObject&, Replier reply) { reply.reply(cmdPing()); });
    addRead(QStringLiteral("get-state"),
            [this](const QJsonObject&, Replier reply) { reply.reply(cmdGetState()); });
    // Task 3 reads. qml-get/ui-query own their reply so a missing target can
    // fail("NO_SUCH_ITEM", ...) after a single findItem(); dump-ui always answers.
    addRead(QStringLiteral("qml-get"),
            [this](const QJsonObject& p, Replier reply) { cmdQmlGet(p, std::move(reply)); });
    addRead(QStringLiteral("ui-query"),
            [this](const QJsonObject& p, Replier reply) { cmdUiQuery(p, std::move(reply)); });
    addRead(QStringLiteral("dump-ui"),
            [this](const QJsonObject& p, Replier reply) { cmdDumpUi(p, std::move(reply)); });
    // Task 4: ui-snapshot has no target to fail() on (like dump-ui, which is also
    // const-shaped in that sense), so it answers by returning its own body rather
    // than owning the Replier. (It is non-const because it rebuilds m_handles.)
    addRead(QStringLiteral("ui-snapshot"),
            [this](const QJsonObject& p, Replier reply) { reply.reply(cmdUiSnapshot(p)); });

    // Task 5: the "hands". The four driving commands sit behind the DRIVE gate —
    // addDrive() states that at the registration site, and dispatch() enforces it
    // centrally, so the handlers never re-check it. Each owns its Replier so it can
    // fail() on a missing target (like the Task 3 reads). ui-wait-for is a READ: it
    // only observes a property, so it registers with addRead — its own timeout_ms
    // deadline (not a gate) is what guarantees it terminates.
    addDrive(QStringLiteral("ui-click"),
             [this](const QJsonObject& p, Replier reply) { cmdUiClick(p, std::move(reply)); });
    addDrive(QStringLiteral("ui-keypress"),
             [this](const QJsonObject& p, Replier reply) { cmdUiKeypress(p, std::move(reply)); });
    addDrive(QStringLiteral("ui-text-input"),
             [this](const QJsonObject& p, Replier reply) { cmdUiTextInput(p, std::move(reply)); });
    addDrive(QStringLiteral("ui-scroll"),
             [this](const QJsonObject& p, Replier reply) { cmdUiScroll(p, std::move(reply)); });
    // J1-Tray-Bridge (2026-08-14): restores a minimized/hidden root window — the
    // bridge's only path onto "the taskbar can't be reached from here" (Lanista
    // ledger, Planned §9). A fifth "hand", Drive-gated like the four above; the
    // real QWindow state-transition API only, never an OS-level taskbar click,
    // FlaUI/pywinauto, or secondary-window enumeration.
    addDrive(QStringLiteral("window-set-state"),
             [this](const QJsonObject& p, Replier reply) { cmdWindowSetState(p, std::move(reply)); });
    addRead(QStringLiteral("ui-wait-for"),
            [this](const QJsonObject& p, Replier reply) { cmdUiWaitFor(p, std::move(reply)); });

    // Task 9: invoke-read — curated, allowlisted, read-only organ calls. A READ:
    // it only observes an organ's invokable, and its allowlist is what guarantees
    // it can never mutate (see cmdInvokeRead). Fallible on its target, so it owns
    // the Replier like the Task 3 reads.
    addRead(QStringLiteral("invoke-read"),
            [this](const QJsonObject& p, Replier reply) { cmdInvokeRead(p, std::move(reply)); });

    // Task 10: the event log. Both are READS (log-mark writes only to the DEV
    // event log, a diagnostic annotation, NOT app state), and neither can fail on
    // a target, so each returns its body via a method like the Task 3+ reads.
    addRead(QStringLiteral("events-tail"),
            [this](const QJsonObject& p, Replier reply) { reply.reply(cmdEventsTail(p)); });
    addRead(QStringLiteral("log-mark"),
            [this](const QJsonObject& p, Replier reply) { reply.reply(cmdLogMark(p)); });

    // F1-Bridge (2026-08-13): one Read-gated call onto F1-Core's bounded Vault
    // projection (VaultForensics, composing VaultLibrary — F0's named safe
    // seam). Fallible on "no owner bound" (e.g. this harness's bare QML-scene
    // fixture, which never constructs a VaultLibrary), so it owns the Replier
    // like the Task 3/9 reads.
    addRead(QStringLiteral("vault-forensics"),
            [this](const QJsonObject& p, Replier reply) { cmdVaultForensics(p, std::move(reply)); });

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

// Defined here (not defaulted in the header) so unique_ptr<LanistaEventLog> can
// see the COMPLETE type — LanistaEventLog.h is included above — when it destroys
// m_events. An inline/implicit dtor would try to delete a forward-declared type.
LanistaServer::~LanistaServer() = default;

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

    // Walk the VISUAL tree first, from the main window's content item. Delegate
    // items built by Repeater/ListView/GridView exist ONLY in childItems(),
    // never in the QObject tree — so the findChild() fallback below cannot
    // resolve any of them, and in Colosseum every shelf/list/grid is that.
    if (QQuickWindow* w = mainWindow()) {
        if (QQuickItem* found = walkNamed(w->contentItem(), objectName, 0))
            return found;
    }

    // Fall back to the QObject tree for items not parented under the main
    // window (a root item that is not a window, or a secondary window's scene).
    for (QObject* root : m_engine->rootObjects()) {
        if (root->objectName() == objectName) {
            if (auto* it = qobject_cast<QQuickItem*>(root)) {
                return it;
            }
        }
        if (auto* found = root->findChild<QQuickItem*>(objectName)) {
            return found;
        }
    }
    return nullptr;
}

// ONE resolver, two forms — the reason the read/grab commands route here rather
// than call findItem() straight (see the handle-model note in the header).
//
// A HANDLE is an opaque token of shape "s<gen>h<n>", where <gen> is the snapshot
// epoch that minted it. It resolves ONLY when <gen> is the CURRENT epoch AND the
// item is still live: a token from a superseded snapshot (any client's new
// ui-snapshot bumps the epoch and clears m_handles) is a CLEAN MISS — return
// nullptr, so the caller answers NO_SUCH_ITEM — NEVER a silent resolution to this
// snapshot's Nth item, and NEVER a fall-through that treats the token as an
// objectName. The epoch gate is what makes staleness deterministic; m_handles is
// keyed by the gen-independent "h<n>" so the gate is the only thing that rejects
// a stale token (drop the gate and "s1h3" would resolve to snapshot 2's 3rd item).
//
// A ref that is NOT handle-shaped is an objectName and falls through to findItem()
// unchanged — name lookups are behaviour-preserving; handle support is additive.
QQuickItem* LanistaServer::resolveTarget(const QString& ref) const
{
    static const QRegularExpression kHandle(QStringLiteral("^s(\\d+)h(\\d+)$"));
    const QRegularExpressionMatch m = kHandle.match(ref);
    if (m.hasMatch()) {
        if (m.captured(1).toInt() == m_snapshotEpoch) {
            const auto it = m_handles.constFind(QStringLiteral("h") + m.captured(2));
            if (it != m_handles.constEnd() && !it->isNull())
                return it->data();
        }
        return nullptr;   // stale-epoch or dangling handle: a clean miss
    }
    return findItem(ref);
}

// L1-Bridge (2026-08-13): ONE token-minting primitive, reused by ui-snapshot,
// dump-ui, and ui-query's structural fields — matching resolveTarget's own
// "one resolver, two forms" discipline just above. Returns the item's EXISTING
// token if it already has one this generation (m_itemHandles is the O(1)
// reverse lookup), or mints the next "s<gen>h<n>" and records it both ways.
QString LanistaServer::mintOrReuseHandle(QQuickItem* item)
{
    const auto it = m_itemHandles.constFind(item);
    if (it != m_itemHandles.constEnd())
        return QStringLiteral("s%1%2").arg(m_snapshotEpoch).arg(*it);
    ++m_handleCounter;
    const QString key = QStringLiteral("h") + QString::number(m_handleCounter);
    m_handles.insert(key, QPointer<QQuickItem>(item));
    m_itemHandles.insert(item, key);
    return QStringLiteral("s%1%2").arg(m_snapshotEpoch).arg(key);
}

// The ONE place every handle map is bumped/cleared together. ui-snapshot and a
// FRESH (non-continuation) dump-ui both call this, so "a new snapshot from ANY
// client invalidates every prior handle" now covers structural handles too —
// the same global-invalidation law the ledger already documents, not a second
// one bolted on beside it.
void LanistaServer::beginNewGeneration()
{
    ++m_snapshotEpoch;
    m_handles.clear();
    m_itemHandles.clear();
    m_handleCounter = 0;
}

// The clip:true ancestor chain between `item` and the root window, nearest
// ancestor first. Each entry mints (or reuses) a handle so a client can
// ui-query the ancestor directly without a second lookup by name. Walking
// parentItem() here is independent of any dump-ui walk boundary (a `root`
// scoped deep in the tree still finds its TRUE ancestors, even ones the walk
// itself never visited) — correctness over convenience.
QJsonArray LanistaServer::clipChainFor(QQuickItem* item)
{
    QJsonArray chain;
    for (QQuickItem* anc = item->parentItem(); anc; anc = anc->parentItem()) {
        if (!anc->clip())
            continue;
        const QRectF ancScene =
            anc->mapRectToScene(QRectF(0, 0, anc->width(), anc->height()));
        chain.append(QJsonObject{
            {QStringLiteral("handle"), mintOrReuseHandle(anc)},
            {QStringLiteral("objectName"), anc->objectName()},
            {QStringLiteral("sceneRect"), QJsonObject{
                {QStringLiteral("x"), ancScene.x()}, {QStringLiteral("y"), ancScene.y()},
                {QStringLiteral("width"), ancScene.width()},
                {QStringLiteral("height"), ancScene.height()}}}});
    }
    return chain;
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
            {QStringLiteral("active"), w->isActive()},
            // J1-Tray-Bridge (2026-08-14): "normal"/"minimized"/"hidden"/
            // "maximized"/"fullscreen" — see windowStateName()'s header note.
            // A tray/minimize journey polls THIS field after window-set-state
            // rather than trusting the mutating call's own echo.
            {QStringLiteral("state"), windowStateName(w)}});
    }
    return {{QStringLiteral("windows"), windows},
            // Reported as a path; it is not created until an artifact is written.
            {QStringLiteral("runDir"), m_runDir},
            // The app's RESOLVED storage roots, re-read per call. These exist so a
            // test-session controller can PROVE isolation (both must carry the
            // COLOSSEUM_APPDATA_TAG suffix) instead of assuming Qt's path rules —
            // on Windows both derive from applicationName, but that is an
            // implementation fact to verify, not a contract to trust.
            {QStringLiteral("appDataRoot"),
             QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)},
            {QStringLiteral("cacheRoot"),
             QStandardPaths::writableLocation(QStandardPaths::CacheLocation)}};
}

// ── Task 3 reads ─────────────────────────────────────────────────────────────

// qml-get: read any named item's live QML properties by name. The agent asks
// for exactly the props it wants; each is returned as-is (QVariant -> JSON), so
// a string stays a string and visible/enabled stay bools.
void LanistaServer::cmdQmlGet(const QJsonObject& p, Replier reply) const
{
    // `object` is a handle-or-name: resolveTarget() accepts an h<N> from the last
    // ui-snapshot as well as an objectName (name lookups are unchanged).
    const QString ref = p.value(QStringLiteral("object")).toString();
    QQuickItem* item = resolveTarget(ref);
    if (!item) {
        reply.fail("NO_SUCH_ITEM", ref);
        return;
    }
    QJsonObject props;
    const QJsonArray want = p.value(QStringLiteral("props")).toArray();
    for (const QJsonValue& v : want) {
        const QString name = v.toString();
        const QByteArray key = name.toUtf8();
        props.insert(name, QJsonValue::fromVariant(item->property(key.constData())));
    }
    reply.reply({{QStringLiteral("props"), props}});
}

// ui-query: an item's geometry and render flags. The rect is in LOGICAL/SCENE
// units — the SAME space as get-state, NOT device pixels — because there is no
// grab here (a PNG would be device pixels; this is pure geometry). clippedByWindow
// answers the question a screenshot cannot: is this item drawn (partly) outside
// the window, where a click would miss it?
//
// L1-Bridge (2026-08-13): clippedByWindow only ever checks the ROOT window —
// L1-Discovery proved it demonstrably wrong for an item clipped by an
// INTERMEDIATE clip:true ancestor (visible:true, clippedByWindow:false, and
// still not actually on screen). The additive fields below — handle, parent,
// child count, z, localRect, and above all clipChain — are the same structural
// vocabulary dump-ui now reports, so a targeted ui-query and a bulk dump-ui row
// for the SAME item agree byte-for-byte on everything but ordering.
void LanistaServer::cmdUiQuery(const QJsonObject& p, Replier reply)
{
    // `object` is a handle-or-name: resolveTarget() accepts an h<N> from the last
    // ui-snapshot/dump-ui generation as well as an objectName (name lookups are
    // unchanged).
    const QString ref = p.value(QStringLiteral("object")).toString();
    QQuickItem* item = resolveTarget(ref);
    if (!item) {
        reply.fail("NO_SUCH_ITEM", ref);
        return;
    }
    // The rect in scene space. mapRectToScene is the transform-correct primitive:
    // it folds in every ancestor transform — translation, and any scale/rotation —
    // so a delegate deep in a Flickable reports where it ACTUALLY sits, and
    // clippedByWindow is derived from THIS rect rather than local-size arithmetic.
    const QRectF sceneRect =
        item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));

    // Single-root-window assumption: clipping is judged against mainWindow() (the
    // FIRST root QQuickWindow), which is where every Colosseum item lives today.
    // An item in a secondary window would be measured against the wrong bounds; a
    // null mainWindow leaves the flag false.
    QQuickWindow* w = mainWindow();
    bool clipped = false;
    if (w) {
        clipped = (sceneRect.right() > w->width()) || (sceneRect.bottom() > w->height())
                  || (sceneRect.left() < 0) || (sceneRect.top() < 0);
    }

    QQuickItem* parent = item->parentItem();
    const QString parentHandle = parent ? mintOrReuseHandle(parent) : QString();

    QJsonObject body{
        // legacy fields — UNCHANGED shape and values for old clients.
        {QStringLiteral("rect"), QJsonObject{
            {QStringLiteral("x"), sceneRect.x()}, {QStringLiteral("y"), sceneRect.y()},
            {QStringLiteral("width"), sceneRect.width()},
            {QStringLiteral("height"), sceneRect.height()}}},
        {QStringLiteral("visible"), item->isVisible()},
        {QStringLiteral("enabled"), item->isEnabled()},
        {QStringLiteral("opacity"), item->opacity()},
        {QStringLiteral("clippedByWindow"), clipped},
        // additive structural fields (L1-Bridge) — the same vocabulary dump-ui
        // reports per item, so a client never needs two different shapes.
        {QStringLiteral("handle"), mintOrReuseHandle(item)},
        {QStringLiteral("objectName"), item->objectName()},
        {QStringLiteral("class"), QString::fromLatin1(item->metaObject()->className())},
        {QStringLiteral("parentHandle"),
         parent ? QJsonValue(parentHandle) : QJsonValue()},
        {QStringLiteral("parentName"),
         parent ? QJsonValue(parent->objectName()) : QJsonValue()},
        {QStringLiteral("childCount"), item->childItems().size()},
        {QStringLiteral("z"), item->z()},
        {QStringLiteral("localRect"), QJsonObject{
            {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
            {QStringLiteral("width"), item->width()},
            {QStringLiteral("height"), item->height()}}},
        {QStringLiteral("clipChain"), clipChainFor(item)},
    };
    if (w)
        body.insert(QStringLiteral("rootWindow"),
                    QJsonObject{{QStringLiteral("width"), w->width()},
                               {QStringLiteral("height"), w->height()}});
    reply.reply(std::move(body));
}

// dump-ui: the whole item tree in one shot, for an agent orienting on a page it
// has never seen. Consumes the shared walkVisual() collector (see the
// anon-namespace note) — no second tree walker.
//
// L1-Bridge (2026-08-13): L1-Discovery measured this command's original
// named-only filter as the single largest blind spot on the harness fixture
// (68 of an estimated ≈133 real items visible) — an agent debugging the
// background fill, a Flickable's own contentItem, a Column wrapper, a
// Repeater, or a delegate's inner Text had NO WAY TO SEE any of them, because
// nothing ever asked for an unnamed item. That filter is gone: every
// QQuickItem is reported now (objectName is "" rather than omitted), each
// carrying an opaque structural handle (mintOrReuseHandle — the SAME identity
// scheme ui-snapshot already uses), its parent's handle/name, child count,
// local + scene rect, z/enabled/opacity, and its clip:true ancestor chain —
// the direct fix for clippedByWindow's demonstrated wrong answer (row 4: an
// item scrolled out of its OWN list's viewport reads visible:true,
// clippedByWindow:false, and is still not actually on screen).
//
// Unbounded was safe while the filter kept replies small; an all-item walk is
// not, so three request-side bounds — `root`, `maxDepth`, `maxItems` — are all
// optional and all clamped server-side, and a reply byte budget (see
// kDumpUiByteBudget) stops the walk outright with `truncated:true` and a
// resumable `continuation.cursor` rather than ever growing toward the wire's
// 1 MiB line ceiling. A continuation is honored ONLY when the caller echoes
// back the `generation` the cursor was minted against — anything else starts a
// brand-new generation, exactly the "handles die at the next snapshot" law the
// rest of the bridge already lives by (see beginNewGeneration()).
//
// Old clients reading only objectName/class/x/y/width/height/visible/depth are
// unaffected: those fields keep their exact prior shape and values. `count`
// keeps meaning `items.size()`, not some larger total — a client that never
// looks at `truncated` simply sees more rows than before, never fewer.
void LanistaServer::cmdDumpUi(const QJsonObject& p, Replier reply)
{
    QQuickWindow* w = mainWindow();

    // `root`: an objectName/handle to start the walk from; default the window's
    // contentItem(), as today. An EXPLICIT root that fails to resolve is a
    // NO_SUCH_ITEM error — silently falling back to the whole tree would turn a
    // caller's typo into a much bigger (and misleading) reply instead of a
    // clean signal that the name was wrong.
    QQuickItem* root = nullptr;
    if (p.contains(QStringLiteral("root"))) {
        const QString rootRef = p.value(QStringLiteral("root")).toString();
        root = resolveTarget(rootRef);
        if (!root) {
            reply.fail("NO_SUCH_ITEM", rootRef);
            return;
        }
    } else if (w) {
        root = w->contentItem();
    }

    // Clamped, never trusted: an absurd client request is silently reduced to
    // the server's own ceiling rather than honored or refused.
    const int maxDepth = p.contains(QStringLiteral("maxDepth"))
        ? qBound(0, p.value(QStringLiteral("maxDepth")).toInt(), kDumpUiMaxDepthCeiling)
        : kDumpUiMaxDepthCeiling;
    const int maxItems = p.contains(QStringLiteral("maxItems"))
        ? qBound(1, p.value(QStringLiteral("maxItems")).toInt(), kDumpUiMaxItemsCeiling)
        : kDumpUiMaxItemsCeiling;

    // Continuation: `cursor` only resumes the earlier walk when `generation`
    // matches the CURRENT epoch. A mismatch (or a fresh call with no cursor at
    // all) starts a brand-new generation — the existing global-invalidation law
    // ("a new snapshot from ANY client invalidates every prior handle") now
    // decides continuation validity too, rather than a second rule bolted on.
    const int requestedGeneration = p.value(QStringLiteral("generation")).toInt(-1);
    const int requestedCursor =
        qMax(0, p.value(QStringLiteral("cursor")).toInt(0));
    const bool continuing = requestedCursor > 0 && requestedGeneration == m_snapshotEpoch;
    if (!continuing)
        beginNewGeneration();

    QJsonArray items;
    qint64 bytesUsed = 0;
    bool truncated = false;
    int nextCursor = -1;
    int walkIndex = 0;

    if (root) {
        walkVisual(root, 0, [&](QQuickItem* it, int depth) {
            const int myIndex = walkIndex++;
            if (myIndex < requestedCursor)
                return false;    // already delivered by an earlier page
            if (depth > maxDepth)
                return false;    // depth-filtered: never added, never counted

            QQuickItem* parent = it->parentItem();
            const QString handle = mintOrReuseHandle(it);
            const QString parentHandle = parent ? mintOrReuseHandle(parent) : QString();
            // x/y are SCENE/logical units (the same space as ui-query and
            // get-state), so an agent consuming both commands sees ONE
            // coordinate system.
            const QRectF sceneRect =
                it->mapRectToScene(QRectF(0, 0, it->width(), it->height()));

            const QJsonObject row{
                // legacy fields — UNCHANGED shape and values for old clients.
                {QStringLiteral("objectName"), it->objectName()},
                {QStringLiteral("class"),
                 QString::fromLatin1(it->metaObject()->className())},
                {QStringLiteral("x"), sceneRect.x()}, {QStringLiteral("y"), sceneRect.y()},
                {QStringLiteral("width"), it->width()},
                {QStringLiteral("height"), it->height()},
                {QStringLiteral("visible"), it->isVisible()},
                {QStringLiteral("depth"), depth},
                // additive structural fields (L1-Bridge).
                {QStringLiteral("handle"), handle},
                {QStringLiteral("parentHandle"),
                 parent ? QJsonValue(parentHandle) : QJsonValue()},
                {QStringLiteral("parentName"),
                 parent ? QJsonValue(parent->objectName()) : QJsonValue()},
                {QStringLiteral("childCount"), it->childItems().size()},
                {QStringLiteral("z"), it->z()},
                {QStringLiteral("enabled"), it->isEnabled()},
                {QStringLiteral("opacity"), it->opacity()},
                {QStringLiteral("localRect"), QJsonObject{
                    {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
                    {QStringLiteral("width"), it->width()},
                    {QStringLiteral("height"), it->height()}}},
                {QStringLiteral("sceneRect"), QJsonObject{
                    {QStringLiteral("x"), sceneRect.x()}, {QStringLiteral("y"), sceneRect.y()},
                    {QStringLiteral("width"), sceneRect.width()},
                    {QStringLiteral("height"), sceneRect.height()}}},
                {QStringLiteral("clipChain"), clipChainFor(it)},
            };

            // Priced BEFORE it is added: a row that would push the reply over
            // budget is never appended, so bytesUsed never itself exceeds the
            // budget it is supposed to enforce.
            const qint64 rowBytes =
                QJsonDocument(row).toJson(QJsonDocument::Compact).size() + 1;
            if (bytesUsed + rowBytes > kDumpUiByteBudget || items.size() >= maxItems) {
                truncated = true;
                nextCursor = myIndex;
                return true;   // stop the WHOLE walk — budget or count ceiling hit
            }
            bytesUsed += rowBytes;
            items.append(row);
            return false;
        });
    }

    QJsonObject body{
        {QStringLiteral("items"), items},
        {QStringLiteral("count"), items.size()},
        {QStringLiteral("generation"), m_snapshotEpoch},
        {QStringLiteral("truncated"), truncated},
        {QStringLiteral("bytesUsed"), bytesUsed},
        {QStringLiteral("maxDepthUsed"), maxDepth},
        {QStringLiteral("maxItemsUsed"), maxItems},
        {QStringLiteral("continuation"),
         truncated ? QJsonValue(QJsonObject{{QStringLiteral("cursor"), nextCursor}})
                   : QJsonValue()},
    };
    body.insert(QStringLiteral("rootWindow"),
                w ? QJsonValue(QJsonObject{{QStringLiteral("width"), w->width()},
                                           {QStringLiteral("height"), w->height()}})
                  : QJsonValue());
    reply.reply(std::move(body));
}

// ui-snapshot: Playwright's model, QML-native. ONE call returns every element an
// agent could act on, each with an opaque session HANDLE. Reuses the shared
// walkVisual() collector (a visit that always returns false = collect every item),
// exactly like cmdDumpUi — no third tree walk.
//
// Each snapshot opens a NEW epoch (++m_snapshotEpoch) and rebuilds m_handles, so a
// handle is single-snapshot-scoped: the token embeds its epoch ("s<gen>h<n>") and
// resolveTarget() rejects any token whose epoch is not current (see its note).
// m_handles is GLOBAL server state — a new snapshot from ANY client invalidates
// every prior handle — which is acceptable under the local single-user model this
// bridge serves. QPointer values mean an item destroyed within the same epoch also
// resolves to null rather than dangling.
QJsonObject LanistaServer::cmdUiSnapshot(const QJsonObject&)
{
    // L1-Bridge (2026-08-13): beginNewGeneration() replaces the inline
    // ++m_snapshotEpoch/m_handles.clear() pair below — byte-identical effect,
    // now shared with dump-ui's fresh (non-continuation) path rather than two
    // copies of the same two lines.
    beginNewGeneration();
    QJsonArray elements;
    if (QQuickWindow* w = mainWindow()) {
        walkVisual(w->contentItem(), 0, [&](QQuickItem* it, int) {
            const QString cls = QString::fromLatin1(it->metaObject()->className());
            // interactive = the item DERIVES from a Qt interactive base. We walk the
            // metaobject SUPERCLASS CHAIN, not just the leaf className: className()
            // reports the MOST-DERIVED type, so a ListView is "QQuickListView" (no
            // "Flickable" substring) and a Controls button is its concrete type —
            // the recognisable token only appears on a BASE class. Honest limit:
            // this catches Qt-DERIVED interactive types (MouseArea; Flickable and
            // its ListView/GridView subclasses; TextInput/TextEdit and their bases;
            // AbstractButton/Button) via those base names. It does NOT detect a plain
            // Item made interactive only by a CHILD TapHandler/MouseArea — a handler
            // is a child, not a base, so it is invisible to a superclass walk.
            bool interactive = false;
            for (const QMetaObject* mo = it->metaObject(); mo && !interactive;
                 mo = mo->superClass()) {
                const QByteArray bn(mo->className());
                interactive = bn.contains("MouseArea") || bn.contains("Flickable")
                           || bn.contains("TextInput") || bn.contains("TextEdit")
                           || bn.contains("AbstractButton") || bn.contains("Button");
            }
            // Actionable = worth handing to an agent: interactive OR named (a named
            // non-interactive item is a landmark the scene author called out). And
            // it must be on-screen with real area — a hidden or zero-sized item has
            // nothing to click.
            const bool named = !it->objectName().isEmpty();
            if (!(interactive || named) || !it->isVisible()
                || it->width() <= 0 || it->height() <= 0)
                return false;

            // mintOrReuseHandle assigns the next "s<gen>h<n>" and records it in
            // BOTH m_handles and m_itemHandles — the same token-minting
            // primitive dump-ui and ui-query's structural fields now share.
            const QString token = mintOrReuseHandle(it);

            // centerX/centerY are the item CENTER in SCENE / LOGICAL units (via
            // mapToScene), the SAME space get-state, ui-query and dump-ui speak —
            // NOT device pixels. A driving command (ui-click, …) consumes these
            // directly; a client that instead compares them against a GRAB must
            // scale by the LIVE devicePixelRatio (1.5x on this machine), because a
            // grab is device pixels. Do NOT hard-code 1.5 off the back of these.
            const QPointF center =
                it->mapToScene(QPointF(it->width() / 2.0, it->height() / 2.0));
            elements.append(QJsonObject{
                {QStringLiteral("handle"), token},
                {QStringLiteral("objectName"), it->objectName()},
                {QStringLiteral("class"), cls},
                {QStringLiteral("interactive"), interactive},
                {QStringLiteral("centerX"), center.x()},
                {QStringLiteral("centerY"), center.y()},
                {QStringLiteral("width"), it->width()},
                {QStringLiteral("height"), it->height()},
                {QStringLiteral("enabled"), it->isEnabled()}});
            return false;   // collect every actionable item — never stop early
        });
    }
    return {{QStringLiteral("elements"), elements},
            {QStringLiteral("count"), elements.size()}};
}

// ── Task 5: the hands ────────────────────────────────────────────────────────
// SYNTHETIC events through the real QQuickWindow delivery path
// (QCoreApplication::sendEvent(item->window(), &ev)) at the item's SCENE center
// (item->mapToScene(center)) — so hover/focus/grabs behave exactly as they would
// for a human. A driving command NEVER takes a pixel coordinate from the client:
// the client names the item, the item hands us the point. The DRIVE gate is
// enforced centrally in dispatch(); none of these re-check it.

// ui-click: press+release LeftButton at the item's scene center. The window
// routes the press to the item under the point and the release to the grabber,
// so a MouseArea sees a real click (onClicked fires). Drop the release and no
// click is synthesised — the whole reason both events are sent.
void LanistaServer::cmdUiClick(const QJsonObject& p, Replier reply) const
{
    const QString ref = p.value(QStringLiteral("target")).toString();
    QQuickItem* item = resolveTarget(ref);
    if (!item) {
        reply.fail("NO_SUCH_ITEM", ref);
        return;
    }
    QQuickWindow* w = item->window();
    if (!w) {
        reply.fail("NO_WINDOW", ref);
        return;
    }
    const QPointF scenePos =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPointF globalPos = w->mapToGlobal(scenePos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, scenePos, globalPos,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.setTimestamp(m_inputClock.elapsed());
    QCoreApplication::sendEvent(w, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, scenePos, globalPos,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    release.setTimestamp(m_inputClock.elapsed());
    QCoreApplication::sendEvent(w, &release);
    reply.reply({{QStringLiteral("clicked"), item->objectName()},
                 {QStringLiteral("atX"), scenePos.x()},
                 {QStringLiteral("atY"), scenePos.y()}});
}

// ui-keypress: parse the key via QKeySequence and send KeyPress+KeyRelease of its
// FIRST combination to the main window's focus item. BAD_KEY on an empty/
// unparseable key. A printable key with no non-shift modifier carries its
// character as the event text, so Keys.onPressed / a TextInput see e.text.
void LanistaServer::cmdUiKeypress(const QJsonObject& p, Replier reply) const
{
    const QString keyStr = p.value(QStringLiteral("key")).toString();
    if (keyStr.isEmpty()) {
        reply.fail("BAD_KEY", QStringLiteral("ui-keypress needs a non-empty key"));
        return;
    }
    const QKeySequence seq(keyStr);
    if (seq.count() == 0) {
        reply.fail("BAD_KEY", QStringLiteral("unparseable key: ") + keyStr);
        return;
    }
    QQuickWindow* w = mainWindow();
    if (!w) {
        reply.fail("NO_WINDOW", QStringLiteral("no root window to receive the key"));
        return;
    }
    const QKeyCombination combo = seq[0];
    const int key = combo.key();
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    QString text;
    if (key >= 0x20 && key <= 0x7e && (mods & ~Qt::ShiftModifier) == Qt::NoModifier)
        text = QChar(key);
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    press.setTimestamp(m_inputClock.elapsed());
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    release.setTimestamp(m_inputClock.elapsed());
    QCoreApplication::sendEvent(w, &release);
    reply.reply({{QStringLiteral("pressed"), keyStr}});
}

// ui-text-input: forceActiveFocus() the target, then a KeyPress carrying each
// character's text to its window — a TextInput inserts on KeyPress, so the whole
// string lands. Returns the text it typed.
void LanistaServer::cmdUiTextInput(const QJsonObject& p, Replier reply) const
{
    const QString ref = p.value(QStringLiteral("target")).toString();
    QQuickItem* item = resolveTarget(ref);
    if (!item) {
        reply.fail("NO_SUCH_ITEM", ref);
        return;
    }
    QQuickWindow* w = item->window();
    if (!w) {
        reply.fail("NO_WINDOW", ref);
        return;
    }
    item->forceActiveFocus();
    const QString text = p.value(QStringLiteral("text")).toString();
    for (const QChar ch : text) {
        QKeyEvent ev(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        ev.setTimestamp(m_inputClock.elapsed());
        QCoreApplication::sendEvent(w, &ev);
    }
    reply.reply({{QStringLiteral("typed"), text}});
}

// ui-scroll: a QWheelEvent with angleDelta QPoint(0, dy) (default dy -120) at the
// item's scene center. The window delivers it to the Flickable/ListView under the
// point, which moves its content. Drop the sendEvent and contentY never moves.
void LanistaServer::cmdUiScroll(const QJsonObject& p, Replier reply) const
{
    const QString ref = p.value(QStringLiteral("target")).toString();
    QQuickItem* item = resolveTarget(ref);
    if (!item) {
        reply.fail("NO_SUCH_ITEM", ref);
        return;
    }
    QQuickWindow* w = item->window();
    if (!w) {
        reply.fail("NO_WINDOW", ref);
        return;
    }
    const int dy = p.contains(QStringLiteral("dy"))
                       ? p.value(QStringLiteral("dy")).toInt() : -120;
    const QPointF scenePos =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    const QPointF globalPos = w->mapToGlobal(scenePos.toPoint());
    QWheelEvent ev(scenePos, globalPos, QPoint(0, 0), QPoint(0, dy),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    ev.setTimestamp(m_inputClock.elapsed());
    QCoreApplication::sendEvent(w, &ev);
    reply.reply({{QStringLiteral("scrolled"), item->objectName()},
                 {QStringLiteral("dy"), dy}});
}

// J1-Tray-Bridge (2026-08-14): window-set-state — the one command that can
// restore a minimized/hidden root window. `state` must be exactly "normal",
// "minimized", or "hidden"; anything else fails BAD_STATE without touching the
// window at all (no partial mutation on a rejected request). First root window
// only (mainWindow(), the SAME "first, not necessarily main" scope every other
// Task 2/3/5 command already uses) — a second root window, if one exists, is
// never addressed. The three branches below call QWindow's OWN state-transition
// API directly: showNormal()/showMinimized()/hide() are exactly what a titlebar
// minimize or a taskbar/tray restore invoke in production. No OS-level click
// simulation, no FlaUI/pywinauto, no secondary-window enumeration — the real
// product path is the whole point (ledger: Lanista Planned §9).
void LanistaServer::cmdWindowSetState(const QJsonObject& p, Replier reply) const
{
    const QString state = p.value(QStringLiteral("state")).toString();
    static const QSet<QString> kValidStates{QStringLiteral("normal"),
                                             QStringLiteral("minimized"),
                                             QStringLiteral("hidden")};
    if (!kValidStates.contains(state)) {
        reply.fail("BAD_STATE",
                   QStringLiteral("state must be one of: normal, minimized, hidden (got \"%1\")")
                       .arg(state));
        return;
    }
    QQuickWindow* w = mainWindow();
    if (!w) {
        reply.fail("NO_WINDOW", QStringLiteral("no root window"));
        return;
    }
    if (state == QStringLiteral("normal"))
        w->showNormal();
    else if (state == QStringLiteral("minimized"))
        w->showMinimized();
    else
        w->hide();

    reply.reply({{QStringLiteral("objectName"), w->objectName()},
                 {QStringLiteral("state"), windowStateName(w)},
                 {QStringLiteral("visible"), w->isVisible()},
                 {QStringLiteral("active"), w->isActive()}});
}

// ui-wait-for: the async command. Poll a property every ~50ms via a QTimer(this);
// answer `matched` the instant object.prop equals the wanted value, or fail
// WAIT_TIMEOUT once now passes the deadline (now + timeout_ms, default 3000). The
// deadline is the backstop that guarantees this always terminates. canReply() is
// the client-gone guard: if the client walked away, tear the timer down and stop.
void LanistaServer::cmdUiWaitFor(const QJsonObject& p, Replier reply)
{
    const QString objName = p.value(QStringLiteral("object")).toString();
    const QByteArray propKey = p.value(QStringLiteral("prop")).toString().toUtf8();
    const QJsonValue wanted = p.value(QStringLiteral("value"));
    int timeoutMs = p.value(QStringLiteral("timeout_ms")).toInt();
    if (timeoutMs <= 0)
        timeoutMs = 3000;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    auto* timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this,
            [this, timer, objName, propKey, wanted, deadline, reply]() mutable {
                if (!reply.canReply()) {   // client vanished: nothing left to answer
                    timer->stop();
                    timer->deleteLater();
                    return;
                }
                // resolveTarget each poll — handle-or-name. A NAME is the stable
                // choice across a long wait: a snapshot HANDLE goes stale the next
                // time any client takes a ui-snapshot, and from then on resolves to
                // a clean miss — so the wait simply rides on to WAIT_TIMEOUT rather
                // than ever matching the wrong item.
                if (QQuickItem* item = resolveTarget(objName)) {
                    if (QJsonValue::fromVariant(item->property(propKey.constData()))
                        == wanted) {
                        timer->stop();
                        timer->deleteLater();
                        reply.reply({{QStringLiteral("matched"), true},
                                     {QStringLiteral("value"), wanted}});
                        return;
                    }
                }
                if (QDateTime::currentMSecsSinceEpoch() > deadline) {
                    timer->stop();
                    timer->deleteLater();
                    reply.fail("WAIT_TIMEOUT",
                               objName + QStringLiteral(".") + QString::fromUtf8(propKey)
                                   + QStringLiteral(" did not reach the wanted value in time"));
                    return;
                }
            });
    timer->start();
}

// ── Task 9: invoke-read ──────────────────────────────────────────────────────
// A CURATED, allowlisted, read-only bridge to a QML organ's Q_INVOKABLE reads.
//
// DESIGN NOTE — this is NOT a generic invoke. Only allowlisted <Organ>.<method>
// pairs may be called, so a READ-gated command can never mutate: an off-list
// method (e.g. TankobanVolumes.remove) is refused before the organ is even
// resolved. v1.0's allowlist is the TankobanVolumes organ ONLY — the one organ
// whose invokable signatures are verified. Other organs' reads ride
// qml-get/ui-snapshot/dump-ui until their signatures are verified and ADDED here
// in a follow-up — never by weakening this list.
//
// The allowlist check comes BEFORE the organ lookup on purpose: a refused method
// never touches the organ. Args marshal as QString (the allowlisted reads take
// string params); the return branches on QVariantList / QVariantMap / bool.
//
// EXTENDING THE BRIDGE — three coupled seams. Adding a read to kAllowlist may
// also require extending (a) the arg marshalling, if the new read takes a
// non-QString param — today all args marshal as QString; and (b) the return-type
// branch, if it returns a type other than QVariantList / QVariantMap / bool. A
// method whose name+arity match but whose return type is not bridged fails with a
// message that says exactly that (not "no matching invokable"), so a missing
// return-type branch never masquerades as a missing method.
void LanistaServer::cmdInvokeRead(const QJsonObject& p, Replier reply) const
{
    // EXACTLY the verified reads below — the whole safety boundary.
    static const QSet<QString> kAllowlist = {
        QStringLiteral("TankobanVolumes.volumesForSeries"),
        QStringLiteral("TankobanVolumes.statusOf"),
        QStringLiteral("TankobanVolumes.activeVolumeJobs"),
        QStringLiteral("TankobanVolumes.downloadedVolumes"),
        QStringLiteral("TankobanVolumes.modeEnabled"),
        QStringLiteral("TankobanVolumes.localPages"),
        // Per-URL image-network diagnostics (Lanista pilot, decision brief
        // 2026-08-06 §4). Both take one QString and return QVariantList —
        // squarely inside the existing marshalling, no seam extension needed.
        QStringLiteral("BiblioImageDiag.rowsForUrl"),
        QStringLiteral("BiblioImageDiag.recentRows"),
    };

    const QString objName = p.value(QStringLiteral("object")).toString();
    const QString method = p.value(QStringLiteral("method")).toString();
    const QString key = objName + QStringLiteral(".") + method;

    // Allowlist FIRST — a refused method never even touches the organ.
    if (!kAllowlist.contains(key)) {
        reply.fail("CMD_FAILED",
                   key + QStringLiteral(" is not on the invoke-read allowlist; "
                                        "extend the bridge in your lane instead"));
        return;
    }

    // Resolve the organ as a context property on the root context.
    QObject* organ = m_engine->rootContext()
                         ->contextProperty(objName).value<QObject*>();
    if (!organ) {
        reply.fail("CMD_FAILED",
                   QStringLiteral("no such context property: ") + objName);
        return;
    }

    // All args as QString — the allowlisted reads take string parameters.
    QStringList args;
    const QJsonArray argsArr = p.value(QStringLiteral("args")).toArray();
    for (const QJsonValue& v : argsArr)
        args << v.toString();

    // DirectConnection so the read resolves in THIS turn. Invokes the EXACT
    // method matched below; args are supplied by count (all QString); >3 is
    // beyond any allowlisted signature.
    auto doInvoke = [&](const QMetaMethod& mm, auto&& ret) -> bool {
        switch (args.size()) {
        case 0: return mm.invoke(organ, Qt::DirectConnection, ret);
        case 1: return mm.invoke(organ, Qt::DirectConnection, ret,
                    Q_ARG(QString, args.at(0)));
        case 2: return mm.invoke(organ, Qt::DirectConnection, ret,
                    Q_ARG(QString, args.at(0)), Q_ARG(QString, args.at(1)));
        case 3: return mm.invoke(organ, Qt::DirectConnection, ret,
                    Q_ARG(QString, args.at(0)), Q_ARG(QString, args.at(1)),
                    Q_ARG(QString, args.at(2)));
        default: return false;
        }
    };

    // Find the matching invokable by name + parameterCount, branch on its return
    // type. An unmatched return type keeps scanning (an overload may fit); a match
    // whose invoke fails is a hard error; falling off the end is no-match.
    const QMetaObject* mo = organ->metaObject();
    const QByteArray methodBytes = method.toUtf8();
    // Did the scan ever find a name+arity match whose return type simply was not
    // one of the three bridged types? That is a DIFFERENT failure from "no such
    // method": the two must not collapse into one message, or the next lane
    // extending the bridge chases a non-existent name/arity bug (see below).
    bool matchedNameArity = false;
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod mm = mo->method(i);
        if (mm.name() != methodBytes || mm.parameterCount() != args.size())
            continue;
        matchedNameArity = true;

        const QByteArray retType = mm.typeName();
        QJsonValue out;
        bool invoked = false;
        if (retType == "QVariantList") {
            QVariantList v;
            invoked = doInvoke(mm, Q_RETURN_ARG(QVariantList, v));
            out = QJsonArray::fromVariantList(v);
        } else if (retType == "QVariantMap") {
            QVariantMap v;
            invoked = doInvoke(mm, Q_RETURN_ARG(QVariantMap, v));
            out = QJsonObject::fromVariantMap(v);
        } else if (retType == "bool") {
            bool v = false;
            invoked = doInvoke(mm, Q_RETURN_ARG(bool, v));
            out = v;
        } else {
            continue;   // unmatched return type — keep scanning for an overload
        }
        if (!invoked) {
            reply.fail("CMD_FAILED", QStringLiteral("invoke failed: ") + key);
            return;
        }
        reply.reply({{QStringLiteral("result"), out}});
        return;
    }
    // Two distinct terminal failures. If a name+arity match WAS found, the method
    // exists — its return type just is not bridged yet, so point the next lane at
    // the return-type branch rather than at a phantom name/arity bug. Only a true
    // no-match keeps the "no matching invokable" message.
    if (matchedNameArity) {
        reply.fail("CMD_FAILED",
                   key + QStringLiteral(": invokable found but its return type is "
                                        "not bridged — add a branch in cmdInvokeRead"));
        return;
    }
    reply.fail("CMD_FAILED", QStringLiteral("no matching invokable: ") + key);
}

// ── Task 10: the DEV event log ───────────────────────────────────────────────
// events-tail returns the last N lines of events.jsonl (default 50); log-mark
// stamps a correlation label into it AND qInfo, so multi-source log analysis
// pivots on the label rather than timestamp guesswork. Both are READS: log-mark
// writes only to the DEV event log (a diagnostic annotation, NOT app state), so
// it is correctly always-on. Neither can fail on a target, so both return their
// body (like cmdDumpUi) rather than owning the Replier.
QJsonObject LanistaServer::cmdEventsTail(const QJsonObject& p) const
{
    return {{QStringLiteral("lines"),
             QJsonArray::fromStringList(
                 m_events->tail(p.value(QStringLiteral("limit")).toInt(50)))}};
}

QJsonObject LanistaServer::cmdLogMark(const QJsonObject& p) const
{
    const QString label = p.value(QStringLiteral("label")).toString();
    m_events->append({{QStringLiteral("type"), QStringLiteral("mark")},
                      {QStringLiteral("label"), label}});
    qInfo("lanista: MARK %s", qUtf8Printable(label));
    return {{QStringLiteral("marked"), label}};
}

// ── F1-Bridge: vault-forensics ────────────────────────────────────────────
// Invokes F1-Core (VaultForensics::queryMarshalled) on its owner thread and
// hands its response map back UNCHANGED — no field renamed, dropped, or
// added, beyond the wire envelope's own "type"/"seq" that reply() adds to
// every command alike. The candidateCount:-1 sentinel (F0 §10: not reachable
// through VaultLibrary's public surface, see VaultForensics.h) is inherited
// honestly here, never patched over. Does not grow a generic reflection/
// write registry: this is one named, typed call onto one named projection.
void LanistaServer::cmdVaultForensics(const QJsonObject& p, Replier reply) const
{
    if (!m_vaultForensics) {
        reply.fail("VAULT_FORENSICS_UNAVAILABLE",
                   QStringLiteral("no VaultForensics owner bound in this process "
                                  "(no live VaultLibrary — e.g. a bare QML-scene harness)"));
        return;
    }

    QVariantMap request;
    request.insert(QStringLiteral("scope"), p.value(QStringLiteral("scope")).toVariant());
    if (p.contains(QStringLiteral("key")))
        request.insert(QStringLiteral("key"), p.value(QStringLiteral("key")).toVariant());
    if (p.contains(QStringLiteral("limit")))
        request.insert(QStringLiteral("limit"), p.value(QStringLiteral("limit")).toVariant());

    // Bridge-level deadline for VaultForensics::queryMarshalled()'s foreign-thread wait.
    // F0 found no thread hop is needed today (LanistaServer and every Vault object share
    // the GUI thread), so this degrades to a direct call in production — the clamp exists
    // for the general-purpose contract, not because production exercises the wait.
    int deadlineMs = p.value(QStringLiteral("timeoutMs")).toInt(2000);
    deadlineMs = qBound(200, deadlineMs, 10000);

    const QVariantMap result = m_vaultForensics->queryMarshalled(request, deadlineMs);
    reply.reply(QJsonObject::fromVariantMap(result));
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
    // The client can hang up between issuing the command and this hook firing
    // (an async handler like selftest-slow/ui-wait-for holds the token across
    // turns). Bail before the "window" path burns a full framebuffer capture
    // and leaves an orphan PNG on disk — the item path's ready-callback already
    // makes the same canReply() check before it saves.
    if (!reply.canReply())
        return;

    const QJsonObject grabObj = payload.value(QStringLiteral("grab")).toObject();
    const QString target = grabObj.value(QStringLiteral("target")).toString();
    // Presentation recording asks the window path for BMP: it avoids spending most of
    // the hold interval PNG-compressing transient frames. Ordinary evidence grabs remain
    // PNG, and item grabs deliberately keep their existing PNG contract.
    const bool rawWindowFrame = target == QStringLiteral("window")
        && grabObj.value(QStringLiteral("format")).toString().compare(
               QStringLiteral("bmp"), Qt::CaseInsensitive) == 0;
    // Stamped now, not at save time: it names the instant the body describes,
    // and for an async item grab those are two different turns.
    const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString file = QStringLiteral("/seq%1-%2.%3")
                             .arg(reply.seq()).arg(++m_grabCounter)
                             .arg(rawWindowFrame ? QStringLiteral("bmp") : QStringLiteral("png"));

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

    // resolveTarget (not findItem): "window" is handled above, so past this point
    // a target is an objectName OR a ui-snapshot handle — a snapshot handle grabs
    // its item just as it reads (behaviour-preserving for objectNames).
    QQuickItem* item = resolveTarget(target);
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
    //
    // payload.grab.timeoutMs (>0) SHORTENS this one grab's deadline. It is
    // clamped to the server ceiling with qMin, so a client can only ask the
    // server to give up SOONER — never push this connection-leak backstop past
    // m_grabTimeoutMs. It earns its place twice: a tight-budget client can bound
    // one slow grab, and it is the seam that lets the timeout test fire
    // GRAB_TIMEOUT in a fraction of a second rather than the 4s default —
    // without endangering the success grabs that run in the same process.
    int timeoutMs = m_grabTimeoutMs;
    const int perGrab = grabObj.value(QStringLiteral("timeoutMs")).toInt();
    if (perGrab > 0)
        timeoutMs = qMin(perGrab, m_grabTimeoutMs);   // may only shorten, never extend

    QTimer::singleShot(timeoutMs, this, [reply, target, timeoutMs]() mutable {
        reply.fail("GRAB_TIMEOUT",
                   target + QStringLiteral(": no frame within %1 ms").arg(timeoutMs));
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
