#include "FeriaProviderHost.h"
#include "../FeriaBrowserPolicy.h"
#include "observer/YouTubeObserverScript.h"

#define NOMINMAX
#include <windows.h>
#include <unknwn.h>

#include <WebView2.h>
#include <wrl.h>

#include <QDebug>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QJsonDocument>
#include <QJsonObject>

#include <climits>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

// Prime probes its Windows app's COM component whenever chrome.webview exists.
// In a generic WebView2 host that getter throws 0x80070490 before the web
// player can mount. Select the browser path before any Prime script executes.
constexpr wchar_t kProviderWebCompatibilityScript[] = LR"JS(
(() => {
    const host = location.hostname.toLowerCase();
    if ((host === 'primevideo.com' || host.endsWith('.primevideo.com'))
            && window.chrome && chrome.webview) {
        Object.defineProperty(chrome, 'webview', {
            value: undefined, configurable: true, writable: true
        });
    }
})();
)JS";

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

    // A retained beforeunload dialog with its native deferral. The shared
    // operation seam never sees these COM objects; adapters answer through
    // the integer nativeToken.
    struct RetainedDialog
    {
        int nativeToken = 0;
        bool forClose = false;
        bool completed = false;
        ComPtr<ICoreWebView2ScriptDialogOpeningEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
    };

    // The captured close transaction. While active, the internal
    // Navigate(about:blank) unload is part of the close: its navigation
    // events never surface as user URL/history/restore updates.
    struct CloseState
    {
        bool active = false;
        QString operationId;
        bool internalUnload = false;
        bool internalStarted = false;
        UINT64 internalNavId = 0;
        bool awaitingDeparture = false; // Leave accepted, departure not yet proven
    };
    // A cancelled internal unload whose terminal callback is still in flight;
    // it must never surface as a user-facing load outcome.
    UINT64 suppressCompletionNavId = 0;

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
    QUrl pendingUrl;
    std::vector<std::unique_ptr<PopupState>> popups;
    // NavigationStarting reports a fresh id per navigation (redirects reuse it),
    // so a NavigationCompleted with any other id belongs to a superseded load.
    UINT64 currentNavigationId = 0;
    // Stop() can surface as OPERATION_CANCELED or as the underlying network
    // error (e.g. ERR_CONNECTION_ABORTED); the flag makes either benign.
    bool stopRequested = false;
    bool initializing = false;
    bool ready = false;
    bool visible = true;
    bool youtubeObserver = false;
    QString documentTitle;
    CloseState close;
    std::map<int, RetainedDialog> dialogs;
    int dialogSerial = 0;
    // Test-only hooks, both env-gated: RED control for unsafe early disposal
    // and trusted CDP input for fixtures. Never set in production paths.
    bool unsafeEarlyDispose = qEnvironmentVariableIsSet("FERIA_UNSAFE_EARLY_DISPOSE");
    bool testInputEnabled = qEnvironmentVariableIsSet("FERIA_ENABLE_TEST_INPUT");

    void log(const QString &message) const
    {
        qInfo().noquote() << "FERIA_A1_HOST" << message;
        emit q->hostLog(message);
    }

    void resetCloseState()
    {
        close = CloseState{};
    }

    // Complete every retained dialog deferral without Accept: the renderer
    // must never wait on a host that has moved on. Draining denies unload.
    void drainDialogs()
    {
        for (auto &entry : dialogs) {
            if (entry.second.completed)
                continue;
            if (entry.second.deferral)
                entry.second.deferral->Complete();
            entry.second.completed = true;
            log(QStringLiteral("DIALOG_DRAINED token=%1 forClose=%2")
                    .arg(entry.second.nativeToken)
                    .arg(entry.second.forClose ? QStringLiteral("true")
                                               : QStringLiteral("false")));
        }
        dialogs.clear();
    }

    void invalidateClose(const QString &reason)
    {
        if (!close.active)
            return;
        const QString operationId = close.operationId;
        drainDialogs();
        resetCloseState();
        log(QStringLiteral("CLOSE_INVALIDATED reason=%1").arg(reason));
        emit q->closeInvalidated(reason);
        Q_UNUSED(operationId);
    }

    void prepareDocumentScripts(ICoreWebView2 *view, bool observeYouTube,
                                const std::function<void(bool)> &done)
    {
        std::wstring script(kProviderWebCompatibilityScript);
        if (observeYouTube) script += kYouTubeObserverScript;
        const HRESULT result = view->AddScriptToExecuteOnDocumentCreated(
            script.c_str(),
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [guard = QPointer<FeriaProviderHost>(q), done](HRESULT result, PCWSTR) -> HRESULT {
                    if (guard && FAILED(result))
                        guard->m_impl->log(QStringLiteral("DOCUMENT_SCRIPTS_FAILED hr=%1").arg(hresultText(result)));
                    done(guard && SUCCEEDED(result));
                    return S_OK;
                }).Get());
        if (FAILED(result)) {
            log(QStringLiteral("DOCUMENT_SCRIPTS_FAILED hr=%1").arg(hresultText(result)));
            done(false);
        }
    }

    bool isAllowed(const QString &rawUri) const
    {
        return FeriaBrowserPolicy::allows(QUrl(rawUri));
    }

    void reportBlocked(const QString &uri)
    {
        log(QStringLiteral("NAV_BLOCKED uri=%1").arg(QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
        emit q->navigationBlocked(uri);
    }

    // NavigationCompleted can still report the pre-traversal CanGoBack/
    // CanGoForward, and HistoryChanged does not reliably fire after traversals.
    // Delayed re-queries let the engine state settle before it is published.
    void scheduleHistoryRefresh(int delayMs)
    {
        ComPtr<ICoreWebView2> view = webView;
        QTimer::singleShot(delayMs, q, [guard = QPointer<FeriaProviderHost>(q), view]() {
            if (!guard || !view) return;
            BOOL back = FALSE;
            BOOL forward = FALSE;
            view->get_CanGoBack(&back);
            view->get_CanGoForward(&forward);
            emit guard->historyChanged(back != FALSE, forward != FALSE);
        });
    }

    void installNavigationPolicy(ICoreWebView2 *view, bool mainView = false)
    {
        EventRegistrationToken token{};
        view->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this, mainView, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                       ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR raw = nullptr;
                    if (FAILED(args->get_Uri(&raw)))
                        return S_OK;
                    const QString uri = takeWideString(raw);
                    if (!isAllowed(uri)) {
                        args->put_Cancel(TRUE);
                        reportBlocked(uri);
                    } else if (mainView) {
                        UINT64 navigationId = 0;
                        const bool haveId = SUCCEEDED(args->get_NavigationId(&navigationId));
                        if (close.active && close.internalUnload && !close.internalStarted
                                && uri == QStringLiteral("about:blank")) {
                            // The internal unload attempt of the captured close
                            // operation. It is never a user-facing navigation:
                            // no URL/history/restore emission, no document
                            // generation advance above this host.
                            if (haveId) {
                                close.internalNavId = navigationId;
                                close.internalStarted = true;
                                currentNavigationId = navigationId;
                            }
                            log(QStringLiteral("CLOSE_INTERNAL_START id=%1")
                                    .arg(QString::number(navigationId)));
                            return S_OK;
                        }
                        if (close.active && close.internalStarted && haveId
                                && navigationId != close.internalNavId) {
                            // A competing navigation cannot be mistaken for the
                            // close departure. Deny the retained dialog (which
                            // cancels the internal attempt) and let the new
                            // navigation proceed as an ordinary one.
                            invalidateClose(QStringLiteral("competing-navigation"));
                        }
                        if (haveId)
                            currentNavigationId = navigationId;
                        // A fresh navigation supersedes any pending stop; its
                        // own failures must still surface honestly.
                        stopRequested = false;
                        emit q->navigationStarted(uri);
                    }
                    return S_OK;
                }).Get(),
            &token);

        view->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, view, mainView, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                             ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    if (!guard) return S_OK;
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    if (mainView) {
                        UINT64 navigationId = 0;
                        const bool haveId = SUCCEEDED(args->get_NavigationId(&navigationId));
                        if (haveId && navigationId == suppressCompletionNavId) {
                            suppressCompletionNavId = 0;
                            log(QStringLiteral("INTERNAL_TERMINAL_SWALLOWED id=%1")
                                    .arg(QString::number(navigationId)));
                            return S_OK;
                        }
                        if (close.active && close.internalStarted && haveId
                                && navigationId == close.internalNavId) {
                            // Terminal of the internal unload navigation. This
                            // is the only place close departure can be proven:
                            // a successful completion whose resulting document
                            // actually left the original URL. Accept, S_OK,
                            // DOM readiness or a timer never qualify.
                            const QString operationId = close.operationId;
                            const UINT64 nativeNavId = close.internalNavId;
                            if (success) {
                                LPWSTR rawSource = nullptr;
                                view->get_Source(&rawSource);
                                const QString source = takeWideString(rawSource);
                                if (source == QStringLiteral("about:blank")) {
                                    resetCloseState();
                                    log(QStringLiteral("CLOSE_DEPARTURE_VERIFIED id=%1")
                                            .arg(QString::number(nativeNavId)));
                                    emit q->closeDepartureVerified(operationId, nativeNavId);
                                } else {
                                    invalidateClose(
                                        QStringLiteral("unexpected-internal-target"));
                                }
                            } else {
                                invalidateClose(QStringLiteral("internal-unload-failed"));
                            }
                            // Either way this attempt is not a user-facing
                            // load outcome.
                            return S_OK;
                        }
                        if (haveId && navigationId != currentNavigationId) {
                            // A newer navigation replaced this load; its own
                            // events own the state, so this callback is noise.
                            log(QStringLiteral("NAV_SUPERSEDED id=%1").arg(navigationId));
                            return S_OK;
                        }
                        if (!success) {
                            COREWEBVIEW2_WEB_ERROR_STATUS error =
                                COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                            args->get_WebErrorStatus(&error);
                            if (error == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED
                                || stopRequested) {
                                // Stop() or an in-page cancellation: the previous
                                // page remains displayed and usable, which is not
                                // a "could not open" failure.
                                success = TRUE;
                            }
                        }
                        stopRequested = false;
                    }
                    LPWSTR raw = nullptr;
                    view->get_Source(&raw);
                    const QString uri = takeWideString(raw);
                    log(QStringLiteral("NAV_COMPLETE success=%1 uri=%2")
                            .arg(success ? QStringLiteral("true")
                                         : QStringLiteral("false"),
                                 QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
                    if (mainView) {
                        // HistoryChanged can trail NavigationCompleted — and
                        // skip traversals entirely — so refresh alongside it and
                        // again once the engine state has settled.
                        BOOL back = FALSE;
                        BOOL forward = FALSE;
                        view->get_CanGoBack(&back);
                        view->get_CanGoForward(&forward);
                        emit q->historyChanged(back != FALSE, forward != FALSE);
                        scheduleHistoryRefresh(150);
                        scheduleHistoryRefresh(500);
                        emit q->navigationCompleted(uri, success != FALSE);
                    }
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
            log(QStringLiteral("POPUP_BLOCKED uri=%1").arg(QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
            return S_OK;
        }

        ComPtr<ICoreWebView2Deferral> deferral;
        if (FAILED(args->GetDeferral(&deferral))) {
            args->put_Handled(TRUE);
            log(QStringLiteral("POPUP_FAILED no-deferral uri=%1").arg(QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
            return S_OK;
        }

        ComPtr<ICoreWebView2NewWindowRequestedEventArgs> argsRef(args);
        auto popup = std::make_unique<PopupState>();
        popup->window = std::make_unique<QWindow>();
        popup->window->setTitle(QStringLiteral("Feria — Sign in"));
        for (QWindow *window : QGuiApplication::allWindows()) {
            if (reinterpret_cast<HWND>(window->winId()) == parentWindow) {
                popup->window->setTransientParent(window);
                break;
            }
        }
        popup->window->resize(900, 700);
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
                    [this, popupRaw, popupHwnd, argsRef, deferral, uri, guard = QPointer<FeriaProviderHost>(q)](
                        HRESULT result,
                        ICoreWebView2Controller *newController) -> HRESULT {
                        if (!guard) {
                            argsRef->put_Handled(TRUE);
                            deferral->Complete();
                            return S_OK;
                        }
                        if (FAILED(result) || !newController) {
                            argsRef->put_Handled(TRUE);
                            deferral->Complete();
                            if (popupRaw->window)
                                popupRaw->window->hide();
                            log(QStringLiteral("POPUP_FAILED controller=%1 uri=%2")
                                    .arg(hresultText(result), QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
                            return S_OK;
                        }

                        popupRaw->controller = newController;
                        popupRaw->controller->get_CoreWebView2(
                            &popupRaw->webView);
                        RECT client{};
                        GetClientRect(popupHwnd, &client);
                        popupRaw->controller->put_Bounds(client);
                        if (!popupRaw->webView) {
                            argsRef->put_Handled(TRUE);
                            deferral->Complete();
                            popupRaw->window->hide();
                            return S_OK;
                        }
                        installNavigationPolicy(popupRaw->webView.Get());
                        auto resizePopup = [popupRaw, popupHwnd]() {
                            if (!popupRaw->controller) return;
                            RECT bounds{};
                            GetClientRect(popupHwnd, &bounds);
                            popupRaw->controller->put_Bounds(bounds);
                        };
                        QObject::connect(popupRaw->window.get(), &QWindow::widthChanged, q, resizePopup);
                        QObject::connect(popupRaw->window.get(), &QWindow::heightChanged, q, resizePopup);
                        QObject::connect(popupRaw->window.get(), &QWindow::visibleChanged, q,
                            [popupRaw](bool visible) {
                                if (!visible && popupRaw->controller) {
                                    popupRaw->controller->Close();
                                    popupRaw->controller.Reset();
                                    popupRaw->webView.Reset();
                                }
                            });
                        EventRegistrationToken closeToken{};
                        popupRaw->webView->add_WindowCloseRequested(
                            Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                                [popupRaw, guard](ICoreWebView2 *, IUnknown *) -> HRESULT {
                                    if (guard) popupRaw->window->hide();
                                    return S_OK;
                                }).Get(), &closeToken);

                        EventRegistrationToken popupToken{};
                        popupRaw->webView->add_NewWindowRequested(
                            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                                       ICoreWebView2NewWindowRequestedEventArgs *nested)
                                    -> HRESULT {
                                    if (!guard) return S_OK;
                                    return openOwnedPopup(nested);
                                }).Get(),
                            &popupToken);

                        prepareDocumentScripts(popupRaw->webView.Get(), false,
                            [guard, popupRaw, argsRef, deferral, uri](bool ok) {
                                if (guard && ok) argsRef->put_NewWindow(popupRaw->webView.Get());
                                argsRef->put_Handled(TRUE);
                                deferral->Complete();
                                if (!guard) return;
                                if (!ok) { popupRaw->window->hide(); return; }
                                guard->m_impl->log(QStringLiteral("POPUP_HOSTED uri=%1").arg(QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
                                emit guard->popupOpened(uri);
                            });
                        return S_OK;
                    }).Get());

        if (FAILED(createResult)) {
            argsRef->put_Handled(TRUE);
            deferral->Complete();
            if (popupRaw->window)
                popupRaw->window->hide();
            log(QStringLiteral("POPUP_FAILED create-call=%1 uri=%2")
                    .arg(hresultText(createResult), QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
        }
        return S_OK;
    }

    void installMainEvents()
    {
        installNavigationPolicy(webView.Get(), true);

        // The consented-close path owns script dialogs through the retained
        // ScriptDialogOpening deferrals (the probe-qualified configuration):
        // default dialogs are disabled so beforeunload raises the event we
        // fence. Non-beforeunload dialogs are dismissed programmatically.
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(webView->get_Settings(&settings)) && settings)
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);

        EventRegistrationToken token{};
        webView->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
                [guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
                    if (!guard) return S_OK;
                    BOOL back = FALSE;
                    BOOL forward = FALSE;
                    sender->get_CanGoBack(&back);
                    sender->get_CanGoForward(&forward);
                    emit guard->historyChanged(back != FALSE, forward != FALSE);
                    return S_OK;
                }).Get(),
            &token);
        webView->add_ContainsFullScreenElementChanged(
            Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
                    if (!guard) return S_OK;
                    BOOL active = FALSE;
                    if (SUCCEEDED(sender->get_ContainsFullScreenElement(&active)))
                        emit q->fullScreenChanged(active == TRUE);
                    return S_OK;
                }).Get(), &token);
        ComPtr<ICoreWebView2_2> documentView;
        if (SUCCEEDED(webView.As(&documentView))) {
            documentView->add_DOMContentLoaded(
                Callback<ICoreWebView2DOMContentLoadedEventHandler>(
                    [this, guard = QPointer<FeriaProviderHost>(q)](
                        ICoreWebView2 *sender, ICoreWebView2DOMContentLoadedEventArgs *args) -> HRESULT {
                        if (!guard) return S_OK;
                        UINT64 navigationId = 0;
                        if (SUCCEEDED(args->get_NavigationId(&navigationId))
                            && navigationId != currentNavigationId) {
                            // Readiness of a superseded document: the load that
                            // replaced it owns the current state.
                            return S_OK;
                        }
                        LPWSTR raw = nullptr;
                        if (SUCCEEDED(sender->get_Source(&raw)))
                            emit q->documentReady(takeWideString(raw));
                        return S_OK;
                    }).Get(), &token);
        }
        if (youtubeObserver) {
            webView->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                           ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                        if (!guard) return S_OK;
                        LPWSTR raw = nullptr;
                        if (SUCCEEDED(args->get_WebMessageAsJson(&raw)))
                            emit q->observerMessage(takeWideString(raw));
                        return S_OK;
                    }).Get(),
                &token);
        }
        webView->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                       ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                    if (!guard) return S_OK;
                    return openOwnedPopup(args);
                }).Get(),
            &token);

        webView->add_ScriptDialogOpening(
            Callback<ICoreWebView2ScriptDialogOpeningEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                       ICoreWebView2ScriptDialogOpeningEventArgs *args) -> HRESULT {
                    if (!guard) return S_OK;
                    COREWEBVIEW2_SCRIPT_DIALOG_KIND kind{};
                    if (FAILED(args->get_Kind(&kind)))
                        return S_OK;
                    if (kind != COREWEBVIEW2_SCRIPT_DIALOG_KIND_BEFOREUNLOAD) {
                        // This slice owns beforeunload consent only. Other
                        // script dialogs are dismissed programmatically so
                        // the renderer is never left waiting; presenting them
                        // belongs to the later request-bridge group.
                        ComPtr<ICoreWebView2Deferral> other;
                        if (SUCCEEDED(args->GetDeferral(&other)) && other)
                            other->Complete();
                        log(QStringLiteral("SCRIPT_DIALOG_DISMISSED kind=%1")
                                .arg(static_cast<int>(kind)));
                        return S_OK;
                    }
                    LPWSTR rawUri = nullptr;
                    QString uri;
                    if (SUCCEEDED(args->get_Uri(&rawUri)))
                        uri = takeWideString(rawUri);
                    ComPtr<ICoreWebView2Deferral> deferral;
                    if (FAILED(args->GetDeferral(&deferral)) || !deferral) {
                        log(QStringLiteral("DIALOG_DEFERRAL_FAILED"));
                        return S_OK;
                    }
                    RetainedDialog dialog;
                    dialog.nativeToken = ++dialogSerial;
                    dialog.forClose = close.active && close.internalUnload;
                    dialog.args = args;
                    dialog.deferral = deferral;
                    const int nativeToken = dialog.nativeToken;
                    const bool forClose = dialog.forClose;
                    dialogs.insert({nativeToken, std::move(dialog)});
                    log(QStringLiteral("BEFOREUNLOAD token=%1 forClose=%2 uri=%3")
                            .arg(QString::number(nativeToken),
                                 forClose ? QStringLiteral("true") : QStringLiteral("false"),
                                 uri));
                    if (forClose)
                        emit q->closeConsentRequested(nativeToken, uri);
                    else
                        emit q->unloadConsentRequested(nativeToken, uri);
                    return S_OK;
                }).Get(),
            &token);

        webView->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *sender,
                        IUnknown *) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(sender->get_DocumentTitle(&raw))) {
                        const QString title = takeWideString(raw);
                        if (title != documentTitle) {
                            documentTitle = title;
                            emit q->titleChanged(title);
                        }
                    }
                    return S_OK;
                }).Get(),
            &token);

        ComPtr<ICoreWebView2_4> processView;
        if (SUCCEEDED(webView.As(&processView))) {
            processView->add_ProcessFailed(
                Callback<ICoreWebView2ProcessFailedEventHandler>(
                    [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2 *,
                           ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                        if (!guard) return S_OK;
                        COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                        if (FAILED(args->get_ProcessFailedKind(&kind)))
                            return S_OK;
                        const int kindValue = static_cast<int>(kind);
                        log(QStringLiteral("PROCESS_FAILED kind=%1").arg(kindValue));
                        if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
                            return S_OK;
                        // Renderer loss: a dead renderer can never answer a
                        // retained deferral. Drain it safely and report; the
                        // shared operation settles truthfully (failed), never
                        // as a successful close.
                        const bool hadClose = close.active;
                        drainDialogs();
                        if (hadClose)
                            invalidateClose(QStringLiteral("renderer-process-failed"));
                        emit q->rendererLost(QString::number(kindValue));
                        return S_OK;
                    }).Get(),
                &token);
        }

        controller->add_GotFocus(
            Callback<ICoreWebView2FocusChangedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2Controller *, IUnknown *) -> HRESULT {
                    if (!guard) return S_OK;
                    log(QStringLiteral("WEBVIEW_GOT_FOCUS"));
                    return S_OK;
                }).Get(),
            &token);

        controller->add_LostFocus(
            Callback<ICoreWebView2FocusChangedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2Controller *, IUnknown *) -> HRESULT {
                    if (!guard) return S_OK;
                    log(QStringLiteral("WEBVIEW_LOST_FOCUS"));
                    return S_OK;
                }).Get(),
            &token);

        controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this, guard = QPointer<FeriaProviderHost>(q)](ICoreWebView2Controller *,
                       ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT {
                    if (!guard) return S_OK;
                    UINT virtualKey = 0;
                    COREWEBVIEW2_KEY_EVENT_KIND kind{};
                    args->get_VirtualKey(&virtualKey);
                    args->get_KeyEventKind(&kind);
                    log(QStringLiteral("WEBVIEW_KEY vk=%1 kind=%2")
                            .arg(virtualKey)
                            .arg(static_cast<int>(kind)));
                    if ((virtualKey == VK_F10 || virtualKey == VK_APPS)
                        && (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                            || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)) {
                        args->put_Handled(TRUE);
                        emit q->appMenuRequested();
                        return S_OK;
                    }
                    if (virtualKey == VK_ESCAPE
                        && (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                            || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)) {
                        BOOL fullscreen = FALSE;
                        if (SUCCEEDED(webView->get_ContainsFullScreenElement(&fullscreen)) && fullscreen) {
                            args->put_Handled(TRUE);
                            q->executeScript(QStringLiteral("fullscreen-exit"),
                                QStringLiteral("if(document.fullscreenElement) document.exitFullscreen()"));
                            return S_OK;
                        }
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
    // No renderer may keep waiting on a host that is going away: drain first,
    // then close the controllers.
    m_impl->drainDialogs();
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
    if (m_impl->initializing || m_impl->controller || m_impl->ready)
        return;
    m_impl->initializing = true;
    m_impl->parentWindow = reinterpret_cast<HWND>(parentWindowId);
    m_impl->pendingUrl = initialUrl;
    m_impl->youtubeObserver = initialUrl.host().endsWith(QStringLiteral("youtube.com"));
    const std::wstring profile = userDataFolder.toStdWString();
    const QPointer<FeriaProviderHost> guard(this);
    auto fail = [guard](const QString &reason) {
        if (!guard) return;
        guard->m_impl->initializing = false;
        guard->m_impl->log(reason);
        QTimer::singleShot(0, guard, [guard, reason] {
            if (guard) emit guard->initializationFailed(reason);
        });
    };
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, profile.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [guard, fail](HRESULT environmentResult, ICoreWebView2Environment *environment) -> HRESULT {
                if (!guard) return S_OK;
                if (FAILED(environmentResult) || !environment) {
                    fail(QStringLiteral("WebView2 environment: %1").arg(hresultText(environmentResult)));
                    return S_OK;
                }
                guard->m_impl->environment = environment;
                const HRESULT createResult = environment->CreateCoreWebView2Controller(
                    guard->m_impl->parentWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [guard, fail](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
                            if (!guard) return S_OK;
                            if (FAILED(controllerResult) || !controller) {
                                fail(QStringLiteral("WebView2 controller: %1").arg(hresultText(controllerResult)));
                                return S_OK;
                            }
                            auto &impl = *guard->m_impl;
                            impl.controller = controller;
                            if (FAILED(controller->get_CoreWebView2(&impl.webView)) || !impl.webView) {
                                fail(QStringLiteral("WebView2 page could not be created"));
                                return S_OK;
                            }
                            impl.installMainEvents();
                            guard->setPhysicalBounds(impl.requestedBounds);
                            guard->setVisible(impl.visible);
                            impl.prepareDocumentScripts(impl.webView.Get(), impl.youtubeObserver,
                                [guard, fail](bool ok) {
                                    if (!guard) return;
                                    if (!ok) { fail(QStringLiteral("WebView2 document scripts could not be installed")); return; }
                                    guard->m_impl->ready = true;
                                    guard->m_impl->initializing = false;
                                    emit guard->readyChanged(true);
                                    guard->navigate(guard->m_impl->pendingUrl);
                                });
                            return S_OK;
                        }).Get());
                if (FAILED(createResult))
                    fail(QStringLiteral("WebView2 controller creation: %1").arg(hresultText(createResult)));
                return S_OK;
            }).Get());
    if (FAILED(result))
        fail(QStringLiteral("WebView2 startup: %1").arg(hresultText(result)));
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
    if (!visible) {
        for (const auto &popup : m_impl->popups) popup->window->hide();
    }
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
    if (!url.isValid()) return;
    m_impl->pendingUrl = url;
    if (!m_impl->webView || !m_impl->ready) return;

    const QString uri = url.toString();
    if (!m_impl->isAllowed(uri)) {
        m_impl->reportBlocked(uri);
        return;
    }

    const std::wstring wideUri = uri.toStdWString();
    const HRESULT result = m_impl->webView->Navigate(wideUri.c_str());
    if (FAILED(result)) {
        m_impl->log(QStringLiteral("NAVIGATE_FAILED hr=%1 uri=%2")
                        .arg(hresultText(result), QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
    } else {
        m_impl->log(QStringLiteral("NAVIGATE uri=%1").arg(QUrl(uri).toDisplayString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo)));
    }
}

