// lanista — console client for Colosseum's dev-control bridge.
//
//   lanista [--pipe <name>] [--timeout <ms>] <cmd> [k=v ...] [--grab <target>]
//   lanista run <scenario.json> [--keep-going] [--pipe <name>]
//   lanista expect <cmd> <dot.path> <op> <value>       (exit 0 pass / 1 fail)
//   lanista bless <grabTarget> <goldenName>            (writes tests/lanista_goldens/)
//   lanista suite [--dir tests/lanista_scenarios] [--out <reportDir>]
//   lanista brief <arcName> [--from <runDir>]
//
// One command per connection, newline-delimited JSON — tankoctl's shape.
// The scenario / golden engines live HERE, client-side: the app never judges
// itself (spec §13).
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRegularExpression>
#include <iostream>

#include "tools/LanistaHash.h"

static QString g_pipe = QStringLiteral("ColosseumLanista");
static int g_timeout = 5000;

static QJsonObject call(const QJsonObject& req, int timeoutMs = -1)
{
    if (timeoutMs < 0) timeoutMs = g_timeout;
    QLocalSocket sock;
    sock.connectToServer(g_pipe);
    if (!sock.waitForConnected(2000))
        return {{QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("code"), QStringLiteral("NO_PIPE")},
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
                {QStringLiteral("code"), QStringLiteral("TIMEOUT")}};
    return QJsonDocument::fromJson(buf.left(nl)).object();
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
    QString a;
    if (actual.isString()) a = actual.toString();
    else if (actual.isBool()) a = actual.toBool() ? QStringLiteral("true")
                                                  : QStringLiteral("false");
    else if (actual.isDouble()) a = QString::number(actual.toDouble());
    else a = QString::fromUtf8(QJsonDocument(QJsonArray{actual})
                                   .toJson(QJsonDocument::Compact));
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

static QList<StepResult> runScenario(const QString& file, bool keepGoing)
{
    QList<StepResult> results;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        results.append({QStringLiteral("open ") + file, false,
                        QStringLiteral("cannot open"), {}});
        return results;
    }
    const QJsonObject scenario = QJsonDocument::fromJson(f.readAll()).object();
    int seq = 100;
    const QJsonArray steps = scenario.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue& sv : steps) {
        const QJsonObject step = sv.toObject();
        const QString label = step.value(QStringLiteral("label"))
                                  .toString(step.value(QStringLiteral("cmd")).toString());
        StepResult res{label, true, {}, {}};

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
                const int dist = lanista::hamming(
                    lanista::dhash(QImage(got)), lanista::dhash(QImage(golden)));
                res.pass = dist <= maxDist;
                res.detail = QStringLiteral("dHash distance %1 (max %2)")
                                 .arg(dist).arg(maxDist);
                res.grabPath = got;
            }
        } else {
            QJsonObject req{{QStringLiteral("cmd"), step.value(QStringLiteral("cmd"))},
                            {QStringLiteral("seq"), ++seq}};
            if (step.contains(QStringLiteral("payload")))
                req.insert(QStringLiteral("payload"), step.value(QStringLiteral("payload")));
            const QJsonObject reply = call(req, 10000);
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
                             QString::fromUtf8(QJsonDocument(QJsonArray{actual})
                                 .toJson(QJsonDocument::Compact)));
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
        std::cout << (res.pass ? "PASS  " : "FAIL  ") << label.toStdString();
        if (!res.detail.isEmpty()) std::cout << "  [" << res.detail.toStdString() << "]";
        if (!res.pass && !res.grabPath.isEmpty())
            std::cout << "  evidence: " << res.grabPath.toStdString();
        std::cout << "\n";
        results.append(res);
        if (!res.pass && !keepGoing) break;
    }
    return results;
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
        std::cout << "usage: lanista <cmd> [k=v ...] [--grab target] | run <file> "
                     "| expect <cmd> <path> <op> <value> | bless <target> <name> "
                     "| suite | brief <arc>\n";
        return 2;
    }
    const QString verb = args.takeFirst();

    if (verb == QStringLiteral("run")) {
        const bool keep = args.removeAll(QStringLiteral("--keep-going")) > 0;
        const auto results = runScenario(args.value(0), keep);
        int failed = 0;
        for (const auto& r : results) if (!r.pass) ++failed;
        std::cout << results.size() << " steps, " << failed << " failed\n";
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
        if (args.size() < 4) { std::cout << "expect <cmd> <path> <op> <value>\n"; return 2; }
        const QJsonObject reply = call({{QStringLiteral("cmd"), args[0]},
                                        {QStringLiteral("seq"), 1}});
        const bool ok = opMatches(dig(reply, args[1]), args[2], args[3]);
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
    return reply.value(QStringLiteral("type")).toString() == QStringLiteral("reply")
               ? 0 : 1;
}
