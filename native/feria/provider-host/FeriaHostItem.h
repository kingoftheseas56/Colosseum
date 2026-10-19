#pragma once

#include <QQuickItem>
#include <QUrl>

class FeriaProviderHost;
class QQuickWindow;

class FeriaHostItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString userDataFolder READ userDataFolder WRITE setUserDataFolder
               NOTIFY userDataFolderChanged)
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(bool suppressed READ suppressed WRITE setSuppressed NOTIFY suppressedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY canGoForwardChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
    explicit FeriaHostItem(QQuickItem *parent = nullptr);
    ~FeriaHostItem() override;

    QString userDataFolder() const;
    void setUserDataFolder(const QString &path);

    QUrl url() const;
    void setUrl(const QUrl &url);

    bool suppressed() const;
    void setSuppressed(bool suppressed);
    bool ready() const;
    bool canGoBack() const;
    bool canGoForward() const;
    QString title() const;
    Q_INVOKABLE void focusWebView();
    Q_INVOKABLE void focusAppControls();
    Q_INVOKABLE void returnToQml();
    Q_INVOKABLE void resumeWebView();
    Q_INVOKABLE void navigate(const QUrl &url);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void executeScript(const QString &label, const QString &script);
    Q_INVOKABLE QString captureGeometry(const QString &label);
    Q_INVOKABLE void clearSiteData(const QUrl &origin);
    Q_INVOKABLE void clearCookies(const QStringList &domains);
    // Consented-close surface; nativeToken values come from
    // closeConsentRequested/unloadConsentRequested and answer exactly once.
    Q_INVOKABLE void requestClose(const QString &operationId);
    Q_INVOKABLE void respondCloseRequest(const QString &nativeToken, bool leave);
    Q_INVOKABLE void respondUnloadRequest(const QString &nativeToken, bool leave);
    Q_INVOKABLE void cancelCloseOperation();
    Q_INVOKABLE void dispatchTrustedClick(int x, int y);
    // Test-build-only process-failure injection through the real handler.
    Q_INVOKABLE void simulateProcessFailureForTest(const QString &kind);

signals:
    void initializationFailed(const QString &reason);
    void navigationStarted(const QString &uri, qulonglong navigationId);
    void loadCancelled(qulonglong navigationId, const QString &uri);
    void navigationSuperseded(qulonglong navigationId);
    void documentReady(const QString &uri);
    void userDataFolderChanged();
    void urlChanged();
    void suppressedChanged();
    void readyChanged();
    void canGoBackChanged();
    void canGoForwardChanged();
    void returnedToQml();
    void appMenuRequested();
    void fullScreenChanged(bool active);
    void qmlFocusRestored(bool activeFocus);
    void navigationBlocked(const QString &uri);
    void navigationCompleted(const QString &uri, bool success);
    void scriptResult(const QString &label, const QString &jsonResult);
    void observerMessage(const QString &jsonMessage);
    void popupOpened(const QString &uri);
    void hostLog(const QString &message);
    void siteDataCleared(bool success);
    void cookiesCleared(bool success);
    void geometryCaptured(const QString &json);
    void titleChanged();
    void closeConsentRequested(const QString &nativeToken, const QString &uri);
    void unloadConsentRequested(const QString &nativeToken, const QString &uri);
    void closeStayRecorded(const QString &nativeToken);
    void closeDepartureVerified(const QString &operationId, qulonglong nativeNavigationId);
    void closeInvalidated(const QString &reason);
    void unloadSettled(const QString &nativeToken, bool leave);
    void rendererLost(const QString &kind);

protected:
    void componentComplete() override;

private:
    void attachWindow(QQuickWindow *window);
    void ensureHost();
    void syncHost();
    bool hostShouldBeVisible() const;
    void updateHistoryState();

    FeriaProviderHost *m_host = nullptr;
    QQuickWindow *m_attachedWindow = nullptr;
    QString m_userDataFolder;
    QUrl m_url;
    bool m_suppressed = false;
    bool m_returnedToQml = false;
    bool m_complete = false;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
};