void FeriaProviderHost::reload()
{
    if (m_impl->webView) m_impl->webView->Reload();
}

void FeriaProviderHost::goBack()
{
    if (!m_impl->webView) return;
    BOOL available = FALSE;
    if (SUCCEEDED(m_impl->webView->get_CanGoBack(&available)) && available)
        m_impl->webView->GoBack();
}

void FeriaProviderHost::goForward()
{
    if (!m_impl->webView) return;
    BOOL available = FALSE;
    if (SUCCEEDED(m_impl->webView->get_CanGoForward(&available)) && available)
        m_impl->webView->GoForward();
}

void FeriaProviderHost::stop()
{
    if (m_impl->webView) {
        m_impl->stopRequested = true;
        m_impl->webView->Stop();
    }
}

bool FeriaProviderHost::canGoBack() const
{
    if (!m_impl->webView) return false;
    BOOL available = FALSE;
    return SUCCEEDED(m_impl->webView->get_CanGoBack(&available)) && available == TRUE;
}

bool FeriaProviderHost::canGoForward() const
{
    if (!m_impl->webView) return false;
    BOOL available = FALSE;
    return SUCCEEDED(m_impl->webView->get_CanGoForward(&available)) && available == TRUE;
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
            [this, label, guard = QPointer<FeriaProviderHost>(this)](HRESULT scriptResult,
                          LPCWSTR resultJson) -> HRESULT {
                if (!guard) return S_OK;
                const QString json =
                    resultJson ? QString::fromWCharArray(resultJson)
                               : QStringLiteral("null");
                m_impl->log(
                    QStringLiteral("SCRIPT label=%1 hr=%2")
                        .arg(label, hresultText(scriptResult)));
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

QString FeriaProviderHost::title() const
{
    return m_impl->documentTitle;
}

void FeriaProviderHost::requestClose(const QString &operationId)
{
    if (!m_impl->webView || !m_impl->ready || m_impl->close.active) {
        m_impl->log(QStringLiteral("CLOSE_REQUEST_REJECTED ready=%1 busy=%2")
                .arg(m_impl->ready ? QStringLiteral("true") : QStringLiteral("false"),
                     m_impl->close.active ? QStringLiteral("true") : QStringLiteral("false")));
        emit closeInvalidated(QStringLiteral("close-unavailable"));
        return;
    }
    m_impl->close.active = true;
    m_impl->close.operationId = operationId;
    m_impl->close.internalUnload = true;
    m_impl->close.internalStarted = false;
    m_impl->close.internalNavId = 0;
    m_impl->close.awaitingDeparture = false;
    m_impl->log(QStringLiteral("CLOSE_REQUEST operation=%1").arg(operationId));
    // Probe-qualified candidate stimulus: the internal about:blank navigation
    // raises the beforeunload ScriptDialogOpening. Stay cancels it before
    // NavigationStarting, so no navigation id exists for that attempt.
    const HRESULT result = m_impl->webView->Navigate(L"about:blank");
    if (FAILED(result)) {
        m_impl->invalidateClose(QStringLiteral("navigate-dispatch-failed")
                                    + QStringLiteral(" hr=") + hresultText(result));
    }
}

void FeriaProviderHost::respondCloseRequest(int nativeToken, bool leave)
{
    auto found = m_impl->dialogs.find(nativeToken);
    if (found == m_impl->dialogs.end() || found->second.completed) {
        m_impl->log(QStringLiteral("CLOSE_ANSWER_STALE token=%1").arg(nativeToken));
        return;
    }
    if (!found->second.forClose) {
        m_impl->log(QStringLiteral("CLOSE_ANSWER_WRONG_KIND token=%1").arg(nativeToken));
        return;
    }
    if (leave) {
        if (found->second.args)
            found->second.args->Accept();
        if (found->second.deferral)
            found->second.deferral->Complete();
        found->second.completed = true;
        m_impl->dialogs.erase(found);
        m_impl->log(QStringLiteral("CLOSE_LEAVE_ACCEPTED token=%1").arg(nativeToken));
        if (m_impl->unsafeEarlyDispose) {
            // Test-only RED control: settle "departure" without the correlated
            // navigation completion, the unsafe parity break the negative
            // control must catch.
            const QString operationId = m_impl->close.operationId;
            m_impl->resetCloseState();
            m_impl->log(QStringLiteral("CLOSE_UNSAFE_EARLY_DISPOSE"));
            emit closeDepartureVerified(operationId, 0);
            return;
        }
        // Leave is now pending until the correlated internal navigation
        // completes and the original document is actually gone. No timer,
        // Accept result or DOM event settles it here.
        m_impl->close.awaitingDeparture = true;
        return;
    }
    // Stay: complete the deferral without Accept. This runtime cancelled the
    // internal navigation before NavigationStarting, so there is no navigation
    // id or terminal for the denied attempt — the completed deferral itself is
    // the explicit decision evidence for the captured operation.
    if (found->second.deferral)
        found->second.deferral->Complete();
    found->second.completed = true;
    m_impl->dialogs.erase(found);
    const bool hadInternalStart = m_impl->close.internalStarted;
    m_impl->resetCloseState();
    m_impl->log(QStringLiteral("CLOSE_STAY_RECORDED token=%1 internalStarted=%2")
            .arg(QString::number(nativeToken),
                 hadInternalStart ? QStringLiteral("true") : QStringLiteral("false")));
    emit closeStayRecorded(nativeToken);
}

void FeriaProviderHost::respondUnloadRequest(int nativeToken, bool leave)
{
    auto found = m_impl->dialogs.find(nativeToken);
    if (found == m_impl->dialogs.end() || found->second.completed) {
        m_impl->log(QStringLiteral("UNLOAD_ANSWER_STALE token=%1").arg(nativeToken));
        return;
    }
    if (found->second.forClose) {
        m_impl->log(QStringLiteral("UNLOAD_ANSWER_WRONG_KIND token=%1").arg(nativeToken));
        return;
    }
    if (leave && found->second.args)
        found->second.args->Accept();
    if (found->second.deferral)
        found->second.deferral->Complete();
    found->second.completed = true;
    m_impl->dialogs.erase(found);
    m_impl->log(QStringLiteral("UNLOAD_SETTLED token=%1 leave=%2")
            .arg(QString::number(nativeToken),
                 leave ? QStringLiteral("true") : QStringLiteral("false")));
    emit unloadSettled(nativeToken, leave);
}

void FeriaProviderHost::cancelCloseOperation()
{
    if (!m_impl->close.active)
        return;
    // Deny any retained dialog (which also cancels the internal attempt) and
    // stop an in-flight internal unload so the original document stays.
    m_impl->drainDialogs();
    const bool internalStarted = m_impl->close.internalStarted;
    const UINT64 internalNavId = m_impl->close.internalNavId;
    m_impl->resetCloseState();
    if (internalStarted && m_impl->webView) {
        m_impl->suppressCompletionNavId = internalNavId;
        m_impl->stopRequested = true;
        m_impl->webView->Stop();
    }
    m_impl->log(QStringLiteral("CLOSE_CANCELLED internalStarted=%1")
            .arg(internalStarted ? QStringLiteral("true") : QStringLiteral("false")));
}

void FeriaProviderHost::drainPendingNativeRequests()
{
    m_impl->drainDialogs();
}

void FeriaProviderHost::dispatchTrustedClick(int x, int y)
{
    if (!m_impl->testInputEnabled || !m_impl->controller || !m_impl->webView) {
        m_impl->log(QStringLiteral("TEST_INPUT_DISABLED"));
        return;
    }
    // Probe-qualified trusted input path: native MoveFocus into the renderer
    // first, then CDP Input.dispatchMouseEvent. No desktop mouse or keyboard.
    m_impl->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    const QString pressed = QStringLiteral(
        R"({"type":"mousePressed","x":%1,"y":%2,"button":"left","clickCount":1})").arg(x).arg(y);
    const QString released = QStringLiteral(
        R"({"type":"mouseReleased","x":%1,"y":%2,"button":"left","clickCount":1})").arg(x).arg(y);
    for (const QString &parameters : {pressed, released}) {
        const std::wstring wide = parameters.toStdWString();
        m_impl->webView->CallDevToolsProtocolMethod(L"Input.dispatchMouseEvent", wide.c_str(),
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [this, parameters](HRESULT error, LPCWSTR) -> HRESULT {
                    if (FAILED(error))
                        m_impl->log(QStringLiteral("TEST_INPUT_FAILED hr=%1 params=%2")
                                .arg(hresultText(error), parameters));
                    return S_OK;
                }).Get());
    }
    m_impl->log(QStringLiteral("TEST_INPUT_CLICK x=%1 y=%2").arg(x).arg(y));
}


void FeriaProviderHost::clearSiteData(const QUrl &origin)
{
    if (!m_impl->webView || !FeriaBrowserPolicy::allows(origin)
        || (origin.scheme() != "http" && origin.scheme() != "https")) {
        emit siteDataCleared(false); return;
    }
    const auto params = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"origin", origin.adjusted(QUrl::RemovePath | QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment).toString()},
        {"storageTypes", "all"}}).toJson(QJsonDocument::Compact)).toStdWString();
    const HRESULT result = m_impl->webView->CallDevToolsProtocolMethod(L"Storage.clearDataForOrigin", params.c_str(),
        Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
            [guard = QPointer<FeriaProviderHost>(this)](HRESULT error, LPCWSTR) -> HRESULT {
                if (guard) emit guard->siteDataCleared(SUCCEEDED(error)); return S_OK;
            }).Get());
    if (FAILED(result)) emit siteDataCleared(false);
}

