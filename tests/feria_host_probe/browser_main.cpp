#include "FeriaBrowserPolicy.h"
#include "FeriaAccountStore.h"
#include "provider-host/FeriaHostItem.h"
#include <QQuickItem>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QtWebEngineQuick/QtWebEngineQuick>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class BrowserReporter : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void report(const QString &url, bool success, const QString &detail) {
        if (!success) ++failures;
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"url", url}, {"success", success},
            {"detail", detail}}).toJson(QJsonDocument::Compact) << Qt::endl;
    }
    Q_INVOKABLE void finish() { QCoreApplication::exit(failures ? 1 : 0); }
    Q_INVOKABLE bool sendKey(int key, QQuickItem *target) {
#ifdef Q_OS_WIN
        if ((key != VK_ESCAPE && key != 'F' && key != VK_F10) || !target || !target->window()) return false;
        {
            const HWND owned = reinterpret_cast<HWND>(target->window()->winId());
            const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
            const DWORD currentThread = GetCurrentThreadId();
            const bool attached = foregroundThread != currentThread && AttachThreadInput(currentThread,foregroundThread,TRUE);
            BringWindowToTop(owned); SetForegroundWindow(owned);
            if (attached) AttachThreadInput(currentThread,foregroundThread,FALSE);
        }
        if (auto *native = qobject_cast<FeriaHostItem *>(target)) native->focusWebView();
        else target->forceActiveFocus(Qt::OtherFocusReason);
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(),&process);
        if (process != GetCurrentProcessId()) return false;
        INPUT input[2]{};
        for (auto &event : input) { event.type = INPUT_KEYBOARD; event.ki.wVk = static_cast<WORD>(key); }
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        return SendInput(2,input,sizeof(INPUT)) == 2;
#else
        return false;
#endif
    }
    int failures = 0;
};

int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    if (argc != 5) return 2; // QML path, engine, JSON URL list, isolated profile directory.
    QCoreApplication::setApplicationName("feria-browser-smoke");
    QCoreApplication::setOrganizationName("Colosseum");
    const QList<QPair<QString, bool>> policyCases{
        {"https://www.netflix.com/", true}, {"https://www.amazon.in/ap/signin", true},
        {"https://accounts.google.com/", true}, {"https://appleid.apple.com/", true},
        {"http://127.0.0.1:1234/auth", true}, {"about:blank", true},
        {"blob:https://example.com/media", true}, {"https:///", false},
        {"javascript:alert(1)", false}, {"file:///C:/secret.txt", false},
        {"data:text/html,hello", false}, {"ms-settings:test", false},
        {"blob:file:///C:/secret.txt", false}, {"", false}
    };
    for (const auto &entry : policyCases)
        if (FeriaBrowserPolicy::allows(QUrl(entry.first)) != entry.second) return 3;
    QFile file(QString::fromLocal8Bit(argv[3]));
    if (!file.open(QIODevice::ReadOnly)) return 4;
    const QJsonDocument targets = QJsonDocument::fromJson(file.readAll());
    if (!targets.isArray() || targets.array().isEmpty()) return 5;
    FeriaBrowserPolicy::registerTypes();
    FeriaBrowserPolicy policy;
    policy.setStorageRoot(QString::fromLocal8Bit(argv[4]) + "/browser");
    FeriaAccountStore account;
    account.setStoragePath(QString::fromLocal8Bit(argv[4]) + "/account-" + QString::fromLocal8Bit(argv[2]) + ".json");
    BrowserReporter reporter;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("FeriaBrowserPolicy", &policy);
    engine.rootContext()->setContextProperty("FeriaAccount", &account);
    engine.rootContext()->setContextProperty("browserReporter", &reporter);
    engine.rootContext()->setContextProperty("smokeEngine", QString::fromLocal8Bit(argv[2]));
    engine.rootContext()->setContextProperty("smokeUrls", targets.toVariant().toList());
    engine.rootContext()->setContextProperty("smokeProfileRoot", QString::fromLocal8Bit(argv[4]));
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (engine.rootObjects().isEmpty()) return 6;
    return app.exec();
}
#include "browser_main.moc"
