// Production native host, real installed WebView2, hidden windows, loopback
// pages and disposable profiles. No desktop input or external account is used.
#include "feria/provider-host/FeriaProviderHost.h"
#include <QGuiApplication>
#include <QWindow>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTest>
#include <QProcess>
#include <QProcessEnvironment>

#include <windows.h>
#include <unknwn.h>
#include <WebView2.h>
#include <wrl.h>
#include <functional>
#include <memory>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
bool waitUntil(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 15000) QTest::qWait(10);
    return ready();
}
struct RawController {
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    ComPtr<ICoreWebView2Profile4> profile;
    bool finished = false;
    HRESULT result = E_PENDING;
    ~RawController() { if (controller) controller->Close(); }
};
std::shared_ptr<RawController> rawController(HWND parent, const QString &path) {
    auto state = std::make_shared<RawController>();
    const auto directory = path.toStdWString();
    const HRESULT call = CreateCoreWebView2EnvironmentWithOptions(nullptr, directory.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state, parent](HRESULT hr, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(hr) || !environment) { state->result = hr; state->finished = true; return S_OK; }
                state->environment = environment;
                const HRESULT create = environment->CreateCoreWebView2Controller(parent,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [state](HRESULT hr, ICoreWebView2Controller *controller) -> HRESULT {
                            state->result = hr;
                            if (SUCCEEDED(hr) && controller) {
                                state->controller = controller;
                                controller->put_IsVisible(FALSE);
                                state->result = controller->get_CoreWebView2(&state->view);
                                ComPtr<ICoreWebView2_13> version;
                                ComPtr<ICoreWebView2Profile> profile;
                                if (SUCCEEDED(state->result)) state->result = state->view.As(&version);
                                if (SUCCEEDED(state->result)) state->result = version->get_Profile(&profile);
                                if (SUCCEEDED(state->result)) state->result = profile.As(&state->profile);
                            }
                            state->finished = true;
                            return S_OK;
                        }).Get());
                if (FAILED(create)) { state->result = create; state->finished = true; }
                return S_OK;
            }).Get());
    if (FAILED(call)) { state->result = call; state->finished = true; }
    return state;
}
bool setPermission(ICoreWebView2Profile4 *profile, const QString &origin,
                   COREWEBVIEW2_PERMISSION_KIND kind, COREWEBVIEW2_PERMISSION_STATE value) {
    auto result = std::make_shared<HRESULT>(E_PENDING);
    const auto uri = origin.toStdWString();
    const HRESULT call = profile->SetPermissionState(kind, uri.c_str(), value,
        Callback<ICoreWebView2SetPermissionStateCompletedHandler>(
            [result](HRESULT hr) -> HRESULT { *result = hr; return S_OK; }).Get());
    return SUCCEEDED(call) && waitUntil([result] { return *result != E_PENDING; }) && SUCCEEDED(*result);
}
int permissionCount(ICoreWebView2Profile4 *profile, COREWEBVIEW2_PERMISSION_STATE expected,
                    COREWEBVIEW2_PERMISSION_KIND expectedKind = COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS) {
    auto count = std::make_shared<int>(-1);
    const HRESULT call = profile->GetNonDefaultPermissionSettings(
        Callback<ICoreWebView2GetNonDefaultPermissionSettingsCompletedHandler>(
            [count, expected, expectedKind](HRESULT hr, ICoreWebView2PermissionSettingCollectionView *rows) -> HRESULT {
                *count = 0;
                UINT32 size = 0;
                if (FAILED(hr) || !rows || FAILED(rows->get_Count(&size))) { *count = -2; return S_OK; }
                for (UINT32 i = 0; i < size; ++i) {
                    ComPtr<ICoreWebView2PermissionSetting> row;
                    COREWEBVIEW2_PERMISSION_KIND kind;
                    COREWEBVIEW2_PERMISSION_STATE state;
                    if (FAILED(rows->GetValueAtIndex(i, &row)) || !row || FAILED(row->get_PermissionKind(&kind))
                            || FAILED(row->get_PermissionState(&state))) { *count = -2; return S_OK; }
                    if (kind == expectedKind && state == expected) ++*count;
                }
                return S_OK;
            }).Get());
    if (FAILED(call) || !waitUntil([count] { return *count != -1; })) return -2;
    return *count;
}
QString script(FeriaProviderHost &host, const QString &source) {
    QSignalSpy results(&host, &FeriaProviderHost::scriptResult);
    host.executeScript("notification-test", source);
    if (!waitUntil([&results] { return !results.empty(); })) return "SCRIPT_TIMEOUT";
    // WebView2 returns a JSON value, including quoted strings.
    const auto value = results.first().at(1).toString();
    const auto array = QJsonDocument::fromJson(("[" + value + "]").toUtf8()).array();
    return array.isEmpty() ? value : array.first().toVariant().toString();
}
}

