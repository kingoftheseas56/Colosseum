// lanista — console client for Colosseum's dev-control bridge.
//
//   lanista [--pipe <name>] [--timeout <ms>] <cmd> [k=v ...] [--grab <target>]
//   lanista run <scenario.json> [--keep-going] [--pipe <name>]
//   lanista expect <cmd> <dot.path> <op> <value>   (op 'exists' takes no value)
//   lanista bless <grabTarget> <goldenName>        (writes tests/lanista_goldens/)
//   lanista suite [--dir tests/lanista_scenarios] [--out <reportDir>]
//   lanista brief <arcName> [--from <runDir>]
//
// One command per connection, newline-delimited JSON — tankoctl's shape.
// The scenario / golden engines live HERE, client-side: the app never judges
// itself (spec §13).
//
// EXIT CODES (a documented contract — Task 7's scenario engine keys off these,
// so a value below must mean exactly one thing and nothing else):
//   0  pass (green): every assertion / command succeeded
//   1  red: an assertion or command FAILED (a legitimate regression)
//   2  usage error: bad or missing arguments
//   3  not-yet-implemented (reserved contract slot; no verb returns this)
//   4  infrastructure: the bridge was unreachable (NO_PIPE / TIMEOUT) — NOT a red
//   5  scenario error: the scenario file was unopenable, malformed, or empty
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <iostream>

#include "tools/LanistaHash.h"

static QString g_pipe = QStringLiteral("ColosseumLanista");
static int g_timeout = 5000;

static QJsonObject call(const QJsonObject& req, int timeoutMs = -1)
{
    if (timeoutMs < 0) timeoutMs = g_timeout;
    const int seq = req.value(QStringLiteral("seq")).toInt();
    QLocalSocket sock;
    sock.connectToServer(g_pipe);
    if (!sock.waitForConnected(2000))
        return {{QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("code"), QStringLiteral("NO_PIPE")},
                {QStringLiteral("seq"), seq},
                {QStringLiteral("message"),
                 QStringLiteral("no lanista listening on ") + g_pipe}};
    sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
    sock.waitForBytesWritten(1000);
    // The server writes its one-line reply and IMMEDIATELY disconnectFromServer()s
    // (LanistaServer::Replier::send). On Windows named pipes that reply and the
    // peer-close routinely surface together, so a failing waitForReadyRead() is NOT
    // proof the reply never came — the bytes are sitting in the socket's read
    // buffer. Drain what is available before ever concluding TIMEOUT (the harness's
    // own in-process client drains on disconnected() for this exact reason). Still
    // fully blocking — NOT a non-blocking rewrite.
    QByteArray buf;
    while (!buf.contains('\n')) {
        if (sock.bytesAvailable() > 0) {
            buf += sock.readAll();
            continue;
        }
        if (sock.waitForReadyRead(timeoutMs)) {
            buf += sock.readAll();
            continue;
        }
        buf += sock.readAll();   // coalesced reply+close: the reply may surface only now
        break;
    }
    const qsizetype nl = buf.indexOf('\n');
    if (nl < 0)
        return {{QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("code"), QStringLiteral("TIMEOUT")},
                {QStringLiteral("seq"), seq},
                {QStringLiteral("message"),
                 QStringLiteral("no reply from ") + g_pipe
                     + QStringLiteral(" within %1 ms").arg(timeoutMs)}};
    return QJsonDocument::fromJson(buf.left(nl)).object();
}

// A reply that means "the bridge could not be reached", NOT "the assertion was
// red". Kept distinct so a dead environment can never masquerade as a regression
// (exit 4, never 1) — the one thing a scenario engine must not confuse.
static bool isInfraError(const QJsonObject& reply)
{
    if (reply.value(QStringLiteral("type")).toString() != QStringLiteral("error"))
        return false;
    const QString code = reply.value(QStringLiteral("code")).toString();
    return code == QStringLiteral("NO_PIPE") || code == QStringLiteral("TIMEOUT");
}

// Stringify a JSON value for display / string-compare WITHOUT the [ ... ]
// array-wrap that QJsonDocument forces on a bare scalar (which turned a plain 42
// into "[42]" at the diagnostic sites). Scalars render as themselves; a double
// renders at FULL precision ('g',17) so contains/matches are never lossy; arrays
// and objects render as compact JSON.
static QString jsonToString(const QJsonValue& v)
{
    if (v.isString())    return v.toString();
    if (v.isBool())      return v.toBool() ? QStringLiteral("true")
                                           : QStringLiteral("false");
    if (v.isDouble())    return QString::number(v.toDouble(), 'g', 17);
    if (v.isNull())      return QStringLiteral("null");
    if (v.isUndefined()) return QStringLiteral("undefined");
    if (v.isArray())
        return QString::fromUtf8(
            QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)).trimmed();
    return QString::fromUtf8(
        QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)).trimmed();
}

