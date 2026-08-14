// Lanista bridge harness. Boots a real QQmlApplicationEngine over the
// deterministic scene, constructs LanistaServer on a TEST pipe, then either:
//   (default)   runs the built-in checks through a real QLocalSocket client
//               and exits 0 with sentinel LANISTA_OK / 1 on first failure;
//   --serve     stays alive so lanista.exe / the .ps1 can drive it.
// Follows the house harness pattern: require() + sentinel + exit code.
//
// The server registers its selftest-* fixture commands only because this harness
// sets COLOSSEUM_LANISTA_SELFTEST=1; the daily app never sees them.
#include "devtools/LanistaServer.h"
#include "engine/VaultConfig.h"
#include "engine/VaultForensics.h"
#include "engine/VaultIdentity.h"
#include "engine/VaultIndex.h"
#include "engine/VaultLibrary.h"
#include "engine/VaultScanner.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>

static void require(bool cond, const QString& msg)
{
    if (!cond) { std::cerr << "FAIL: " << qUtf8Printable(msg) << "\n"; std::exit(1); }
}

// Why the last exchange came back empty/short. Without this a bridge that never
// came up is indistinguishable from a bridge that answered wrong.
static QString g_lastCallError;
static QString why()
{
    return g_lastCallError.isEmpty()
               ? QString()
               : QStringLiteral(" [") + g_lastCallError + QLatin1Char(']');
}

// Writes `chunks` — one per event-loop turn, so the server really does see them
// as separate readyRead deliveries — and collects EVERY byte the server writes
// back until it hangs up or the timeout expires. The framing tests assert on the
// NUMBER of reply lines, so this must never stop at the first one.
//
// Client and server share this process AND this thread, so the obvious
// waitForConnected()/waitForReadyRead() spelling deadlocks: it blocks the very
// event loop the server needs in order to answer. Everything here is signal-driven.
static QByteArray rawExchange(const QString& pipe, const QList<QByteArray>& chunks,
                              int timeoutMs = 5000)
{
    g_lastCallError.clear();
    QLocalSocket sock;
    QEventLoop loop;
    QByteArray got;

    QObject::connect(&sock, &QLocalSocket::readyRead, &loop,
                     [&]() { got += sock.readAll(); });
    QObject::connect(&sock, &QLocalSocket::disconnected, &loop, [&]() {
        got += sock.readAll();
        loop.quit();
    });
    QObject::connect(&sock, &QLocalSocket::errorOccurred, &loop,
                     [&](QLocalSocket::LocalSocketError e) {
                         if (e != QLocalSocket::PeerClosedError)
                             g_lastCallError = sock.errorString();
                         loop.quit();
                     });
    QObject::connect(&sock, &QLocalSocket::connected, &loop, [&]() {
        for (int i = 0; i < chunks.size(); ++i) {
            const QByteArray chunk = chunks.at(i);
            QTimer::singleShot(i * 80, &sock, [&sock, chunk]() {
                sock.write(chunk);
                sock.flush();
            });
        }
    });
    QTimer::singleShot(timeoutMs, &loop, [&]() {
        if (g_lastCallError.isEmpty())
            g_lastCallError = QStringLiteral("timed out after %1 ms").arg(timeoutMs);
        loop.quit();
    });

    sock.connectToServer(pipe);
    loop.exec();
    return got;
}

static int lineCount(const QByteArray& raw) { return int(raw.count('\n')); }

static QJsonObject firstObject(const QByteArray& raw)
{
    const qsizetype nl = raw.indexOf('\n');
    return QJsonDocument::fromJson(nl < 0 ? raw : raw.left(nl)).object();
}

// One command per connection, exactly like a real client.
static QJsonObject call(const QString& pipe, const QJsonObject& req, int timeoutMs = 5000)
{
    const QByteArray raw = rawExchange(
        pipe, {QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n"}, timeoutMs);
    return raw.isEmpty() ? QJsonObject() : firstObject(raw);
}

static int dispatchCount(const QString& pipe)
{
    return call(pipe, {{"cmd", "selftest-dispatches"}, {"seq", 0}})
        .value("dispatches").toInt(-1);
}

// Runs a nested event loop for `ms` so the server can get on with its work.
static void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, [&]() { loop.quit(); });
    loop.exec();
}

// ── F1-Bridge fixture: a REAL VaultLibrary, small enough to build in one harness run ──
// Mirrors tests/auto/vault/tst_vault_forensics.cpp's buildNestedFixture idiom (F1-Core's own
// fixture pattern, reused rather than reinvented): one confirmed root -> one "Shows"
// intermediate folder -> `childCount` sibling film folders (one video file each). childCount is
// deliberately > VaultForensics::kMaxLimit (100) so vault_forensics_clamps_limit has real rows to
// clamp, not just an assumption the clamp would bite if there were ever more.
struct VaultForensicsFixture {
    QTemporaryDir tmp;
    QString rootPath;
    QString showsPath;
    std::unique_ptr<VaultIndex> index;
    std::unique_ptr<VaultConfig> config;
    std::unique_ptr<VaultIdentity> identity;
    std::unique_ptr<VaultScanner> scanner;
    std::unique_ptr<VaultLibrary> library;   // destroyed FIRST (reverse declaration order)
};

static void writeFixtureStub(const QString& path)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("vault forensics fixture: stub file writes: ") + path);
    f.write(QByteArrayLiteral("stub"));
}

static std::unique_ptr<VaultForensicsFixture> buildVaultForensicsFixture(int childCount)
{
    auto fx = std::make_unique<VaultForensicsFixture>();
    require(fx->tmp.isValid(), QStringLiteral("vault forensics fixture: temp dir is valid"));

    // Lower-cased on Windows to match VaultConfig::norm()'s own normalization (private, mirrored
    // here the same way tst_vault_forensics.cpp's normalizedRootPath() does) -- otherwise
    // rootsDetail()'s (lowercased) path silently never matches a FileRow.rootPath built from
    // QTemporaryDir::path()'s mixed-case form.
    QString root = QDir::cleanPath(QDir(fx->tmp.path()).filePath(QStringLiteral("root")));
#ifdef Q_OS_WIN
    root = root.toLower();
#endif
    fx->rootPath = root;
    fx->showsPath = QDir(fx->rootPath).filePath(QStringLiteral("Shows"));
    QDir().mkpath(fx->showsPath);

    fx->index = std::make_unique<VaultIndex>(
        QDir(fx->tmp.path()).filePath(QStringLiteral("index.sqlite")));
    fx->config = std::make_unique<VaultConfig>(fx->tmp.path());
    fx->identity = std::make_unique<VaultIdentity>(fx->tmp.path());
    fx->scanner = std::make_unique<VaultScanner>(fx->index.get(), fx->identity.get());
    fx->config->addRoot(fx->rootPath);
    fx->config->confirmRoot(fx->rootPath);

    QList<VaultIndex::FileRow> rows;
    for (int i = 0; i < childCount; ++i) {
        const QString folder = QDir(fx->showsPath).filePath(QStringLiteral("c%1").arg(i));
        QDir().mkpath(folder);
        const QString file = QDir(folder).filePath(QStringLiteral("movie.mp4"));
        writeFixtureStub(file);

        VaultIndex::FileRow r;
        r.id = QStringLiteral("vault:child-%1").arg(i);
        r.rootPath = fx->rootPath;
        const QString canonicalFolder = QFileInfo(folder).absoluteFilePath();
        r.subtreePath = canonicalFolder;
        r.groupKey = canonicalFolder;
        r.groupTitle = QStringLiteral("Movie");
        r.kind = QStringLiteral("video");
        r.path = QFileInfo(file).absoluteFilePath();
        r.displayTitle = QStringLiteral("Movie");
        r.realName = QStringLiteral("movie.mp4");
        r.size = 4;
        r.mtimeMs = 1000 + i;
        rows.append(r);
    }
    fx->index->publish(rows);
    // cacheDir (browse-artwork execution plan, Slice 3 part 2): mirrors main.cpp's own choice
    // of reusing the SAME dir vaultConfig/vaultIdentity/vaultIndex already sit under (vaultDir)
    // rather than a second cache root. fx->tmp is this fixture's own QTemporaryDir, already used
    // exclusively for index.sqlite/config/identity above -- reusing its path here is isolated
    // (unique per fixture run, auto-cleaned on destruction) and needs no extra mkpath.
    fx->library = std::make_unique<VaultLibrary>(fx->index.get(), fx->scanner.get(),
                                                  fx->config.get(), fx->identity.get(),
                                                  fx->tmp.path());
    return fx;
}

