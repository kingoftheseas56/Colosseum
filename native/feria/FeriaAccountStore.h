#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>

// Local, profile-owned Feria activity. Provider credentials stay in the browser.
class FeriaAccountStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantList continueItems READ continueItems NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList browserBookmarks READ browserBookmarks NOTIFY changed)
    Q_PROPERTY(QVariantList browserHistory READ browserHistory NOTIFY changed)
    Q_PROPERTY(QVariantMap browserPreferences READ browserPreferences NOTIFY changed)
    Q_PROPERTY(QVariantList browserTabs READ browserTabs NOTIFY changed)
    Q_PROPERTY(QVariantList browserDownloadHistory READ browserDownloadHistory NOTIFY changed)
public:
    explicit FeriaAccountStore(QObject *parent = nullptr);
    void setStoragePath(const QString &path);
    QVariantList sessions() const;
    QVariantList continueItems() const;
    bool recording() const;
    QString error() const { return m_error; }
    Q_INVOKABLE void beginVisit(const QVariantMap &context);
    Q_INVOKABLE void endVisit();
    Q_INVOKABLE QString beginSessionVisit(const QString &sessionId, const QVariantMap &context);
    Q_INVOKABLE void endSessionVisit(const QString &sessionId);
    Q_INVOKABLE bool observeSession(const QString &sessionId, const QString &token, const QVariantMap &sample);
    Q_INVOKABLE bool saveSessionReadingPlace(const QString &sessionId, const QString &token,
                                            const QString &url, const QString &title);
    Q_INVOKABLE QString visitToken() const { return m_legacyToken; }
    Q_INVOKABLE void setMediaCaptureBlocked(bool blocked, const QString &visitToken = QString());
    Q_INVOKABLE bool observe(const QVariantMap &sample);
    Q_INVOKABLE bool saveReadingPlace(const QString &url, const QString &title);
    Q_INVOKABLE bool setRecording(bool enabled);
    Q_INVOKABLE bool clearHistory();
    Q_INVOKABLE bool clearActivity();
    Q_INVOKABLE bool dismissContinue(const QString &id);
    static QString safeUrl(const QString &url);
    QVariantList browserBookmarks() const;
    QVariantList browserHistory() const;
    QVariantMap browserPreferences() const;
    QVariantList browserTabs() const;
    QVariantList browserDownloadHistory() const;
    Q_INVOKABLE bool setBrowserBookmark(const QString &url, const QString &title, bool saved);
    Q_INVOKABLE bool recordBrowserVisit(const QString &sessionId, const QString &token,
                                       const QString &url, const QString &title);
    Q_INVOKABLE bool clearBrowserHistory();
    Q_INVOKABLE bool setBrowserPreference(const QString &key, const QVariant &value);
    Q_INVOKABLE bool saveBrowserTabs(const QVariantList &descriptors);
    Q_INVOKABLE QString authorizeBrowserDownload(const QString &sessionId, const QString &visitToken);
    Q_INVOKABLE bool cancelBrowserDownload(const QString &downloadToken);
    Q_INVOKABLE bool recordBrowserDownload(const QVariantMap &descriptor);
    Q_INVOKABLE bool clearBrowserDownloads();
signals:
    void changed();
    void profileChanged();
private:
    struct Visit {
        QVariantMap context;
        QString sessionId, lastItem, token;
        double lastPosition = 0;
        bool lastPlaying = false;
        bool mediaCaptureBlocked = false;
        bool privateBrowsing = false;
        QElapsedTimer clock;
    };
    struct DownloadAuthorization {
        QString profilePath, providerId;
        bool committing = false;
    };
    Visit *visitFor(const QString &sessionId, const QString &token);
    bool observeVisit(Visit &visit, const QVariantMap &sample);
    bool saveReadingPlace(Visit &visit, const QString &url, const QString &title);
    bool commit(const QVariantMap &state);
    bool record(Visit &visit, const QVariantMap &sample, bool reading);
    QVariantMap m_state;
    QString m_path, m_error, m_legacyToken;
    QHash<QString, Visit> m_visits;
    QHash<QString, DownloadAuthorization> m_downloadAuthorizations;
};
