#include "PlatformRuntime.h"

#include <QEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>
#include <QWindow>

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniObject>
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
    if (!m_window) {
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
}

void Runtime::dismissKeyboard() {
    if (qGuiApp && qGuiApp->inputMethod())
        qGuiApp->inputMethod()->hide();
}

void Runtime::dispatchSystemBack() {
    if (m_capabilities.systemBack)
        emit backRequested();
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
