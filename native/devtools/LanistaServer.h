#pragma once
// Lanista — Colosseum's dev-control bridge (spec 2026-08-01, locked).
//
// QLocalServer on pipe "ColosseumLanista" (env COLOSSEUM_LANISTA_PIPE overrides,
// so suite-booted instances never collide with the daily app). One command per
// connection: one JSON line in, one JSON line out, close. Single-threaded on
// the Qt UI thread. ALWAYS ON for reads/grabs — local machine only, never a
// network port. Driving gates on COLOSSEUM_LANISTA_DRIVE=1 per command;
// mutations on COLOSSEUM_LANISTA_WRITE=1 (two separate switches, Tankoban 2's
// proven split).
//
// Wire format (Tankoban 2's, deliberately unchanged so brothers who know
// tankoctl already know lanista):
//   request = {"cmd": <name>, "seq": <int>, "payload": {...}}
//   reply   = {"type":"reply","seq":<int>, ...}
//   error   = {"type":"error","seq":<int>,"code":"UPPER_SNAKE","message":"..."}
//
// Framing errors the server raises on its own (seq -1 when the request could not
// be parsed): BAD_JSON, EXTRA_INPUT (more than one command on a connection),
// LINE_TOO_LONG, IDLE_TIMEOUT. Gate refusals: DRIVE_DISABLED, WRITE_DISABLED.
//
// THE COMBINED REPLY (the heart): any command's payload may carry
//   "grab": {"target": "<objectName>"|"window"}
// and the reply gains grabPath + grabbedAt — state and pixels captured in the
// SAME event-loop turn, so they can never disagree about which instant they
// describe. Grabs are async (grabToImage callback); the socket stays open
// until the callback replies. Schema: colosseum.dev.v1.
//
// Grab error codes (an agent branches on these, so each one means one thing):
//   GRAB_TARGET_NOT_FOUND — no target given, or no such objectName in the scene
//   GRAB_NOT_RENDERABLE   — the target exists but has no pixels to give (not in
//                           a window, zero-sized, or the window never rendered)
//   GRAB_SAVE_FAILED      — pixels taken, PNG could not be written
//   GRAB_TIMEOUT          — grabToImage's callback never fired (see attachGrab)
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <functional>

class QLocalServer;
class QLocalSocket;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;

class LanistaServer : public QObject
{
    Q_OBJECT
public:
    explicit LanistaServer(QQmlApplicationEngine* engine, QObject* parent = nullptr);

    static QString pipeName();

    // Did listen() succeed? Construction never throws and never aborts, and
    // qInfo()/qWarning() do NOT reach redirected output on Windows — so callers
    // (harness, scripts) must be able to ask in code instead of grepping a log.
    bool isListening() const;
    QString listenError() const { return m_listenError; }

    // Where this session's artifacts (grabs, dumps) go. The PATH is computed at
    // construction; the DIRECTORY is created only by ensureRunDir(), on the first
    // artifact write. The bridge is always on in Hemanth's daily app, so it must
    // not leave one empty timestamped directory behind per launch. Callers of
    // runDir() must therefore assume it may not exist yet.
    QString runDir() const { return m_runDir; }
    QString ensureRunDir();

    static constexpr const char* kSchema = "colosseum.dev.v1.0";

    // ── THE REPLY TOKEN — read this before adding a command ─────────────────
    //
    // A handler receives a Replier and NOTHING ELSE ANSWERS FOR IT. Two shapes
    // are legal and they look identical at the call site:
    //
    //   sync   — call reply()/fail() before returning (ping, get-state).
    //   async  — copy the Replier into a callback (grabToImage, a QTimer, a
    //            signal) and answer on a later event-loop turn. The connection
    //            stays open until then. This is how Task 2's grabs and Task 5's
    //            ui-wait-for work.
    //
    // A handler that takes ownership MUST survive the client vanishing: the
    // client can close the pipe at any moment, and the socket is deleteLater()'d
    // as soon as it does. That is exactly why a Replier is passed instead of a
    // QLocalSocket* — it holds a QPointer, so a freed socket becomes a no-op
    // rather than a use-after-free. reply()/fail() also no-op when the token has
    // already been used, so a double answer cannot corrupt the stream. Copies of
    // a Replier share that latch, so passing it by value into a lambda is safe.
    //
    // Errors: use fail() with a specific UPPER_SNAKE code (NO_SUCH_ITEM,
    // NOT_VISIBLE, TIMEOUT, …). Do not funnel everything through one generic
    // code — the client is an agent and the code is what it branches on.
    class Replier
    {
    public:
        Replier() = default;
        Replier(QLocalSocket* sock, int seq);

        // False once answered, or once the client has gone away.
        bool canReply() const;

