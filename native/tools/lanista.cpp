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
//   3  not-yet-implemented (suite / brief — land in Task 11)
//   4  infrastructure: the bridge was unreachable (NO_PIPE / TIMEOUT) — NOT a red
//   5  scenario error: the scenario file was unopenable, malformed, or empty
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QRegularExpression>
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
            const QJsonObject reply = call(req, 10000);

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

static void printUsage(std::ostream& os)
{
    os << "usage:\n"
       << "  lanista [--pipe <name>] [--timeout <ms>] <cmd> [k=v ...] [--grab <target>]\n"
       << "  lanista run <scenario.json> [--keep-going]\n"
       << "  lanista expect <cmd> <dot.path> <op> <value>   (op 'exists' takes no value)\n"
       << "  lanista bless <target> <name>\n"
       << "  lanista suite | brief <arc>                    (NYI until Task 11)\n"
       << "\n"
       << "exit codes:\n"
       << "  0  pass (green)\n"
       << "  1  assertion or command failed (red)\n"
       << "  2  usage error\n"
       << "  3  not yet implemented (suite/brief — Task 11)\n"
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
    if (verb == QStringLiteral("suite") || verb == QStringLiteral("brief")) {
        // Implemented in Task 11 — refuse honestly until then.
        std::cout << "NYI until Task 11\n";
        return 3;
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
