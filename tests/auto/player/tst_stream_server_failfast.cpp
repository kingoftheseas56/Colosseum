#include "player/streamserver.h"

#include <QFile>
#include <QHostAddress>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {

#if defined(Q_OS_LINUX)
constexpr auto kRuntimeName = "stremio-runtime";
constexpr auto kUnavailableMessage =
    "Streaming engine unavailable. Install or start Stremio Service.";
#else
constexpr auto kRuntimeName = "stremio-runtime.exe";
constexpr auto kUnavailableMessage =
    "Streaming engine unavailable. Repair or reinstall Colosseum.";
#endif

class ScopedEnvironment {
public:
    ScopedEnvironment(const char *name, const QByteArray &value)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_oldValue(qgetenv(name))
    {
        qputenv(m_name, value);
    }

    ~ScopedEnvironment()
    {
        if (m_wasSet)
            qputenv(m_name, m_oldValue);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name;
    bool m_wasSet;
    QByteArray m_oldValue;
};

bool rejectProbe(QTcpServer *server)
{
    bool accepted = false;
    while (server->hasPendingConnections()) {
        auto *socket = server->nextPendingConnection();
        accepted = true;
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
            socket->readAll();
            socket->write("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
        });
    }
    return accepted;
}

bool stageFakeRuntime(const QString &runtimeDir)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString fixture = appDir.filePath(QStringLiteral("fake_stremio_runtime")
#if defined(Q_OS_WIN)
                                             + QStringLiteral(".exe")
#endif
    );
    if (!QFile::copy(fixture, QDir(runtimeDir).filePath(QString::fromLatin1(kRuntimeName))))
        return false;
#if defined(Q_OS_WIN)
    for (const QString &dll : {QStringLiteral("Qt6Core.dll"), QStringLiteral("Qt6Network.dll")}) {
        if (!QFile::copy(appDir.filePath(dll), QDir(runtimeDir).filePath(dll)))
            return false;
    }
#endif
    return true;
}

} // namespace

class tst_stream_server_failfast : public QObject
{
    Q_OBJECT

private slots:
    void failedStartReportsAndCanRetry();
    void hungChildTimesOutAndCanRetry();
    void ownedChildWithDeadListenerRestartsPendingPlay();
    void readyChildShutdownIsNotReportedAsStartupFailure();
};

void tst_stream_server_failfast::hungChildTimesOutAndCanRetry()
{
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());

    QVERIFY(stageFakeRuntime(runtimeDir.path()));
    QFile serverScript(runtimeDir.filePath(QStringLiteral("server.js")));
    QVERIFY(serverScript.open(QIODevice::WriteOnly));
    QVERIFY(serverScript.write("var port = 11470;") > 0);
    serverScript.close();

    ScopedEnvironment runtimeOverride("COLOSSEUM_STREAM_SERVER", runtimeDir.path().toUtf8());
    ScopedEnvironment mode("COLOSSEUM_FAKE_STREMIO_MODE", QByteArrayLiteral("hang-before-ready"));
    StreamServer stream;
    QSignalSpy errorSpy(&stream, &StreamServer::streamError);

    stream.play(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);
    QTRY_VERIFY_WITH_TIMEOUT(stream.engineUnavailable(), 3000);
    QVERIFY(!stream.starting());
    QVERIFY(!stream.ready());
    QCOMPARE(errorSpy.count(), 1);

    stream.play(QStringLiteral("fedcba9876543210fedcba9876543210fedcba98"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 2, 3000);
    QVERIFY(!stream.starting());
}

