// A-01 Task Zero only: real WebView + foreign QWindow + overlapping QML window.
// No publication access, bridge, reader state, or production renderer is implemented here.
#include <QGuiApplication>
#include <QJniObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>
#include <QFuture>
#include <QPointer>
#include <QTimer>

using Android = QNativeInterface::QAndroidApplication;

class ReaderProbe : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow *foreignWindow READ foreignWindow NOTIFY foreignWindowChanged)
public:
    using QObject::QObject;
    ~ReaderProbe() override { retire(); }
    QWindow *foreignWindow() const { return m_window; }

    Q_INVOKABLE void recreate()
    {
        if (m_creating)
            return;
        retire();
        m_creating = true;
        Android::runOnAndroidMainThread([]() -> QVariant {
            QJniObject web("android/webkit/WebView", "(Landroid/content/Context;)V",
                           Android::context().object<jobject>());
            if (!web.isValid())
                return {};
            auto settings = web.callObjectMethod("getSettings", "()Landroid/webkit/WebSettings;");
            settings.callMethod<void>("setJavaScriptEnabled", "(Z)V", false);
            settings.callMethod<void>("setAllowFileAccess", "(Z)V", false);
            settings.callMethod<void>("setAllowContentAccess", "(Z)V", false);
            settings.callMethod<void>("setBlockNetworkLoads", "(Z)V", true);
            QString html = QStringLiteral(
                "<title>Reader composition probe</title>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<style>body{background:#fff7e5;color:#202020;font:20px serif;padding:20px}"
                "p{margin:28px 0}input{font-size:20px;width:90%}</style>"
                "<h1>Android WebView</h1><input placeholder='Tap to test keyboard'>"
                "<p>Select this text. Scroll the paper below the QML overlay.</p>");
            for (int i = 1; i <= 40; ++i)
                html += QStringLiteral("<p>Composition probe paragraph %1. Select, scroll, rotate, "
                                       "background and return.</p>").arg(i);
            const auto base = QJniObject::fromString(QStringLiteral("https://appassets.androidplatform.net/probe/"));
            const auto content = QJniObject::fromString(html);
            const auto mime = QJniObject::fromString(QStringLiteral("text/html"));
            const auto encoding = QJniObject::fromString(QStringLiteral("UTF-8"));
            web.callMethod<void>("loadDataWithBaseURL",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                base.object<jstring>(), content.object<jstring>(), mime.object<jstring>(),
                encoding.object<jstring>(), nullptr);
            return QVariant::fromValue(web);
        }).then(this, [this](const QVariant &result) {
            m_creating = false;
            m_web = result.value<QJniObject>();
            if (m_web.isValid())
                m_window = QWindow::fromWinId(WId(m_web.object<jobject>()));
            qInfo() << "reader-probe foreign window" << bool(m_window);
            emit foreignWindowChanged();
            QTimer::singleShot(5000, this, [this] {
                if (!m_web.isValid())
                    return;
                Android::runOnAndroidMainThread([web = m_web]() -> QVariant {
                    qInfo() << "reader-probe document"
                            << web.callObjectMethod("getTitle", "()Ljava/lang/String;").toString()
                            << "progress" << web.callMethod<jint>("getProgress");
                    return {};
                });
            });
        });
    }

    void setActive(bool active)
    {
        if (!m_web.isValid())
            return;
        Android::runOnAndroidMainThread([web = m_web, active]() -> QVariant {
            web.callMethod<void>(active ? "onResume" : "onPause");
            return {};
        });
    }
signals:
    void foreignWindowChanged();
private:
    void retire()
    {
        auto *old = m_window.data();
        m_window = nullptr;
        emit foreignWindowChanged(); // detach the container before releasing the foreign handle
        delete old;
        if (m_web.isValid()) {
            Android::runOnAndroidMainThread([web = m_web]() -> QVariant {
                web.callMethod<void>("stopLoading");
                web.callMethod<void>("destroy");
                return {};
            });
        }
        m_web = {};
    }
    QPointer<QWindow> m_window;
    QJniObject m_web;
    bool m_creating = false;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ReaderProbe probe;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("ReaderProbe"), &probe);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &probe,
        [&probe](Qt::ApplicationState state) { probe.setActive(state == Qt::ApplicationActive); });
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    probe.recreate();
    return app.exec();
}

#include "main.moc"
