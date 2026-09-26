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
    Q_INVOKABLE void focusWebView();
    Q_INVOKABLE void returnToQml();
    Q_INVOKABLE void resumeWebView();
    Q_INVOKABLE void navigate(const QUrl &url);
    Q_INVOKABLE void executeScript(const QString &label, const QString &script);
    Q_INVOKABLE QString captureGeometry(const QString &label);

signals:
    void userDataFolderChanged();
    void urlChanged();
    void suppressedChanged();
    void readyChanged();
    void returnedToQml();
    void qmlFocusRestored(bool activeFocus);
    void navigationBlocked(const QString &uri);
    void navigationCompleted(const QString &uri, bool success);
    void scriptResult(const QString &label, const QString &jsonResult);
    void observerMessage(const QString &jsonMessage);
    void popupOpened(const QString &uri);
    void hostLog(const QString &message);
    void geometryCaptured(const QString &json);

protected:
    void componentComplete() override;

private:
    void attachWindow(QQuickWindow *window);
    void ensureHost();
    void syncHost();
    bool hostShouldBeVisible() const;

    FeriaProviderHost *m_host = nullptr;
    QQuickWindow *m_attachedWindow = nullptr;
    QString m_userDataFolder;
    QUrl m_url;
    bool m_suppressed = false;
    bool m_returnedToQml = false;
    bool m_complete = false;
};
