#include "FeriaProviderHost.h"
#include "observer/YouTubeObserverScript.h"

#define NOMINMAX
#include <windows.h>
#include <unknwn.h>

#include <WebView2.h>
#include <wrl.h>

#include <QDebug>
#include <QGuiApplication>
#include <QStringList>
#include <QWindow>

#include <climits>
#include <cmath>
#include <memory>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

QString takeWideString(LPWSTR value)
{
    const QString text = value ? QString::fromWCharArray(value) : QString();
    CoTaskMemFree(value);
    return text;
}

QString hresultText(HRESULT result)
{
    return QStringLiteral("0x%1")
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

struct ChildCandidate
{
    HWND hwnd = nullptr;
    QRect rect;
    QString className;
    int score = INT_MAX;
};

struct ChildSearchContext
{
    HWND parent = nullptr;
    QRect expected;
    bool preferRenderHost = false;
    ChildCandidate best;
};

BOOL CALLBACK enumChildWindowsProc(HWND child, LPARAM data)
{
    auto *context = reinterpret_cast<ChildSearchContext *>(data);
    RECT screenRect{};
    if (!GetWindowRect(child, &screenRect))
        return TRUE;

    POINT points[2] = {
        {screenRect.left, screenRect.top},
        {screenRect.right, screenRect.bottom}
    };
    MapWindowPoints(HWND_DESKTOP, context->parent, points, 2);
    const QRect rect(points[0].x, points[0].y,
                     points[1].x - points[0].x,
                     points[1].y - points[0].y);

    wchar_t classBuffer[256]{};
    GetClassNameW(child, classBuffer, 256);
    const QString className = QString::fromWCharArray(classBuffer);

    const QRect expected = context->expected;
    int score = std::abs(rect.x() - expected.x())
        + std::abs(rect.y() - expected.y())
        + std::abs(rect.width() - expected.width())
        + std::abs(rect.height() - expected.height());

    if (context->preferRenderHost) {
        if (className.contains(QStringLiteral("Chrome_RenderWidgetHost"),
                               Qt::CaseInsensitive))
            score -= 20000;
        else if (className.contains(QStringLiteral("Chrome_WidgetWin"),
                                    Qt::CaseInsensitive))
            score -= 10000;
    } else {
        if (className.contains(QStringLiteral("Chrome_WidgetWin"),
                               Qt::CaseInsensitive))
            score -= 20000;
        else if (className.contains(QStringLiteral("Chrome_RenderWidgetHost"),
                                    Qt::CaseInsensitive))
            score -= 10000;
    }
    if (!IsWindowVisible(child))
        score += 1000;

    if (score < context->best.score) {
        context->best.hwnd = child;
        context->best.rect = rect;
        context->best.className = className;
        context->best.score = score;
    }
    return TRUE;
}

} // namespace

struct FeriaProviderHost::Impl
{
    struct PopupState
    {
        std::unique_ptr<QWindow> window;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> webView;
    };

    explicit Impl(FeriaProviderHost *owner)
        : q(owner)
    {
    }

    FeriaProviderHost *q = nullptr;
    HWND parentWindow = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webView;
    QRect requestedBounds;
    QStringList allowedDomains;
    std::vector<std::unique_ptr<PopupState>> popups;
    bool initializing = false;
    bool ready = false;
    bool visible = true;
    bool youtubeObserver = false;

    void log(const QString &message) const
    {
        qInfo().noquote() << "FERIA_A1_HOST" << message;
        emit q->hostLog(message);
    }

    void configureAllowlist(const QUrl &initialUrl)
    {
        allowedDomains.clear();
        youtubeObserver = false;
        const QString host = initialUrl.host().toLower();
        if (host == QStringLiteral("netflix.com")
            || host.endsWith(QStringLiteral(".netflix.com"))) {
            allowedDomains << QStringLiteral("netflix.com");
        } else if (host == QStringLiteral("youtube.com")
                   || host.endsWith(QStringLiteral(".youtube.com"))
                   || host == QStringLiteral("youtu.be")) {
            youtubeObserver = true;
            allowedDomains << QStringLiteral("youtube.com")
                           << QStringLiteral("youtu.be")
                           << QStringLiteral("google.com");
        } else if (!host.isEmpty()) {
            allowedDomains << host;
        }
        log(QStringLiteral("ALLOWLIST provider=%1 domains=%2")
                .arg(host, allowedDomains.join(QLatin1Char(','))));
    }

