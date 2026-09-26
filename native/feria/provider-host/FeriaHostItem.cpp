#include "FeriaHostItem.h"

#include "FeriaProviderHost.h"

#define NOMINMAX
#include <windows.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>

#include <cmath>

namespace {

QJsonObject rectJson(const QRect &rect)
{
    return {
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()}
    };
}

QJsonObject rectFJson(const QRectF &rect)
{
    return {
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()}
    };
}

} // namespace

FeriaHostItem::FeriaHostItem(QQuickItem *parent)
    : QQuickItem(parent)
    , m_host(new FeriaProviderHost(this))
{
    setFlag(ItemHasContents, false);
    setActiveFocusOnTab(true);

    connect(this, &QQuickItem::xChanged,
            this, &FeriaHostItem::syncHost);
    connect(this, &QQuickItem::yChanged,
            this, &FeriaHostItem::syncHost);
    connect(this, &QQuickItem::widthChanged,
            this, &FeriaHostItem::syncHost);
    connect(this, &QQuickItem::heightChanged,
            this, &FeriaHostItem::syncHost);
    connect(this, &QQuickItem::visibleChanged,
            this, &FeriaHostItem::syncHost);
    connect(this, &QQuickItem::windowChanged,
            this, &FeriaHostItem::attachWindow);

    connect(m_host, &FeriaProviderHost::readyChanged,
            this, [this](bool) {
                syncHost();
                emit readyChanged();
            });
    connect(m_host, &FeriaProviderHost::navigationBlocked,
            this, &FeriaHostItem::navigationBlocked);
    connect(m_host, &FeriaProviderHost::navigationCompleted,
            this, &FeriaHostItem::navigationCompleted);
    connect(m_host, &FeriaProviderHost::scriptResult,
            this, &FeriaHostItem::scriptResult);
    connect(m_host, &FeriaProviderHost::observerMessage,
            this, &FeriaHostItem::observerMessage);
    connect(m_host, &FeriaProviderHost::popupOpened,
            this, &FeriaHostItem::popupOpened);
    connect(m_host, &FeriaProviderHost::hostLog,
            this, &FeriaHostItem::hostLog);
    connect(m_host, &FeriaProviderHost::escapeRequested,
            this, &FeriaHostItem::returnToQml);
}

FeriaHostItem::~FeriaHostItem() = default;

QString FeriaHostItem::userDataFolder() const
{
    return m_userDataFolder;
}

void FeriaHostItem::setUserDataFolder(const QString &path)
{
    if (m_userDataFolder == path)
        return;
    m_userDataFolder = path;
    emit userDataFolderChanged();
    ensureHost();
}

QUrl FeriaHostItem::url() const
{
    return m_url;
}

void FeriaHostItem::setUrl(const QUrl &url)
{
    if (m_url == url)
        return;
    m_url = url;
    emit urlChanged();
    if (m_host->isReady())
        m_host->navigate(m_url);
    else
        ensureHost();
}

bool FeriaHostItem::suppressed() const
{
    return m_suppressed;
}

void FeriaHostItem::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed)
        return;
    m_suppressed = suppressed;
    emit suppressedChanged();
    syncHost();
}

bool FeriaHostItem::ready() const
{
    return m_host->isReady();
}

void FeriaHostItem::componentComplete()
{
    QQuickItem::componentComplete();
    m_complete = true;
    attachWindow(window());
    ensureHost();
}

void FeriaHostItem::attachWindow(QQuickWindow *quickWindow)
{
    if (!quickWindow || m_attachedWindow == quickWindow)
        return;

    if (m_attachedWindow)
        disconnect(m_attachedWindow, nullptr, this, nullptr);
    m_attachedWindow = quickWindow;

    connect(quickWindow, &QQuickWindow::widthChanged,
            this, &FeriaHostItem::syncHost);
    connect(quickWindow, &QQuickWindow::heightChanged,
            this, &FeriaHostItem::syncHost);
    connect(quickWindow, &QQuickWindow::visibilityChanged,
            this, &FeriaHostItem::syncHost);
    connect(quickWindow, &QQuickWindow::screenChanged,
            this, [this](QScreen *) { syncHost(); });

    ensureHost();
}

void FeriaHostItem::ensureHost()
{
    if (!m_complete || !window() || m_userDataFolder.isEmpty()
        || !m_url.isValid() || m_url.isEmpty() || m_host->isReady())
        return;

    const quintptr nativeId = window()->winId();
    if (!nativeId)
        return;

    syncHost();
    m_host->initialize(nativeId, m_userDataFolder, m_url);
}

