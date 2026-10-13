#include "WebEngineStartupPolicy.h"
#include <QtTest>
#include <QProcess>
#include <cstdio>

class WebEngineStartupPolicyTest : public QObject {
    Q_OBJECT
private slots:
    void unsafeEnvironment_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("value");
        QTest::newRow("sandbox-zero-still-disables") << "QTWEBENGINE_DISABLE_SANDBOX" << "0";
        QTest::newRow("sandbox-empty-still-disables") << "QTWEBENGINE_DISABLE_SANDBOX" << "";
        QTest::newRow("sandbox-one") << "QTWEBENGINE_DISABLE_SANDBOX" << "1";
        QTest::newRow("remote-local") << "QTWEBENGINE_REMOTE_DEBUGGING" << "9222";
        QTest::newRow("remote-all-interfaces") << "QTWEBENGINE_REMOTE_DEBUGGING" << "0.0.0.0:9222";
    }
    void unsafeEnvironment() {
        QFETCH(QString, name); QFETCH(QString, value);
        QProcessEnvironment environment;
        environment.insert(name, value);
        QVERIFY(!WebEngineStartupPolicy::violation({"colosseum"}, environment).isEmpty());
    }

    void engineOptions_data() {
        QTest::addColumn<QStringList>("flags");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("none") << QStringList{} << true;
        QTest::newRow("graphics") << QStringList{"--disable-gpu", "--disable-gpu-compositing"} << true;
        QTest::newRow("angle") << QStringList{"--use-gl=angle", "--use-angle=d3d11"} << true;
        QTest::newRow("logs") << QStringList{"--enable-logging", "--log-level=2", "--v=1"} << true;
        QTest::newRow("disable-logs") << QStringList{"--disable-logging"} << true;
        QTest::newRow("sandbox") << QStringList{"--no-sandbox"} << false;
        QTest::newRow("sandbox-equals-zero") << QStringList{"--no-sandbox=0"} << false;
        QTest::newRow("gpu-sandbox") << QStringList{"--disable-gpu-sandbox"} << false;
        QTest::newRow("certificate") << QStringList{"--ignore-certificate-errors"} << false;
        QTest::newRow("certificate-spki") << QStringList{"--ignore-certificate-errors-spki-list=secret"} << false;
        QTest::newRow("localhost-certificate") << QStringList{"--allow-insecure-localhost"} << false;
        QTest::newRow("origin") << QStringList{"--disable-web-security"} << false;
        QTest::newRow("mixed-content") << QStringList{"--allow-running-insecure-content"} << false;
        QTest::newRow("trust-http-origin") << QStringList{"--unsafely-treat-insecure-origin-as-secure=http://example.test"} << false;
        QTest::newRow("file-access") << QStringList{"--allow-file-access-from-files"} << false;
        QTest::newRow("single-process") << QStringList{"--single-process"} << false;
        QTest::newRow("isolation") << QStringList{"--disable-site-isolation-trials"} << false;
        QTest::newRow("feature-list") << QStringList{"--disable-features=IsolateOrigins,site-per-process"} << false;
        QTest::newRow("network-in-process") << QStringList{"--enable-features=NetworkServiceInProcess"} << false;
        QTest::newRow("remote-port") << QStringList{"--remote-debugging-port=9222"} << false;
        QTest::newRow("remote-pipe") << QStringList{"--remote-debugging-pipe"} << false;
        QTest::newRow("remote-origins") << QStringList{"--remote-allow-origins=*"} << false;
        QTest::newRow("single-dash") << QStringList{"-no-sandbox"} << false;
        QTest::newRow("windows-slash") << QStringList{"/no-sandbox"} << false;
        QTest::newRow("uppercase") << QStringList{"--NO-SANDBOX"} << false;
        QTest::newRow("unknown-future-flag") << QStringList{"--future-unsafe-option"} << false;
        QTest::newRow("embedded-default-overrides") << QStringList{"--enable-embedded-switches"} << false;
        QTest::newRow("terminator") << QStringList{"--", "--no-sandbox"} << false;
        QTest::newRow("split-value") << QStringList{"--use-gl", "angle"} << false;
        QTest::newRow("value-injection") << QStringList{"--use-gl=angle --no-sandbox"} << false;
        QTest::newRow("graphics-unknown-value") << QStringList{"--use-angle=unreviewed"} << false;
    }
    void engineOptions() {
        QFETCH(QStringList, flags); QFETCH(bool, allowed);
        QStringList arguments{"colosseum", "--WeBeNgInEaRgS"};
        arguments.append(flags);
        QCOMPARE(WebEngineStartupPolicy::violation(arguments, {}).isEmpty(), allowed);
        QProcessEnvironment environment;
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", flags.join(' '));
        QCOMPARE(WebEngineStartupPolicy::violation({"colosseum"}, environment).isEmpty(), allowed);
    }
    void malformedEnvironment_data() {
        QTest::addColumn<QString>("flags");
        QTest::newRow("quoted-switch") << "\"--no-sandbox\"";
        QTest::newRow("quote-splicing") << "--no-\"sandbox\"";
        QTest::newRow("unclosed-quote") << "\"--disable-gpu";
        QTest::newRow("tab") << "--disable-gpu\t--no-sandbox";
        QTest::newRow("newline") << "--disable-gpu\n--no-sandbox";
        QTest::newRow("carriage-return") << "--disable-gpu\r--no-sandbox";
    }
    void malformedEnvironment() {
        QFETCH(QString, flags);
        QProcessEnvironment environment;
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", flags);
        QVERIFY(!WebEngineStartupPolicy::violation({"colosseum"}, environment).isEmpty());
    }
    void bothSourcesChecked() {
        QProcessEnvironment environment;
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
        QVERIFY(!WebEngineStartupPolicy::violation(
            {"colosseum", "--webEngineArgs", "--no-sandbox"}, environment).isEmpty());
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox");
        QVERIFY(!WebEngineStartupPolicy::violation(
            {"colosseum", "--webEngineArgs", "--disable-gpu"}, environment).isEmpty());
    }
    void ordinaryApplicationConfiguration() {
        QProcessEnvironment environment;
        environment.insert("QSG_RHI_BACKEND", "opengl");
        environment.insert("QT_OPENGL", "software");
        environment.insert("COLOSSEUM_PLAYER2", "1");
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", "  --disable-gpu   --log-level=1  ");
        QVERIFY(WebEngineStartupPolicy::violation(
            {"colosseum", "--qml", "C:/some path/Main.qml", "https://example.test/?secret=x"}, environment).isEmpty());
    }
    void rejectionDoesNotExposeValues() {
        QProcessEnvironment environment;
        environment.insert("QTWEBENGINE_CHROMIUM_FLAGS", "--unknown=credential-secret");
        const auto message = WebEngineStartupPolicy::violation({"colosseum"}, environment);
        QVERIFY(!message.isEmpty());
        QVERIFY(!message.contains("credential-secret"));
    }
    void inheritedProcessConfiguration_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<int>("exitCode");
        QTest::newRow("safe-default") << "" << "" << QStringList{} << 0;
        QTest::newRow("safe-graphics") << "QTWEBENGINE_CHROMIUM_FLAGS" << "--disable-gpu"
                                     << QStringList{} << 0;
        QTest::newRow("unsafe-env") << "QTWEBENGINE_DISABLE_SANDBOX" << "0"
                                   << QStringList{} << 78;
        QTest::newRow("unsafe-argv") << "" << ""
            << QStringList{"--webEngineArgs", "--ignore-certificate-errors"} << 78;
        QTest::newRow("env-does-not-hide-unsafe-argv") << "QTWEBENGINE_CHROMIUM_FLAGS" << ""
            << QStringList{"--WEBENGINEARGS", "--no-sandbox"} << 78;
    }
    void inheritedProcessConfiguration() {
        QFETCH(QString, name); QFETCH(QString, value);
        QFETCH(QStringList, arguments); QFETCH(int, exitCode);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("QTWEBENGINE_DISABLE_SANDBOX");
        environment.remove("QTWEBENGINE_REMOTE_DEBUGGING");
        environment.remove("QTWEBENGINE_CHROMIUM_FLAGS");
        if (!name.isEmpty()) environment.insert(name, value);
        QProcess child;
        child.setProcessEnvironment(environment);
        arguments.prepend("--startup-policy-probe");
        child.start(QCoreApplication::applicationFilePath(), arguments);
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), exitCode);
        QCOMPARE(child.readAllStandardOutput().contains("startup allowed"), exitCode == 0);
    }
};
int main(int argc, char *argv[]) {
    if (argc > 1 && QByteArray(argv[1]) == "--startup-policy-probe") {
        // Exercise the same raw argv/environment boundary used before the app
        // constructor. This probe deliberately does not initialize WebEngine.
        const auto error = WebEngineStartupPolicy::currentViolation(argc, argv);
        if (!error.isEmpty()) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 78;
        }
        std::puts("startup allowed");
        return 0;
    }
    QCoreApplication app(argc, argv);
    WebEngineStartupPolicyTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "webengine_startup_policy_test.moc"
