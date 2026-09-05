#include "../runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QtTest>

#define private public
#include "player/streamserver.h"
#undef private

namespace {

QByteArray exchange(const QUrl &url, const QByteArray &request)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
    if (!socket.waitForConnected(3000))
        return {};
    socket.write(request);
    if (!socket.waitForBytesWritten(3000))
        return {};

    QByteArray wire;
    for (int i = 0; i < 100 && socket.state() != QAbstractSocket::UnconnectedState; ++i) {
        if (socket.waitForReadyRead(100))
            wire += socket.readAll();
        QCoreApplication::processEvents();
    }
    wire += socket.readAll();
    return wire;
}

} // namespace

class StreamServerNativeCutoverTest final : public QObject
{
    Q_OBJECT

private slots:
    void startsNativeRuntimeWithoutChildProcess();
    void connectionRefusalFailsRequestAndRecoversRuntime();
};

void StreamServerNativeCutoverTest::startsNativeRuntimeWithoutChildProcess()
{
    QStandardPaths::setTestModeEnabled(true);
    StreamServer stream;
    QSignalSpy ready(&stream, &StreamServer::readyChanged);

    stream.warmUp();

    QVERIFY2(stream.ready(), "StreamServer must become ready from the native runtime");
    QVERIFY(!stream.engineUnavailable());
    QVERIFY(ready.count() >= 1);

    const QUrl streamUrl(stream.streamUrl(QStringLiteral(
        "0123456789abcdef0123456789abcdef01234567"), 0));
    QCOMPARE(streamUrl.scheme(), QStringLiteral("http"));
    QCOMPARE(streamUrl.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(streamUrl.port() > 0);

    const QByteArray response = exchange(streamUrl,
        "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    QVERIFY2(response.startsWith("HTTP/1.1 200 "),
             "StreamServer's native listener must expose the server.js heartbeat");
    QVERIFY(response.endsWith("{\"success\":true}"));
}

void StreamServerNativeCutoverTest::connectionRefusalFailsRequestAndRecoversRuntime()
{
    QStandardPaths::setTestModeEnabled(true);
    StreamServer stream;
    QSignalSpy errors(&stream, &StreamServer::streamError);
    QSignalSpy streams(&stream, &StreamServer::streamReady);

    stream.warmUp();
    QVERIFY2(stream.ready(), "initial native runtime must start");
    QVERIFY(stream.m_runtime);

    stream.m_runtime->stop();

    bool recoveredRuntime = false;
    connect(&stream, &StreamServer::readyChanged, &stream, [&]() {
        if (stream.ready())
            recoveredRuntime = true;
    });

    stream.play(QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 0);

    QTRY_VERIFY_WITH_TIMEOUT(recoveredRuntime, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > 0, 3000);
    QCOMPARE(streams.count(), 0);
    QVERIFY2(stream.ready(), "runtime supervision must recover independently for the next request");
    QVERIFY2(!stream.engineUnavailable(), "a healthy replacement runtime must remain available");
}

QTEST_GUILESS_MAIN(StreamServerNativeCutoverTest)

#include "streamserver_native_cutover_test.moc"