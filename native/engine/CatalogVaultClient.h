#pragma once
// CatalogVaultClient — keeps the four Colosseum-Data catalogue databases (mal_catalog.db,
// tankoban_catalog.db, comics_catalog.db, imdb_catalog.db) fresh in AppData from the public
// kingoftheseas56/Colosseum-Data GitHub release (data-vault adoption, Slice 1, 2026-08-22).
// Touches nothing outside vaultDir: `<vaultDir>/state.json` ({tag, fetchedAt ISO8601 UTC,
// assets:{name:{size}}}) plus the four db files themselves. Serial downloads only (one asset
// in flight at a time — this machine is RAM/IO constrained and parallel 90MB downloads help
// nobody). No retry timers, no internal scheduling: the caller decides WHEN checkAndFetch()
// runs.
//
// Live-swap contract (for the NEXT slice, catalog hot-reload): before a freshly downloaded
// file is renamed OVER an EXISTING target, aboutToReplace(name) fires SYNCHRONOUSLY (a
// direct Qt connection, no event-loop hop) so a connected catalog can close its SQLite
// handle in that slot before the Windows rename is attempted — Windows refuses to rename
// over a file that is still open. If the rename still fails after that signal, the temp
// `.downloading` file is left in place and fetchFailed(name, "target locked") is emitted
// instead of touching the (still-valid) old target.
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class CatalogVaultClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool fetching READ isFetching NOTIFY fetchingChanged)
    Q_PROPERTY(QString currentTag READ currentTag NOTIFY tagChanged)
public:
    explicit CatalogVaultClient(
        QNetworkAccessManager* nam, const QString& vaultDir,
        const QString& apiBaseUrl =
            QStringLiteral("https://api.github.com/repos/kingoftheseas56/Colosseum-Data"),
        QObject* parent = nullptr);

    bool isFetching() const { return m_fetching; }
    QString currentTag() const { return m_currentTag; }

    // Entry point. Throttled: if state.json's fetchedAt is under 24h old AND every MANAGED
    // db file exists on disk, emits allFresh(tag) immediately with zero network. Otherwise
    // checks the release manifest and downloads only what changed or is missing.
    Q_INVOKABLE void checkAndFetch();

    // Data-vault Slice 2 (2026-08-22): restrict which of the four known assets this
    // client will ever fetch — a catalog whose dev-machine override already resolved to a
    // local `data/*.db` file has no business being vault-managed. Unknown names are
    // ignored; an empty/never-called call manages all four (the Slice-1 default). Call
    // BEFORE checkAndFetch(); changing it mid-fetch does not affect an in-flight pass.
    Q_INVOKABLE void setManagedNames(const QStringList& names);

signals:
    void databaseUpdated(QString name, QString path);
    void allFresh(QString tag);
    void fetchFailed(QString name, QString error);
    void aboutToReplace(QString name);
    void fetchingChanged();
    void tagChanged();

private:
    struct AssetInfo {
        QString name;
        QUrl url;
        qint64 size = 0;
    };

    void setFetching(bool value);
    void setCurrentTag(const QString& tag);

    QStringList managedAssetNames() const;
    bool allFourFilesPresent() const;
    bool readState(QString* tag, QDateTime* fetchedAt, QHash<QString, qint64>* sizes) const;
    void writeState(const QString& tag, const QHash<QString, qint64>& sizes) const;

    void fetchManifest();
    void onManifestReply(QNetworkReply* reply);
    void beginDownloads(const QString& newTag, const QVector<AssetInfo>& toDownload,
                        const QHash<QString, qint64>& finalSizes);
    void downloadNext();
    void onDownloadFinished();
    bool landDownload(const QString& name, const QString& tmpPath, const QString& targetPath);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_vaultDir;
    QString m_apiBaseUrl;
    bool m_fetching = false;
    QString m_currentTag;
    QStringList m_managedNames; // meaningful only when m_managedNamesSet is true
    bool m_managedNamesSet = false; // false == manage all four (Slice-1 default, setManagedNames never called)

    // In-flight download-plan state for the current checkAndFetch() pass.
    QString m_pendingTag;
    QVector<AssetInfo> m_queue;
    QHash<QString, qint64> m_finalSizes;
    int m_queueIndex = 0;
    QFile* m_downloadFile = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
};