void FeriaProviderHost::clearCookies(const QStringList &domains)
{
    ComPtr<ICoreWebView2_2> view;
    ComPtr<ICoreWebView2CookieManager> manager;
    if (!m_impl->webView || FAILED(m_impl->webView.As(&view))
        || FAILED(view->get_CookieManager(&manager)) || domains.isEmpty()) {
        emit cookiesCleared(false); return;
    }
    const auto guard = QPointer<FeriaProviderHost>(this);
    const HRESULT result = manager->GetCookies(L"", Callback<ICoreWebView2GetCookiesCompletedHandler>(
        [guard, manager, domains](HRESULT error, ICoreWebView2CookieList *list) -> HRESULT {
            if (!guard) return S_OK;
            if (FAILED(error) || !list) { emit guard->cookiesCleared(false); return S_OK; }
            UINT count = 0;
            if (FAILED(list->get_Count(&count))) { emit guard->cookiesCleared(false); return S_OK; }
            bool ok = true;
            for (UINT i = 0; i < count; ++i) {
                ComPtr<ICoreWebView2Cookie> cookie; LPWSTR raw = nullptr;
                if (FAILED(list->GetValueAtIndex(i, &cookie)) || FAILED(cookie->get_Domain(&raw))) { ok = false; continue; }
                const QString domain = takeWideString(raw);
                if (FeriaBrowserPolicy::cookieInScope(domain, domains) && FAILED(manager->DeleteCookie(cookie.Get()))) ok = false;
            }
            if (!ok) { emit guard->cookiesCleared(false); return S_OK; }
            const HRESULT verify = manager->GetCookies(L"", Callback<ICoreWebView2GetCookiesCompletedHandler>(
                [guard, domains](HRESULT error, ICoreWebView2CookieList *remaining) -> HRESULT {
                    if (!guard) return S_OK;
                    bool clean = SUCCEEDED(error) && remaining;
                    UINT count = 0;
                    if (clean) clean = SUCCEEDED(remaining->get_Count(&count));
                    for (UINT i = 0; clean && i < count; ++i) {
                        ComPtr<ICoreWebView2Cookie> cookie; LPWSTR raw = nullptr;
                        if (FAILED(remaining->GetValueAtIndex(i, &cookie)) || FAILED(cookie->get_Domain(&raw))) { clean = false; break; }
                        if (FeriaBrowserPolicy::cookieInScope(takeWideString(raw), domains)) clean = false;
                    }
                    emit guard->cookiesCleared(clean); return S_OK;
                }).Get());
            if (FAILED(verify)) emit guard->cookiesCleared(false);
            return S_OK;
        }).Get());
    if (FAILED(result)) emit cookiesCleared(false);
}
