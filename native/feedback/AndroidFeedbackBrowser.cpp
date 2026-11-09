#include "AndroidFeedbackBrowser.h"

#include <QFuture>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QTimer>

namespace {
using Android = QNativeInterface::QAndroidApplication;
constexpr auto hostClass = "org/colosseum/feedback/FeedbackWebHost";
QPointer<AndroidFeedbackBrowser> browser;
void JNICALL eventFromJava(JNIEnv*, jclass, jlong instance, jstring event)
{
    const QString name = QJniObject(event).toString();
    QMetaObject::invokeMethod(qApp, [instance, name] {
        if (browser) browser->receive(quint64(instance), name);
    }, Qt::QueuedConnection);
}
void destroyHost(QJniObject host)
{
    if (host.isValid()) Android::runOnAndroidMainThread([host] { host.callMethod<void>("destroy"); });
}
}

AndroidFeedbackBrowser::AndroidFeedbackBrowser(QObject* parent) : QObject(parent)
{
    browser = this;
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (m_host.isValid()) Android::runOnAndroidMainThread([host=m_host, active=state == Qt::ApplicationActive] {
            host.callMethod<void>("setActive", "(Z)V", jboolean(active));
        });
    });
}

AndroidFeedbackBrowser::~AndroidFeedbackBrowser() { close(); }

void AndroidFeedbackBrowser::close()
{
    ++m_instance;
    auto* window = m_window.data();
    m_window = nullptr;
    emit foreignWindowChanged();
    delete window; // Detach Qt's non-owning wrapper before destroying the Java View.
    const auto host = m_host;
    m_host = {};
    destroyHost(host);
    m_loading = false;
    emit stateChanged();
}

void AndroidFeedbackBrowser::fail()
{
    close();
    m_error = tr("Feedback could not load. Check your connection and try again.");
    emit stateChanged();
}

void AndroidFeedbackBrowser::open(const QUrl& url, bool bundledSupport)
{
    m_error.clear();
    close();
    const QString path = url.path();
    const bool support = path == "/support" || path == "/support/";
    const bool feedback = path == "/feedback/" || path == "/feedback" || path == "/feedback/index.html"
        || path == "/feedback/report" || path == "/feedback/report.html";
    if (url.scheme() != "https" || url.host() != "getcolosseum.world"
        || !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443)
        || (!support && (!feedback || bundledSupport))) {
        fail();
        return;
    }
    const JNINativeMethod methods[] = {{const_cast<char*>("onEvent"),
        const_cast<char*>("(JLjava/lang/String;)V"), reinterpret_cast<void*>(eventFromJava)}};
    if (!QJniEnvironment().registerNativeMethods(hostClass, methods, 1)) { fail(); return; }
    m_loading = true;
    emit stateChanged();
    const auto instance = m_instance;
    const auto encoded = url.toString(QUrl::FullyEncoded);
    Android::runOnAndroidMainThread([instance, encoded, bundledSupport]() -> QVariant {
        QJniObject host(hostClass, "(Landroid/content/Context;JLjava/lang/String;Z)V",
            Android::context().object<jobject>(), jlong(instance),
            QJniObject::fromString(encoded).object<jstring>(), jboolean(bundledSupport));
        return QVariant::fromValue(host);
    }).then(this, [this, instance](const QVariant& result) {
        const auto host = result.value<QJniObject>();
        if (m_instance != instance) { destroyHost(host); return; }
        m_host = host;
        if (host.isValid()) {
            const auto view = host.callObjectMethod("view", "()Landroid/webkit/WebView;");
            if (view.isValid()) m_window = QWindow::fromWinId(WId(view.object<jobject>()));
        }
        emit foreignWindowChanged();
        if (!m_window) fail();
    });
    QTimer::singleShot(support && !bundledSupport ? 10000 : 30000, this, [this, instance] {
        if (m_instance == instance && m_loading) fail();
    });
}

void AndroidFeedbackBrowser::receive(quint64 instance, const QString& event)
{
    if (instance != m_instance) return;
    if (event == "close") { emit closeRequested(); return; }
    if (event == "error") { fail(); return; }
    if (event.startsWith("external:")) {
        const QUrl destination(event.mid(9), QUrl::StrictMode);
        if (destination.isValid()) emit externalLinkRequested(destination);
        return;
    }
    m_loading = event != "loaded";
    emit stateChanged();
}