// k=v pairs -> payload. Numbers and booleans are typed; all else is string.
static QJsonObject payloadFromArgs(const QStringList& kvs)
{
    QJsonObject p;
    for (const QString& kv : kvs) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString k = kv.left(eq), v = kv.mid(eq + 1);
        bool okInt = false; const int i = v.toInt(&okInt);
        bool okDbl = false; const double d = v.toDouble(&okDbl);
        if (v == QStringLiteral("true")) p.insert(k, true);
        else if (v == QStringLiteral("false")) p.insert(k, false);
        else if (okInt) p.insert(k, i);
        else if (okDbl) p.insert(k, d);
        else p.insert(k, v);
    }
    return p;
}

// Dot-path into a JSON reply: "windows.0.width" etc.
static QJsonValue dig(const QJsonValue& root, const QString& path)
{
    QJsonValue cur = root;
    const QStringList parts = path.split(QLatin1Char('.'));
    for (const QString& part : parts) {
        bool isIdx = false; const int idx = part.toInt(&isIdx);
        if (cur.isArray() && isIdx) cur = cur.toArray().at(idx);
        else if (cur.isObject()) cur = cur.toObject().value(part);
        else return QJsonValue::Undefined;
    }
    return cur;
}

// T2's P1 review lesson folded in: numeric coercion only when the actual IS
// numeric; a non-numeric actual never silently passes a numeric op.
static bool opMatches(const QJsonValue& actual, const QString& op, const QString& want)
{
    if (op == QStringLiteral("exists")) return !actual.isUndefined();

    // == / != on two NUMBERS compare NUMERICALLY. Stringifying the actual first
    // is lossy (default 'g' keeps 6 sig-digits: 1234567 -> "1.23457e+06"), which
    // would make `== 1234567` wrongly FAIL and `!= 1234567` wrongly PASS — a
    // false result out of an assertion tool. When the actual is a number and the
    // wanted value parses as one, the comparison is on the doubles; anything else
    // (string actual, non-numeric want like `== ready`) falls through to the
    // string compare below, unchanged.
    if ((op == QStringLiteral("==") || op == QStringLiteral("!=")) && actual.isDouble()) {
        bool okW = false;
        const double dw = want.toDouble(&okW);
        if (okW) {
            const bool eq = actual.toDouble() == dw;
            return (op == QStringLiteral("==")) ? eq : !eq;
        }
    }

    const QString a = jsonToString(actual);
    if (op == QStringLiteral("==")) return a == want;
    if (op == QStringLiteral("!=")) return a != want;
    if (op == QStringLiteral("contains")) return a.contains(want);
    if (op == QStringLiteral("matches"))
        return QRegularExpression(want).match(a).hasMatch();
    if (!actual.isDouble()) return false;          // numeric ops need a number
    bool okW = false;
    const double dw = want.toDouble(&okW);
    if (!okW) return false;
    const double da = actual.toDouble();
    if (op == QStringLiteral(">=")) return da >= dw;
    if (op == QStringLiteral("<=")) return da <= dw;
    if (op == QStringLiteral(">"))  return da > dw;
    if (op == QStringLiteral("<"))  return da < dw;
    return false;
}

static QString goldenDir() { return QStringLiteral("tests/lanista_goldens"); }

// ── test sessions (decision brief 2026-08-06 §1–§3) ─────────────────────────
//
// A session is a CLIENT-OWNED disposable app process: unique pipe (never the
// daily app's default), tagged AppData/cache roots (COLOSSEUM_APPDATA_TAG),
// explicit gates, captured stdout/stderr, a readiness ping that must answer
// with the launched pid, an ISOLATION PROOF (both storage roots must carry the
// tag — asserted against get-state, never assumed from Qt's path rules), and a
// machine-readable session.json manifest. The daily app is not a test fixture:
// a session that would land on the default pipe refuses to start.

struct SessionSpec {
    QString exe = QStringLiteral("native/build-msvc/colosseum.exe");
    QString qml = QStringLiteral("qml/Main.qml");
    QString tag;                 // COLOSSEUM_APPDATA_TAG value; defaults to the session id
    QString seedDir;             // optional fixture tree copied into the AppData root pre-launch
    bool drive = false;          // COLOSSEUM_LANISTA_DRIVE=1
    int readyMs = 30000;         // ping-until-ready deadline
};

static bool copyTree(const QString& srcDir, const QString& dstDir)
{
    QDir().mkpath(dstDir);
    QDirIterator it(srcDir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(srcDir).relativeFilePath(it.filePath());
        const QString dst = dstDir + QLatin1Char('/') + rel;
        if (it.fileInfo().isDir()) {
            if (!QDir().mkpath(dst)) return false;
        } else {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!QFile::copy(it.filePath(), dst)) return false;
        }
    }
    return true;
}