    bool isAllowed(const QString &rawUri) const
    {
        if (rawUri == QStringLiteral("about:blank"))
            return true;

        const QUrl url(rawUri);
        const QString scheme = url.scheme().toLower();
        if (scheme != QStringLiteral("https")
            && scheme != QStringLiteral("http"))
            return false;

        const QString host = url.host().toLower();
        for (const QString &domain : allowedDomains) {
            if (host == domain
                || host.endsWith(QStringLiteral(".") + domain))
                return true;
        }
        return false;
    }

    void reportBlocked(const QString &uri)
    {
        log(QStringLiteral("NAV_BLOCKED uri=%1").arg(uri));
        emit q->navigationBlocked(uri);
    }

    void installNavigationPolicy(ICoreWebView2 *view)
    {
        EventRegistrationToken token{};
        view->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2 *,
                       ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    if (FAILED(args->get_Uri(&raw)))
                        return S_OK;
                    const QString uri = takeWideString(raw);
                    if (!isAllowed(uri)) {
                        args->put_Cancel(TRUE);
                        reportBlocked(uri);
                    }
                    return S_OK;
                }).Get(),
            &token);

        view->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, view](ICoreWebView2 *,
                             ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    LPWSTR raw = nullptr;
                    view->get_Source(&raw);
                    const QString uri = takeWideString(raw);
                    log(QStringLiteral("NAV_COMPLETE success=%1 uri=%2")
                            .arg(success ? QStringLiteral("true")
                                         : QStringLiteral("false"),
                                 uri));
                    emit q->navigationCompleted(uri, success != FALSE);
                    return S_OK;
                }).Get(),
            &token);
    }

    HRESULT openOwnedPopup(ICoreWebView2NewWindowRequestedEventArgs *args)
    {
        LPWSTR raw = nullptr;
        if (FAILED(args->get_Uri(&raw))) {
            args->put_Handled(TRUE);
            return S_OK;
        }

        const QString uri = takeWideString(raw);
        if (!isAllowed(uri)) {
            args->put_Handled(TRUE);
            reportBlocked(uri);
            log(QStringLiteral("POPUP_BLOCKED uri=%1").arg(uri));
            return S_OK;
        }

        ComPtr<ICoreWebView2Deferral> deferral;
        if (FAILED(args->GetDeferral(&deferral))) {
            args->put_Handled(TRUE);
            log(QStringLiteral("POPUP_FAILED no-deferral uri=%1").arg(uri));
            return S_OK;
        }

        ComPtr<ICoreWebView2NewWindowRequestedEventArgs> argsRef(args);
        auto popup = std::make_unique<PopupState>();
        popup->window = std::make_unique<QWindow>();
        popup->window->setTitle(QStringLiteral("Feria Provider Popup"));
        popup->window->resize(720, 480);
        popup->window->show();
        popup->window->requestActivate();

        PopupState *popupRaw = popup.get();
        const HWND popupHwnd =
            reinterpret_cast<HWND>(popup->window->winId());
        popups.push_back(std::move(popup));

        const HRESULT createResult =
            environment->CreateCoreWebView2Controller(
                popupHwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, popupRaw, popupHwnd, argsRef, deferral, uri](
                        HRESULT result,
                        ICoreWebView2Controller *newController) -> HRESULT {
                        if (FAILED(result) || !newController) {
                            argsRef->put_Handled(TRUE);
                            deferral->Complete();
                            if (popupRaw->window)
                                popupRaw->window->hide();
                            log(QStringLiteral("POPUP_FAILED controller=%1 uri=%2")
                                    .arg(hresultText(result), uri));
                            return S_OK;
                        }

                        popupRaw->controller = newController;
                        popupRaw->controller->get_CoreWebView2(
                            &popupRaw->webView);
                        RECT client{};
                        GetClientRect(popupHwnd, &client);
                        popupRaw->controller->put_Bounds(client);
                        installNavigationPolicy(popupRaw->webView.Get());

                        EventRegistrationToken popupToken{};
                        popupRaw->webView->add_NewWindowRequested(
                            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [this](ICoreWebView2 *,
                                       ICoreWebView2NewWindowRequestedEventArgs *nested)
                                    -> HRESULT {
                                    return openOwnedPopup(nested);
                                }).Get(),
                            &popupToken);

                        argsRef->put_NewWindow(popupRaw->webView.Get());
                        argsRef->put_Handled(TRUE);
                        deferral->Complete();
                        log(QStringLiteral("POPUP_HOSTED uri=%1").arg(uri));
                        emit q->popupOpened(uri);
                        return S_OK;
                    }).Get());

        if (FAILED(createResult)) {
            argsRef->put_Handled(TRUE);
            deferral->Complete();
            if (popupRaw->window)
                popupRaw->window->hide();
            log(QStringLiteral("POPUP_FAILED create-call=%1 uri=%2")
                    .arg(hresultText(createResult), uri));
        }
        return S_OK;
    }

    void installMainEvents()
    {
        installNavigationPolicy(webView.Get());

        EventRegistrationToken token{};
        if (youtubeObserver) {
            webView->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [this](ICoreWebView2 *,
                           ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                        LPWSTR raw = nullptr;
                        if (SUCCEEDED(args->get_WebMessageAsJson(&raw)))
                            emit q->observerMessage(takeWideString(raw));
                        return S_OK;
                    }).Get(),
                &token);
        }
        webView->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this](ICoreWebView2 *,
                       ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                    return openOwnedPopup(args);
                }).Get(),
            &token);

        controller->add_GotFocus(
            Callback<ICoreWebView2FocusChangedEventHandler>(
                [this](ICoreWebView2Controller *, IUnknown *) -> HRESULT {
                    log(QStringLiteral("WEBVIEW_GOT_FOCUS"));
                    return S_OK;
                }).Get(),
            &token);

        controller->add_LostFocus(
            Callback<ICoreWebView2FocusChangedEventHandler>(
                [this](ICoreWebView2Controller *, IUnknown *) -> HRESULT {
                    log(QStringLiteral("WEBVIEW_LOST_FOCUS"));
                    return S_OK;
                }).Get(),
            &token);

        controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this](ICoreWebView2Controller *,
                       ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT {
                    UINT virtualKey = 0;
                    COREWEBVIEW2_KEY_EVENT_KIND kind{};
                    args->get_VirtualKey(&virtualKey);
                    args->get_KeyEventKind(&kind);
                    log(QStringLiteral("WEBVIEW_KEY vk=%1 kind=%2")
                            .arg(virtualKey)
                            .arg(static_cast<int>(kind)));
                    if (virtualKey == VK_ESCAPE
                        && (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                            || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)) {
                        args->put_Handled(TRUE);
                        log(QStringLiteral("ESCAPE_FROM_WEBVIEW"));
                        emit q->escapeRequested();
                    }
                    return S_OK;
                }).Get(),
            &token);
    }

    ChildCandidate bestNativeChild(bool preferRenderHost = false) const
    {
        ChildSearchContext context;
        context.parent = parentWindow;
        context.expected = requestedBounds;
        context.preferRenderHost = preferRenderHost;
        EnumChildWindows(parentWindow, enumChildWindowsProc,
                         reinterpret_cast<LPARAM>(&context));
        return context.best;
    }
};