class WebView2Notifications : public QObject {
    Q_OBJECT
    QTcpServer server;
    QWindow window;
    QString origin;
private slots:
    void initTestCase() {
        QVERIFY2(!qEnvironmentVariableIsSet("WEBVIEW2_USER_DATA_FOLDER"), "Unset the WebView2 profile override before testing");
        qputenv("FERIA_HIDE_TEST_POPUPS", "1");
        window.resize(800, 600);
        window.create(); // Never shown or activated.
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        origin = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket,
                    [socket, data = QByteArray{}]() mutable {
                        data += socket->readAll();
                        if (!data.contains("\r\n\r\n")) return;
                        const QByteArray body("<!doctype html><title>Notification fixture</title>"
                            "<body><h1>Local fixture</h1><script>window.result='waiting';"
                            "function ask(){Notification.requestPermission().then(r=>{window.result=r;"
                            "if(window.opener)window.opener.postMessage('popup:'+r,'*')})}"
                            "window.addEventListener('message',e=>window.result=e.data);"
                            "if(location.pathname==='/popup')ask();</script></body>");
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    void freshRequestsDenied() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FeriaProviderHost host;
        // Keep the renderer active inside an unshown parent window.
        host.setVisible(true);
        QSignalSpy loaded(&host, &FeriaProviderHost::navigationCompleted);
        QSignalSpy failed(&host, &FeriaProviderHost::initializationFailed);
        host.initialize(quintptr(window.winId()), directory.path(), QUrl(origin));
        QVERIFY2(waitUntil([&] { return !loaded.empty() || !failed.empty(); }), "Host did not initialize");
        QVERIFY(failed.empty());
        QVERIFY(host.isReady());
        QVERIFY(loaded.last().at(1).toBool());
        script(host, "ask();true");
        QVERIFY2(waitUntil([&] { return script(host, "window.result") == "denied"; }), "Notification request was not denied");
        QCOMPARE(script(host, "Notification.permission"), "denied");
        loaded.clear();
        host.reload();
        QVERIFY(waitUntil([&] { return !loaded.empty(); }));
        QCOMPARE(script(host, "Notification.permission"), "denied");
    }
    void savedGrantsRevoked() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString seedPath = qEnvironmentVariable("FERIA_NOTIFICATION_SEED_PROFILE");
        if (seedPath.isEmpty()) {
            // A separate process creates the old grants and exits normally.
            // The observer below must see both grants restored from disk.
            QProcess seeder;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("FERIA_NOTIFICATION_SEED_PROFILE", directory.path());
            environment.insert("FERIA_NOTIFICATION_SEED_ORIGIN", origin);
            seeder.setProcessEnvironment(environment);
            seeder.start(QCoreApplication::applicationFilePath(), {"savedGrantsRevoked"});
            QVERIFY(waitUntil([&] { return seeder.state() == QProcess::NotRunning; }));
            QCOMPARE(seeder.exitStatus(), QProcess::NormalExit);
            QVERIFY2(seeder.exitCode() == 0, seeder.readAllStandardOutput().constData());
        }
        auto seed = rawController(reinterpret_cast<HWND>(window.winId()), seedPath.isEmpty() ? directory.path() : seedPath);
        QVERIFY(waitUntil([seed] { return seed->finished; }));
        QVERIFY(SUCCEEDED(seed->result));
        if (!seedPath.isEmpty()) {
            const QString savedOrigin = qEnvironmentVariable("FERIA_NOTIFICATION_SEED_ORIGIN");
            QVERIFY(setPermission(seed->profile.Get(), savedOrigin, COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS, COREWEBVIEW2_PERMISSION_STATE_ALLOW));
            QVERIFY(setPermission(seed->profile.Get(), "https://saved-notice.example", COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS, COREWEBVIEW2_PERMISSION_STATE_ALLOW));
            QVERIFY(setPermission(seed->profile.Get(), savedOrigin, COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION, COREWEBVIEW2_PERMISSION_STATE_ALLOW));
            QCOMPARE(permissionCount(seed->profile.Get(), COREWEBVIEW2_PERMISSION_STATE_ALLOW), 2);
            UINT32 browserPid = 0;
            QVERIFY(SUCCEEDED(seed->view->get_BrowserProcessId(&browserPid)));
            HANDLE browserProcess = OpenProcess(SYNCHRONIZE, FALSE, browserPid);
            QVERIFY(browserProcess != nullptr);
            seed->controller->Close();
            seed->profile.Reset(); seed->view.Reset(); seed->controller.Reset(); seed->environment.Reset();
            const bool exited = waitUntil([browserProcess] { return WaitForSingleObject(browserProcess, 0) == WAIT_OBJECT_0; });
            CloseHandle(browserProcess);
            QVERIFY2(exited, "The old browser process did not exit; this would not prove disk restoration");
            return;
        }
        QCOMPARE(permissionCount(seed->profile.Get(), COREWEBVIEW2_PERMISSION_STATE_ALLOW), 2);
        // Keep this independent controller alive so its profile remains a
        // valid observer while the production host updates the shared store.
        FeriaProviderHost host;
        host.setVisible(true);
        QSignalSpy loaded(&host, &FeriaProviderHost::navigationCompleted);
        QSignalSpy failed(&host, &FeriaProviderHost::initializationFailed);
        host.initialize(quintptr(window.winId()), directory.path(), QUrl(origin));
        QVERIFY(waitUntil([&] { return !loaded.empty() || !failed.empty(); }));
        QVERIFY(failed.empty());
        QCOMPARE(permissionCount(seed->profile.Get(), COREWEBVIEW2_PERMISSION_STATE_DENY), 2);
        script(host, "ask();true");
        QVERIFY(waitUntil([&] { return script(host, "window.result") == "denied"; }));
        QCOMPARE(script(host, "Notification.permission"), "denied");
        // OS location privacy can override the web-facing result. Verify that
        // the stored site choice itself was preserved, without reading location.
        QCOMPARE(permissionCount(seed->profile.Get(), COREWEBVIEW2_PERMISSION_STATE_ALLOW,
                                 COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION), 1);
    }
    void popupRequestsDenied() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FeriaProviderHost host;
        host.setVisible(true);
        QSignalSpy loaded(&host, &FeriaProviderHost::navigationCompleted);
        QSignalSpy failed(&host, &FeriaProviderHost::initializationFailed);
        QSignalSpy popup(&host, &FeriaProviderHost::popupOpened);
        host.initialize(quintptr(window.winId()), directory.path(), QUrl(origin));
        QVERIFY(waitUntil([&] { return !loaded.empty() || !failed.empty(); }));
        QVERIFY(failed.empty());
        script(host, "window.open('/popup','notification-policy-probe');true");
        QVERIFY(waitUntil([&] { return !popup.empty(); }));
        QVERIFY(waitUntil([&] { return script(host, "window.result") == "popup:denied"; }));
    }
};
QTEST_MAIN(WebView2Notifications)
#include "notifications.moc"
