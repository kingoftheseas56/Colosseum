// tst_player1_selftest_bridge — Function 0009 Player 1 runtime-proof prerequisite.
//
// Drives the REAL LanistaServer::dispatch() path over a real QLocalSocket. The inline
// QML fixture is intentionally tiny: this target proves command registration, central
// gate behavior, fixed setup/method invocation, and reply shape. Production PlayerPage
// behavior is exercised separately by the tagged colosseum.exe Lanista scenarios.

#include "devtools/LanistaServer.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtTest>

class tst_player1_selftest_bridge : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void selftest_off_command_is_absent();
    void selftest_on_drive_off_refuses_mutation();
    void setup_resume_has_fixed_reply_shape();
    void loaded_stall_invokes_real_named_method();
    void failover_reports_retired_current();
    void no_video_reports_decoded_dimensions();
};


namespace {

const char* kPipe = "ColosseumLanistaPlayer1SelftestBridge";

QQuickWindow* loadPlayerFixture(QQmlApplicationEngine& engine)
{
    const QByteArray qml = QByteArrayLiteral(R"QML(
import QtQuick
import QtQuick.Window

Window {
    objectName: "root"
    visible: true
    width: 640
    height: 360

    Item {
        id: player
        objectName: "player"
        anchors.fill: parent

        property bool starting: false
        property bool fileReady: false
        property bool errored: false
        property string statusMsg: ""
        property int streamRetryCount: 0
        property var streamCandidates: []
        property int currentStreamIndex: -1
        property var deadStreamKeys: ({})
        property string mediaResumeHash: ""
        property int mediaResumeFileIdx: 0
        property string mediaLocalPath: ""
        property string currentPlaybackUrl: ""
        property string subStreamId: ""
        property string mediaId: ""
        property real pendingSeekSec: -1
        property bool resumePromptConsumed: false
        property bool resumeChoiceOpen: false
        property real resumeChoiceSec: -1
        property real noVideoGraceSeconds: 20
        property bool recoverySawVideo: false
        property double recoveryNoVideoSince: 0
        property double recoveryUrlStartedAt: 0
        property double recoveryLastMovedAt: 0

        function handleStreamWatchdog() {
            errored = true
            starting = false
            statusMsg = "watchdog-fired"
        }
        function handlePlaybackFailure(reason) {
            var next = ({})
            next["url:file:///z:/__colosseum_lanista__/missing-a.mp4:0"] = reason
            deadStreamKeys = next
            currentStreamIndex = 1
            statusMsg = "switching"
        }
        function isStreamDead(index) {
            if (index !== 0)
                return false
            return deadStreamKeys["url:file:///z:/__colosseum_lanista__/missing-a.mp4:0"] !== undefined
        }
        function tickRecoveryWatch() {
            errored = true
            statusMsg = "no video"
        }

        Item {
            id: mpv
            objectName: "playerMpv"
            property bool pause: false
            property int decodedWidth: 0
            property int decodedHeight: 0
        }
    }
}
)QML");

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                     [](const QList<QQmlError>& warnings) {
                         for (const QQmlError& error : warnings)
                             qWarning() << "QML warning:" << error.toString();
                     });
    engine.loadData(qml, QUrl());
    for (int i = 0; i < 10 && engine.rootObjects().isEmpty(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return qobject_cast<QQuickWindow*>(
        engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().constFirst());
}

QJsonObject sendCmd(const QString& cmd, const QJsonObject& payload = {})
{
    QLocalSocket socket;
    QEventLoop loop;
    QByteArray bytes;
    bool timedOut = false;

    QObject::connect(&socket, &QLocalSocket::readyRead, &loop,
                     [&]() { bytes += socket.readAll(); });
    QObject::connect(&socket, &QLocalSocket::disconnected, &loop, [&]() {
        bytes += socket.readAll();
        loop.quit();
    });
    QObject::connect(&socket, &QLocalSocket::errorOccurred, &loop,
                     [&](QLocalSocket::LocalSocketError error) {
                         if (error != QLocalSocket::PeerClosedError)
                             loop.quit();
                     });
    QObject::connect(&socket, &QLocalSocket::connected, &loop, [&]() {
        QJsonObject request{
            {QStringLiteral("cmd"), cmd},
            {QStringLiteral("seq"), 1},
        };
        if (!payload.isEmpty())
            request.insert(QStringLiteral("payload"), payload);
        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
        socket.flush();
    });
    QTimer::singleShot(5000, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    socket.connectToServer(QString::fromLatin1(kPipe));
    loop.exec();

    if (timedOut || bytes.isEmpty()) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("TEST_NO_REPLY")},
        };
    }
    const qsizetype newline = bytes.indexOf('\n');
    return QJsonDocument::fromJson(
               newline < 0 ? bytes : bytes.left(newline)).object();
}

} // namespace

