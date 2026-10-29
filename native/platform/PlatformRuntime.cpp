#include "PlatformRuntime.h"

#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#include <cmath>

#if defined(Q_OS_ANDROID)
#include "AndroidSurfaceRedrawGate.h"
#include <QCoreApplication>
#include <QJniObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <atomic>

namespace Colosseum::Platform {
struct AndroidRedrawState {
    QMutex mutex;
    AndroidSurfaceRedrawGate gate;
    QSize framePixels;
};
}

namespace {
QMutex backBridgeMutex;
QHash<qint64, QPointer<Colosseum::Platform::Runtime>> backRuntimes;
std::atomic<qint64> nextBackBridgeHandle{1};
void attachAndroidBackBridge(qint64 handle) {
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([handle]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return false;
        activity.callMethod<void>("setBackBridgeHandle", "(J)V", jlong(handle));
        return true;
    });
}
void acknowledgeSurfaceRedraw(qint64 token) {
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([token]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) activity.callMethod<void>("onSurfaceRedrawComplete", "(J)V", jlong(token));
        return {};
    });
}
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_QtSurfaceRedrawGate_nativeRequestRedraw(
    JNIEnv *, jclass, jlong handle, jlong token, jint width, jint height) {
    QMutexLocker lock(&backBridgeMutex);
    const auto runtime = backRuntimes.value(handle);
    if (runtime) QMetaObject::invokeMethod(runtime, [runtime, token, width, height] {
        if (runtime) runtime->requestSurfaceRedraw(token, width, height);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_ColosseumActivity_nativeBackGesture(
    JNIEnv *, jclass, jlong handle, jint phase, jfloat progress, jint edge) {
    QMutexLocker lock(&backBridgeMutex);
    const auto runtime = backRuntimes.value(handle);
    if (runtime) QMetaObject::invokeMethod(runtime, [runtime, phase, progress, edge] {
        if (runtime) runtime->acceptBackGesture(phase, progress, edge);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_ColosseumActivity_nativePlaybackBrightness(
    JNIEnv *, jclass, jlong handle, jfloat value) {
    QMutexLocker lock(&backBridgeMutex);
    const auto runtime = backRuntimes.value(handle);
    if (runtime) QMetaObject::invokeMethod(runtime, [runtime, value] {
        if (runtime) runtime->acceptPlaybackBrightness(value);
    }, Qt::QueuedConnection);
}

extern "C" JNIEXPORT void JNICALL
Java_org_colosseum_platform_ColosseumActivity_nativeFontScale(
    JNIEnv *, jclass, jlong handle, jfloat value) {
    QMutexLocker lock(&backBridgeMutex);
    const auto runtime = backRuntimes.value(handle);
    if (runtime) QMetaObject::invokeMethod(runtime, [runtime, value] {
        if (runtime) runtime->acceptFontScale(value);
    }, Qt::QueuedConnection);
}
#endif

namespace Colosseum::Platform {
namespace {

bool detectAndroidTelevision() {
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return false;

    const QJniObject serviceName = QJniObject::fromString(QStringLiteral("uimode"));
    const QJniObject uiModeManager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        serviceName.object<jstring>());
    if (uiModeManager.isValid()
        && uiModeManager.callMethod<jint>("getCurrentModeType", "()I") == 4) {
        return true;
    }

    const QJniObject packageManager = context.callObjectMethod(
        "getPackageManager", "()Landroid/content/pm/PackageManager;");
    if (!packageManager.isValid())
        return false;
    const QJniObject leanback = QJniObject::fromString(
        QStringLiteral("android.software.leanback"));
    return packageManager.callMethod<jboolean>(
        "hasSystemFeature", "(Ljava/lang/String;)Z", leanback.object<jstring>());
#else
    return false;
#endif
}

QString stateName(Qt::ApplicationState state) {
    switch (state) {
    case Qt::ApplicationActive:
        return QStringLiteral("active");
    case Qt::ApplicationInactive:
        return QStringLiteral("inactive");
    case Qt::ApplicationSuspended:
        return QStringLiteral("suspended");
    case Qt::ApplicationHidden:
        return QStringLiteral("hidden");
    }
    return QStringLiteral("unknown");
}

} // namespace

Runtime::Runtime(QObject *parent, Kind kind)
    : QObject(parent),
      m_kind(kind),
      m_capabilities(capabilitiesFor(kind)),
      m_androidTelevision(kind == Kind::Android && detectAndroidTelevision()) {
#if defined(Q_OS_ANDROID)
    if (m_kind == Kind::Android) {
        m_backBridgeHandle = nextBackBridgeHandle.fetch_add(1);
        QMutexLocker lock(&backBridgeMutex);
        backRuntimes.insert(m_backBridgeHandle, this);
    }
#endif
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged,
                this, [this](Qt::ApplicationState) {
                    emit applicationStateChanged();
                });
        if (QInputMethod *inputMethod = qGuiApp->inputMethod()) {
            connect(inputMethod, &QInputMethod::visibleChanged,
                    this, &Runtime::keyboardVisibleChanged);
        }
    }
}

Runtime::~Runtime() {
    attachWindow(nullptr);
#if defined(Q_OS_ANDROID)
    QMutexLocker lock(&backBridgeMutex);
    backRuntimes.remove(m_backBridgeHandle);
#endif
}

QString Runtime::kind() const {
    return QString::fromLatin1(kindName(m_kind));
}

bool Runtime::android() const {
    return m_kind == Kind::Android;
}

bool Runtime::androidTelevision() const {
    return m_androidTelevision;
}

QVariantMap Runtime::videoDecodeCapabilities() const {
    if (!m_videoDecodeCapabilities.isEmpty())
        return m_videoDecodeCapabilities;

    // Defer the one-time OS enumeration until sources first need it.
    m_videoDecodeCapabilities = {{QStringLiteral("available"), false},
                                {QStringLiteral("complete"), false},
                                {QStringLiteral("decoders"), QVariantList{}}};
#if defined(Q_OS_ANDROID)
    if (m_kind != Kind::Android)
        return m_videoDecodeCapabilities;
    const QJniObject payload = QJniObject::callStaticObjectMethod(
        "org/colosseum/platform/DeviceVideoCapabilities", "snapshot", "()Ljava/lang/String;");
    if (!payload.isValid())
        return m_videoDecodeCapabilities;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toString().toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("available")).isBool()
                && object.value(QStringLiteral("complete")).isBool()
                && object.value(QStringLiteral("decoders")).isArray()) {
            m_videoDecodeCapabilities = object.toVariantMap();
        }
    }
#endif
    return m_videoDecodeCapabilities;
}

QString Runtime::applicationState() const {
    return qGuiApp ? stateName(qGuiApp->applicationState())
                   : QStringLiteral("unknown");
}

bool Runtime::foreground() const {
    if (!qGuiApp)
        return false;
    const Qt::ApplicationState state = qGuiApp->applicationState();
    return state == Qt::ApplicationActive || state == Qt::ApplicationInactive;
}

bool Runtime::keyboardVisible() const {
    return qGuiApp && qGuiApp->inputMethod()
        ? qGuiApp->inputMethod()->isVisible()
        : false;
}

QMargins Runtime::safeAreaMargins() const {
    return m_safeAreaMargins;
}

bool Runtime::surfaceAvailable() const {
    return m_surfaceAvailable;
}

bool Runtime::storageAccessFrameworkAvailable() const {
    return m_capabilities.storageAccessFramework;
}

bool Runtime::backgroundDownloadNotificationsAvailable() const {
    return m_capabilities.backgroundDownloadNotifications;
}

bool Runtime::desktopUpdaterAvailable() const {
    return m_capabilities.desktopUpdater;
}

bool Runtime::desktopWindowChromeAvailable() const {
    return m_capabilities.desktopWindowChrome;
}

void Runtime::attachWindow(QWindow *window) {
    if (m_window == window)
        return;

    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }

    m_window = window;
    m_androidRedraw.reset();
#if defined(Q_OS_ANDROID)
    if (m_kind == Kind::Android) attachAndroidBackBridge(window ? m_backBridgeHandle : 0);
#endif
    if (!m_window) {
        acceptBackGesture(2, 0, 0);
        if (!m_safeAreaMargins.isNull()) {
            m_safeAreaMargins = {};
            emit safeAreaMarginsChanged();
        }
        setSurfaceAvailable(false);
        return;
    }

    m_window->installEventFilter(this);
    connect(m_window, &QWindow::safeAreaMarginsChanged,
            this, [this](const QMargins &) { refreshSafeArea(); });
    refreshSafeArea();
    setSurfaceAvailable(m_window->handle() != nullptr);
#if defined(Q_OS_ANDROID)
    if (auto *quick = qobject_cast<QQuickWindow *>(window)) {
        auto state = std::make_shared<AndroidRedrawState>();
        m_androidRedraw = state;
        connect(quick, &QQuickWindow::beforeSynchronizing, this, [quick, state] {
            // The GUI thread is blocked here, so this is the geometry of this frame.
            QMutexLocker lock(&state->mutex);
            const auto dpr = quick->devicePixelRatio();
            state->framePixels = QSize(qRound(quick->width() * dpr), qRound(quick->height() * dpr));
        }, Qt::DirectConnection);
        connect(quick, &QQuickWindow::frameSwapped, this, [this, state] {
            AndroidSurfaceRedrawGate::Result result;
            {
                QMutexLocker lock(&state->mutex);
                result = state->gate.presented(state->framePixels.width(), state->framePixels.height());
            }
            if (result.action == AndroidSurfaceRedrawGate::Action::None) return;
            QMetaObject::invokeMethod(this, [this, state, result] {
                if (state != m_androidRedraw) return;
                {
                    QMutexLocker lock(&state->mutex);
                    if (!state->gate.isCurrent(result.token)) return;
                }
                if (result.action == AndroidSurfaceRedrawGate::Action::Complete)
                    acknowledgeSurfaceRedraw(result.token);
                else if (auto *quick = qobject_cast<QQuickWindow *>(m_window.data()))
                    quick->update();
            }, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    }
#endif
}

void Runtime::requestSurfaceRedraw(qint64 token, int width, int height) {
#if defined(Q_OS_ANDROID)
    if (!m_androidRedraw) {
        if (token > 0) acknowledgeSurfaceRedraw(token);
        return;
    }
    {
        QMutexLocker lock(&m_androidRedraw->mutex);
        m_androidRedraw->gate.begin(token, width, height);
    }
    if (token > 0) {
        if (auto *quick = qobject_cast<QQuickWindow *>(m_window.data())) quick->update();
    }
#else
    Q_UNUSED(token)
    Q_UNUSED(width)
    Q_UNUSED(height)
#endif
}

void Runtime::dismissKeyboard() {
    if (qGuiApp && qGuiApp->inputMethod())
        qGuiApp->inputMethod()->hide();
}

void Runtime::dispatchSystemBack() {
    if (m_capabilities.systemBack)
        emit backRequested();
}

void Runtime::acceptBackGesture(int phase, qreal progress, int edge) {
    if (!m_capabilities.systemBack || phase < 0 || phase > 3) return;
    if (phase == 1 && (!m_backGestureActive || !std::isfinite(progress))) return;
    if (phase == 0 || phase == 1) {
        m_backGestureActive = true;
        m_backGestureProgress = std::isfinite(progress) ? qBound(qreal(0), progress, qreal(1)) : 0;
        m_backGestureEdge = edge == 1 ? 1 : 0;
    } else {
        m_backGestureActive = false;
        m_backGestureProgress = 0;
    }
    emit backGestureChanged();
    if (phase == 0) qInfo("[platform] Back gesture started");
    if (phase == 2) qInfo("[platform] Back gesture cancelled");
    if (phase == 3) {
        qInfo("[platform] Back gesture committed");
        dispatchSystemBack();
    }
}

void Runtime::acceptPlaybackBrightness(qreal value) {
    if (m_kind != Kind::Android || !std::isfinite(value)) return;
    m_playbackBrightness = qBound(qreal(0.01), value, qreal(1));
    emit playbackBrightnessChanged();
}

void Runtime::acceptFontScale(qreal value) {
    if (m_kind != Kind::Android || !std::isfinite(value) || value <= 0) return;
    const auto scale = qBound(qreal(0.5), value, qreal(3));
    if (qFuzzyCompare(scale, m_fontScale)) return;
    m_fontScale = scale;
    emit fontScaleChanged();
}

void Runtime::beginPlaybackBrightness() {
#if defined(Q_OS_ANDROID)
    if (m_kind != Kind::Android) return;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) activity.callMethod<void>("beginPlaybackBrightness", "()V");
        return {};
    });
#endif
}