static QString fileSha256(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

struct Session {
    SessionSpec spec;
    QString id;
    QString pipe;
    QString dir;                 // artifacts/lanista-sessions/<id>/
    QString appDataRoot;         // as REPORTED by the app, not as computed
    QString cacheRoot;
    QProcess proc;
    QJsonObject manifest;
    QString error;               // non-empty => start failed

    void writeManifest()
    {
        QFile f(dir + QStringLiteral("/session.json"));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    }
};

// Launch + prove isolation. On ANY failure the child is killed and error is set:
// a session that cannot prove it is disposable must not survive to be driven.
static void startSession(Session& s)
{
    // Identity. QUuid keeps parallel sessions collision-free without needing a
    // registry; the stamp keeps the artifact dirs humanly sortable.
    s.id = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
         + QLatin1Char('-')
         + QUuid::createUuid().toString(QUuid::Id128).left(8);
    if (s.spec.tag.isEmpty()) s.spec.tag = s.id;
    s.pipe = QStringLiteral("ColosseumLanista-") + s.id;
    s.dir = QStringLiteral("artifacts/lanista-sessions/") + s.id;
    QDir().mkpath(s.dir);

    // The daily-app safety line: the controller never lands on the default pipe.
    if (s.pipe == QStringLiteral("ColosseumLanista")) {
        s.error = QStringLiteral("refusing the daily app's default pipe");
        return;
    }
    if (!QFileInfo::exists(s.spec.exe)) {
        s.error = QStringLiteral("exe not found: ") + s.spec.exe;
        return;
    }

    // Optional fixture seed. The tagged AppData root is deterministic on Windows
    // (Roaming/<Org>/<AppName>); computed ONLY for seeding — isolation is still
    // proven from the app's own report after boot.
    const QString expectedAppData =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Brotherhood/Colosseum-dltest-") + s.spec.tag;
    if (!s.spec.seedDir.isEmpty()) {
        if (!QDir(s.spec.seedDir).exists()) {
            s.error = QStringLiteral("seed dir not found: ") + s.spec.seedDir;
            return;
        }
        if (!copyTree(s.spec.seedDir, expectedAppData)) {
            s.error = QStringLiteral("seed copy failed into ") + expectedAppData;
            return;
        }
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COLOSSEUM_LANISTA_PIPE"), s.pipe);
    env.insert(QStringLiteral("COLOSSEUM_APPDATA_TAG"), s.spec.tag);
    if (s.spec.drive)
        env.insert(QStringLiteral("COLOSSEUM_LANISTA_DRIVE"), QStringLiteral("1"));
    env.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    s.proc.setProcessEnvironment(env);
    s.proc.setStandardOutputFile(s.dir + QStringLiteral("/stdout.log"));
    s.proc.setStandardErrorFile(s.dir + QStringLiteral("/stderr.log"));
    s.proc.setProgram(s.spec.exe);
    s.proc.setArguments({s.spec.qml});

    const QString launchedAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    s.proc.start();
    if (!s.proc.waitForStarted(10000)) {
        s.error = QStringLiteral("process failed to start");
        return;
    }
    const qint64 pid = s.proc.processId();

    // Readiness: ping the SESSION pipe until it answers — with OUR pid. A pid
    // mismatch means a stranger owns this pipe name; kill our child and abort
    // rather than drive an unknown process.
    g_pipe = s.pipe;
    QString readyAt;
    QElapsedTimer clock; clock.start();
    while (clock.elapsed() < s.spec.readyMs) {
        const QJsonObject pong = call({{QStringLiteral("cmd"), QStringLiteral("ping")},
                                       {QStringLiteral("seq"), 1}}, 2000);
        if (pong.value(QStringLiteral("type")).toString() == QStringLiteral("reply")) {
            if (qint64(pong.value(QStringLiteral("pid")).toDouble()) != pid) {
                s.proc.kill(); s.proc.waitForFinished(5000);
                s.error = QStringLiteral("pipe answered with a foreign pid");
                return;
            }
            readyAt = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
            break;
        }
        if (s.proc.state() != QProcess::Running) {
            s.error = QStringLiteral("app exited before ready (code %1) — see %2/stderr.log")
                          .arg(s.proc.exitCode()).arg(s.dir);
            return;
        }
        QThread::msleep(250);
    }
    if (readyAt.isEmpty()) {
        s.proc.kill(); s.proc.waitForFinished(5000);
        s.error = QStringLiteral("bridge never became ready");
        return;
    }

    // Isolation proof: the app's OWN resolved roots must carry the tag. This is
    // the acceptance gate that turns "Qt should re-root both" into evidence.
    const QJsonObject st = call({{QStringLiteral("cmd"), QStringLiteral("get-state")},
                                 {QStringLiteral("seq"), 2}});
    s.appDataRoot = st.value(QStringLiteral("appDataRoot")).toString();
    s.cacheRoot = st.value(QStringLiteral("cacheRoot")).toString();
    const QString marker = QStringLiteral("Colosseum-dltest-") + s.spec.tag;
    if (!s.appDataRoot.contains(marker) || !s.cacheRoot.contains(marker)) {
        s.proc.kill(); s.proc.waitForFinished(5000);
        s.error = QStringLiteral("ISOLATION FAILED — appDataRoot=%1 cacheRoot=%2 (expected marker %3)")
                      .arg(s.appDataRoot, s.cacheRoot, marker);
        return;
    }

    s.manifest = QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("colosseum.session.v1")},
        {QStringLiteral("sessionId"), s.id},
        {QStringLiteral("tag"), s.spec.tag},
        {QStringLiteral("pipe"), s.pipe},
        {QStringLiteral("exe"), QFileInfo(s.spec.exe).absoluteFilePath()},
        {QStringLiteral("exeSha256"), fileSha256(s.spec.exe)},
        {QStringLiteral("qml"), s.spec.qml},
        {QStringLiteral("pid"), double(pid)},
        {QStringLiteral("drive"), s.spec.drive},
        {QStringLiteral("seedDir"), s.spec.seedDir},
        {QStringLiteral("launchedAt"), launchedAt},
        {QStringLiteral("readyAt"), readyAt},
        {QStringLiteral("appDataRoot"), s.appDataRoot},
        {QStringLiteral("cacheRoot"), s.cacheRoot},
        {QStringLiteral("stdoutPath"), s.dir + QStringLiteral("/stdout.log")},
        {QStringLiteral("stderrPath"), s.dir + QStringLiteral("/stderr.log")},
    };
    s.writeManifest();
}

