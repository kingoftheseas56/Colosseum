#include "AndroidEbookRenderer.h"
#include "Reader2Bridge.h"
#include "../platform/AndroidGraphicsWorkarounds.h"

#include <QFuture>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

namespace {
using Android = QNativeInterface::QAndroidApplication;
constexpr auto hostClass = "org/colosseum/reader/AndroidEbookHost";
QPointer<AndroidEbookRenderer> renderer; // accessed only on Qt's GUI thread
quint64 nextInstance = 0;

void JNICALL eventFromJava(JNIEnv*, jclass, jlong instance, jstring event, jstring json)
{
    const QString name = QJniObject(event).toString();
    const QString payload = QJniObject(json).toString();
    QMetaObject::invokeMethod(qApp, [instance, name, payload] {
        if (renderer) renderer->receive(quint64(instance), name, payload);
    }, Qt::QueuedConnection);
}

void destroyHost(QJniObject host)
{
    if (host.isValid()) Android::runOnAndroidMainThread([host] {
        host.callMethod<void>("destroy");
    });
}

QString jsonString(const QVariant& value)
{
    const auto plain = value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant() : value;
    return QString::fromUtf8(QJsonDocument::fromVariant(plain).toJson(QJsonDocument::Compact));
}
}

AndroidEbookRenderer::AndroidEbookRenderer(Reader2Bridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge)
{
    renderer = this;
    connect(bridge, &Reader2Bridge::personalStateAboutToChange, this, &AndroidEbookRenderer::close);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (!m_host.isValid()) return;
        Android::runOnAndroidMainThread([host = m_host, active = state == Qt::ApplicationActive] {
            host.callMethod<void>("setActive", "(Z)V", jboolean(active));
        });
    });
}

AndroidEbookRenderer::~AndroidEbookRenderer() { close(); }

void AndroidEbookRenderer::setReady(bool ready)
{
    if (m_ready == ready) return;
    m_ready = ready;
    emit glueUpChanged();
}