void Runtime::setPlaybackBrightness(qreal value) {
    if (m_kind != Kind::Android || !std::isfinite(value)) return;
    acceptPlaybackBrightness(value);
#if defined(Q_OS_ANDROID)
    const auto level = m_playbackBrightness;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([level]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) activity.callMethod<void>("setPlaybackBrightness", "(F)V", jfloat(level));
        return {};
    });
#endif
}

void Runtime::restorePlaybackBrightness() {
#if defined(Q_OS_ANDROID)
    if (m_kind != Kind::Android) return;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) return false;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid()) activity.callMethod<void>("restorePlaybackBrightness", "()V");
        return {};
    });
#endif
}

void Runtime::backgroundApplication() {
#if defined(Q_OS_ANDROID)
    if (m_kind != Kind::Android)
        return;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() -> QVariant {
        if (!QNativeInterface::QAndroidApplication::isActivityContext()) {
            qWarning("[platform] Cannot background Android task without an activity");
            return false;
        }
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
            return false;
        const bool moved = activity.callMethod<jboolean>(
            "moveTaskToBack", "(Z)Z", jboolean(true));
        if (moved)
            qInfo("[platform] Android task sent to background");
        else
            qWarning("[platform] Android declined to background the task");
        return moved;
    });
#endif
}