void FeriaHostItem::syncHost()
{
    QQuickWindow *quickWindow = window();
    if (!quickWindow)
        return;

    const HWND hwnd = reinterpret_cast<HWND>(quickWindow->winId());
    if (!hwnd)
        return;

    RECT client{};
    if (!GetClientRect(hwnd, &client))
        return;

    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0
        || quickWindow->width() <= 0 || quickWindow->height() <= 0)
        return;

    const qreal scaleX =
        static_cast<qreal>(clientWidth) / quickWindow->width();
    const qreal scaleY =
        static_cast<qreal>(clientHeight) / quickWindow->height();
    const QPointF topLeft = mapToScene(QPointF(0.0, 0.0));

    QRect physicalRect(
        qRound(topLeft.x() * scaleX),
        qRound(topLeft.y() * scaleY),
        qRound(width() * scaleX),
        qRound(height() * scaleY));

    physicalRect = physicalRect.intersected(
        QRect(0, 0, clientWidth, clientHeight));

    m_host->setPhysicalBounds(physicalRect);
    m_host->setVisible(hostShouldBeVisible());
}

bool FeriaHostItem::hostShouldBeVisible() const
{
    return m_complete && window() && isVisible()
        && window()->isVisible() && !m_suppressed;
}

void FeriaHostItem::focusWebView()
{
    ensureHost();
    setFocus(false);
    if (m_host->isReady())
        m_host->focusWebView();
}

void FeriaHostItem::returnToQml()
{
    if (!window())
        return;

    m_host->pauseMedia(QStringLiteral("escape-pause"));
    const HWND hwnd = reinterpret_cast<HWND>(window()->winId());
    if (hwnd) {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
    window()->requestActivate();
    forceActiveFocus(Qt::OtherFocusReason);
    emit returnedToQml();

    QTimer::singleShot(0, this, [this]() {
        emit qmlFocusRestored(hasActiveFocus());
    });
}

void FeriaHostItem::resumeWebView()
{
    focusWebView();
}

void FeriaHostItem::navigate(const QUrl &url)
{
    if (!url.isValid())
        return;
    if (m_host->isReady())
        m_host->navigate(url);
}

void FeriaHostItem::executeScript(const QString &label,
                                  const QString &script)
{
    if (m_host->isReady())
        m_host->executeScript(label, script);
}

QString FeriaHostItem::captureGeometry(const QString &label)
{
    QQuickWindow *quickWindow = window();
    if (!quickWindow)
        return QStringLiteral("{}");

    const HWND hwnd = reinterpret_cast<HWND>(quickWindow->winId());
    RECT client{};
    GetClientRect(hwnd, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    const qreal scaleX = quickWindow->width() > 0
        ? static_cast<qreal>(clientWidth) / quickWindow->width()
        : 0.0;
    const qreal scaleY = quickWindow->height() > 0
        ? static_cast<qreal>(clientHeight) / quickWindow->height()
        : 0.0;

    const QPointF topLeft = mapToScene(QPointF(0.0, 0.0));
    const QRectF qmlRect(topLeft, QSizeF(width(), height()));
    QRect expected(
        qRound(qmlRect.x() * scaleX),
        qRound(qmlRect.y() * scaleY),
        qRound(qmlRect.width() * scaleX),
        qRound(qmlRect.height() * scaleY));
    expected = expected.intersected(
        QRect(0, 0, clientWidth, clientHeight));

    const QRect controller = m_host->physicalBounds();
    const QRect child = m_host->nativeChildBounds();

    QJsonObject json{
        {QStringLiteral("label"), label},
        {QStringLiteral("pid"), static_cast<qint64>(GetCurrentProcessId())},
        {QStringLiteral("windowVisibility"),
         static_cast<int>(quickWindow->visibility())},
        {QStringLiteral("devicePixelRatio"), quickWindow->devicePixelRatio()},
        {QStringLiteral("scaleX"), scaleX},
        {QStringLiteral("scaleY"), scaleY},
        {QStringLiteral("qmlLogicalRect"), rectFJson(qmlRect)},
        {QStringLiteral("nativeClientRect"),
         rectJson(QRect(0, 0, clientWidth, clientHeight))},
        {QStringLiteral("expectedPhysicalRect"), rectJson(expected)},
        {QStringLiteral("controllerBounds"), rectJson(controller)},
        {QStringLiteral("nativeChildBounds"), rectJson(child)},
        {QStringLiteral("nativeChildClass"), m_host->nativeChildClassName()},
        {QStringLiteral("controllerExact"), controller == expected},
        {QStringLiteral("nativeChildExact"), child == expected},
        {QStringLiteral("hostVisible"), m_host->isVisible()},
        {QStringLiteral("suppressed"), m_suppressed},
        {QStringLiteral("qmlActiveFocus"), hasActiveFocus()}
    };

    const QString result = QString::fromUtf8(
        QJsonDocument(json).toJson(QJsonDocument::Compact));
    emit geometryCaptured(result);
    return result;
}
