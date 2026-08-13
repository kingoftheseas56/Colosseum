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
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QString>
#include <functional>
#include <memory>

class QLocalServer;
class QLocalSocket;
class QQmlApplicationEngine;
class QQuickItem;
class QQuickWindow;
class LanistaEventLog;

class LanistaServer : public QObject
{
    Q_OBJECT
public:
    explicit LanistaServer(QQmlApplicationEngine* engine, QObject* parent = nullptr);
    // Out-of-line (defined in the .cpp) ONLY so the unique_ptr<LanistaEventLog>
    // member below can destroy a type that is merely forward-declared here — the
    // deleter needs the complete type, and the .cpp has the include.
    ~LanistaServer();

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
    // Task 3. The two fallible reads take the Replier directly (like attachGrab):
    // they resolve findItem() ONCE and either fail("NO_SUCH_ITEM", ...) or reply,
    // rather than resolving in the lambda and again in the method. dump-ui cannot
    // fail on a target, so it stays a plain body-returning read.
    void cmdQmlGet(const QJsonObject& p, Replier reply) const;
    // L1-Bridge (2026-08-13): dump-ui is no longer const-shaped — an explicit
    // `root` can fail NO_SUCH_ITEM, and the walk now mints structural handles
    // (mutates m_handles/m_itemHandles/m_snapshotEpoch), so it owns the Replier
    // like the Task 3 fallible reads. ui-query lost its const for the same
    // reason: it now mints a handle for its target and clip-chain ancestors too.
    void cmdDumpUi(const QJsonObject& p, Replier reply);
    void cmdUiQuery(const QJsonObject& p, Replier reply);
    // Task 4
    QJsonObject cmdUiSnapshot(const QJsonObject& p);
    // Task 5 — the "hands". Each can fail on its target (NO_SUCH_ITEM / NO_WINDOW,
    // and ui-keypress on BAD_KEY), so they own the Replier and fail()/reply()
    // themselves, exactly like the Task 3 fallible reads. The DRIVE gate is
    // enforced centrally in dispatch(); the handlers never re-check it.
    //
    // KEYS vs TEXT: ui-keypress is for keys/shortcuts — QKeySequence NORMALISES
    // the name, so "a" and "A" both parse to Key_A (text "A"); use it for
    // Enter/Tab/Ctrl+S and the like. ui-text-input is the channel for LITERAL
    // characters, sending each char's text verbatim into the focused field.
    //
    // These four are const: the SERVER object is untouched. That is NOT
    // "observational" — the APP is genuinely driven, via synthetic events posted
    // to its window; const only says the mutation lands in the scene, not in us.
    void cmdUiClick(const QJsonObject& p, Replier reply) const;
    void cmdUiKeypress(const QJsonObject& p, Replier reply) const;
    void cmdUiTextInput(const QJsonObject& p, Replier reply) const;
    void cmdUiScroll(const QJsonObject& p, Replier reply) const;
    void cmdUiWaitFor(const QJsonObject& p, Replier reply);
    // Task 9 — invoke-read is FALLIBLE (off-allowlist / missing organ / no match),
    // so it owns the Replier and fail()/reply()s itself, exactly like the Task 3
    // fallible reads. It is const: it only READS an organ's invokable, never the
    // server.
    void cmdInvokeRead(const QJsonObject& p, Replier reply) const;
    // Task 10 — the DEV event log. Neither can fail on a target, so both RETURN
    // their body (like cmdDumpUi) rather than owning the Replier. Both are const:
    // log-mark's only mutation lands in the external events.jsonl, not in us —
    // the same sense in which the Task 5 driving commands are const.
    QJsonObject cmdEventsTail(const QJsonObject& p) const;
    QJsonObject cmdLogMark(const QJsonObject& p) const;

    // NOTE (Tasks 2-3 targeting): these see ROOT objects only. A QML-declared
    // secondary Window, a Popup with its own window, or anything not reachable
    // as a child of a root object is invisible here — and therefore ungrabbable.
    // mainWindow() returns the FIRST root QQuickWindow, which is "first", not
    // necessarily "main"; with one root window (Colosseum today) they coincide.
    QQuickWindow* mainWindow() const;
    QQuickItem* findItem(const QString& objectName) const;

