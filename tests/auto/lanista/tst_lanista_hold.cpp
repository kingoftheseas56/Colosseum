#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

class LanistaHoldTest final : public QObject
{
    Q_OBJECT

private:
    struct RunResult {
        int exitCode = -1;
        QByteArray standardOutput;
        QByteArray standardError;
        qint64 elapsedMs = -1;
        bool started = false;
        bool finished = false;
    };

    static QString lanistaExecutable()
    {
#ifdef Q_OS_WIN
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lanista.exe"));
#else
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lanista"));
#endif
    }

    static QJsonObject holdStep(const QJsonValue& duration, bool includeDuration = true,
                                const QString& label = QStringLiteral("hold beat"))
    {
        QJsonObject payload;
        if (includeDuration)
            payload.insert(QStringLiteral("duration_ms"), duration);
        return QJsonObject{
            {QStringLiteral("cmd"), QStringLiteral("hold")},
            {QStringLiteral("label"), label},
            {QStringLiteral("payload"), payload}
        };
    }

    static QString writeScenario(QTemporaryDir& dir, const QJsonArray& steps)
    {
        const QString path = dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        const QJsonObject root{
            {QStringLiteral("name"), QStringLiteral("arc63-hold-test")},
            {QStringLiteral("steps"), steps}
        };
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        return path;
    }

    static RunResult runScenario(const QJsonArray& steps)
    {
        QTemporaryDir dir;
        RunResult result;
        if (!dir.isValid())
            return result;

        const QString scenario = writeScenario(dir, steps);
        if (scenario.isEmpty())
            return result;

        QProcess process;
        QElapsedTimer timer;
        timer.start();
        process.start(lanistaExecutable(), {QStringLiteral("run"), scenario});
        result.started = process.waitForStarted(3000);
        if (!result.started)
            return result;
        result.finished = process.waitForFinished(5000);
        result.elapsedMs = timer.elapsed();
        if (!result.finished) {
            process.kill();
            process.waitForFinished(1000);
        }
        result.exitCode = process.exitCode();
        result.standardOutput = process.readAllStandardOutput();
        result.standardError = process.readAllStandardError();
        return result;
    }

private slots:
    void boundedHoldWaitsAndReportsSuccess()
    {
        const RunResult result = runScenario(QJsonArray{holdStep(60)});
        QVERIFY2(result.started, "lanista child did not start");
        QVERIFY2(result.finished, "lanista child did not finish");
        QCOMPARE(result.exitCode, 0);
        QVERIFY2(result.elapsedMs >= 60, qPrintable(QStringLiteral("elapsed=%1 ms").arg(result.elapsedMs)));
        QVERIFY(result.standardOutput.contains("PASS  hold beat  [held 60 ms]"));
        QVERIFY(result.standardOutput.contains("1 steps, 0 failed"));
        QVERIFY(result.standardError.isEmpty());
    }

    void invalidDurationsFailAsScenarioReds_data()
    {
        QTest::addColumn<QJsonValue>("duration");
        QTest::addColumn<bool>("includeDuration");

        QTest::newRow("missing") << QJsonValue() << false;
        QTest::newRow("negative") << QJsonValue(-1) << true;
        QTest::newRow("above-bound") << QJsonValue(20001) << true;
        QTest::newRow("string") << QJsonValue(QStringLiteral("100")) << true;
        QTest::newRow("boolean") << QJsonValue(true) << true;
        QTest::newRow("fractional") << QJsonValue(1.5) << true;
    }

    void invalidDurationsFailAsScenarioReds()
    {
        QFETCH(QJsonValue, duration);
        QFETCH(bool, includeDuration);

        const RunResult result = runScenario(
            QJsonArray{holdStep(duration, includeDuration, QStringLiteral("invalid hold"))});
        QVERIFY2(result.started, "lanista child did not start");
        QVERIFY2(result.finished, "lanista child did not finish");
        QCOMPARE(result.exitCode, 1);
        QVERIFY(result.standardOutput.contains(
            "FAIL  invalid hold  [hold duration_ms must be between 0 and 20000]"));
        QVERIFY(result.standardOutput.contains("1 steps, 1 failed"));
        QVERIFY(!result.standardError.contains("INFRA ERROR"));
    }