        // The request's seq — handlers name their artifacts after it.
        int seq() const;

        void reply(QJsonObject body);
        void fail(const char* code, const QString& message);

    private:
        friend class LanistaServer;

        // THE COMBINED REPLY's seam. dispatch() installs this hook when the
        // request carries "grab", so a handler's reply() detours through
        // attachGrab() on its way to the wire: every command gains pixels and
        // NO handler has to know grabs exist. The hook is TAKEN (not copied) on
        // the way through, so it fires exactly once and the token handed to it
        // answers the wire directly. fail() ignores it — an error carries no
        // pixels, and a refused command must not photograph anything.
        using ReplyHook = std::function<void(QJsonObject body, Replier onward)>;
        void setReplyHook(ReplyHook hook);

        struct State {
            QPointer<QLocalSocket> sock;
            int seq = -1;
            bool sent = false;
            ReplyHook hook;
        };
        void send(QJsonObject line);
        QSharedPointer<State> m_state;
    };

private:
    // Restrictive value first: a default-constructed Command fails closed rather
    // than exposing an ungated command in the daily app.
    enum class Gate { Write = 0, Drive, Read };

    using Handler = std::function<void(const QJsonObject& payload, Replier reply)>;

    struct Command {
        Gate gate = Gate::Write;
        Handler fn;
    };

    // Registration. Use these — never insert into m_commands directly — so that
    // the gate is spelled out at every single registration site and adding a
    // command by copy-paste cannot silently inherit the wrong one.
    void addRead(const QString& name, Handler fn);
    void addDrive(const QString& name, Handler fn);
    void addWrite(const QString& name, Handler fn);
    void addCommand(Gate gate, const QString& name, Handler fn);

    void registerSelfTestCommands();   // COLOSSEUM_LANISTA_SELFTEST=1 only

    static bool driveOpen();
    static bool writeOpen();

    void onNewConnection();
    void onReadyRead(QLocalSocket* sock);
    void dispatch(QLocalSocket* sock, const QJsonObject& req);

    // Task 1
    QJsonObject cmdPing() const;
    QJsonObject cmdGetState() const;
    // Task 2 — never called directly: dispatch() installs it as the Replier's
    // hook, and it answers `reply` on every path (including its own deadline).
    void attachGrab(const QJsonObject& payload, QJsonObject body, Replier reply);
    // Task 3
    QJsonObject cmdQmlGet(const QJsonObject& p) const;
    QJsonObject cmdDumpUi(const QJsonObject& p) const;
    QJsonObject cmdUiQuery(const QJsonObject& p) const;
    // Task 4
    QJsonObject cmdUiSnapshot(const QJsonObject& p);
    // Task 5
    QJsonObject cmdUiClick(const QJsonObject& p) const;
    QJsonObject cmdUiKeypress(const QJsonObject& p) const;
    QJsonObject cmdUiTextInput(const QJsonObject& p) const;
    QJsonObject cmdUiScroll(const QJsonObject& p) const;
    void cmdUiWaitFor(const QJsonObject& p, Replier reply);
    // Task 9
    QJsonObject cmdInvokeRead(const QJsonObject& p) const;

    // NOTE (Tasks 2-3 targeting): these see ROOT objects only. A QML-declared
    // secondary Window, a Popup with its own window, or anything not reachable
    // as a child of a root object is invisible here — and therefore ungrabbable.
    // mainWindow() returns the FIRST root QQuickWindow, which is "first", not
    // necessarily "main"; with one root window (Colosseum today) they coincide.
    QQuickWindow* mainWindow() const;
    QQuickItem* findItem(const QString& objectName) const;
    QQuickItem* resolveTarget(const QString& ref) const;   // objectName or "h<N>" handle

    // Per-connection state. `spent` latches once a command line has been taken
    // from this connection: the wire contract is one command per connection, so
    // anything arriving afterwards is dropped rather than buffered or (the
    // original defect) re-dispatched.
    struct Conn {
        QByteArray buf;
        bool spent = false;
    };

    QQmlApplicationEngine* m_engine;
    QLocalServer* m_server = nullptr;
    QString m_listenError;
    QHash<QString, Command> m_commands;
    QHash<QLocalSocket*, Conn> m_conns;
    QHash<QString, QPointer<QQuickItem>> m_handles;   // last ui-snapshot
    QString m_runDir;
    bool m_runDirCreated = false;
    int m_idleTimeoutMs = 0;
    int m_grabTimeoutMs = 4000;   // COLOSSEUM_LANISTA_GRAB_MS overrides (tests)
    int m_dispatchCount = 0;
    int m_grabCounter = 0;
    bool m_orphanChecked = false;      // selftest-orphan only
    bool m_orphanCouldReply = false;   // selftest-orphan only
};