// Graceful-first stop: closeAllWindows via the process's own WM_CLOSE path
// (QProcess::terminate posts it on Windows), bounded wait, then kill. The
// manifest records which one it took — a kill is evidence, not housekeeping.
static void stopSession(Session& s)
{
    QString killReason = QStringLiteral("graceful");
    if (s.proc.state() == QProcess::Running) {
        s.proc.terminate();
        if (!s.proc.waitForFinished(8000)) {
            s.proc.kill();
            s.proc.waitForFinished(5000);
            killReason = QStringLiteral("killed after graceful timeout");
        }
    }
    s.manifest.insert(QStringLiteral("exitedAt"),
                      QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    s.manifest.insert(QStringLiteral("exitCode"), s.proc.exitCode());
    s.manifest.insert(QStringLiteral("crashed"),
                      s.proc.exitStatus() == QProcess::CrashExit);
    s.manifest.insert(QStringLiteral("killReason"), killReason);
    s.writeManifest();
}

// Grab a target through the bridge; returns the PNG path or empty.
static QString grabTarget(const QString& target, int seq)
{
    const QJsonObject r = call({{QStringLiteral("cmd"), QStringLiteral("get-state")},
                                {QStringLiteral("seq"), seq},
                                {QStringLiteral("payload"),
                                 QJsonObject{{QStringLiteral("grab"),
                                              QJsonObject{{QStringLiteral("target"),
                                                           target}}}}}},
                               10000);
    return r.value(QStringLiteral("grabPath")).toString();
}

// ── scenario runner ─────────────────────────────────────────────────────────
struct StepResult { QString label; bool pass; QString detail; QString grabPath; };

// The whole run, classified so main can map it to the exit-code contract:
//   scenarioError -> 5 (file unopenable/malformed/empty)
//   infra         -> 4 (a step hit NO_PIPE/TIMEOUT — a dead bridge, not a red)
//   else          -> 1 if any step failed, 0 if all passed
struct ScenarioRun {
    bool scenarioError = false;
    bool infra = false;
    QList<StepResult> steps;
};

// verbose: print every step's full reply body. Exists because the gate's
// PASS/FAIL-only output made every diagnostic blind — an agent had to FORCE a
// failure to see a value (GLM's Biblio Library slice, 2026-08-06). Off by
// default so gates stay terse.
static bool g_verbose = false;

static ScenarioRun runScenario(const QString& file, bool keepGoing)
{
    ScenarioRun out;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        std::cerr << "SCENARIO ERROR: cannot open " << file.toStdString() << "\n";
        out.scenarioError = true;
        return out;
    }

    // I1: a malformed scenario must NEVER report success. A JSON typo — or any
    // file without a non-empty "steps" array — used to parse to an empty object,
    // yield zero steps, and print "0 steps, 0 failed" with exit 0. A regression
    // gate that green-lights a broken scenario is worse than useless.
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        std::cerr << "SCENARIO ERROR: " << file.toStdString() << ": "
                  << perr.errorString().toStdString()
                  << " (offset " << perr.offset << ")\n";
        out.scenarioError = true;
        return out;
    }
    const QJsonObject scenario = doc.object();
    if (!scenario.value(QStringLiteral("steps")).isArray()
        || scenario.value(QStringLiteral("steps")).toArray().isEmpty()) {
        std::cerr << "SCENARIO ERROR: " << file.toStdString()
                  << ": no non-empty \"steps\" array\n";
        out.scenarioError = true;
        return out;
    }

    int seq = 100;
    const QJsonArray steps = scenario.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue& sv : steps) {
        const QJsonObject step = sv.toObject();
        const QString label = step.value(QStringLiteral("label"))
                                  .toString(step.value(QStringLiteral("cmd")).toString());
        StepResult res{label, true, {}, {}};
        bool infraStop = false;

        // expect_visual step: golden compare, no command needed
        if (step.contains(QStringLiteral("expect_visual"))) {
            const QJsonObject ev = step.value(QStringLiteral("expect_visual")).toObject();
            const QString target = ev.value(QStringLiteral("target")).toString();
            const QString golden = goldenDir() + QLatin1Char('/')
                                   + ev.value(QStringLiteral("golden")).toString()
                                   + QStringLiteral(".png");
            const int maxDist = ev.value(QStringLiteral("maxDistance")).toInt(6);
            const QString got = grabTarget(target, ++seq);
            if (got.isEmpty() || !QFileInfo::exists(golden)) {
                res.pass = false;
                res.detail = got.isEmpty() ? QStringLiteral("grab failed")
                                           : QStringLiteral("no golden: ") + golden;
            } else {
                // M1: an unreadable/zero-byte PNG dHashes to 0, so two null images
                // would score distance 0 and FALSELY PASS the visual gate. Reject
                // a null decode outright rather than hash it.
                const QImage gotImg(got), goldenImg(golden);
                if (gotImg.isNull() || goldenImg.isNull()) {
                    res.pass = false;
                    res.detail = gotImg.isNull()
                                     ? QStringLiteral("unreadable grab: ") + got
                                     : QStringLiteral("unreadable golden: ") + golden;
                    res.grabPath = got;
                } else {
                    const int dist = lanista::hamming(
                        lanista::dhash(gotImg), lanista::dhash(goldenImg));
                    res.pass = dist <= maxDist;
                    res.detail = QStringLiteral("dHash distance %1 (max %2)")
                                     .arg(dist).arg(maxDist);
                    res.grabPath = got;
                }
            }
        } else {
            QJsonObject req{{QStringLiteral("cmd"), step.value(QStringLiteral("cmd"))},
                            {QStringLiteral("seq"), ++seq}};
            if (step.contains(QStringLiteral("payload")))
                req.insert(QStringLiteral("payload"), step.value(QStringLiteral("payload")));
            // The client deadline must OUTLIVE a step's own declared server-side wait
            // (ui-wait-for timeout_ms) — the first pilot run proved a 60 s wait dies at
            // the old flat 10 s client cap as a phantom INFRA while the server is still
            // honestly polling. Server timeout + 5 s slack, floor 10 s.
            int stepTimeout = 10000;
            const int waitMs = step.value(QStringLiteral("payload")).toObject()
                                   .value(QStringLiteral("timeout_ms")).toInt(0);
            if (waitMs > 0) stepTimeout = qMax(stepTimeout, waitMs + 5000);
            const QJsonObject reply = call(req, stepTimeout);
            if (g_verbose)
                std::cout << "  reply " << label.toStdString() << ": "
                          << QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString()
                          << "\n";

            // I3: a step whose call() came back NO_PIPE/TIMEOUT is an INFRA
            // failure, not a red assertion — the bridge is gone, so keep the run
            // from scoring it as a legitimate regression and stop (a dead pipe
            // won't heal mid-run). Diagnostic to stderr; the run's exit becomes 4.
            if (isInfraError(reply)) {
                out.infra = true;
                infraStop = true;
                res.pass = false;
                res.detail = reply.value(QStringLiteral("code")).toString()
                             + QStringLiteral(": ")
                             + reply.value(QStringLiteral("message")).toString();
                std::cerr << "INFRA ERROR: " << res.detail.toStdString()
                          << " (step " << label.toStdString() << ")\n";
            } else {
                const QJsonArray expects = step.value(QStringLiteral("expect")).toArray();
                for (const QJsonValue& exv : expects) {
                    const QJsonObject ex = exv.toObject();
                    const QJsonValue actual = dig(reply, ex.value(QStringLiteral("path")).toString());
                    if (!opMatches(actual, ex.value(QStringLiteral("op")).toString(),
                                   ex.value(QStringLiteral("value")).toVariant().toString())) {
                        res.pass = false;
                        res.detail = QStringLiteral("%1 %2 %3 — got %4")
                            .arg(ex.value(QStringLiteral("path")).toString(),
                                 ex.value(QStringLiteral("op")).toString(),
                                 ex.value(QStringLiteral("value")).toVariant().toString(),
                                 jsonToString(actual));
                        break;
                    }
                }
                // AUTO-GRAB ON FAILURE — every red step leaves its own evidence
                // without a re-run (spec §6).
                if (!res.pass) {
                    const QString t = step.value(QStringLiteral("grab_on_fail"))
                                          .toString(QStringLiteral("window"));
                    res.grabPath = grabTarget(t, ++seq);
                }
            }
        }
        std::cout << (infraStop ? "INFRA " : (res.pass ? "PASS  " : "FAIL  "))
                  << label.toStdString();
        if (!res.detail.isEmpty()) std::cout << "  [" << res.detail.toStdString() << "]";
        if (!res.pass && !res.grabPath.isEmpty())
            std::cout << "  evidence: " << res.grabPath.toStdString();
        std::cout << "\n";
        out.steps.append(res);
        if (infraStop) break;                    // dead bridge: stop, even with --keep-going
        if (!res.pass && !keepGoing) break;
    }
    return out;
}