    void holdComposesBetweenKeypressesWithoutBecomingABridgeCommand()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QJsonArray steps{
            QJsonObject{
                {QStringLiteral("cmd"), QStringLiteral("ui-keypress")},
                {QStringLiteral("label"), QStringLiteral("first key")},
                {QStringLiteral("payload"), QJsonObject{{QStringLiteral("key"), QStringLiteral("Down")}}}
            },
            holdStep(40, true, QStringLiteral("rhythm beat")),
            QJsonObject{
                {QStringLiteral("cmd"), QStringLiteral("ui-keypress")},
                {QStringLiteral("label"), QStringLiteral("second key")},
                {QStringLiteral("payload"), QJsonObject{{QStringLiteral("key"), QStringLiteral("Down")}}}
            }
        };
        const QString scenario = writeScenario(dir, steps);
        QVERIFY(!scenario.isEmpty());

        const QString pipeName = QStringLiteral("arc63-hold-%1")
                                     .arg(QUuid::createUuid().toString(QUuid::Id128));
        QLocalServer::removeServer(pipeName);
        QLocalServer server;
        QVERIFY2(server.listen(pipeName), qPrintable(server.errorString()));

        QStringList observedCommands;
        bool malformedRequest = false;

        connect(&server, &QLocalServer::newConnection, this, [&]() {
            while (QLocalSocket* socket = server.nextPendingConnection()) {
                auto* buffer = new QByteArray;
                const auto consumeRequest = [&, socket, buffer]() {
                    *buffer += socket->readAll();
                    const qsizetype newline = buffer->indexOf('\n');
                    if (newline < 0)
                        return;

                    QJsonParseError error{};
                    const QJsonObject request =
                        QJsonDocument::fromJson(buffer->left(newline), &error).object();
                    if (error.error != QJsonParseError::NoError || request.isEmpty()) {
                        malformedRequest = true;
                        socket->disconnectFromServer();
                        return;
                    }

                    observedCommands.append(request.value(QStringLiteral("cmd")).toString());
                    const QJsonObject reply{
                        {QStringLiteral("type"), QStringLiteral("reply")},
                        {QStringLiteral("seq"), request.value(QStringLiteral("seq"))}
                    };
                    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
                    socket->flush();
                    socket->disconnectFromServer();
                };
                connect(socket, &QLocalSocket::readyRead, socket, consumeRequest);
                if (socket->bytesAvailable() > 0)
                    consumeRequest();
            }
        });

        QProcess process;
        QElapsedTimer timer;
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(5000);
        bool timedOut = false;

        connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, [&]() {
            timedOut = true;
            process.kill();
            loop.quit();
        });

        timer.start();
        process.start(lanistaExecutable(),
                      {QStringLiteral("--pipe"), pipeName, QStringLiteral("run"), scenario});
        QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
        timeout.start();
        loop.exec();
        timeout.stop();

        QVERIFY2(!timedOut, "lanista rhythm scenario timed out");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY2(!malformedRequest, "fake bridge received malformed JSON");
        QCOMPARE(observedCommands,
                 QStringList({QStringLiteral("ui-keypress"), QStringLiteral("ui-keypress")}));
        QVERIFY2(timer.elapsed() >= 40, "runner-local hold did not wait the requested duration");

        const QByteArray output = process.readAllStandardOutput();
        QVERIFY(output.contains("PASS  first key"));
        QVERIFY(output.contains("PASS  rhythm beat  [held 40 ms]"));
        QVERIFY(output.contains("PASS  second key"));
        QVERIFY(output.contains("3 steps, 0 failed"));
        QVERIFY(!process.readAllStandardError().contains("INFRA ERROR"));

        server.close();
        QLocalServer::removeServer(pipeName);
    }
};

QTEST_GUILESS_MAIN(LanistaHoldTest)
#include "tst_lanista_hold.moc"