FeriaProviderHost::FeriaProviderHost(QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this))
{
}

FeriaProviderHost::~FeriaProviderHost()
{
    if (m_impl->controller)
        m_impl->controller->Close();
    for (const auto &popup : m_impl->popups) {
        if (popup->controller)
            popup->controller->Close();
    }
}

void FeriaProviderHost::initialize(quintptr parentWindowId,
                                   const QString &userDataFolder,
                                   const QUrl &initialUrl)
{
    if (m_impl->initializing || m_impl->environment
        || m_impl->controller || m_impl->ready)
        return;

    m_impl->initializing = true;
    m_impl->parentWindow = reinterpret_cast<HWND>(parentWindowId);
    m_impl->configureAllowlist(initialUrl);
    const std::wstring profile = userDataFolder.toStdWString();

    m_impl->log(QStringLiteral("ENV_CREATE parent=0x%1 profile=%2")
                    .arg(parentWindowId, 0, 16)
                    .arg(userDataFolder));

    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        profile.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, initialUrl](HRESULT environmentResult,
                               ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(environmentResult) || !environment) {
                    m_impl->log(QStringLiteral("ENV_FAILED hr=%1")
                                    .arg(hresultText(environmentResult)));
                    return S_OK;
                }

                m_impl->environment = environment;
                return m_impl->environment->CreateCoreWebView2Controller(
                    m_impl->parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, initialUrl](
                            HRESULT controllerResult,
                            ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                m_impl->log(
                                    QStringLiteral("CONTROLLER_FAILED hr=%1")
                                        .arg(hresultText(controllerResult)));
                                return S_OK;
                            }

                            m_impl->controller = controller;
                            if (FAILED(m_impl->controller->get_CoreWebView2(
                                    &m_impl->webView))
                                || !m_impl->webView) {
                                m_impl->log(
                                    QStringLiteral("WEBVIEW_FAILED get_CoreWebView2"));
                                return S_OK;
                            }

                            m_impl->installMainEvents();
                            setPhysicalBounds(m_impl->requestedBounds);
                            setVisible(m_impl->visible);
                            m_impl->ready = true;
                            m_impl->log(QStringLiteral("HOST_READY"));
                            emit readyChanged(true);

                            if (m_impl->youtubeObserver) {
                                const HRESULT addResult = m_impl->webView
                                    ->AddScriptToExecuteOnDocumentCreated(
                                        kYouTubeObserverScript,
                                        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                            [this, initialUrl](HRESULT scriptResult, PCWSTR) -> HRESULT {
                                                m_impl->log(QStringLiteral("OBSERVER_INSTALL hr=%1")
                                                    .arg(hresultText(scriptResult)));
                                                if (SUCCEEDED(scriptResult))
                                                    navigate(initialUrl);
                                                return S_OK;
                                            }).Get());
                                if (FAILED(addResult))
                                    m_impl->log(QStringLiteral("OBSERVER_ADD_FAILED hr=%1")
                                        .arg(hresultText(addResult)));
                            } else if (initialUrl.isValid() && !initialUrl.isEmpty()) {
                                navigate(initialUrl);
                            }
                            return S_OK;
                        }).Get());
            }).Get());

    if (FAILED(result)) {
        m_impl->log(QStringLiteral("ENV_CREATE_FAILED hr=%1")
                        .arg(hresultText(result)));
    }
}

