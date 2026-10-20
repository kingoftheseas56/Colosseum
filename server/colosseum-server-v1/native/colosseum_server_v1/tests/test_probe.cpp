#include "server1/media/Probe.h"
#include "server1/ports/ProcessPort.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <cstdlib>
#include <iostream>
using namespace server1::media;
static void check(bool value, const char* why)
{
    if (!value) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile cases(argc > 1 ? argv[1] : "");
    check(cases.open(QIODevice::ReadOnly), "fixture open");
    const auto rows = QJsonDocument::fromJson(cases.readAll()).array();
    for (const auto& v : rows) {
        const auto row = v.toObject();
        auto actual = row["kind"] == QStringLiteral("legacy")
            ? LegacyProbe::normalize(row["input"].toString().toUtf8())
            : Probe::normalize(row["input"].toObject());
        if (actual != row["expected"].toObject()) {
            std::cerr << QJsonDocument(actual).toJson().constData() << "expected "
                      << QJsonDocument(row["expected"].toObject()).toJson().constData();
            check(false, "oracle mismatch");
        }
    }
    check(Probe::arguments("file:test").last() == QStringLiteral("file:test"), "probe URL arg");
    QFile argumentFixture(QFileInfo(cases.fileName()).dir().filePath("probe-arguments.json"));
    check(argumentFixture.open(QIODevice::ReadOnly), "argument fixture open");
    QStringList expectedArguments;
    for (const auto& arg : QJsonDocument::fromJson(argumentFixture.readAll()).array())
        expectedArguments << arg.toString();
    check(Probe::arguments("fixture:a") == expectedArguments, "exact M861 argument vector");
    std::cout << "normalization fixtures: " << rows.size() << " PASS\n";
    if (argc < 3)
        return 0;
    Probe probe(argv[2]);
    auto run = [&](const QString& url, int timeout = 120000) {
        QEventLoop loop;
        ProbeResult result;
        bool called = false;
        probe.start(
            url,
            [&](const ProbeResult& r) {
                result = r;
                called = true;
                loop.quit();
            },
            timeout);
        if (!called) {
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        check(called, "callback deadline");
        return result;
    };
    auto a = run("fixture:a");
    check(a.ok(), "probe success");
    check(!a.cached, "first not cached");
    check(run("fixture:a").cached, "cache hit");
    run("fixture:b");
    run("fixture:c");
    check(run("fixture:a").cached, "hit no refresh");
    run("fixture:d");
    check(!probe.contains("fixture:a"), "FIFO eviction");
    check(run("fixture:fail").error["message"].toString() == "Probe process exited with code: 7",
        "exit error");
    check(!probe.contains("fixture:fail"), "failure not cached");
    check(run("fixture:malformed").error["message"].toString() == "Probe process failed",
        "malformed error");
    check(run("fixture:invalid").error["message"].toString() == "Probe process failed",
        "invalid JSON error");
    check(run("fixture:multiple").value["format"].toObject()["duration"].toDouble() == 1,
        "first JSONStream value only");
    auto timeout = run("fixture:slow", 20);
    check(timeout.error.value("message").toString()
            == QStringLiteral("Probe process has been terminated with signal: SIGKILL"),
        "timeout signal");
    QEventLoop loop;
    bool canceled = false;
    auto id = probe.start("fixture:slow", [&](const ProbeResult& r) {
        canceled = !r.ok();
        loop.quit();
    });
    check(probe.cancel(id), "cancel accepted");
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    check(canceled, "cancel callback");
    {
        auto* p = new Probe(argv[2]);
        p->start("fixture:slow",
            [&](const ProbeResult&) { check(false, "callback after destruction"); });
        delete p;
    }
    std::cout << "process/cache/failure/timeout/cancel/destruction: PASS\n";
    {
        QEventLoop remoteLoop;
        bool complete = false;
        QStringList observed;
        RemoteProbeTransport transport;
        transport.dispatch = [&](const QString&, const QStringList& args,
                                 std::function<void(const RemoteProbeResult&)> cb) {
            observed = args;
            cb({ R"({"streams":[],"format":{"duration":"2"}})", { } });
        };
        Probe remote("unused", transport);
        auto id = remote.start(
            "remote:test",
            [&](const ProbeResult& r) {
                check(r.ok(), "remote success");
                complete = true;
                remoteLoop.quit();
            },
            1);
        check(!remote.cancel(id), "remote destroy is no-op");
        check(!complete, "source remote close delay");
        QTimer::singleShot(2000, &remoteLoop, &QEventLoop::quit);
        remoteLoop.exec();
        check(complete, "remote complete");
        check(observed == Probe::arguments("remote:test"), "remote args");
    }
    std::cout << "remote probe bridge: PASS\n";
    {
        LegacyProbe legacy("/usr/bin/ffmpeg");
        QEventLoop l;
        bool done = false;
        legacy.start("/tmp/nonexistent-m01-media", [&](const ProbeResult& r) {
            check(r.ok(), "legacy pipe finish must precede exit error");
            check(r.value == QJsonObject { { "streams", QJsonArray { } } }, "legacy empty streams");
            done = true;
            l.quit();
        });
        QTimer::singleShot(3000, &l, &QEventLoop::quit);
        l.exec();
        check(done, "legacy finish callback");
    }
    {
        Probe missing("/tmp/nonexistent-m01-program");
        QEventLoop l;
        bool done = false;
        missing.start("fixture:x", [&](const ProbeResult& r) {
            check(!r.ok(), "missing executable error");
            done = true;
            l.quit();
        });
        QTimer::singleShot(3000, &l, &QEventLoop::quit);
        l.exec();
        check(done, "missing callback");
    }
    {
        LegacyProbe missing("/tmp/nonexistent-m01-program");
        QEventLoop l;
        bool done = false;
        missing.start("fixture:x", [&](const ProbeResult& r) {
            check(!r.ok(), "legacy missing executable error");
            done = true;
            l.quit();
        });
        QTimer::singleShot(3000, &l, &QEventLoop::quit);
        l.exec();
        check(done, "legacy missing callback");
    }
    std::cout << "legacy stderr-before-exit/missing executable: PASS\n";
    {
        QEventLoop loop;
        bool closed = false;
        auto* port = new ProcessPort;
        ProcessCallbacks callbacks;
        callbacks.onOutputClosed = [&] {
            closed = true;
            delete port;
            loop.quit();
        };
        callbacks.onFinished
            = [&](const ProcessResult&) { check(false, "finish after close destroyed port"); };
        (void)port->start(ProcessSpec(argv[2], { "fixture:test" }), std::move(callbacks));
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        check(closed, "output-closed callback deletion");
    }
    std::cout << "output EOF callback owner deletion: PASS\n";
    if (argc > 3 && QString::fromLocal8Bit(argv[3]) == "--legacy-timeout") {
        LegacyProbe legacy(argv[2]);
        QEventLoop loop;
        QElapsedTimer elapsed;
        elapsed.start();
        bool done = false;
        legacy.start("fixture:slow", [&](const ProbeResult& r) {
            check(r.error["httpCode"].toInt() == 504, "legacy timeout status");
            check(r.error["message"].toString() == "cannot probe within 20000",
                "legacy timeout message");
            check(elapsed.elapsed() >= 19000, "legacy source timeout boundary");
            done = true;
            loop.quit();
        });
        QTimer::singleShot(23000, &loop, &QEventLoop::quit);
        loop.exec();
        check(done, "legacy timeout callback");
        std::cout << "legacy 20000ms timeout: PASS\n";
        return 0;
    }
    if (argc > 3) {
        Probe real("/usr/bin/ffprobe");
        QEventLoop l;
        bool done = false;
        real.start(argv[3], [&](const ProbeResult& r) {
            check(r.ok(), "real ffprobe");
            bool matched = false;
            for (const auto& rowValue : rows) {
                auto row = rowValue.toObject();
                if (row["kind"].toString() == "hlsv2"
                    && row["media"].toString() == QFileInfo(argv[3]).fileName()) {
                    check(
                        r.value == row["expected"].toObject(), "real ffprobe exact oracle result");
                    matched = true;
                }
            }
            check(matched, "real ffprobe expected fixture");
            check(!r.value["streams"].toArray().isEmpty(), "real tracks");
            std::cout << QJsonDocument(r.value).toJson(QJsonDocument::Compact).constData() << '\n';
            done = true;
            l.quit();
        });
        QTimer::singleShot(5000, &l, &QEventLoop::quit);
        l.exec();
        check(done, "real deadline");
        LegacyProbe legacy("/usr/bin/ffmpeg");
        done = false;
        legacy.start(argv[3], [&](const ProbeResult& r) {
            check(r.ok(), "real legacy probe");
            bool matched = false;
            for (const auto& rowValue : rows) {
                auto row = rowValue.toObject();
                if (row["kind"].toString() == "legacy"
                    && row["media"].toString() == QFileInfo(argv[3]).fileName()) {
                    check(r.value == row["expected"].toObject(), "real ffmpeg exact oracle result");
                    matched = true;
                }
            }
            check(matched, "real legacy expected fixture");
            done = true;
            l.quit();
        });
        QTimer::singleShot(5000, &l, &QEventLoop::quit);
        l.exec();
        check(done, "real legacy deadline");
    }
}