void tst_player1_selftest_bridge::init()
{
    qunsetenv("COLOSSEUM_LANISTA_SELFTEST");
    qunsetenv("COLOSSEUM_LANISTA_DRIVE");
}

void tst_player1_selftest_bridge::selftest_off_command_is_absent()
{
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QVERIFY(loadPlayerFixture(engine));
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-setup-resume"),
                                      {{QStringLiteral("seconds"), 12.5}});
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(reply.value(QStringLiteral("code")).toString(), QStringLiteral("UNKNOWN_CMD"));
}

void tst_player1_selftest_bridge::selftest_on_drive_off_refuses_mutation()
{
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    QQmlApplicationEngine engine;
    QVERIFY(loadPlayerFixture(engine));
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-setup-resume"),
                                      {{QStringLiteral("seconds"), 12.5}});
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(reply.value(QStringLiteral("code")).toString(), QStringLiteral("DRIVE_DISABLED"));
}

void tst_player1_selftest_bridge::setup_resume_has_fixed_reply_shape()
{
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QQuickWindow* window = loadPlayerFixture(engine);
    QVERIFY(window);
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-setup-resume"),
                                      {{QStringLiteral("seconds"), 12.5}});
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(reply.value(QStringLiteral("resumeChoiceOpen")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("resumeChoiceSec")).toDouble(), 12.5);
    QCOMPARE(reply.value(QStringLiteral("starting")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("paused")).toBool(), true);

    QQuickItem* player = window->findChild<QQuickItem*>(QStringLiteral("player"));
    QVERIFY(player);
    QCOMPARE(player->property("resumeChoiceOpen").toBool(), true);
}

void tst_player1_selftest_bridge::loaded_stall_invokes_real_named_method()
{
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QVERIFY(loadPlayerFixture(engine));
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-loaded-stall"));
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(reply.value(QStringLiteral("starting")).toBool(), false);
    QCOMPARE(reply.value(QStringLiteral("fileReady")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("errored")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("statusMsg")).toString(),
             QStringLiteral("watchdog-fired"));
}

void tst_player1_selftest_bridge::failover_reports_retired_current()
{
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QVERIFY(loadPlayerFixture(engine));
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-failover"));
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(reply.value(QStringLiteral("retiredCurrent")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("currentStreamIndex")).toInt(), 1);
}

void tst_player1_selftest_bridge::no_video_reports_decoded_dimensions()
{
    qputenv("COLOSSEUM_LANISTA_SELFTEST", "1");
    qputenv("COLOSSEUM_LANISTA_DRIVE", "1");
    QQmlApplicationEngine engine;
    QVERIFY(loadPlayerFixture(engine));
    LanistaServer server(&engine);
    QVERIFY2(server.isListening(), qUtf8Printable(server.listenError()));

    const QJsonObject reply = sendCmd(QStringLiteral("test-player1-no-video"));
    QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(reply.value(QStringLiteral("decodedWidth")).toInt(), 0);
    QCOMPARE(reply.value(QStringLiteral("decodedHeight")).toInt(), 0);
    QCOMPARE(reply.value(QStringLiteral("errored")).toBool(), true);
    QCOMPARE(reply.value(QStringLiteral("statusMsg")).toString(), QStringLiteral("no video"));
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef PLAYER1_SELFTEST_QT_PLATFORMS_DIR
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM_PLUGIN_PATH"))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", PLAYER1_SELFTEST_QT_PLATFORMS_DIR);
#endif
    qputenv("COLOSSEUM_LANISTA_PIPE", kPipe);
    QGuiApplication app(argc, argv);
    tst_player1_selftest_bridge test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_player1_selftest_bridge.moc"