bool Runtime::requestStorageAccess(const QString &purpose) {
    if (!m_capabilities.storageAccessFramework)
        return false;
    emit storageAccessRequested(purpose.trimmed());
    return true;
}

bool Runtime::requestPermission(const QString &permission) {
    const QString normalized = permission.trimmed();
    if (m_kind != Kind::Android || normalized.isEmpty())
        return false;
    emit permissionRequested(normalized);
    return true;
}

bool Runtime::eventFilter(QObject *watched, QEvent *event) {
    if (watched != m_window || !event)
        return QObject::eventFilter(watched, event);

    if (m_capabilities.systemBack && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Back) {
            keyEvent->accept();
            emit backRequested();
            return true;
        }
    }

    if (event->type() == QEvent::SafeAreaMarginsChange) {
        refreshSafeArea();
    } else if (event->type() == QEvent::PlatformSurface) {
        const auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        setSurfaceAvailable(
            surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated);
    }

    return QObject::eventFilter(watched, event);
}

void Runtime::refreshSafeArea() {
    const QMargins next = m_window ? m_window->safeAreaMargins() : QMargins();
    if (next == m_safeAreaMargins)
        return;
    m_safeAreaMargins = next;
    emit safeAreaMarginsChanged();
}

void Runtime::setSurfaceAvailable(bool available) {
    if (m_surfaceAvailable == available)
        return;
    m_surfaceAvailable = available;
    emit surfaceAvailableChanged();
}

} // namespace Colosseum::Platform
