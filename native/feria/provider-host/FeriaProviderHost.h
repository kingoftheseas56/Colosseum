#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <QUrl>

#include <memory>

class FeriaProviderHost final : public QObject
{
    Q_OBJECT

public:
    explicit FeriaProviderHost(QObject *parent = nullptr);
    ~FeriaProviderHost() override;

    void initialize(quintptr parentWindowId,
                    const QString &userDataFolder,
                    const QUrl &initialUrl);
    void setPhysicalBounds(const QRect &bounds);
    QRect physicalBounds() const;
    QRect nativeChildBounds() const;
    QString nativeChildClassName() const;
    void setVisible(bool visible);
    bool isVisible() const;
    bool isReady() const;
    void focusWebView();
    void navigate(const QUrl &url);
    void reload();
    void goBack();
    void goForward();
    void stop();
    bool canGoBack() const;
    bool canGoForward() const;
    void executeScript(const QString &label, const QString &script);
    void pauseMedia(const QString &label);
    void clearSiteData(const QUrl &origin);
    void clearCookies(const QStringList &domains);

    // Consented close (feria-browser-engine-contract-v1). The candidate
    // stimulus is the probe-qualified internal Navigate(about:blank); the
    // beforeunload ScriptDialogOpening is captured with its deferral. Stay
    // completes without Accept and needs no navigation id; Leave accepts and
    // completes, then stays pending until the correlated internal navigation
    // completes and the original document is actually gone.
    void requestClose(const QString &operationId);
    void respondCloseRequest(int nativeToken, bool leave);
    void respondUnloadRequest(int nativeToken, bool leave);
    void cancelCloseOperation();
    QString title() const;
    // Test-only trusted renderer input through CDP after MoveFocus, gated by
    // FERIA_ENABLE_TEST_INPUT; production callers never set it.
    void dispatchTrustedClick(int x, int y);
    // Complete every retained script-dialog deferral without Accept so the
    // renderer is never left waiting on a torn-down host.
    void drainPendingNativeRequests();

signals:
    void initializationFailed(const QString &reason);
    void navigationStarted(const QString &uri);
    void documentReady(const QString &uri);
    void readyChanged(bool ready);
    void navigationBlocked(const QString &uri);
    void navigationCompleted(const QString &uri, bool success);
    void historyChanged(bool canGoBack, bool canGoForward);
    void scriptResult(const QString &label, const QString &jsonResult);
    void observerMessage(const QString &jsonMessage);
    void escapeRequested();
    void appMenuRequested();
    void fullScreenChanged(bool active);
    void popupOpened(const QString &uri);
    void hostLog(const QString &message);
    void siteDataCleared(bool success);
    void cookiesCleared(bool success);
    void titleChanged(const QString &title);
    // A beforeunload dialog bound to the active app-close operation is asking
    // for consent. nativeToken answers it exactly once through
    // respondCloseRequest.
    void closeConsentRequested(int nativeToken, const QString &uri);
    // A site-initiated beforeunload (no app close intent) is asking for
    // navigation consent only; answering Leave never deletes the space.
    void unloadConsentRequested(int nativeToken, const QString &uri);
    // The retained close dialog was completed without Accept: an explicit
    // Stay decision exists independent of any navigation id (probe finding).
    void closeStayRecorded(int nativeToken);
    // Leave was accepted and the correlated internal navigation completed with
    // the original document actually replaced.
    void closeDepartureVerified(const QString &operationId, qulonglong nativeNavigationId);
    // The captured close operation can no longer complete safely.
    void closeInvalidated(const QString &reason);
    // An external unload request was answered (leave false = Stay).
    void unloadSettled(int nativeToken, bool leave);
    // A render process failed; retained dialogs were drained.
    void rendererLost(const QString &kind);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