int main(int argc, char** argv)
{
    // FIRST LINE, before the platform plugin loads: offscreen carries no font
    // database of its own, so without this every grab comes back with tofu boxes
    // where the text should be (house trap; Task 8's goldens ride on it). Point
    // it at the system fonts so offscreen text renders as REAL GLYPHS.
    qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");   // offscreen text renders real glyphs

    // Headless by default. Caller wins, and that is load-bearing rather than
    // tidy: offscreen loads the SOFTWARE scene graph backend, so every grab the
    // default run proves is a software-rendered one. QT_QPA_PLATFORM=windows
    // re-runs the same checks through the RHI the real app uses.
    //
    // ⚠ That RHI run needs QSG_NO_VSYNC=1 (measured 2026-08-01, cost an hour):
    //   QT_QPA_PLATFORM=windows QSG_NO_VSYNC=1 lanista_harness.exe
    // Without it every ITEM grab comes back GRAB_TIMEOUT. Not a bridge defect —
    // this scene is inert and its window is unfocused, so the D3D11 present path
    // stalls and the window renders exactly ONE frame for its whole life;
    // grabToImage needs a frame and never gets one. (Same family as A4's
    // "timers stop firing, QSG_NO_VSYNC=1 fixes it".) The real app renders
    // continuously, and item grabs were proven against the SHIPPED binary over
    // this very scene — colosseum.exe takes a QML path as argv[1] — with no
    // vsync knob at all. Window grabs are immune either way: grabWindow()
    // renders on demand rather than waiting for the loop.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // House trap (see tests/window_shell_gui_harness.cpp): the windeployqt'd
    // platforms/ dir beside the exe carries ONLY qwindows.dll and shadows the Qt
    // install's plugin dir, so "offscreen" cannot load and the process fail-fasts
    // silently. Point the QPA loader back at the real Qt plugins dir. The path is
    // baked in by CMake (LANISTA_QT_PLATFORMS_DIR) rather than read from
    // QLibraryInfo, which resolves relative to the DEPLOYED Qt6Core.dll and so
    // hands back the very directory that is shadowing us. Caller wins if set.
#ifdef LANISTA_QT_PLATFORMS_DIR
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH"))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", LANISTA_QT_PLATFORMS_DIR);
#endif

    QGuiApplication app(argc, argv);
    const bool serve = app.arguments().contains(QStringLiteral("--serve"));

    // Test pipe — NEVER the default, so a running daily Colosseum is untouched.
    const QString pipe = QStringLiteral("ColosseumLanistaTest");
    qputenv("COLOSSEUM_LANISTA_PIPE", pipe.toUtf8());
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    // Short idle timeout so the always-on listener's hang-up is testable in
    // about a second instead of ten. Every real connection below speaks at once.
    qputenv("COLOSSEUM_LANISTA_IDLE_MS", "1200");
    // The gates must start CLOSED: the denial tests are the point.
    qunsetenv("COLOSSEUM_LANISTA_DRIVE");
    qunsetenv("COLOSSEUM_LANISTA_WRITE");

    QQmlApplicationEngine engine;
#ifdef LANISTA_SCENE_DIR
    const QString scene = QDir::cleanPath(
        QStringLiteral(LANISTA_SCENE_DIR "/lanista_harness_scene.qml"));
#else
    const QString scene = QFileInfo(QStringLiteral(__FILE__)).absolutePath()
                          + QStringLiteral("/lanista_harness_scene.qml");
