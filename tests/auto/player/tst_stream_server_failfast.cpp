#include "player/streamserver.h"

#include <QFile>
#include <QHostAddress>
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

} // namespace

class tst_stream_server_failfast : public QObject
{
    Q_OBJECT

private slots:
    void failedStartReportsAndCanRetry();
};

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
    QVERIFY(serverScript.write("// fixture") > 0);
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