void FeriaProviderHost::setPhysicalBounds(const QRect &bounds)
{
    m_impl->requestedBounds = bounds;
    if (!m_impl->controller)
        return;

    RECT nativeBounds{
        bounds.x(),
        bounds.y(),
        bounds.x() + bounds.width(),
        bounds.y() + bounds.height()
    };
    const HRESULT result = m_impl->controller->put_Bounds(nativeBounds);
    if (FAILED(result)) {
        m_impl->log(QStringLiteral("BOUNDS_FAILED hr=%1")
                        .arg(hresultText(result)));
    }
}

QRect FeriaProviderHost::physicalBounds() const
{
    if (!m_impl->controller)
        return m_impl->requestedBounds;

    RECT bounds{};
    if (FAILED(m_impl->controller->get_Bounds(&bounds)))
        return m_impl->requestedBounds;
    return QRect(bounds.left, bounds.top,
                 bounds.right - bounds.left,
                 bounds.bottom - bounds.top);
}

QRect FeriaProviderHost::nativeChildBounds() const
{
    if (!m_impl->parentWindow)
        return {};
    return m_impl->bestNativeChild().rect;
}

QString FeriaProviderHost::nativeChildClassName() const
{
    if (!m_impl->parentWindow)
        return {};
    return m_impl->bestNativeChild().className;
}