// junit attribute values go out RAW today, so a failing step's detail (a `<` /
// `<=` op, or a quoted JSON actual in "got …") can carry `<`, `&`, `>`, `"` and
// make a CI junit parser reject the ENTIRE file — defeating the point of emitting
// junit at all. Escape the XML-significant chars; ampersand FIRST, or the later
// replacements would double-encode their own `&`.
static QString xmlEscape(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}

// A markdown table cell: a raw `|` opens phantom columns and a newline ends the
// row early, so a label/detail carrying either corrupts the report table. Collapse
// newlines to a space and backslash-escape the pipe.
static QString mdCell(QString s)
{
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    return s;
}

static void printUsage(std::ostream& os)
{
    os << "usage:\n"
       << "  lanista [--pipe <name>] [--timeout <ms>] <cmd> [k=v ...] [--grab <target>]\n"
       << "  lanista run <scenario.json> [--keep-going]\n"
       << "  lanista expect <cmd> <dot.path> <op> <value>   (op 'exists' takes no value)\n"
       << "  lanista bless <target> <name>\n"
       << "  lanista suite [--dir <scenarioDir>] [--out <reportDir>]\n"
       << "  lanista brief <arcName> [--from <runDir>]\n"
       << "  lanista session run <scenario.json> [--exe <path>] [--qml <path>] [--tag <t>]\n"
       << "                      [--drive] [--seed <dir>] [--ready-ms <n>] [--keep-going]\n"
       << "     (launches a DISPOSABLE tagged app on a unique pipe, proves isolation,\n"
       << "      runs the scenario, stops the app, writes artifacts/lanista-sessions/<id>/)\n"
       << "\n"
       << "exit codes:\n"
       << "  0  pass (green)\n"
       << "  1  assertion or command failed (red)\n"
       << "  2  usage error\n"
       << "  3  not yet implemented (reserved contract slot)\n"
       << "  4  infrastructure: bridge unreachable (NO_PIPE / TIMEOUT)\n"
       << "  5  scenario error: file unopenable, malformed, or empty\n";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);

    // global flags
    for (int i = 0; i < args.size();) {
        if (args[i] == QStringLiteral("--pipe") && i + 1 < args.size()) {
            g_pipe = args[i + 1]; args.removeAt(i); args.removeAt(i);
        } else if (args[i] == QStringLiteral("--timeout") && i + 1 < args.size()) {
            g_timeout = args[i + 1].toInt(); args.removeAt(i); args.removeAt(i);
        } else if (args[i] == QStringLiteral("--verbose")) {
            g_verbose = true; args.removeAt(i);
        } else ++i;
    }
    if (args.isEmpty()) {
        printUsage(std::cout);
        return 2;
    }
    const QString verb = args.takeFirst();
    if (verb == QStringLiteral("--help") || verb == QStringLiteral("-h")) {
        printUsage(std::cout);
        return 0;
    }

    if (verb == QStringLiteral("session")) {
        if (args.isEmpty() || args.takeFirst() != QStringLiteral("run")) {
            std::cerr << "session run <scenario.json> [--exe <path>] [--qml <path>] "
                         "[--tag <t>] [--drive] [--seed <dir>] [--ready-ms <n>] [--keep-going]\n";
            return 2;
        }
        SessionSpec spec;
        const bool keep = args.removeAll(QStringLiteral("--keep-going")) > 0;
        spec.drive = args.removeAll(QStringLiteral("--drive")) > 0;
        auto takeOpt = [&args](const QString& flag) -> QString {
            const int i = args.indexOf(flag);
            if (i < 0 || i + 1 >= args.size()) return {};
            const QString v = args[i + 1];
            args.removeAt(i); args.removeAt(i);
            return v;
        };
        if (const QString v = takeOpt(QStringLiteral("--exe")); !v.isEmpty()) spec.exe = v;
        if (const QString v = takeOpt(QStringLiteral("--qml")); !v.isEmpty()) spec.qml = v;
        if (const QString v = takeOpt(QStringLiteral("--tag")); !v.isEmpty()) spec.tag = v;
        if (const QString v = takeOpt(QStringLiteral("--seed")); !v.isEmpty()) spec.seedDir = v;
        if (const QString v = takeOpt(QStringLiteral("--ready-ms")); !v.isEmpty())
            spec.readyMs = v.toInt();
        if (args.isEmpty()) { std::cerr << "session run: missing <scenario.json>\n"; return 2; }
        const QString scenario = args.takeFirst();

        Session s; s.spec = spec;
        startSession(s);
        if (!s.error.isEmpty()) {
            std::cerr << "SESSION START FAILED: " << s.error.toStdString() << "\n";
            stopSession(s);
            return 4;   // the bridge/app never became drivable — infrastructure, not a red
        }
        std::cout << "SESSION " << s.id.toStdString() << " pipe=" << s.pipe.toStdString()
                  << "\n  appData=" << s.appDataRoot.toStdString()
                  << "\n  cache=" << s.cacheRoot.toStdString() << "\n";

        const ScenarioRun run = runScenario(scenario, keep);
        int failed = 0;
        for (const auto& r : run.steps) if (!r.pass) ++failed;

        // Pull the app-side artifacts (grabs) into the session dir before the
        // tagged tree is considered disposable.
        const QJsonObject st = call({{QStringLiteral("cmd"), QStringLiteral("get-state")},
                                     {QStringLiteral("seq"), 9000}});
        const QString runDir = st.value(QStringLiteral("runDir")).toString();
        if (!runDir.isEmpty() && QDir(runDir).exists()) {
            QDirIterator pngs(runDir, {QStringLiteral("*.png")}, QDir::Files);
            while (pngs.hasNext()) {
                pngs.next();
                QFile::copy(pngs.filePath(),
                            s.dir + QLatin1Char('/') + pngs.fileName());
            }
        }
        stopSession(s);

        std::cout << run.steps.size() << " steps, " << failed << " failed"
                  << "  (manifest: " << s.dir.toStdString() << "/session.json)\n";
        if (run.scenarioError) return 5;
        if (run.infra) return 4;
        return failed ? 1 : 0;
    }

    if (verb == QStringLiteral("run")) {
        const bool keep = args.removeAll(QStringLiteral("--keep-going")) > 0;
        if (args.isEmpty()) {
            std::cerr << "run <scenario.json> [--keep-going]\n";
            return 2;
        }
        const ScenarioRun run = runScenario(args.value(0), keep);
        if (run.scenarioError)
            return 5;   // SCENARIO ERROR already went to stderr
        int failed = 0;
        for (const auto& r : run.steps) if (!r.pass) ++failed;
        std::cout << run.steps.size() << " steps, " << failed << " failed\n";
        if (run.infra)
            return 4;   // a step hit a dead bridge — not a legitimate red
        return failed ? 1 : 0;
    }
    if (verb == QStringLiteral("bless")) {
        if (args.size() < 2) { std::cout << "bless <target> <name>\n"; return 2; }
        const QString got = grabTarget(args[0], 1);
        if (got.isEmpty()) { std::cout << "grab failed\n"; return 1; }
        QDir().mkpath(goldenDir());
        const QString dst = goldenDir() + QLatin1Char('/') + args[1]
                            + QStringLiteral(".png");
        QFile::remove(dst);
        const bool ok = QFile::copy(got, dst);
        std::cout << (ok ? "BLESSED " : "FAILED ") << dst.toStdString() << "\n";
        return ok ? 0 : 1;
    }
    if (verb == QStringLiteral("expect")) {
        // M5: `exists` needs only <cmd> <path> exists (3 args); every other op
        // needs a value (4 args).
        if (args.size() < 3) {
            std::cout << "expect <cmd> <path> <op> <value>   (op 'exists' takes no value)\n";
            return 2;
        }
        const bool isExists = args[2] == QStringLiteral("exists");
        if (!isExists && args.size() < 4) {
            std::cout << "expect <cmd> <path> <op> <value>   (op 'exists' takes no value)\n";
            return 2;
        }
        const QJsonObject reply = call({{QStringLiteral("cmd"), args[0]},
                                        {QStringLiteral("seq"), 1}});
        // I3: bridge-unreachable is infrastructure (exit 4), decided BEFORE the
        // assertion — a dead environment must never read as a legitimate red.
        if (isInfraError(reply)) {
            std::cerr << "INFRA ERROR: " << reply.value(QStringLiteral("code")).toString().toStdString()
                      << " " << reply.value(QStringLiteral("message")).toString().toStdString() << "\n";
            return 4;
        }
        const bool ok = opMatches(dig(reply, args[1]), args[2], args.value(3));
        std::cout << (ok ? "PASS\n" : "FAIL\n");
        return ok ? 0 : 1;
    }
    if (verb == QStringLiteral("suite")) {
        QString dir = QStringLiteral("tests/lanista_scenarios");
        QString outDir = QStringLiteral("agents/eyes-on/suite-latest");
        for (int i = 0; i < args.size();) {
            if (args[i] == QStringLiteral("--dir") && i + 1 < args.size()) {
                dir = args[i + 1]; args.removeAt(i); args.removeAt(i);
            } else if (args[i] == QStringLiteral("--out") && i + 1 < args.size()) {
                outDir = args[i + 1]; args.removeAt(i); args.removeAt(i);
            } else ++i;
        }
        QDir().mkpath(outDir);
        int total = 0, failed = 0;
        bool infraAbort = false;
        QString junit, report;
        QDirIterator it(dir, {QStringLiteral("*.json")}, QDir::Files);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString scenario = QFileInfo(file).baseName();
            // Each scenario is graded independently, keepGoing so a red step never
            // truncates its own scenario's rows.
            const ScenarioRun run = runScenario(file, /*keepGoing=*/true);

            // A dead bridge (NO_PIPE / TIMEOUT) makes the REST of the suite
            // meaningless — every remaining scenario would only re-discover the
            // same unreachable pipe. Stop here and let the exit code say
            // "infrastructure" (4), never a green 0. runScenario already put the
            // INFRA ERROR detail on stderr; name the abort so the log is unambiguous.
            if (run.infra) {
                std::cerr << "INFRA ERROR: bridge unreachable at " << scenario.toStdString()
                          << " — aborting suite\n";
                infraAbort = true;
                break;
            }

            // A scenario the engine could not even run (unopenable / malformed /
            // no steps) must NEVER be silently skipped — that silent skip is the
            // exact broken-gate the `run` verb refuses (exit 5). Record it as a
            // FAIL row and count it, so the suite can never green-light a scenario
            // it never actually executed.
            if (run.scenarioError) {
                ++total; ++failed;
                junit += QStringLiteral(
                    "  <testcase classname=\"%1\" name=\"%2\">"
                    "<failure message=\"%3\"/></testcase>\n")
                    .arg(xmlEscape(scenario), xmlEscape(QStringLiteral("scenario")),
                         xmlEscape(QStringLiteral("scenario error")));
                report += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                    .arg(mdCell(scenario), mdCell(QStringLiteral("scenario")),
                         QStringLiteral("FAIL"), mdCell(QStringLiteral("scenario error")));
                continue;
            }

            for (const StepResult& r : run.steps) {
                ++total;
                junit += QStringLiteral(
                    "  <testcase classname=\"%1\" name=\"%2\">%3</testcase>\n")
                    .arg(xmlEscape(scenario), xmlEscape(r.label),
                         r.pass ? QString()
                                : QStringLiteral("<failure message=\"%1\"/>")
                                      .arg(xmlEscape(r.detail)));
                report += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                    .arg(mdCell(scenario), mdCell(r.label),
                         r.pass ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                         mdCell(r.detail));
                if (!r.pass) {
                    ++failed;
                    if (!r.grabPath.isEmpty())
                        QFile::copy(r.grabPath, outDir + QLatin1Char('/')
                                    + QFileInfo(r.grabPath).fileName());
                }
            }
        }
        QFile jx(outDir + QStringLiteral("/junit.xml"));
        if (jx.open(QIODevice::WriteOnly))
            jx.write(QStringLiteral(
                "<?xml version=\"1.0\"?>\n<testsuite tests=\"%1\" failures=\"%2\">\n%3</testsuite>\n")
                .arg(total).arg(failed).arg(junit).toUtf8());
        QFile rp(outDir + QStringLiteral("/report.md"));
        if (rp.open(QIODevice::WriteOnly))
            rp.write((QStringLiteral("# lanista suite\n\n| scenario | step | verdict | detail |\n|---|---|---|---|\n")
                      + report
                      + QStringLiteral("\n**%1 steps, %2 failed.**\n").arg(total).arg(failed))
                         .toUtf8());
        std::cout << total << " steps, " << failed << " failed -> "
                  << outDir.toStdString() << "\n";
        if (infraAbort) {
            std::cout << "INFRA ERROR: suite aborted — bridge unreachable\n";
            return 4;   // a dead bridge is infrastructure, never a green suite
        }
        return failed ? 1 : 0;   // any red step OR scenario-error is a red suite
    }
    if (verb == QStringLiteral("brief")) {
        if (args.isEmpty()) { std::cout << "brief <arcName> [--from <runDir>]\n"; return 2; }
        const QString arc = args.takeFirst();
        QString from;
        const int fi = args.indexOf(QStringLiteral("--from"));
        if (fi >= 0 && fi + 1 < args.size()) from = args[fi + 1];
        if (from.isEmpty()) {
            // default: the newest run dir the bridge reported via get-state
            const QJsonObject st = call({{QStringLiteral("cmd"), QStringLiteral("get-state")},
                                         {QStringLiteral("seq"), 1}});
            from = st.value(QStringLiteral("runDir")).toString();
        }
        if (from.isEmpty() || !QDir(from).exists()) {
            std::cout << "no run dir\n"; return 1;
        }
        const QString dst = QStringLiteral("agents/eyes-on/")
            + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
            + QLatin1Char('-') + arc;
        QDir().mkpath(dst);
        QString gallery = QStringLiteral("# Eyes-on brief — %1\n\n"
            "One line per surface: what changed, what to look at.\n\n").arg(arc);
        QDirIterator pngs(from, {QStringLiteral("*.png")}, QDir::Files);
        while (pngs.hasNext()) {
            const QString png = pngs.next();
            const QString name = QFileInfo(png).fileName();
            QFile::copy(png, dst + QLatin1Char('/') + name);
            gallery += QStringLiteral("## %1\n\n![%1](%1)\n\n_What to check:_ (fill in)\n\n")
                           .arg(name);
        }
        QFile g(dst + QStringLiteral("/gallery.md"));
        if (g.open(QIODevice::WriteOnly)) g.write(gallery.toUtf8());
        std::cout << "BRIEF " << dst.toStdString() << "\n";
        return 0;
    }

    // plain command: lanista <cmd> [k=v ...] [--grab target]
    QString grabName;
    const int gi = args.indexOf(QStringLiteral("--grab"));
    if (gi >= 0 && gi + 1 < args.size()) {
        grabName = args[gi + 1];
        args.removeAt(gi); args.removeAt(gi);
    }
    QJsonObject payload = payloadFromArgs(args);
    if (!grabName.isEmpty())
        payload.insert(QStringLiteral("grab"),
                       QJsonObject{{QStringLiteral("target"), grabName}});
    QJsonObject req{{QStringLiteral("cmd"), verb}, {QStringLiteral("seq"), 1}};
    if (!payload.isEmpty()) req.insert(QStringLiteral("payload"), payload);
    const QJsonObject reply = call(req, grabName.isEmpty() ? g_timeout : 10000);
    std::cout << QJsonDocument(reply).toJson(QJsonDocument::Indented).toStdString();
    // Honour the exit-code contract: a NO_PIPE/TIMEOUT is infrastructure (4), a
    // handler's coded error is a red (1), a reply is green (0).
    if (isInfraError(reply)) return 4;
    return reply.value(QStringLiteral("type")).toString() == QStringLiteral("reply")
               ? 0 : 1;
}