    // THE HANDLE MODEL (Task 4). ui-snapshot returns every actionable element with
    // an OPAQUE session handle — token shape "s<gen>h<n>", to be treated as a
    // cookie, not parsed by clients. resolveTarget(ref) is the ONE resolver every
    // targeted read/grab routes through: a handle-shaped ref resolves via m_handles
    // (handle takes precedence), anything else is an objectName resolved by
    // findItem() unchanged. A handle is SINGLE-SNAPSHOT-SCOPED: each ui-snapshot
    // bumps m_snapshotEpoch and rebuilds m_handles, so a token minted by an earlier
    // snapshot is a clean miss (NO_SUCH_ITEM), never a silent hit on the new Nth
    // item. m_handles is GLOBAL server state — a new snapshot from ANY client
    // invalidates every prior handle — which is fine for this local single-user
    // bridge. Handles live only until the next snapshot; names live as long as the
    // item does.
    QQuickItem* resolveTarget(const QString& ref) const;   // objectName or snapshot handle

    // L1-Bridge (2026-08-13): the SAME identity scheme above, reused rather than
    // reinvented (L1-Discovery's verdict) so dump-ui's all-item walk and
    // ui-query's targeted read can mint/reuse "s<gen>h<n>" tokens for items
    // ui-snapshot never actually visited (it only visits actionable ones).
    //
    // mintOrReuseHandle hands back the SAME token for the SAME item within one
    // generation — so an item's own dump-ui row and its appearance inside a
    // sibling's clipChain agree — and a brand-new token the first time an item
    // is seen this generation. beginNewGeneration() is the ONE place that bumps
    // m_snapshotEpoch and clears every handle map; ui-snapshot and a FRESH
    // dump-ui both call it, so "handles die at the next snapshot — from ANY
    // client" now covers structural handles too, unchanged in spirit.
    QString mintOrReuseHandle(QQuickItem* item);
    void beginNewGeneration();
    // The ordered clip:true ancestor chain between `item` and the root window,
    // nearest ancestor first, each carrying its own minted handle and scene
    // rect. This is the direct fix for the demonstrated wrong answer
    // clippedByWindow gives today (L1-Discovery row 4): an item can be
    // visible:true, clippedByWindow:false, and still not actually be on
    // screen because an intermediate clip:true ancestor cuts it off first.
    QJsonArray clipChainFor(QQuickItem* item);

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
    QHash<QString, QPointer<QQuickItem>> m_handles;   // last snapshot/dump-ui gen, keyed "h<n>"
    // L1-Bridge: the reverse of m_handles (item -> "h<n>", no "s<gen>" prefix) —
    // an O(1) "does this item already have a token this generation" check, used
    // by clip-chain ancestor minting and dump-ui's parent-handle lookups so an
    // item visited twice (once as a row, once as another row's clip ancestor)
    // never gets two different handles. Cleared alongside m_handles, always.
    QHash<QQuickItem*, QString> m_itemHandles;
    int m_snapshotEpoch = 0;   // bumped per ui-snapshot/fresh dump-ui; embedded in each handle token
    int m_handleCounter = 0;   // next "<n>" to mint; reset to 0 by beginNewGeneration()
    QString m_runDir;
    bool m_runDirCreated = false;
    // Task 10: the rotating JSONL event stream. log-mark writes a diagnostic
    // annotation here (NOT app state), and events-tail reads it back — both are
    // READ-gated/always-on. Constructed after m_runDir setup in the ctor; a
    // unique_ptr so the always-on bridge's lone event log is freed with the server
    // rather than leaked (this class parents everything else to Qt).
    std::unique_ptr<LanistaEventLog> m_events;
    int m_idleTimeoutMs = 0;
    int m_grabTimeoutMs = 0;   // ctor resolves: COLOSSEUM_LANISTA_GRAB_MS, else
                               // kGrabTimeoutMs. Always lands positive (0 is not
                               // "no timeout" — that would leak connections).
    int m_dispatchCount = 0;
    int m_grabCounter = 0;
    // Monotonic source for synthetic-event timestamps. Started in the ctor; each
    // driving command stamps its QMouseEvent/QKeyEvent/QWheelEvent with elapsed()
    // so a press/release pair share a near-identical time and successive commands
    // are strictly later — the sequence Qt needs to keep chained clicks from
    // coalescing as double-clicks and to give wheel momentum a defined direction.
    QElapsedTimer m_inputClock;
    bool m_orphanChecked = false;      // selftest-orphan only
    bool m_orphanCouldReply = false;   // selftest-orphan only
};
