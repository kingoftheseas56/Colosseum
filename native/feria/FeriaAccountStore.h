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
    Q_PROPERTY(QVariantList browserRestoreOffers READ browserRestoreOffers NOTIFY changed)
    Q_PROPERTY(QVariantList browserDownloadHistory READ browserDownloadHistory NOTIFY changed)
    Q_PROPERTY(int browserGeneration READ browserGeneration NOTIFY profileChanged)
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
    QVariantList browserRestoreOffers() const;
    QVariantList browserDownloadHistory() const;
    // Moves with every account change. A management call names the generation
    // its rows were read under and is refused once the account has changed;
    // 0 is the unguarded form for callers that hold no rows.
    int browserGeneration() const { return m_generation; }
    // Every word of the query must appear in the title or the address.
    Q_INVOKABLE QVariantList searchBrowserBookmarks(const QString &query) const;
    Q_INVOKABLE QVariantList searchBrowserHistory(const QString &query) const;
    Q_INVOKABLE bool isBrowserBookmarked(const QString &url) const;
    Q_INVOKABLE bool setBrowserBookmark(const QString &url, const QString &title, bool saved,
                                       int generation = 0);
    // {ok, reason, url, title}; reasons: stale-account, not-found, invalid-url,
    // duplicate, save-failed. The bookmark keeps its place and saved time.
    Q_INVOKABLE QVariantMap editBrowserBookmark(int generation, const QString &url,
                                               const QString &newUrl, const QString &newTitle);
    Q_INVOKABLE bool removeBrowserBookmark(int generation, const QString &url);
    // History holds one row per address, newest visit first. `document` is the
    // tab's document serial; an observation older than the one this visit last
    // recorded is refused.
    Q_INVOKABLE bool recordBrowserVisit(const QString &sessionId, const QString &token,
                                       const QString &url, const QString &title,
                                       qint64 document = 0);
    // A title that arrives after the load amends the row that document
    // recorded, and nothing else.
    Q_INVOKABLE bool updateBrowserVisitTitle(const QString &sessionId, const QString &token,
                                            qint64 document, const QString &title);
    Q_INVOKABLE bool removeBrowserHistoryEntry(int generation, const QString &url);
    Q_INVOKABLE bool clearBrowserHistory(int generation = 0);
    Q_INVOKABLE bool setBrowserPreference(const QString &key, const QVariant &value);
    Q_INVOKABLE bool saveBrowserTabs(const QVariantList &descriptors);
    Q_INVOKABLE bool captureBrowserRestoreOffers();
    Q_INVOKABLE bool saveBrowserRestoreOffers(const QVariantList &descriptors);
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
        QString sessionId, lastItem, token, historyUrl;
        qint64 historyDocument = 0;
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
    int m_generation = 1;
    bool m_restoreCaptured = false;
    bool m_writeFailed = false;
};