#endif
    engine.load(QUrl::fromLocalFile(scene));
    require(!engine.rootObjects().isEmpty(),
            QStringLiteral("harness scene loads: ") + scene);

    // A PNG is in DEVICE pixels; the scene (and get-state) speaks LOGICAL ones.
    // On this machine that is a 1.5x difference under the real platform and 1x
    // offscreen, so the grab checks below scale the scene's declared sizes by
    // the live ratio instead of hard-coding either one.
    auto* rootWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    require(rootWindow != nullptr, QStringLiteral("harness root is a Window"));
    const qreal dpr = rootWindow->devicePixelRatio();

    auto* server = new LanistaServer(&engine, &app);
    require(server->isListening(),
            QStringLiteral("lanista listens on ") + pipe + QStringLiteral(": ")
                + (server->listenError().isEmpty() ? QStringLiteral("(no error reported)")
                                                   : server->listenError()));

    // F1-Bridge: a REAL VaultLibrary (105 rows -- 5 over VaultForensics::kMaxLimit) wired into
    // the server exactly the way main.cpp wires the production one, so "vault-forensics" is
    // genuinely exercised here rather than left permanently VAULT_FORENSICS_UNAVAILABLE.
    auto vaultFixture = buildVaultForensicsFixture(105);
    auto vaultForensics = std::make_unique<VaultForensics>(vaultFixture->library.get());
    server->setVaultForensics(vaultForensics.get());

    if (serve) {
        std::cout << "LANISTA_SERVING " << pipe.toStdString() << "\n" << std::flush;
        return app.exec();
    }

    int rc = 1;
    QTimer::singleShot(0, &app, [&]() {
        // ── ping: schema + command list ──────────────────────────────────
        QJsonObject pong = call(pipe, {{"cmd", "ping"}, {"seq", 1}});
        require(pong.value("type").toString() == "reply", "ping replies" + why());
        require(pong.value("seq").toInt() == 1, "seq echoed");
        require(pong.value("schema").toString().startsWith("colosseum.dev.v1"),
                "schema is colosseum.dev.v1.x");
        require(pong.value("commands").toArray().contains(QJsonValue("get-state")),
                "command list present");

        // command list is sorted — QHash order is randomised per process
        QStringList names;
        const QJsonArray cmdArray = pong.value("commands").toArray();
        for (const QJsonValue& v : cmdArray) names << v.toString();
        QStringList sorted = names;
        std::sort(sorted.begin(), sorted.end());
        require(names == sorted, "ping's command list is sorted");

        // ping reports the gate state so a client need not know our env names
        const QJsonObject gates = pong.value("gates").toObject();
        require(gates.value("read").toBool(), "ping: read gate open");
        require(!gates.value("drive").toBool(), "ping: drive gate reported closed");
        require(!gates.value("write").toBool(), "ping: write gate reported closed");

        // ── get-state: window geometry ───────────────────────────────────
        QJsonObject st = call(pipe, {{"cmd", "get-state"}, {"seq", 2}});
        require(st.value("type").toString() == "reply", "get-state replies" + why());
        require(st.value("windows").toArray().size() == 1, "one window reported");
        QJsonObject w = st.value("windows").toArray().first().toObject();
        require(w.value("width").toInt() == 800 && w.value("height").toInt() == 600,
                "window geometry is real");

        // runDir is a PATH, not a directory the always-on app litters per launch
        const QString runDir = st.value("runDir").toString();
        require(!runDir.isEmpty(), "get-state reports a runDir");
        require(!QFileInfo::exists(runDir),
                QStringLiteral("runDir is not created until an artifact is written: ") + runDir);

        // ── unknown command errors loudly, never hangs ───────────────────
        QJsonObject bad = call(pipe, {{"cmd", "no-such"}, {"seq", 3}});
        require(bad.value("type").toString() == "error", "unknown cmd is an error" + why());
        require(bad.value("code").toString() == "UNKNOWN_CMD", "with the code");

        // ── malformed input ──────────────────────────────────────────────
        QByteArray raw = rawExchange(pipe, {QByteArray("this is not json\n")});
        require(lineCount(raw) == 1, "BAD_JSON: exactly one reply line" + why());
        require(firstObject(raw).value("code").toString() == "BAD_JSON",
                "BAD_JSON: with the code");

        // ── framing: a single line split across two writes ───────────────
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"ping\",\"se"),
                                 QByteArray("q\":7}\n")});
        require(lineCount(raw) == 1, "split line: exactly one reply" + why());
        require(firstObject(raw).value("seq").toInt() == 7, "split line: reassembled");

        // ── C1 REGRESSION: a consumed line is never dispatched twice ─────
        // Trailing bytes arrive in their own readyRead while an ASYNC command
        // still holds the connection open. Before the fix the buffer was never
        // consumed, so that second delivery re-found the same newline and ran
        // the same command again. The duplicate reply is INVISIBLE on the wire
        // (it lands after the first reply has already closed the socket), which
        // is exactly why this asserts on the server's dispatch counter too.
        const int before = dispatchCount(pipe);
        require(before >= 0, "selftest-dispatches is available" + why());
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"selftest-slow\",\"seq\":10}\n"),
                                 QByteArray("zzzz")});
        require(lineCount(raw) == 1, "trailing bytes: exactly ONE reply" + why());
        require(firstObject(raw).value("slow").toBool(),
                "trailing bytes: the reply is the command's own");
        const int after = dispatchCount(pipe);
        require(after == before + 2,
                QStringLiteral("C1: the consumed line is never re-dispatched "
                               "(dispatches %1 -> %2, expected +2)").arg(before).arg(after));

        // ── framing: pipelining is refused explicitly, not dropped in silence ──
        raw = rawExchange(pipe, {QByteArray("{\"cmd\":\"ping\",\"seq\":8}\n"
                                            "{\"cmd\":\"ping\",\"seq\":9}\n")});
        require(lineCount(raw) == 1, "pipelined: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "EXTRA_INPUT",
                "pipelined: EXTRA_INPUT rather than silence");

        // ── I3: an unbounded line is refused ─────────────────────────────
        raw = rawExchange(pipe, {QByteArray(1100000, 'x')});
        require(lineCount(raw) == 1, "oversize line: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "LINE_TOO_LONG",
                "oversize line: LINE_TOO_LONG");

        // ── handlers own their error codes ───────────────────────────────
        QJsonObject failed = call(pipe, {{"cmd", "selftest-fail"}, {"seq", 11}});
        require(failed.value("type").toString() == "error", "selftest-fail errors" + why());
        require(failed.value("code").toString() == "SELFTEST_FAILURE",
                "a handler's own UPPER_SNAKE code reaches the client");

        // ── C2 REGRESSION: the client may vanish mid-command ─────────────
        // selftest-orphan answers ~250ms later through a Replier it captured. We
        // hang up immediately. The token must refuse to touch the dead socket —
        // a raw QLocalSocket* held across those turns is a use-after-free.
        //
        // Do NOT assert this by expecting a crash: writing to the freed socket
        // usually succeeds quietly, so "the harness survived" proves nothing.
        // Ask the server what the token itself decided.
        {
            // NB: connectToServer() can emit connected() synchronously, so this
            // must NOT be driven by quitting a nested loop from that handler —
            // the quit would land before exec() and we would hang around for the
            // timeout instead of hanging up. Fixed short wait, then abort.
            QLocalSocket sock;
            QObject::connect(&sock, &QLocalSocket::connected, &sock, [&]() {
                sock.write("{\"cmd\":\"selftest-orphan\",\"seq\":12}\n");
                sock.flush();
            });
            sock.connectToServer(pipe);
            settle(60);     // long enough for the server to read and dispatch
            sock.abort();   // walk away, ~200ms before the handler answers
        }
        settle(600);        // let the orphaned handler fire into the void
        QJsonObject orphan = call(pipe, {{"cmd", "selftest-orphan-result"}, {"seq", 13}});
        require(orphan.value("checked").toBool(),
                "C2: the orphaned handler did run" + why());
        require(!orphan.value("couldReply").toBool(),
                "C2: a Replier whose client vanished refuses to touch the socket");
        QJsonObject alive = call(pipe, {{"cmd", "ping"}, {"seq", 14}});
        require(alive.value("type").toString() == "reply",
                "server survives a client that vanished mid-command" + why());

        // ── the gates: the whole safety model ────────────────────────────
        QJsonObject drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 20}});
        require(drive.value("code").toString() == "DRIVE_DISABLED",
                "drive gate is CLOSED by default" + why());
        qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
        drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 21}});
        require(drive.value("drove").toBool(), "drive gate OPENS with the env var" + why());
        qunsetenv("COLOSSEUM_LANISTA_DRIVE");
        drive = call(pipe, {{"cmd", "selftest-drive"}, {"seq", 22}});
        require(drive.value("code").toString() == "DRIVE_DISABLED",
                "drive gate CLOSES again" + why());

        QJsonObject write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 23}});
        require(write.value("code").toString() == "WRITE_DISABLED",
                "write gate is CLOSED by default" + why());
        qputenv("COLOSSEUM_LANISTA_WRITE", "1");
        write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 24}});
        require(write.value("wrote").toBool(), "write gate OPENS with the env var" + why());
        qunsetenv("COLOSSEUM_LANISTA_WRITE");
        write = call(pipe, {{"cmd", "selftest-write"}, {"seq", 25}});
        require(write.value("code").toString() == "WRITE_DISABLED",
                "write gate CLOSES again" + why());

        // a read command is never caught by either gate
        require(call(pipe, {{"cmd", "ping"}, {"seq", 26}}).value("type").toString() == "reply",
                "reads stay open with both gates closed" + why());

        // ── I3: a silent connection is hung up on ────────────────────────
        raw = rawExchange(pipe, {}, 5000);
        require(lineCount(raw) == 1, "idle connection: exactly one reply" + why());
        require(firstObject(raw).value("code").toString() == "IDLE_TIMEOUT",
                "idle connection: IDLE_TIMEOUT");

        // ── THE COMBINED REPLY: state + pixels of the same instant ───────
        // The scene is 800x600 and longList is 300x400 (lanista_harness_scene.qml),
        // so the PNG's own dimensions say WHICH grab actually ran: a window path
        // that quietly answered an item request, or the reverse, cannot pass.
        QJsonObject g = call(pipe, {{"cmd", "get-state"}, {"seq", 30},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "window"}}}}}},
            8000);
        require(g.value("type").toString() == "reply", "grab-carrying reply arrives" + why());
        const QString gp = g.value("grabPath").toString();
        require(!gp.isEmpty(), "reply carries grabPath");
        require(QFileInfo::exists(gp), "the PNG exists on disk: " + gp);
        require(QFileInfo(gp).size() > 1000, "the PNG is not empty");
        QImage windowPng(gp);
        require(windowPng.width() == qRound(800 * dpr)
                    && windowPng.height() == qRound(600 * dpr),
                QStringLiteral("the window PNG is the window (800x600 @%1), got %2x%3")
                    .arg(dpr).arg(windowPng.width()).arg(windowPng.height()));
        require(QDateTime::fromString(g.value("grabbedAt").toString(),
                                      Qt::ISODateWithMs).isValid(),
                "grabbedAt is a real ISO timestamp with ms: "
                    + g.value("grabbedAt").toString());
        // The GRAB DECORATES the reply; it does not replace it. State and
        // pixels arrive together or the whole design is pointless.
        require(g.value("windows").toArray().size() == 1,
                "the command's own body survives alongside the pixels");
        // First artifact written -> the lazy run dir exists NOW (it was proven
        // absent above, so nothing but this grab can have created it).
        require(QFileInfo::exists(runDir),
                "ensureRunDir() created the run dir on the first artifact: " + runDir);
        require(gp.startsWith(runDir + QStringLiteral("/")),
                "the PNG landed inside the run dir get-state announced");

        // Item grab: ASYNC — the server answers from grabToImage's callback.
        QJsonObject ig = call(pipe, {{"cmd", "get-state"}, {"seq", 31},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "longList"}}}}}},
            8000);
        require(ig.value("type").toString() == "reply", "item grab replies" + why());
        const QString ip = ig.value("grabPath").toString();
        require(QFileInfo::exists(ip), "item grab lands on disk: " + ip);
        QImage itemPng(ip);
        require(itemPng.width() == qRound(300 * dpr)
                    && itemPng.height() == qRound(400 * dpr),
                QStringLiteral("the item PNG is the ITEM (300x400 @%1), got %2x%3")
                    .arg(dpr).arg(itemPng.width()).arg(itemPng.height()));

        // ── I3: a DELEGATE item resolves via the VISUAL-tree walk ────────
        // longList's Repeater builds row0, row1, … These delegate items live
        // ONLY in the visual tree (childItems); the QObject-tree findChild()
        // the bridge shipped with could not see them, so every delegate-built
        // objectName (all 110 in production) returned GRAB_TARGET_NOT_FOUND.
        // The row delegate is 300x50 (lanista_harness_scene.qml), so reading the
        // saved PNG's own dimensions off disk with QImage(path) — exactly as the
        // seq 31 item grab does — proves the grab captured the DELEGATE and not
        // just "some item". (The "read back undefined" caution is about the
        // QML-side result.image.width property, NOT the PNG-on-disk dimensions
        // used here.)
        QJsonObject dg = call(pipe, {{"cmd", "get-state"}, {"seq", 36},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "row0"}}}}}},
            8000);
        require(dg.value("type").toString() == "reply",
                "delegate item row0 resolves and grabs (visual-tree walk)" + why());
        const QString dp = dg.value("grabPath").toString();
        require(QFileInfo::exists(dp) && QFileInfo(dp).size() > 0,
                "delegate item row0 lands a non-empty PNG on disk: " + dp);
        QImage rowPng(dp);
        require(rowPng.width() == qRound(300 * dpr)
                    && rowPng.height() == qRound(50 * dpr),
                QStringLiteral("the delegate PNG is the ROW (300x50 @%1), got %2x%3")
                    .arg(dpr).arg(rowPng.width()).arg(rowPng.height()));

        // A grab rides ANY command, not just get-state.
        QJsonObject pg = call(pipe, {{"cmd", "ping"}, {"seq", 32},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "counterButton"}}}}}},
            8000);
        require(pg.value("schema").toString().startsWith("colosseum.dev.v1"),
                "grab on ping: ping's own reply is intact" + why());
        require(QFileInfo::exists(pg.value("grabPath").toString()),
                "grab on ping: pixels too");

        // A bad target is an error, not a hang.
        QJsonObject ng = call(pipe, {{"cmd", "get-state"}, {"seq", 33},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "no-such"}}}}}},
            8000);
        require(ng.value("type").toString() == "error"
                && ng.value("code").toString() == "GRAB_TARGET_NOT_FOUND",
                "unknown grab target errors loudly" + why());

        // An EMPTY target must be refused, never resolved: findChild("") would
        // otherwise match the first unnamed item and photograph a stranger.
        QJsonObject eg = call(pipe, {{"cmd", "get-state"}, {"seq", 34},
            {"payload", QJsonObject{{"grab", QJsonObject{}}}}}, 8000);
        require(eg.value("code").toString() == "GRAB_TARGET_NOT_FOUND",
                "an empty grab target is refused, not resolved" + why());
        require(eg.value("grabPath").toString().isEmpty(), "and photographs nothing");

        // A command that FAILS keeps its own error code — the grab hook must
        // not swallow it, and a refusal is never photographed.
        QJsonObject fg = call(pipe, {{"cmd", "selftest-fail"}, {"seq", 35},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", "window"}}}}}},
            8000);
        require(fg.value("code").toString() == "SELFTEST_FAILURE",
                "a failing command still answers with ITS code, grab or not" + why());
        require(fg.value("grabPath").toString().isEmpty(),
                "a refused command carries no pixels");

        // ── I1: a grab whose client vanished writes NO orphan PNG ─────────
        // selftest-slow holds the token ~250ms, then answers through the grab
        // hook. We hang up first. attachGrab must early-out on !canReply(): no
        // framebuffer capture, no orphan file. Same discipline as C2 above,
        // but proving the grab path's OWN artifact is not written. runDir
        // already exists (seq 30's grab created it), so an empty match here
        // means the guard truly suppressed the write.
        {
            QLocalSocket sock;
            QObject::connect(&sock, &QLocalSocket::connected, &sock, [&]() {
                sock.write("{\"cmd\":\"selftest-slow\",\"seq\":40,"
                           "\"payload\":{\"grab\":{\"target\":\"window\"}}}\n");
                sock.flush();
            });
            sock.connectToServer(pipe);
            settle(60);     // server reads, dispatches, arms the 250ms timer
            sock.abort();   // walk away before the timer answers
        }
        settle(400);        // let the slow timer fire the grab hook into the void
        require(QDir(runDir).entryList({QStringLiteral("seq40-*.png")}, QDir::Files).isEmpty(),
                QStringLiteral("I1: a departed client's grab writes no orphan PNG in ") + runDir);

        // ── I2: the grab deadline fires as GRAB_TIMEOUT ──────────────────
        // An item grab is ASYNC by design: grabToImage() answers from `ready`
        // on a LATER event-loop turn, after a full scene-graph render + readback
        // + queued signal. That path measures 13-60ms in this offscreen harness
        // (min 13ms over many samples) and only grows slower under load, while
        // the per-grab timeoutMs override arms a fixed 1ms deadline the instant
        // the grab is requested. The deadline therefore wins by a >10x margin
        // that widens (never narrows) as the machine slows — so a real item
        // grab deterministically returns GRAB_TIMEOUT here. Negative control:
        // deleting attachGrab's singleShot arm lets the grab complete (~13ms+)
        // and the reply lands as `reply`, turning this red. timeoutMs is the
        // seam that makes this fast (1ms vs the 4s default) without shortening
        // the budget for the success grabs above.
        QJsonObject tg = call(pipe, {{"cmd", "get-state"}, {"seq", 41},
            {"payload", QJsonObject{{"grab", QJsonObject{
                {"target", "counterButton"}, {"timeoutMs", 1}}}}}},
            5000);
        require(tg.value("type").toString() == "error"
                    && tg.value("code").toString() == "GRAB_TIMEOUT",
                QStringLiteral("I2: an item grab's deadline fires as GRAB_TIMEOUT, got type=")
                    + tg.value("type").toString() + QStringLiteral(" code=")
                    + tg.value("code").toString() + why());

        // ── Task 3: qml-get — any named item's live properties by name ────
        QJsonObject qg = call(pipe, {{"cmd", "qml-get"}, {"seq", 7},
            {"payload", QJsonObject{{"object", "counterLabel"},
                                    {"props", QJsonArray{"text", "visible"}}}}});
        require(qg.value("props").toObject().value("text").toString()
                    == QStringLiteral("clicks: 0"),
                "qml-get reads a live property" + why());

        // ── Task 3: ui-query — geometry in SCENE units (the clipped killer) ──
        QJsonObject uq = call(pipe, {{"cmd", "ui-query"}, {"seq", 8},
            {"payload", QJsonObject{{"object", "clippedBox"}}}});
        QJsonObject r = uq.value("rect").toObject();
        require(r.value("x").toDouble() + r.value("width").toDouble() > 800.0,
                "ui-query exposes the overflow past the window edge" + why());
        require(uq.value("clippedByWindow").toBool() == true,
                "and names it: clippedByWindow=true" + why());

        // ── Task 3: ui-query reports SCENE coords, not local (nested delegate) ──
        // row0 is a delegate INSIDE longList (a Flickable at scene x=400), so its
        // LOCAL x is 0 but its SCENE x is ~400. This pins mapRectToScene as the
        // headline feature: a regression to item->x() reports 0 and this goes red
        // (clippedBox alone cannot catch it — there scene x == local x).
        QJsonObject uqRow = call(pipe, {{"cmd", "ui-query"}, {"seq", 52},
            {"payload", QJsonObject{{"object", "row0"}}}});
        const double rowX = uqRow.value("rect").toObject().value("x").toDouble();
        require(rowX > 399.0 && rowX < 401.0,
                QStringLiteral("ui-query reports row0's SCENE x (~400), not local 0, got ")
                    + QString::number(rowX) + why());

        // ── Task 3: dump-ui — the named-object tree, checked STRUCTURALLY ─
        // Not a substring smoke check: assert a non-empty tree, then locate the
        // counterButton ENTRY in items[] and assert its fields, so a stray
        // "counterButton" anywhere in the JSON cannot fake a pass.
        QJsonObject du = call(pipe, {{"cmd", "dump-ui"}, {"seq", 9}});
        require(du.value("count").toInt() > 0, "dump-ui reports a non-empty tree" + why());
        const QJsonArray dumpItems = du.value("items").toArray();
        require(dumpItems.size() == du.value("count").toInt(),
                "dump-ui count matches items[] length");
        QJsonObject btnEntry;
        for (const QJsonValue& v : dumpItems) {
            const QJsonObject o = v.toObject();
            if (o.value("objectName").toString() == QStringLiteral("counterButton")) {
                btnEntry = o;
                break;
            }
        }
        require(!btnEntry.isEmpty(),
                "dump-ui has a counterButton entry in items[]" + why());
        require(!btnEntry.value("class").toString().isEmpty(),
                "dump-ui counterButton entry carries a non-empty class");
        require(btnEntry.contains("depth"),
                "dump-ui counterButton entry carries a depth");

        // ── Task 3: negative path — a missing object is NO_SUCH_ITEM ──────
        QJsonObject qgN = call(pipe, {{"cmd", "qml-get"}, {"seq", 50},
            {"payload", QJsonObject{{"object", "no-such-item"},
                                    {"props", QJsonArray{"text"}}}}});
        require(qgN.value("type").toString() == "error"
                    && qgN.value("code").toString() == "NO_SUCH_ITEM",
                "qml-get on a missing object is NO_SUCH_ITEM" + why());
        QJsonObject uqN = call(pipe, {{"cmd", "ui-query"}, {"seq", 51},
            {"payload", QJsonObject{{"object", "no-such-item"}}}});
        require(uqN.value("type").toString() == "error"
                    && uqN.value("code").toString() == "NO_SUCH_ITEM",
                "ui-query on a missing object is NO_SUCH_ITEM" + why());

        // ── L1-Bridge: dump-ui/ui-query all-item structural dump (2026-08-13) ──
        // Eight named cases, each proving one piece of the L1-Discovery
        // contract (docs/visibility/lanista-structural-gap.md). Every call
        // below is scoped by `root` to a dedicated fixture subtree
        // (tests/lanista_harness_scene.qml's "L1-Bridge structural fixtures"
        // block) so a case's own assertions never depend on what any OTHER
        // fixture or case left behind.

        // structural_fields_are_versioned: the new field vocabulary is present
        // on every row with the right shape/values, and `generation` is a real
        // monotonic counter — not a decorative constant.
        {
            QJsonObject du1 = call(pipe, {{"cmd", "dump-ui"}, {"seq", 200},
                {"payload", QJsonObject{{"root", "structuralFixtures"}}}});
            require(du1.value("type").toString() == "reply",
                    "structural_fields_are_versioned: dump-ui replies" + why());
            const int gen1 = du1.value("generation").toInt(-1);
            require(gen1 > 0,
                    "structural_fields_are_versioned: generation is a real counter" + why());

            QJsonObject transparentRow, disabledRow, zeroSizeRow;
            for (const QJsonValue& v : du1.value("items").toArray()) {
                const QJsonObject o = v.toObject();
                const QString name = o.value("objectName").toString();
                if (name == QStringLiteral("transparentItem")) transparentRow = o;
                if (name == QStringLiteral("disabledItem")) disabledRow = o;
                if (name == QStringLiteral("zeroSizeItem")) zeroSizeRow = o;
            }
            require(!transparentRow.isEmpty(),
                    "structural_fields_are_versioned: transparentItem row found" + why());
            require(!disabledRow.isEmpty(),
                    "structural_fields_are_versioned: disabledItem row found" + why());
            require(!zeroSizeRow.isEmpty(),
                    "structural_fields_are_versioned: zeroSizeItem row found" + why());

            // The full new-field vocabulary, checked STRUCTURALLY (key presence
            // + type), not a substring smoke check.
            require(transparentRow.contains("handle")
                        && !transparentRow.value("handle").toString().isEmpty(),
                    "structural_fields_are_versioned: row carries a non-empty handle");
            require(transparentRow.contains("parentHandle"),
                    "structural_fields_are_versioned: row carries parentHandle (key present)");
            require(transparentRow.contains("parentName"),
                    "structural_fields_are_versioned: row carries parentName (key present)");
            require(transparentRow.contains("childCount"),
                    "structural_fields_are_versioned: row carries childCount");
            require(transparentRow.contains("z"),
                    "structural_fields_are_versioned: row carries z");
            require(transparentRow.value("localRect").toObject().contains("width"),
                    "structural_fields_are_versioned: row carries localRect{x,y,width,height}");
            require(transparentRow.value("sceneRect").toObject().contains("width"),
                    "structural_fields_are_versioned: row carries sceneRect{x,y,width,height}");
            require(transparentRow.value("clipChain").isArray(),
                    "structural_fields_are_versioned: row carries a clipChain array");

            // dump-ui's own "no visibility filter" law, now proven on THESE
            // three deliberately non-default fixtures, not just named ones.
            require(transparentRow.value("opacity").toDouble() == 0.0,
                    "structural_fields_are_versioned: transparentItem opacity==0 survives the dump"
                        + why());
            require(disabledRow.value("enabled").toBool() == false,
                    "structural_fields_are_versioned: disabledItem enabled==false survives the dump"
                        + why());
            require(zeroSizeRow.value("width").toDouble() == 0.0
                        && zeroSizeRow.value("height").toDouble() == 0.0,
                    "structural_fields_are_versioned: zeroSizeItem 0x0 survives the dump" + why());

            // A SECOND fresh call must open a NEW generation — the versioning
            // half of the contract, not just the shape half.
            QJsonObject du2 = call(pipe, {{"cmd", "dump-ui"}, {"seq", 201},
                {"payload", QJsonObject{{"root", "structuralFixtures"}}}});
            const int gen2 = du2.value("generation").toInt(-1);
            require(gen2 > gen1,
                    QStringLiteral("structural_fields_are_versioned: generation is monotonic (%1 -> %2)")
                        .arg(gen1).arg(gen2) + why());
            std::cout << "CASE_OK: structural_fields_are_versioned\n";
        }

        // structural_dump_includes_unnamed_items: THE L1-Discovery gap, closed.
        // unnamedItemHost has exactly one child, deliberately unnamed — the
        // negative control removes that one Rectangle from the QML, and this
        // is the ONE case that must go red.
        {
            QJsonObject du = call(pipe, {{"cmd", "dump-ui"}, {"seq", 202},
                {"payload", QJsonObject{{"root", "unnamedItemHost"}}}});
            require(du.value("type").toString() == "reply",
                    "structural_dump_includes_unnamed_items: dump-ui replies" + why());
            const QJsonArray items = du.value("items").toArray();
            int unnamedCount = 0;
            for (const QJsonValue& v : items)
                if (v.toObject().value("objectName").toString().isEmpty())
                    ++unnamedCount;
            require(unnamedCount == 1,
                    QStringLiteral("structural_dump_includes_unnamed_items: exactly one unnamed "
                                   "row under unnamedItemHost, got %1").arg(unnamedCount) + why());
            require(items.size() == 2,
                    QStringLiteral("structural_dump_includes_unnamed_items: host + unnamed child, "
                                   "got %1 rows").arg(items.size()) + why());
            std::cout << "CASE_OK: structural_dump_includes_unnamed_items\n";
        }

        // parent_chain_is_exact: a NAMED leaf under an UNNAMED middle item under
        // a NAMED host — the parent-handle chain must resolve exactly, hop by
        // hop, even across the unnamed middle link.
        {
            QJsonObject du = call(pipe, {{"cmd", "dump-ui"}, {"seq", 203},
                {"payload", QJsonObject{{"root", "parentChainHost"}}}});
            require(du.value("type").toString() == "reply",
                    "parent_chain_is_exact: dump-ui replies" + why());
            const QJsonArray items = du.value("items").toArray();
            require(items.size() == 3,
                    QStringLiteral("parent_chain_is_exact: host + unnamed middle + leaf, got %1")
                        .arg(items.size()) + why());

            QJsonObject hostRow, middleRow, leafRow;
            for (const QJsonValue& v : items) {
                const QJsonObject o = v.toObject();
                const QString name = o.value("objectName").toString();
                if (name == QStringLiteral("parentChainHost")) hostRow = o;
                else if (name == QStringLiteral("parentChainLeaf")) leafRow = o;
                else if (name.isEmpty()) middleRow = o;
            }
            require(!hostRow.isEmpty() && !middleRow.isEmpty() && !leafRow.isEmpty(),
                    "parent_chain_is_exact: all three rows found" + why());

            require(leafRow.value("parentHandle").toString() == middleRow.value("handle").toString(),
                    "parent_chain_is_exact: leaf's parentHandle is the UNNAMED middle row's own handle"
                        + why());
            require(middleRow.value("parentHandle").toString() == hostRow.value("handle").toString(),
                    "parent_chain_is_exact: the unnamed middle row's parentHandle is the host's handle"
                        + why());
            require(hostRow.value("childCount").toInt() == 1,
                    "parent_chain_is_exact: host childCount==1" + why());
            require(middleRow.value("childCount").toInt() == 1,
                    "parent_chain_is_exact: unnamed middle childCount==1" + why());
            require(leafRow.value("childCount").toInt() == 0,
                    "parent_chain_is_exact: leaf childCount==0" + why());

            // The host's OWN parent lies OUTSIDE this walk (structuralFixtures)
            // — parentHandle must still resolve to the TRUE parent, not go null
            // just because that parent fell outside the requested root.
            const QString hostParentHandle = hostRow.value("parentHandle").toString();
            require(!hostParentHandle.isEmpty(),
                    "parent_chain_is_exact: the walk ROOT still carries its TRUE parent handle"
                        + why());
            QJsonObject outside = call(pipe, {{"cmd", "ui-query"}, {"seq", 204},
                {"payload", QJsonObject{{"object", hostParentHandle}}}});
            require(outside.value("objectName").toString() == QStringLiteral("structuralFixtures"),
                    "parent_chain_is_exact: that handle resolves to the REAL out-of-walk parent"
                        + why());
            std::cout << "CASE_OK: parent_chain_is_exact\n";
        }

        // clipping_chain_is_exact: the direct fix for L1-Discovery row 4 —
        // clippedByWindow says false (both items sit fully inside the window)
        // while the clip chain names the REAL clip:true ancestor(s) that cut
        // them off, in nearest-first order.
        {
            QJsonObject single = call(pipe, {{"cmd", "ui-query"}, {"seq", 205},
                {"payload", QJsonObject{{"object", "clipHostChild"}}}});
            require(single.value("type").toString() == "reply",
                    "clipping_chain_is_exact: ui-query(clipHostChild) replies" + why());
            require(single.value("clippedByWindow").toBool() == false,
                    "clipping_chain_is_exact: clipHostChild is fully inside the window" + why());
            const QJsonArray chain1 = single.value("clipChain").toArray();
            require(chain1.size() == 1,
                    QStringLiteral("clipping_chain_is_exact: exactly one clip ancestor, got %1")
                        .arg(chain1.size()) + why());
            const QJsonObject anc1 = chain1.at(0).toObject();
            require(anc1.value("objectName").toString() == QStringLiteral("clipHost"),
                    "clipping_chain_is_exact: the ancestor is clipHost" + why());
            const QJsonObject anc1Rect = anc1.value("sceneRect").toObject();
            require(anc1Rect.value("x").toDouble() == 500.0
                        && anc1Rect.value("y").toDouble() == 400.0
                        && anc1Rect.value("width").toDouble() == 60.0
                        && anc1Rect.value("height").toDouble() == 40.0,
                    "clipping_chain_is_exact: clipHost's own scene rect is exact" + why());
            // The item's own rect falls ENTIRELY outside that ancestor's rect —
            // the real reason it is not on screen, despite clippedByWindow==false.
            const QJsonObject itemRect = single.value("rect").toObject();
            require(itemRect.value("x").toDouble()
                        >= anc1Rect.value("x").toDouble() + anc1Rect.value("width").toDouble(),
                    "clipping_chain_is_exact: clipHostChild's rect is fully outside clipHost's rect"
                        + why());

            QJsonObject nested = call(pipe, {{"cmd", "ui-query"}, {"seq", 206},
                {"payload", QJsonObject{{"object", "doubleClippedChild"}}}});
            require(nested.value("clippedByWindow").toBool() == false,
                    "clipping_chain_is_exact: doubleClippedChild is fully inside the window" + why());
            const QJsonArray chain2 = nested.value("clipChain").toArray();
            require(chain2.size() == 2,
                    QStringLiteral("clipping_chain_is_exact: TWO nested clip ancestors, got %1")
                        .arg(chain2.size()) + why());
            require(chain2.at(0).toObject().value("objectName").toString() == QStringLiteral("clipInner"),
                    "clipping_chain_is_exact: nearest ancestor first (clipInner)" + why());
            require(chain2.at(1).toObject().value("objectName").toString() == QStringLiteral("clipOuter"),
                    "clipping_chain_is_exact: then the outer ancestor (clipOuter)" + why());

            // dump-ui reports the SAME chain for the SAME item — one vocabulary,
            // not two different shapes depending which command is asked.
            QJsonObject viaDump = call(pipe, {{"cmd", "dump-ui"}, {"seq", 207},
                {"payload", QJsonObject{{"root", "clipOuter"}}}});
            QJsonObject dumpLeaf;
            for (const QJsonValue& v : viaDump.value("items").toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value("objectName").toString() == QStringLiteral("doubleClippedChild"))
                    dumpLeaf = o;
            }
            require(!dumpLeaf.isEmpty(),
                    "clipping_chain_is_exact: dump-ui also reaches doubleClippedChild" + why());
            const QJsonArray dumpChain = dumpLeaf.value("clipChain").toArray();
            require(dumpChain.size() == 2
                        && dumpChain.at(0).toObject().value("objectName").toString()
                               == QStringLiteral("clipInner")
                        && dumpChain.at(1).toObject().value("objectName").toString()
                               == QStringLiteral("clipOuter"),
                    "clipping_chain_is_exact: dump-ui's clipChain agrees with ui-query's" + why());
            std::cout << "CASE_OK: clipping_chain_is_exact\n";
        }

        // stale_structural_handle_is_rejected: a handle minted by dump-ui obeys
        // the SAME epoch law ui-snapshot's handles already do — a later
        // snapshot (from ANY command) invalidates it to a clean NO_SUCH_ITEM,
        // never a silent wrong hit.
        {
            QJsonObject du = call(pipe, {{"cmd", "dump-ui"}, {"seq", 208},
                {"payload", QJsonObject{{"root", "parentChainHost"}}}});
            QString leafHandle;
            for (const QJsonValue& v : du.value("items").toArray()) {
                const QJsonObject o = v.toObject();
                if (o.value("objectName").toString() == QStringLiteral("parentChainLeaf"))
                    leafHandle = o.value("handle").toString();
            }
            require(!leafHandle.isEmpty(),
                    "stale_structural_handle_is_rejected: minted a real handle first" + why());

            call(pipe, {{"cmd", "ui-snapshot"}, {"seq", 209}});   // bumps the epoch

            QJsonObject stale = call(pipe, {{"cmd", "ui-query"}, {"seq", 210},
                {"payload", QJsonObject{{"object", leafHandle}}}});
            require(stale.value("type").toString() == "error"
                        && stale.value("code").toString() == "NO_SUCH_ITEM",
                    "stale_structural_handle_is_rejected: a dump-ui handle from a superseded "
                    "generation misses cleanly (NO_SUCH_ITEM)" + why());
            std::cout << "CASE_OK: stale_structural_handle_is_rejected\n";
        }

        // requested_bounds_are_clamped: an absurd client request is silently
        // reduced to the server's OWN ceiling, never honored and never
        // refused; a SANE small request is genuinely enforced, not merely
        // reported.
        {
            QJsonObject absurd = call(pipe, {{"cmd", "dump-ui"}, {"seq", 211},
                {"payload", QJsonObject{{"root", "overBudgetContainer"},
                                        {"maxDepth", 999999}, {"maxItems", 999999999}}}});
            require(absurd.value("type").toString() == "reply",
                    "requested_bounds_are_clamped: dump-ui replies" + why());
            require(absurd.value("maxDepthUsed").toInt() == 64,
                    QStringLiteral("requested_bounds_are_clamped: maxDepth clamped to the server "
                                   "ceiling (64), got %1").arg(absurd.value("maxDepthUsed").toInt())
                        + why());
            require(absurd.value("maxItemsUsed").toInt() == 5000,
                    QStringLiteral("requested_bounds_are_clamped: maxItems clamped to the server "
                                   "ceiling (5000), got %1").arg(absurd.value("maxItemsUsed").toInt())
                        + why());

            QJsonObject small = call(pipe, {{"cmd", "dump-ui"}, {"seq", 212},
                {"payload", QJsonObject{{"root", "overBudgetContainer"}, {"maxItems", 5}}}});
            require(small.value("items").toArray().size() == 5,
                    QStringLiteral("requested_bounds_are_clamped: a sane maxItems=5 is genuinely "
                                   "enforced, got %1 rows")
                        .arg(small.value("items").toArray().size()) + why());
            require(small.value("truncated").toBool() == true,
                    "requested_bounds_are_clamped: truncated==true once maxItems is hit" + why());
            std::cout << "CASE_OK: requested_bounds_are_clamped\n";
        }

        // reply_budget_sets_truncated: the BYTE ceiling alone — independent of
        // any client-requested maxItems — stops an oversized reply before it
        // ever nears the wire's 1 MiB line ceiling.
        {
            QJsonObject over = call(pipe, {{"cmd", "dump-ui"}, {"seq", 213},
                {"payload", QJsonObject{{"root", "overBudgetContainer"}}}});
            require(over.value("type").toString() == "reply",
                    "reply_budget_sets_truncated: dump-ui replies" + why());
            require(over.value("truncated").toBool() == true,
                    "reply_budget_sets_truncated: the byte budget alone truncates this reply"
                        + why());
            const int gotItems = over.value("items").toArray().size();
            require(gotItems > 0 && gotItems < 601,
                    QStringLiteral("reply_budget_sets_truncated: a partial page, got %1 rows")
                        .arg(gotItems) + why());
            require(!over.value("continuation").isNull(),
                    "reply_budget_sets_truncated: a truncated reply carries continuation metadata"
                        + why());
            require(over.value("continuation").toObject().value("cursor").toInt(-1) >= 0,
                    "reply_budget_sets_truncated: continuation carries a real cursor" + why());
            std::cout << "CASE_OK: reply_budget_sets_truncated\n";
        }

        // continuation_resumes_without_duplicates: paging through a truncated
        // dump-ui reply visits every item EXACTLY once — no repeats, no gaps —
        // as long as the caller echoes back the generation its cursor was
        // minted against.
        {
            QSet<QString> seenHandles;
            int totalSeen = 0;
            int expectedTotal = -1;
            int cursor = 0, generation = -1;
            bool truncated = true;
            int pages = 0;
            int seq = 220;
            while (truncated && pages < 10) {
                QJsonObject payload{{"root", "overBudgetContainer"}};
                if (pages > 0) {
                    payload.insert("cursor", cursor);
                    payload.insert("generation", generation);
                }
                QJsonObject page = call(pipe, {{"cmd", "dump-ui"}, {"seq", seq++},
                    {"payload", payload}});
                require(page.value("type").toString() == "reply",
                        QStringLiteral("continuation_resumes_without_duplicates: page %1 replies")
                            .arg(pages) + why());
                const QJsonArray items = page.value("items").toArray();
                require(!items.isEmpty(),
                        QStringLiteral("continuation_resumes_without_duplicates: page %1 is non-empty")
                            .arg(pages) + why());
                for (const QJsonValue& v : items) {
                    const QString h = v.toObject().value("handle").toString();
                    require(!seenHandles.contains(h),
                            QStringLiteral("continuation_resumes_without_duplicates: handle %1 "
                                           "repeated across pages").arg(h) + why());
                    seenHandles.insert(h);
                }
                if (pages == 0) {
                    // overBudgetContainer is item index 0 of its own walk — its
                    // OWN reported childCount is the ground truth for how many
                    // items this fixture really contains, independent of any
                    // assumption about where exactly Repeater sits in the tree.
                    const QJsonObject containerRow = items.first().toObject();
                    require(containerRow.value("objectName").toString()
                                == QStringLiteral("overBudgetContainer"),
                            "continuation_resumes_without_duplicates: page 0's first row is the "
                            "walk root itself" + why());
                    expectedTotal = 1 + containerRow.value("childCount").toInt();
                }
                totalSeen += items.size();
                truncated = page.value("truncated").toBool();
                if (truncated) {
                    cursor = page.value("continuation").toObject().value("cursor").toInt();
                    generation = page.value("generation").toInt();
                }
                ++pages;
            }
            require(!truncated,
                    "continuation_resumes_without_duplicates: draining terminates (truncated==false)"
                        + why());
            require(pages > 1,
                    QStringLiteral("continuation_resumes_without_duplicates: more than one page was "
                                   "actually needed (%1)").arg(pages) + why());
            require(expectedTotal > 0 && totalSeen == expectedTotal,
                    QStringLiteral("continuation_resumes_without_duplicates: every item visited "
                                   "exactly once (%1 seen, %2 expected)")
                        .arg(totalSeen).arg(expectedTotal) + why());
            std::cout << "CASE_OK: continuation_resumes_without_duplicates ("
                      << pages << " pages, " << totalSeen << " items)\n";
        }

        // ── Task 4: ui-snapshot — every actionable element, each with a handle ─
        // Playwright's model, QML-native: ONE call returns everything an agent
        // could act on, each carrying an OPAQUE session handle valid only within
        // the snapshot that minted it. centerX/centerY are SCENE/LOGICAL units
        // (documented in cmdUiSnapshot), the same space get-state/ui-query speak.
        QJsonObject sn = call(pipe, {{"cmd", "ui-snapshot"}, {"seq", 60}});
        QJsonArray els = sn.value("elements").toArray();
        require(!els.isEmpty(), "snapshot lists elements" + why());
        bool sawMouse = false, sawList = false;
        QString mouseHandle, buttonHandle;
        for (const QJsonValue& v : els) {
            const QJsonObject el = v.toObject();
            const QString name = el.value("objectName").toString();
            if (name == QStringLiteral("counterMouse")) {
                sawMouse = true; mouseHandle = el.value("handle").toString();
                require(el.value("interactive").toBool(), "MouseArea marked interactive");
            }
            if (name == QStringLiteral("counterButton"))
                buttonHandle = el.value("handle").toString();
            if (name == QStringLiteral("mainList")) {
                sawList = true;
                // The chain-walk pin: ListView's LEAF class "QQuickListView" carries
                // no "Flickable" token; only its QQuickFlickable BASE does. A
                // leaf-only substring check reports interactive:false here.
                require(el.value("interactive").toBool(),
                        "ListView marked interactive via superclass chain (QQuickFlickable base)"
                            + why());
            }
        }
        // Handles are OPAQUE: require non-empty and prove resolvable below; never
        // assume an internal format.
        require(sawMouse && !mouseHandle.isEmpty(),
                "the counter's MouseArea carries a handle" + why());
        require(sawList, "ui-snapshot includes the ListView fixture" + why());

        // count == elements.length, and every handle is non-empty AND unique.
        require(sn.value("count").toInt() == els.size(),
                "ui-snapshot count matches elements[] length" + why());
        {
            QStringList handles;
            for (const QJsonValue& v : els) {
                const QString h = v.toObject().value("handle").toString();
                require(!h.isEmpty(), "every element carries a non-empty handle" + why());
                handles << h;
            }
            require(QSet<QString>(handles.begin(), handles.end()).size() == handles.size(),
                    "ui-snapshot handles are unique" + why());
        }

        // ── Task 4: resolveTarget round-trip — a handle resolves to the SAME item ─
        // Identity-exact: read the objectName back THROUGH the handle (a rect
        // compare is weak — counterMouse and its parent share geometry). Proves the
        // handle resolves in a read AND that there is ONE resolver, not two.
        QJsonObject qgByHandle = call(pipe, {{"cmd", "qml-get"}, {"seq", 61},
            {"payload", QJsonObject{{"object", mouseHandle},
                                    {"props", QJsonArray{"objectName"}}}}});
        require(qgByHandle.value("type").toString() == "reply",
                "qml-get resolves a snapshot handle" + why());
        require(qgByHandle.value("props").toObject().value("objectName").toString()
                    == QStringLiteral("counterMouse"),
                "resolveTarget: the handle resolves to the SAME item (objectName round-trips)"
                    + why());

        // ── Task 4: grab by handle — attachGrab routes through resolveTarget ──
        // A snapshot handle is a grab target too. counterButton grabs cleanly in
        // both configs (offscreen software SG; RHI with QSG_NO_VSYNC=1), so its
        // handle must land a PNG on disk. Negative control: revert attachGrab to
        // findItem() and the handle is not a name -> GRAB_TARGET_NOT_FOUND.
        require(!buttonHandle.isEmpty(), "snapshot carries counterButton's handle" + why());
        QJsonObject hg = call(pipe, {{"cmd", "get-state"}, {"seq", 63},
            {"payload", QJsonObject{{"grab", QJsonObject{{"target", buttonHandle}}}}}},
            8000);
        require(hg.value("type").toString() == "reply",
                "grab by a snapshot handle replies" + why());
        require(QFileInfo::exists(hg.value("grabPath").toString()),
                "grab by a snapshot handle lands a PNG on disk: "
                    + hg.value("grabPath").toString() + why());

        // ── Task 4: snapshot-scoped handles — a superseded handle misses cleanly ─
        // A handle is single-snapshot-scoped: taking a NEW snapshot (from ANY
        // client) bumps the epoch and clears m_handles, so a handle from the prior
        // snapshot must now MISS deterministically — never silently resolve to the
        // new snapshot's Nth item. Negative control: drop resolveTarget's epoch
        // gate and the stale handle resolves to snapshot B's same-index item.
        const QString staleHandle = mouseHandle;             // minted by snapshot A (seq 60)
        call(pipe, {{"cmd", "ui-snapshot"}, {"seq", 64}});   // snapshot B: new epoch, m_handles rebuilt
        QJsonObject stale = call(pipe, {{"cmd", "qml-get"}, {"seq", 65},
            {"payload", QJsonObject{{"object", staleHandle},
                                    {"props", QJsonArray{"objectName"}}}}});
        require(stale.value("type").toString() == "error"
                    && stale.value("code").toString() == "NO_SUCH_ITEM",
                "a handle from a superseded snapshot misses cleanly (NO_SUCH_ITEM)" + why());

        // ── Task 5: the DRIVE gate — synthetic driving is refused until opened ──
        // The gate is enforced centrally in dispatch(); this proves ui-click sits
        // behind it. DRIVE is unset here (the gate tests above left it closed).
        QJsonObject clickDenied = call(pipe, {{"cmd", "ui-click"}, {"seq", 70},
            {"payload", QJsonObject{{"target", "counterMouse"}}}});
        require(clickDenied.value("type").toString() == "error"
                    && clickDenied.value("code").toString() == "DRIVE_DISABLED",
                "ui-click is refused with the DRIVE gate closed" + why());

        // Open the DRIVE gate for the rest of the hands tests. driveOpen() reads
        // the env live per call, so this takes effect immediately.
        qputenv("COLOSSEUM_LANISTA_DRIVE", "1");

        // ── Task 5: ui-click genuinely lands — the counter really increments ──
        // A reply is not enough: the press+release must reach counterMouse and
        // fire onClicked. counterLabel binds to win.clickCount, so its text is the
        // ground truth that a real click happened (drop the release -> stays 0).
        QJsonObject clicked = call(pipe, {{"cmd", "ui-click"}, {"seq", 71},
            {"payload", QJsonObject{{"target", "counterMouse"}}}});
        require(clicked.value("type").toString() == "reply", "ui-click replies" + why());
        require(clicked.value("clicked").toString() == QStringLiteral("counterMouse"),
                "ui-click names the item it drove" + why());
        QJsonObject afterClick = call(pipe, {{"cmd", "qml-get"}, {"seq", 72},
            {"payload", QJsonObject{{"object", "counterLabel"},
                                    {"props", QJsonArray{"text"}}}}});
        require(afterClick.value("props").toObject().value("text").toString()
                    == QStringLiteral("clicks: 1"),
                "ui-click REALLY incremented the counter (press+release delivered)" + why());

        // ── Task 5: ui-text-input types real characters into a TextInput ──
        // forceActiveFocus() + a KeyPress per char. The ui-click first mirrors a
        // human tabbing in; the field's own text is the proof each char landed.
        call(pipe, {{"cmd", "ui-click"}, {"seq", 73},
            {"payload", QJsonObject{{"target", "nameField"}}}});
        QJsonObject typed = call(pipe, {{"cmd", "ui-text-input"}, {"seq", 74},
            {"payload", QJsonObject{{"target", "nameField"}, {"text", "luffy"}}}});
        require(typed.value("typed").toString() == QStringLiteral("luffy"),
                "ui-text-input echoes the text it typed" + why());
        QJsonObject fieldText = call(pipe, {{"cmd", "qml-get"}, {"seq", 75},
            {"payload", QJsonObject{{"object", "nameField"},
                                    {"props", QJsonArray{"text"}}}}});
        require(fieldText.value("props").toObject().value("text").toString()
                    == QStringLiteral("luffy"),
                "ui-text-input REALLY landed each character in the field" + why());

        // ── Task 5: ui-keypress lands a real key on the focused item ──
        // The scene has no global key handler, so we focus keySink first (its
        // MouseArea forceActiveFocus()es it on click), then a KeyPress must reach
        // Keys.onPressed and record e.text. Proves the key genuinely lands.
        call(pipe, {{"cmd", "ui-click"}, {"seq", 76},
            {"payload", QJsonObject{{"target", "keySinkMouse"}}}});   // focus the sink
        QJsonObject pressed = call(pipe, {{"cmd", "ui-keypress"}, {"seq", 77},
            {"payload", QJsonObject{{"key", "A"}}}});
        require(pressed.value("pressed").toString() == QStringLiteral("A"),
                "ui-keypress echoes the key it pressed" + why());
        QJsonObject sinkKey = call(pipe, {{"cmd", "qml-get"}, {"seq", 78},
            {"payload", QJsonObject{{"object", "keySink"},
                                    {"props", QJsonArray{"lastKey"}}}}});
        require(sinkKey.value("props").toObject().value("lastKey").toString()
                    == QStringLiteral("A"),
                "ui-keypress REALLY reached the focused item (Keys.onPressed saw 'A')" + why());

        // ── Task 5: an unparseable (empty) key is BAD_KEY ──
        QJsonObject badKey = call(pipe, {{"cmd", "ui-keypress"}, {"seq", 79},
            {"payload", QJsonObject{{"key", ""}}}});
        require(badKey.value("type").toString() == "error"
                    && badKey.value("code").toString() == "BAD_KEY",
                "ui-keypress with an empty key is BAD_KEY" + why());

        // ── Task 5: ui-scroll moves a real ListView's contentY ──
        // mainList overflows its viewport (contentHeight 480 > height 120), so a
        // wheel event has room to move it. Genuine effect, not just a reply: drop
        // the wheel sendEvent and contentY never budges.
        QJsonObject beforeScroll = call(pipe, {{"cmd", "qml-get"}, {"seq", 80},
            {"payload", QJsonObject{{"object", "mainList"},
                                    {"props", QJsonArray{"contentY"}}}}});
        const double cyBefore =
            beforeScroll.value("props").toObject().value("contentY").toDouble();
        QJsonObject scrolled = call(pipe, {{"cmd", "ui-scroll"}, {"seq", 81},
            {"payload", QJsonObject{{"target", "mainList"}, {"dy", -240}}}});
        require(scrolled.value("scrolled").toString() == QStringLiteral("mainList"),
                "ui-scroll names the item it scrolled" + why());
        // The wheel drives a FLICK that advances the content over later frames.
        // A fixed sleep here would be a wall-clock wait on an async effect — the
        // exact flake lanista exists to kill, so it has no place in lanista's own
        // suite. Poll contentY (bounded, ~2s cap) and break the instant it moves.
        double cyAfter = cyBefore;
        for (int i = 0; i < 40 && cyAfter <= cyBefore; ++i) {
            settle(50);
            cyAfter = call(pipe, {{"cmd", "qml-get"}, {"seq", 82},
                {"payload", QJsonObject{{"object", "mainList"},
                                        {"props", QJsonArray{"contentY"}}}}})
                          .value("props").toObject().value("contentY").toDouble();
        }
        require(cyAfter > cyBefore,
                QStringLiteral("ui-scroll REALLY moved contentY (%1 -> %2)")
                    .arg(cyBefore).arg(cyAfter) + why());

        // ── Task 5: ui-wait-for matches a property value (async poll) ──
        // Drive the counter once more (clicks: 2), then wait for exactly that.
        call(pipe, {{"cmd", "ui-click"}, {"seq", 83},
            {"payload", QJsonObject{{"target", "counterMouse"}}}});
        QJsonObject waited = call(pipe, {{"cmd", "ui-wait-for"}, {"seq", 84},
            {"payload", QJsonObject{{"object", "counterLabel"}, {"prop", "text"},
                                    {"value", "clicks: 2"}, {"timeout_ms", 2000}}}});
        require(waited.value("type").toString() == "reply"
                    && waited.value("matched").toBool(),
                "ui-wait-for matches the property value" + why());

        // ── Task 5: ui-wait-for hits its deadline as WAIT_TIMEOUT ──
        // A value that never comes true must terminate on the timeout_ms deadline.
        // Disable the deadline branch and this call times out empty -> reds.
        QJsonObject waitTimeout = call(pipe, {{"cmd", "ui-wait-for"}, {"seq", 85},
            {"payload", QJsonObject{{"object", "counterLabel"}, {"prop", "text"},
                                    {"value", "clicks: 99999"}, {"timeout_ms", 200}}}});
        require(waitTimeout.value("type").toString() == "error"
                    && waitTimeout.value("code").toString() == "WAIT_TIMEOUT",
                "ui-wait-for that never matches fails as WAIT_TIMEOUT (deadline fires)" + why());

        // ── Task 9: invoke-read — REFUSAL PATHS ONLY ──────────────────────────
        // invoke-read is a curated, allowlisted, read-only bridge into a QML
        // organ's Q_INVOKABLE reads. The real-invoke HAPPY PATH is deliberately
        // deferred to Task 12 (against the real app): this harness scene declares
        // NO context-property organs, so there is nothing real to invoke here —
        // only the two refusals are provable, and they are the whole safety model.
        //
        // invoke-read: a NAMED error for a missing organ
        QJsonObject ir = call(pipe, {{"cmd","invoke-read"},{"seq",90},
            {"payload", QJsonObject{{"object","TankobanVolumes"},{"method","volumesForSeries"},
                                    {"args", QJsonArray{"nope"}}}}});
        require(ir.value("type").toString()=="error" && ir.value("code").toString()=="CMD_FAILED"
                && ir.value("message").toString().contains("context property"),
                "invoke-read names a missing organ" + why());
        // a method OFF the allowlist is refused BEFORE the organ lookup
        QJsonObject na = call(pipe, {{"cmd","invoke-read"},{"seq",91},
            {"payload", QJsonObject{{"object","TankobanVolumes"},{"method","remove"},
                                    {"args", QJsonArray{"v1"}}}}});
        require(na.value("type").toString()=="error" && na.value("code").toString()=="CMD_FAILED"
                && na.value("message").toString().contains("allowlist"),
                "a method off the allowlist is refused before the organ lookup" + why());

        // ── Task 10: log-mark round-trips through events-tail ─────────────────
        // log-mark stamps a correlation label into events.jsonl (a DEV annotation,
        // NOT app state — hence a READ, always on); events-tail reads the tail back.
        // The label written here must be readable through the reader, proving the
        // append truly hit the file and the tail truly read it (negative control:
        // skip the write in LanistaEventLog::append and this round-trip reds).
        QJsonObject marked = call(pipe, {{"cmd", "log-mark"}, {"seq", 100},
            {"payload", QJsonObject{{"label", "harness-mark-1"}}}});
        require(marked.value("type").toString() == "reply"
                    && marked.value("marked").toString() == QStringLiteral("harness-mark-1"),
                "log-mark echoes the label it stamped" + why());
        QJsonObject ev = call(pipe, {{"cmd", "events-tail"}, {"seq", 101},
            {"payload", QJsonObject{{"limit", 5}}}});
        require(ev.value("type").toString() == "reply", "events-tail replies" + why());
        require(QJsonDocument(ev).toJson().contains("harness-mark-1"),
                "the mark is readable back through events-tail" + why());

        // ── F1-Bridge: vault-forensics ─────────────────────────────────────────
        // One Read-gated call onto F1-Core (VaultForensics) through the REAL fixture
        // built above. Both gates are still closed here (nothing since Task 5 opened
        // them again) — vault_forensics_is_read_gated rides that fact rather than
        // re-toggling anything.
        {
            QJsonObject summary = call(pipe, {{"cmd", "vault-forensics"}, {"seq", 110},
                {"payload", QJsonObject{{"scope", "summary"}, {"limit", 10}}}});
            require(summary.value("type").toString() == "reply",
                    "vault_forensics_is_read_gated: answers with both DRIVE and WRITE closed"
                        + why());
            require(summary.value("schema").toString()
                        == QStringLiteral("colosseum.vault.forensics.v1"),
                    "vault_forensics_is_read_gated: schema is colosseum.vault.forensics.v1");
            std::cout << "CASE_OK: vault_forensics_is_read_gated\n";

            // vault_forensics_passes_response_unchanged: the bridge's reply, minus the
            // wire envelope's own "type"/"seq" (added by every command alike, not
            // specific to this one), must equal calling VaultForensics::query() directly
            // field-for-field — proving cmdVaultForensics() truly hands the map through
            // unchanged rather than re-shaping or dropping anything.
            const QVariantMap direct = vaultForensics->query(
                QVariantMap{{QStringLiteral("scope"), QStringLiteral("summary")},
                            {QStringLiteral("limit"), 10}});
            QJsonObject bridgeBody = summary;
            bridgeBody.remove(QStringLiteral("type"));
            bridgeBody.remove(QStringLiteral("seq"));
            const QJsonObject directBody = QJsonObject::fromVariantMap(direct);
            require(bridgeBody == directBody,
                    "vault_forensics_passes_response_unchanged: bridge reply == "
                    "VaultForensics::query() verbatim (envelope aside)" + why());
            std::cout << "CASE_OK: vault_forensics_passes_response_unchanged\n";

            // vault_forensics_rejects_bad_scope: an unknown scope is F1-Core's OWN
            // diagnostic (its errors[] array) — the bridge never duplicates F1-Core's
            // scope validation, so the WIRE reply stays type:"reply", never type:"error".
            QJsonObject bad = call(pipe, {{"cmd", "vault-forensics"}, {"seq", 111},
                {"payload", QJsonObject{{"scope", "bogus"}}}});
            require(bad.value("type").toString() == "reply",
                    "vault_forensics_rejects_bad_scope: still a reply, not a wire-level error"
                        + why());
            require(!bad.value("errors").toArray().isEmpty(),
                    "vault_forensics_rejects_bad_scope: F1-Core's own errors[] carries the "
                    "rejection" + why());
            require(QJsonDocument(bad).toJson().contains("unknown scope"),
                    "vault_forensics_rejects_bad_scope: names the bad scope in the diagnostic");
            std::cout << "CASE_OK: vault_forensics_rejects_bad_scope\n";

            // vault_forensics_clamps_limit: the fixture holds 105 real rows under
            // fx->showsPath (5 over VaultForensics::kMaxLimit=100) — request limit=101
            // and prove the clamp holds THROUGH the bridge: never 101 rows, truncated set.
            QJsonObject node = call(pipe, {{"cmd", "vault-forensics"}, {"seq", 112},
                {"payload", QJsonObject{{"scope", "node"},
                                        {"key", vaultFixture->showsPath}, {"limit", 101}}}});
            require(node.value("type").toString() == "reply",
                    "vault_forensics_clamps_limit: replies" + why());
            const int rowCount = node.value("browse").toObject().value("rows").toArray().size();
            require(rowCount <= 100,
                    QStringLiteral("vault_forensics_clamps_limit: never more than 100 rows "
                                   "through the bridge (got %1)").arg(rowCount));
            require(node.value("truncated").toBool(),
                    "vault_forensics_clamps_limit: truncated is set");
            std::cout << "CASE_OK: vault_forensics_clamps_limit (" << rowCount << " rows)\n";

            // vault_forensics_deadline_is_bounded: F0 found the bridge and VaultLibrary
            // share one thread today, so queryMarshalled() always degrades to a direct
            // call here — this proves that degrade path is genuinely FAST and bounded,
            // wall-clock, rather than merely trusting the code comment that says so.
            QElapsedTimer clock;
            clock.start();
            QJsonObject bounded = call(pipe, {{"cmd", "vault-forensics"}, {"seq", 113},
                {"payload", QJsonObject{{"scope", "summary"}, {"timeoutMs", 500}}}});
            const qint64 elapsedMs = clock.elapsed();
            require(bounded.value("type").toString() == "reply",
                    "vault_forensics_deadline_is_bounded: replies" + why());
            require(elapsedMs < 3000,
                    QStringLiteral("vault_forensics_deadline_is_bounded: bridge call returns "
                                   "quickly (%1 ms, same-thread degrade path)").arg(elapsedMs));
            std::cout << "CASE_OK: vault_forensics_deadline_is_bounded (" << elapsedMs << " ms)\n";
        }

        // Close the DRIVE gate again — leave the process as the denial tests found it.
        qunsetenv("COLOSSEUM_LANISTA_DRIVE");

        std::cout << "LANISTA_OK\n";
        rc = 0;
        app.quit();
    });
    app.exec();
    return rc;
}