void tst_stream_server_failfast::ownedChildWithDeadListenerRestartsPendingPlay()
{
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());

    QVERIFY(stageFakeRuntime(runtimeDir.path()));
    QFile serverScript(runtimeDir.filePath(QStringLiteral("server.js")));
    QVERIFY(serverScript.open(QIODevice::WriteOnly));
    QVERIFY(serverScript.write("var port = 11470;") > 0);
    serverScript.close();

    const QString marker = runtimeDir.filePath(QStringLiteral("listener-dropped"));
    ScopedEnvironment runtimeOverride("COLOSSEUM_STREAM_SERVER", runtimeDir.path().toUtf8());
    ScopedEnvironment mode("COLOSSEUM_FAKE_STREMIO_MODE", QByteArrayLiteral("drop-once"));
    ScopedEnvironment markerOverride("COLOSSEUM_FAKE_STREMIO_MARKER", marker.toUtf8());
    StreamServer stream;
    QSignalSpy readySpy(&stream, &StreamServer::streamReady);
    QSignalSpy errorSpy(&stream, &StreamServer::streamError);

    stream.warmUp();
    QTRY_VERIFY_WITH_TIMEOUT(stream.ready(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 3000);

    const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
    constexpr int requestCount = 12;
    for (int fileIdx = 0; fileIdx < requestCount; ++fileIdx)
        stream.play(hash, fileIdx);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.count(), requestCount, 6000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(readySpy.at(0).at(1).toString(), hash);
    QVERIFY(stream.ready());
}

void tst_stream_server_failfast::readyChildShutdownIsNotReportedAsStartupFailure()
{
#if !defined(Q_OS_LINUX)
    QSKIP("Linux packaged-runtime shutdown regression");
#else
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());

    QFile runtime(runtimeDir.filePath(QString::fromLatin1(kRuntimeName)));
    QVERIFY(runtime.open(QIODevice::WriteOnly));
    const QByteArray script =
        "#!/bin/sh\n"
        "echo 'EngineFS server started at http://127.0.0.1:11470'\n"
        "trap 'exit 0' TERM INT\n"
        "while :; do sleep 1; done\n";
    QCOMPARE(runtime.write(script), script.size());
    runtime.close();
    QVERIFY(runtime.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner));
    QFile serverScript(runtimeDir.filePath(QStringLiteral("server.js")));
    QVERIFY(serverScript.open(QIODevice::WriteOnly));
    QVERIFY(serverScript.write("var port = 11470;") > 0);
    serverScript.close();

    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 11470))
        QSKIP("127.0.0.1:11470 is occupied by an external Stremio service");
    QObject::connect(&probe, &QTcpServer::newConnection, &probe, [&probe]() { rejectProbe(&probe); });

    ScopedEnvironment runtimeOverride("COLOSSEUM_STREAM_SERVER", runtimeDir.path().toUtf8());
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*engine failed before ready.*")));
    {
        StreamServer stream;
        stream.warmUp();
        QTRY_VERIFY_WITH_TIMEOUT(stream.ready(), 6000);
    }
#endif
}

void tst_stream_server_failfast::failedStartReportsAndCanRetry()
{
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());

    QFile invalidRuntime(runtimeDir.filePath(QString::fromLatin1(kRuntimeName)));
    QVERIFY(invalidRuntime.open(QIODevice::WriteOnly));
    QVERIFY(invalidRuntime.write("not a runtime executable") > 0);
    invalidRuntime.close();
    QVERIFY(invalidRuntime.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner));
    QFile serverScript(runtimeDir.filePath(QStringLiteral("server.js")));
    QVERIFY(serverScript.open(QIODevice::WriteOnly));
    QVERIFY(serverScript.write("var port = 11470;") > 0);
    serverScript.close();

    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 11470))
        QSKIP("127.0.0.1:11470 is occupied by an external Stremio service");
    QObject::connect(&probe, &QTcpServer::newConnection, &probe, [&probe]() {
        rejectProbe(&probe);
    });

    ScopedEnvironment runtimeOverride("COLOSSEUM_STREAM_SERVER", runtimeDir.path().toUtf8());
    StreamServer stream;
    QSignalSpy errorSpy(&stream, &StreamServer::streamError);

    stream.play(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);
    QTRY_VERIFY_WITH_TIMEOUT(stream.engineUnavailable(), 6000);
    QVERIFY(!stream.starting());
    QVERIFY(!stream.ready());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(errorSpy.at(0).at(0).toString(), QString::fromUtf8(kUnavailableMessage));

    // A FailedToStart callback must not leave m_proc/m_starting wedged: a later
    // play() must get a fresh probe and produce a fresh actionable error.
    stream.play(QStringLiteral("fedcba9876543210fedcba9876543210fedcba98"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 2, 6000);
    QVERIFY(stream.engineUnavailable());
    QVERIFY(!stream.starting());
}

QTEST_MAIN(tst_stream_server_failfast)

#include "tst_stream_server_failfast.moc"