void FeriaProviderHost::setVisible(bool visible)
{
    m_impl->visible = visible;
    if (!m_impl->controller)
        return;

    const HRESULT result =
        m_impl->controller->put_IsVisible(visible ? TRUE : FALSE);
    if (FAILED(result)) {
        m_impl->log(QStringLiteral("VISIBILITY_FAILED hr=%1")
                        .arg(hresultText(result)));
    } else {
        m_impl->log(QStringLiteral("VISIBILITY visible=%1")
                        .arg(visible ? QStringLiteral("true")
                                     : QStringLiteral("false")));
    }
}

bool FeriaProviderHost::isVisible() const
{
    if (!m_impl->controller)
        return m_impl->visible;

    BOOL visible = FALSE;
    if (FAILED(m_impl->controller->get_IsVisible(&visible)))
        return m_impl->visible;
    return visible != FALSE;
}

bool FeriaProviderHost::isReady() const
{
    return m_impl->ready;
}

void FeriaProviderHost::focusWebView()
{
    if (!m_impl->controller)
        return;

    BringWindowToTop(m_impl->parentWindow);
    SetForegroundWindow(m_impl->parentWindow);
    SetActiveWindow(m_impl->parentWindow);
    SetFocus(m_impl->parentWindow);
    const HRESULT result =
        m_impl->controller->MoveFocus(
            COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    const ChildCandidate child = m_impl->bestNativeChild(true);
    const HWND focused = GetFocus();
    wchar_t classBuffer[256]{};
    if (focused)
        GetClassNameW(focused, classBuffer, 256);
    m_impl->log(QStringLiteral("FOCUS_WEBVIEW hr=%1 child=0x%2 class=%3 focus=0x%4 focusClass=%5")
                    .arg(hresultText(result))
                    .arg(reinterpret_cast<quintptr>(child.hwnd), 0, 16)
                    .arg(child.className)
                    .arg(reinterpret_cast<quintptr>(focused), 0, 16)
                    .arg(QString::fromWCharArray(classBuffer)));
}

void FeriaProviderHost::navigate(const QUrl &url)
{
    if (!m_impl->webView || !url.isValid())
        return;

    const QString uri = url.toString();
    if (!m_impl->isAllowed(uri)) {
        m_impl->reportBlocked(uri);
        return;
    }

    const std::wstring wideUri = uri.toStdWString();
    const HRESULT result = m_impl->webView->Navigate(wideUri.c_str());
    if (FAILED(result)) {
        m_impl->log(QStringLiteral("NAVIGATE_FAILED hr=%1 uri=%2")
                        .arg(hresultText(result), uri));
    } else {
        m_impl->log(QStringLiteral("NAVIGATE uri=%1").arg(uri));
    }
}

void FeriaProviderHost::executeScript(const QString &label,
                                      const QString &script)
{
    if (!m_impl->webView) {
        emit scriptResult(label, QStringLiteral("null"));
        return;
    }

    const std::wstring wideScript = script.toStdWString();
    const HRESULT result = m_impl->webView->ExecuteScript(
        wideScript.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, label](HRESULT scriptResult,
                          LPCWSTR resultJson) -> HRESULT {
                const QString json =
                    resultJson ? QString::fromWCharArray(resultJson)
                               : QStringLiteral("null");
                m_impl->log(
                    QStringLiteral("SCRIPT label=%1 hr=%2 result=%3")
                        .arg(label, hresultText(scriptResult), json));
                emit this->scriptResult(label, json);
                return S_OK;
            }).Get());

    if (FAILED(result)) {
        m_impl->log(QStringLiteral("SCRIPT_CALL_FAILED label=%1 hr=%2")
                        .arg(label, hresultText(result)));
    }
}

void FeriaProviderHost::pauseMedia(const QString &label)
{
    executeScript(
        label,
        QStringLiteral(
            "(()=>{const m=[...document.querySelectorAll('video,audio')];"
            "m.forEach(x=>x.pause());"
            "return {count:m.length,paused:m.every(x=>x.paused),"
            "currentTime:m[0]?m[0].currentTime:null};})()"));
}