void AndroidEbookRenderer::create()
{
    if (m_host.isValid() || m_creating) return;
    const JNINativeMethod methods[] = {{const_cast<char*>("onEvent"),
        const_cast<char*>("(JLjava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void*>(eventFromJava)}};
    if (!QJniEnvironment().registerNativeMethods(hostClass, methods, 1)) {
        emit eventRaised(QStringLiteral("error"), QStringLiteral("{\"message\":\"EPUB renderer could not initialize\"}"));
        return;
    }
    m_creating = true;
    const quint64 instance = m_instance = ++nextInstance;
    QTimer::singleShot(15000, this, [this, instance] {
        if (m_instance == instance && !m_ready)
            emit eventRaised(QStringLiteral("error"), QStringLiteral("{\"message\":\"The EPUB reader did not finish loading. Close and reopen the book.\"}"));
    });
    Android::runOnAndroidMainThread([instance]() -> QVariant {
        QJniObject host(hostClass, "(Landroid/content/Context;J)V",
                        Android::context().object<jobject>(), jlong(instance));
        return QVariant::fromValue(host);
    }).then(this, [this, instance](const QVariant& result) {
        const auto host = result.value<QJniObject>();
        if (m_instance != instance) { destroyHost(host); return; }
        m_creating = false;
        m_host = host;
        if (host.isValid()) {
            const auto view = host.callObjectMethod("view", "()Landroid/webkit/WebView;");
            if (view.isValid()) m_window = QWindow::fromWinId(WId(view.object<jobject>()));
        }
        emit foreignWindowChanged();
        setReady(m_shellReady && m_window);
        if (!m_window) {
            close();
            emit eventRaised(QStringLiteral("error"), QStringLiteral("{\"message\":\"EPUB rendering surface is unavailable\"}"));
        }
    });
}

void AndroidEbookRenderer::close()
{
    m_instance = ++nextInstance; // invalidate queued Java events and asynchronous creation
    m_generation = 0;
    m_creating = false;
    m_shellReady = false;
    setReady(false);
    auto* window = m_window.data();
    m_window = nullptr;
    emit foreignWindowChanged(); // detach WindowContainer before destroying its non-owning wrapper
    delete window;
    const auto host = m_host;
    m_host = {};
    destroyHost(host); // Java View must outlive the foreign QWindow
}

void AndroidEbookRenderer::open(const QString& path, const QString& cfi, int generation)
{
    if (!m_host.isValid() || !m_ready || !m_bridge || m_bridge->personalStateSealed()) return;
    m_generation = generation;
    QUrl source(path);
    if (source.scheme().isEmpty()) source = QUrl::fromLocalFile(path);
    Android::runOnAndroidMainThread([host = m_host, source = source.toString(QUrl::FullyEncoded), cfi, generation] {
        host.callMethod<void>("open", "(Ljava/lang/String;Ljava/lang/String;J)V",
            QJniObject::fromString(source).object<jstring>(), QJniObject::fromString(cfi).object<jstring>(), jlong(generation));
    });
}

void AndroidEbookRenderer::receive(quint64 instance, const QString& event, const QString& json)
{
    if (instance != m_instance || json.size() > 512 * 1024) return;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
    if (event == QStringLiteral("rendererLost")) { close(); return; }
    if (event == QStringLiteral("glueLoaded")) { m_shellReady = true; setReady(m_window && m_host.isValid()); return; }
    const auto value = doc.object().value(QStringLiteral("gen"));
    if (value.isDouble() && value.toInt(-1) != m_generation) return;
    if (m_bridge && !m_bridge->personalStateSealed()) emit eventRaised(event, json);
}

void AndroidEbookRenderer::command(const QString& name, const QVariantList& args)
{
    if (!m_ready || !m_host.isValid()) return;
    const auto json = QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(args)).toJson(QJsonDocument::Compact));
    Android::runOnAndroidMainThread([host = m_host, name, json] {
        host.callMethod<void>("command", "(Ljava/lang/String;Ljava/lang/String;)V",
            QJniObject::fromString(name).object<jstring>(), QJniObject::fromString(json).object<jstring>());
    });
}

void AndroidEbookRenderer::next() { command(QStringLiteral("next")); }
void AndroidEbookRenderer::prev() { command(QStringLiteral("prev")); }
void AndroidEbookRenderer::goTo(const QVariant& value) { command(QStringLiteral("goTo"), {value}); }
void AndroidEbookRenderer::setAppearance(const QVariant& value) { command(QStringLiteral("setAppearance"), {jsonString(value)}); }
void AndroidEbookRenderer::search(const QString& value) { command(QStringLiteral("search"), {value}); }
void AndroidEbookRenderer::clearSearch() { command(QStringLiteral("clearSearch")); }
void AndroidEbookRenderer::addHighlight(const QVariant& value) { command(QStringLiteral("addHighlight"), {jsonString(value)}); }
void AndroidEbookRenderer::removeHighlight(const QString& value) { command(QStringLiteral("removeHighlight"), {value}); }
void AndroidEbookRenderer::clearSelection() { command(QStringLiteral("clearSelection")); }
void AndroidEbookRenderer::setReadAlongStyle(const QVariant& value) { command(QStringLiteral("setReadAlongStyle"), {jsonString(value)}); }
void AndroidEbookRenderer::paintReadAlong(const QVariant& value) { command(QStringLiteral("paintReadAlong"), {jsonString(value)}); }
void AndroidEbookRenderer::clearReadAlong() { command(QStringLiteral("clearReadAlong")); }
void AndroidEbookRenderer::ensureReadAlongVisible(const QVariant& value) { command(QStringLiteral("ensureReadAlongVisible"), {jsonString(value)}); }
void AndroidEbookRenderer::navigateReadAlong(const QVariant& value) { command(QStringLiteral("navigateReadAlong"), {jsonString(value)}); }
void AndroidEbookRenderer::focusPaper() { command(QStringLiteral("focusPaper")); }

void AndroidEbookRenderer::configureOverlay(QWindow* window)
{
    if (auto* quick = qobject_cast<QQuickWindow*>(window))
        Colosseum::Platform::installAndroidGraphicsWorkarounds(quick);
}
